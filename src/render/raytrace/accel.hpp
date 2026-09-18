#pragma once
#include <render/raytrace/bvh.hpp>



struct SceneHit {
    float           t   = 1e30f;
    const bvh::Tri* tri = nullptr;
};




struct ISceneAccel {
    virtual ~ISceneAccel() = default;


    virtual bool intersect(const vec3& origin, const vec3& dir, SceneHit& out) const = 0;


    virtual bool occluded(const vec3& origin, const vec3& dir, float max_t) const = 0;
};
