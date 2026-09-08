#pragma once
// Ball: position + velocity + radius, integrated with delta time. This is the
// only place ball motion rules live. It knows nothing about rendering, the BVH,
// or SDL — pure math over vec3 + AABB.

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>   // AABB

struct Ball {
    vec3  pos    = vec3(0.0f, 2.0f, 0.0f);
    vec3  vel    = vec3(0.0f, 0.0f, 0.0f);
    float radius = 0.5f;

    // Advance one step: constant-velocity motion (no gravity/drag yet — that is
    // the advanced-physics phase) then reflect off the inside faces of `bounds`
    // so the ball stays in the arena. Frame-rate independent: state only changes
    // through `dt`. Returns the number of wall reflections this step.
    int update(float dt, const AABB& bounds);
};
