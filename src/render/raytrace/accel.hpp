#pragma once
#include <render/raytrace/bvh.hpp>   // bvh::Tri, vec3

// Result of a nearest-hit scene query: distance along the ray and the triangle
// that was hit (borrowed, owned by the acceleration structure).
struct SceneHit {
    float           t   = 1e30f;
    const bvh::Tri* tri = nullptr;
};

// Acceleration structure the CPU ray tracer queries. Both our own SAH BVH and
// the Embree reference backend implement it, so the shading core (trace_ray)
// is written once against this interface and never branches on the backend.
struct ISceneAccel {
    virtual ~ISceneAccel() = default;

    // Nearest hit against the whole scene (static + dynamic).
    virtual bool intersect(const vec3& origin, const vec3& dir, SceneHit& out) const = 0;

    // Any-hit up to max_t, for shadow rays.
    virtual bool occluded(const vec3& origin, const vec3& dir, float max_t) const = 0;
};
