#include <render/raytrace/sr_raytrace.hpp>
#include <core/sr_texture.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

const vec3  SUN_DIR    = normalize(vec3(-0.3f, 1.0f, -0.2f));
const float AMBIENT    = 0.0f;
const float SHADOW_EPS = 1e-4f;

uint32_t pack(vec3 c) {
    auto ch = [](float v){ return (uint32_t)(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return 0xFF000000 | (ch(c.x) << 16) | (ch(c.y) << 8) | ch(c.z);
}

vec3 get_ray_direction(const camera& cam, int px, int py, int w, int h) {
    float ndcX = (2.0f * (px + 0.5f)) / w - 1.0f;
    float ndcY = 1.0f - (2.0f * (py + 0.5f)) / h;
    float b = tanf(to_radians(cam._fov) * 0.5f);
    float a = b / cam._aspectRatio;
    vec3 dirCam(ndcX * b, ndcY * a, -1.0f);
    vec4 dw = cam.rotation() * vec4(dirCam.x, dirCam.y, dirCam.z, 0.0f);
    return normalize(vec3(dw.x, dw.y, dw.z));
}

static vec3 sample_face(const texture& t, float u, float v) {
    if (!t.data || t.width <= 0) return vec3(0.0f, 0.0f, 0.0f);
    int x = std::clamp((int)(u * t.width),  0, t.width  - 1);
    int y = std::clamp((int)(v * t.height), 0, t.height - 1);
    uint32_t c = t.data[y * t.width + x];
    return vec3(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f);
}

static vec3 sample_sky(const std::array<texture, 6>& f, const vec3& d) {
    float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    float m  = std::max(ax, std::max(ay, az));
    if (m <= 0.0f) return vec3(0.0f, 0.0f, 0.0f);
    float px = d.x / m, py = d.y / m, pz = d.z / m;
    int idx; float u, v;
    if (ax >= ay && ax >= az) {
        if (px > 0) { idx = 2; u = pz * 0.5f + 0.5f;   v = py * 0.5f + 0.5f; }
        else        { idx = 1; u = 0.5f - pz * 0.5f;   v = py * 0.5f + 0.5f; }
    } else if (ay >= ax && ay >= az) {
        if (py > 0) { idx = 4; u = px * 0.5f + 0.5f;   v = pz * 0.5f + 0.5f; }
        else        { idx = 5; u = px * 0.5f + 0.5f;   v = 0.5f - pz * 0.5f; }
    } else {
        if (pz > 0) { idx = 3; u = 0.5f - px * 0.5f;  v = py * 0.5f + 0.5f; }
        else        { idx = 0; u = px * 0.5f + 0.5f;   v = py * 0.5f + 0.5f; }
    }
    return sample_face(f[idx], u, v);
}

static vec3 tri_smooth_normal(const bvh::Tri& tr, const vec3& P) {
    vec3 e1 = tr.v1 - tr.v0, e2 = tr.v2 - tr.v0, p = P - tr.v0;
    float d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2);
    float p1  = dot(p, e1),  p2  = dot(p, e2);
    float det = d11 * d22 - d12 * d12;
    if (std::fabs(det) < 1e-12f) return tr.normal;
    float inv = 1.0f / det;
    float v   = (d22 * p1 - d12 * p2) * inv;
    float w   = (d11 * p2 - d12 * p1) * inv;
    float u   = 1.0f - v - w;
    return normalize(tr.n0 * u + tr.n1 * v + tr.n2 * w);
}

static vec2 tri_interp_uv(const bvh::Tri& tr, const vec3& P) {
    vec3 e1 = tr.v1 - tr.v0, e2 = tr.v2 - tr.v0, p = P - tr.v0;
    float d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2);
    float p1  = dot(p, e1),  p2  = dot(p, e2);
    float det = d11 * d22 - d12 * d12;
    if (std::fabs(det) < 1e-12f) return tr.uv0;
    float inv = 1.0f / det;
    float vv  = (d22 * p1 - d12 * p2) * inv;
    float ww  = (d11 * p2 - d12 * p1) * inv;
    float uu  = 1.0f - vv - ww;
    return vec2(tr.uv0.x * uu + tr.uv1.x * vv + tr.uv2.x * ww,
                tr.uv0.y * uu + tr.uv1.y * vv + tr.uv2.y * ww);
}

