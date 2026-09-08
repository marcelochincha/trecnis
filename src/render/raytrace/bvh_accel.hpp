#pragma once
#include <render/raytrace/accel.hpp>
#include <vector>

// Acceleration backed by our own SAH BVH. Wraps the app's two trees (static
// scenery + per-frame dynamic) plus the flat triangle list used for the
// brute-force fallback. All pointers are borrowed and refreshed each frame.
class BvhAccel : public ISceneAccel {
public:
    const bvh::BVH*              static_bvh  = nullptr;
    const bvh::BVH*              dynamic_bvh = nullptr;
    const std::vector<bvh::Tri>* brute_tris = nullptr;
    bool use_bvh = true;

    bool intersect(const vec3& origin, const vec3& dir, SceneHit& out) const override;
    bool occluded(const vec3& origin, const vec3& dir, float max_t) const override;
};
