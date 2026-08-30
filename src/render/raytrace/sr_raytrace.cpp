#include <render/raytrace/sr_raytrace.hpp>
#include <core/sr_texture.hpp>
#include <algorithm>
#include <cmath>

const vec3  SUN_DIR    = normalize(vec3(-0.3f, 1.0f, -0.2f));
const float AMBIENT    = 0.15f;
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

static float randf(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
}

vec3 trace_ray(const ray& r, const RenderScene& scene, int depth, uint32_t& seed, bool skip_emission) {
    SceneHit hit;
    if (!scene.accel->intersect(r.origin, r.direction, hit))
        return scene.skybox_enabled ? sample_sky(*scene.skybox, r.direction)
                                     : vec3(AMBIENT, AMBIENT, AMBIENT);

    const bvh::Tri& tr = *hit.tri;

    if (tr.emission.x + tr.emission.y + tr.emission.z > 0.0f)
        return skip_emission ? vec3(0.0f, 0.0f, 0.0f) : tr.emission;

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

    const std::vector<bvh::Tri>& lights = *scene.emissive;

    vec3 light_color;
    if (lights.empty()) {
        ray  shadow(P + N_geom * SHADOW_EPS, SUN_DIR);
        bool in_shadow = scene.accel->occluded(shadow.origin, shadow.direction, 1e30f);
        float diff = in_shadow ? 0.0f : std::max(0.0f, dot(N, SUN_DIR));
        float l = AMBIENT + diff * (1.0f - AMBIENT);
        light_color = vec3(l, l, l);
    } else {
        // NEE: pick one random emissive triangle, weight by N_lt (unbiased)
        vec3 direct(0.0f, 0.0f, 0.0f);
        int N_lt = (int)lights.size();
        int li   = std::min((int)(randf(seed) * N_lt), N_lt - 1);
        const bvh::Tri& lt = lights[li];
        float r1 = randf(seed), r2 = randf(seed);
        float sq1 = std::sqrt(r1);
        vec3  lp    = lt.v0 * (1.0f - sq1) + lt.v1 * (sq1 * (1.0f - r2)) + lt.v2 * (sq1 * r2);
        vec3  ldir  = lp - P;
        float ldist = std::sqrt(dot(ldir, ldir));
        if (ldist >= 1e-4f) {
            ldir = ldir / ldist;
            float NdotL = dot(N, ldir);
            if (NdotL > 0.0f) {
                ray shadow(P + N_geom * SHADOW_EPS, ldir);
                if (!scene.accel->occluded(shadow.origin, shadow.direction, ldist - SHADOW_EPS)) {
                    vec3  e1     = lt.v1 - lt.v0, e2 = lt.v2 - lt.v0;
                    float lt_area = std::sqrt(dot(cross(e1, e2), cross(e1, e2))) * 0.5f;
                    float cos_lt  = std::fabs(dot(lt.normal, -ldir));
                    float G       = lt_area * cos_lt / (ldist * ldist);
                    direct = lt.emission * (NdotL * G * (float)N_lt);
                }
            }
        }
        light_color = direct;
    }

    float k_d  = (1.0f - tr.metallic) + tr.metallic * tr.roughness;
    vec3  local = vec3(albedo.x * light_color.x,
                       albedo.y * light_color.y,
                       albedo.z * light_color.z) * k_d;

    if (!lights.empty() && depth < scene.max_bounces && tr.metallic < 0.5f) {
        float di1 = randf(seed), di2 = randf(seed);
        float phi   = 2.0f * 3.14159265f * di1;
        float cos_t = std::sqrt(di2);
        float sin_t = std::sqrt(std::max(0.0f, 1.0f - di2));
        vec3 up = std::fabs(N.x) < 0.9f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 1.0f, 0.0f);
        vec3 T  = normalize(cross(up, N));
        vec3 B  = cross(N, T);
        vec3 bd = T * (sin_t * std::cos(phi)) + B * (sin_t * std::sin(phi)) + N * cos_t;
        vec3 ind = trace_ray(ray(P + N_geom * SHADOW_EPS, bd), scene, depth + 1, seed, true);
        ind = vec3(std::min(ind.x, 2.0f), std::min(ind.y, 2.0f), std::min(ind.z, 2.0f));
        local = local + vec3(albedo.x * ind.x, albedo.y * ind.y, albedo.z * ind.z) * k_d;
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
    vec3 refl = trace_ray(ray(P + N_geom * SHADOW_EPS, R), scene, depth + 1, seed);

    vec3 tint = tr.metallic > 0.5f ? albedo : vec3(1.0f, 1.0f, 1.0f);
    return local * (1.0f - spec) + vec3(refl.x * spec * tint.x,
                                        refl.y * spec * tint.y,
                                        refl.z * spec * tint.z);
}

vec3 trace_ray(const ray& r, const RenderScene& scene) {
    uint32_t seed = 0;
    return trace_ray(r, scene, 0, seed);
}