static vec3 reflect_dir(vec3 d, vec3 n) { return d - n * (2.0f * dot(d, n)); }

// Deterministic per-surface-point hash (no RNG state, stable across frames):
// used to rotate the fixed GI sample set so neighbouring points don't share the
// exact same directions (breaks banding without introducing temporal noise).
static uint32_t hash_pos(const vec3& p) {
    auto bits = [](float f){ uint32_t u; std::memcpy(&u, &f, 4); return u; };
    uint32_t h = bits(p.x) * 73856093u ^ bits(p.y) * 19349663u ^ bits(p.z) * 83492791u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

// Van der Corput radical inverse base 2 — the second Hammersley coordinate for
// a low-discrepancy, well-spread set of hemisphere directions.
static float radinv2(uint32_t i) {
    i = (i << 16) | (i >> 16);
    i = ((i & 0x55555555u) << 1) | ((i & 0xAAAAAAAAu) >> 1);
    i = ((i & 0x33333333u) << 2) | ((i & 0xCCCCCCCCu) >> 2);
    i = ((i & 0x0F0F0F0Fu) << 4) | ((i & 0xF0F0F0F0u) >> 4);
    i = ((i & 0x00FF00FFu) << 8) | ((i & 0xFF00FF00u) >> 8);
    return i * 2.3283064365386963e-10f;
}

vec3 trace_ray(const ray& r, const RenderScene& scene, int depth) {
    SceneHit hit;
    if (!scene.accel->intersect(r.origin, r.direction, hit))
        return scene.skybox_enabled ? sample_sky(*scene.skybox, r.direction)
                                     : scene.bg_color;

    const bvh::Tri& tr = *hit.tri;

    // Emissive surfaces are the lights themselves: read out their emission.
    if (tr.emission.x + tr.emission.y + tr.emission.z > 0.0f)
        return tr.emission;

    vec3 P      = r.origin + r.direction * hit.t;
    vec3 N_geom = dot(tr.normal, r.direction) < 0.0f ? tr.normal : -tr.normal;
    vec3 N      = tr.smooth ? tri_smooth_normal(tr, P) : tr.normal;
    if (dot(N, r.direction) > 0.0f) N = -N;

    // Use texture albedo if available, otherwise fall back to material color
    vec3 albedo = tr.albedo;
    if (tr.tex) {
        vec2 uv = tri_interp_uv(tr, P);
        albedo = sample_face(*tr.tex, uv.x, uv.y);
    }

    // ---- Direct lighting (deterministic, hard shadows) ---------------------
    // Ambient fill + an optional directional sun, then every area light treated
    // as a point light at its centroid. No random sampling, so every shadow is
    // hard and the image is noise-free — classic ray tracing, not path tracing.
    float mono = AMBIENT;   // grey ambient + sun term
    if (scene.sun_enabled) {
        ray shadow(P + N_geom * SHADOW_EPS, SUN_DIR);
        if (!scene.accel->occluded(shadow.origin, shadow.direction, 1e30f))
            mono += std::max(0.0f, dot(N, SUN_DIR)) * (1.0f - AMBIENT);
    }

    vec3 light_color(mono, mono, mono);
    if (scene.emissive) for (const bvh::Tri& lt : *scene.emissive) {
        vec3  lp    = (lt.v0 + lt.v1 + lt.v2) * (1.0f / 3.0f);  // centroid
        vec3  ldir  = lp - P;
        float ldist = std::sqrt(dot(ldir, ldir));
        if (ldist < 1e-4f) continue;
        ldir = ldir / ldist;
        float NdotL = dot(N, ldir);
        if (NdotL <= 0.0f) continue;
        ray shadow(P + N_geom * SHADOW_EPS, ldir);
        if (scene.accel->occluded(shadow.origin, shadow.direction, ldist - SHADOW_EPS)) continue;
        vec3  e1      = lt.v1 - lt.v0, e2 = lt.v2 - lt.v0;
        float lt_area = std::sqrt(dot(cross(e1, e2), cross(e1, e2))) * 0.5f;
        float cos_lt  = std::fabs(dot(lt.normal, -ldir));
        float G       = lt_area * cos_lt / (ldist * ldist);
        light_color = light_color + lt.emission * (NdotL * G);
    }

    // Dynamic point lights: same deterministic treatment as the area lights —
    // one hard shadow ray, inverse-square falloff, no geometry so they can move.
    if (scene.point_lights) {
        for (const PointLight& pl : *scene.point_lights) {
            vec3  ldir  = pl.pos - P;
            float ldist = std::sqrt(dot(ldir, ldir));
            if (ldist < 1e-4f) continue;
            ldir = ldir / ldist;
            float NdotL = dot(N, ldir);
            if (NdotL <= 0.0f) continue;
            ray shadow(P + N_geom * SHADOW_EPS, ldir);
            if (scene.accel->occluded(shadow.origin, shadow.direction, ldist - SHADOW_EPS)) continue;
            float atten = pl.intensity / (ldist * ldist);
            light_color = light_color + pl.color * (NdotL * atten);
        }
    }

    float k_d  = (1.0f - tr.metallic) + tr.metallic * tr.roughness;
    vec3  local = vec3(albedo.x * light_color.x,
                       albedo.y * light_color.y,
                       albedo.z * light_color.z) * k_d;

    // ---- Indirect diffuse (deterministic one-bounce GI) --------------------
    // Only from the camera-primary diffuse hit (depth == 0), so it is exactly
    // one bounce and never recurses. A fixed, cosine-weighted set of hemisphere
    // directions (Hammersley), rotated per surface point by a position hash so
    // the pattern doesn't band — no RNG, so no fireflies and no temporal flicker.
    // Each sample gathers the DIRECT lighting of whatever it hits (secondary
    // call is forced terminal via a large depth: no specular, no further GI),
    // which is how the moving point light bleeds colour onto nearby surfaces.
    if (scene.gi_enabled && depth == 0 && k_d > 0.0f && tr.metallic < 0.5f) {
        vec3 T = normalize(std::fabs(N.x) > 0.9f ? cross(N, vec3(0, 1, 0))
                                                 : cross(N, vec3(1, 0, 0)));
        vec3 B = cross(N, T);
        float rot = (hash_pos(P) & 0xFFFFu) * (6.2831853f / 65535.0f);
        int   NS  = std::max(1, scene.gi_samples);
        vec3  gi(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < NS; ++i) {
            float u1  = (i + 0.5f) / NS;
            float phi = 6.2831853f * radinv2((uint32_t)i) + rot;
            float rr  = std::sqrt(u1);
            float sx  = rr * std::cos(phi), sy = rr * std::sin(phi);
            float sz  = std::sqrt(std::max(0.0f, 1.0f - u1));
            vec3  dir = normalize(T * sx + B * sy + N * sz);
            gi = gi + trace_ray(ray(P + N_geom * SHADOW_EPS, dir), scene, 1 << 20);
        }
        gi = gi * (1.0f / NS);
        local = local + vec3(albedo.x * gi.x, albedo.y * gi.y, albedo.z * gi.z)
                        * (k_d * scene.gi_strength);
    }

    float cosV = std::max(0.0f, dot(N, -r.direction));
    float F0   = tr.metallic > 0.5f
                 ? (albedo.x + albedo.y + albedo.z) / 3.0f
                 : 0.04f;
    float fres = F0 + (1.0f - F0) * std::pow(1.0f - cosV, 5.0f);
    float spec = fres * (1.0f - tr.roughness);

    if (depth >= scene.max_bounces || spec <= 0.01f || !scene.reflections) return local;

    vec3 R = reflect_dir(r.direction, N);
    float RdotG = dot(R, N_geom);
    if (RdotG < 0.0f) {
        R = normalize(R - N_geom * RdotG);
        R = normalize(R + N_geom * 1e-4f);
    }
    vec3 refl = trace_ray(ray(P + N_geom * SHADOW_EPS, R), scene, depth + 1);

    vec3 tint = tr.metallic > 0.5f ? albedo : vec3(1.0f, 1.0f, 1.0f);
    return local * (1.0f - spec) + vec3(refl.x * spec * tint.x,
                                        refl.y * spec * tint.y,
                                        refl.z * spec * tint.z);
}
