#include <render/raytrace/bvh_accel.hpp>
#include <cmath>


static bool ray_intersect_triangle(const vec3& o, const vec3& d,
                                   const vec3& v0, const vec3& v1, const vec3& v2,
                                   float& t)
{
    const float EPS = 1e-8f;
    vec3 e1 = v1 - v0, e2 = v2 - v0;
    vec3 h  = cross(d, e2);
    float a = dot(e1, h);
    if (std::fabs(a) < EPS) return false;
    float f = 1.0f / a;
    vec3  s = o - v0;
    float u = f * dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    vec3  q = cross(s, e1);
    float v = f * dot(d, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * dot(e2, q);
    return t > EPS;
}

bool BvhAccel::intersect(const vec3& origin, const vec3& dir, SceneHit& out) const {
    bool  hit  = false;
    float best = 1e30f;

    if (use_bvh) {
        if (dynamic_bvh) {
            bvh::Hit h;
            if (dynamic_bvh->intersect(origin, dir, h)) {
                best = h.t; out.t = h.t; out.tri = &dynamic_bvh->tri(h.tri); hit = true;
            }
        }
    } else if (brute_tris) {
        const bvh::Tri* bt = nullptr;
        for (const bvh::Tri& tr : *brute_tris) {
            float t;
            if (ray_intersect_triangle(origin, dir, tr.v0, tr.v1, tr.v2, t) && t < best)
                { best = t; bt = &tr; }
        }
        if (bt) { out.t = best; out.tri = bt; hit = true; }
    }

    if (static_bvh && !static_bvh->empty()) {
        bvh::Hit h; h.t = best;
        if (static_bvh->intersect(origin, dir, h)) {
            best = h.t; out.t = h.t; out.tri = &static_bvh->tri(h.tri); hit = true;
        }
    }
    return hit;
}

bool BvhAccel::occluded(const vec3& origin, const vec3& dir, float max_t) const {
    if (static_bvh && !static_bvh->empty() && static_bvh->occluded(origin, dir, max_t))
        return true;
    if (use_bvh)
        return dynamic_bvh && dynamic_bvh->occluded(origin, dir, max_t);
    if (brute_tris) {
        for (const bvh::Tri& tr : *brute_tris) {
            float t;
            if (ray_intersect_triangle(origin, dir, tr.v0, tr.v1, tr.v2, t) && t < max_t)
                return true;
        }
    }
    return false;
}
