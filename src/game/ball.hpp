#pragma once
// Ball: position + velocity + radius, integrated with delta time under gravity.
// This is the only place ball motion rules live. It knows nothing about
// rendering, the BVH, or SDL — pure math over vec3 + AABB.
//
// Integration: a fixed physics sub-step (1/240 s) is consumed from an
// accumulator, so the trajectory is frame-rate independent. Each sub-step is
// semi-implicit (symplectic) Euler with constant gravity along -Y. A surface
// contact reflects the normal velocity component and scales it by the
// restitution coefficient e: |v_n'| = e * |v_n|, so the kinetic energy in the
// normal direction drops to e^2 of its pre-impact value — a physically coherent
// loss per bounce. Tangential velocity is unchanged (frictionless).
//
// Not modelled yet (later phase): spin, aerodynamic drag, Magnus.

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>   // AABB

struct Ball {
    vec3  pos    = vec3(0.0f, 2.0f, 0.0f);
    vec3  vel    = vec3(0.0f, 0.0f, 0.0f);
    float radius = 0.5f;

    // Tunables (scene units, seconds).
    float gravity     = 9.81f;   // downward acceleration along -Y
    float restitution = 0.75f;   // fraction of normal speed kept per bounce
    float rest_speed  = 0.50f;   // floor rebound slower than this -> settle (rest)

    // Advance by the real frame dt. Returns the number of rebounds this frame
    // (a settle onto the floor does not count).
    int update(float dt, const AABB& bounds);

    float speed() const { return magnitude(vel); }

private:
    float accum_ = 0.0f;                       // unconsumed simulated time
    int   step_fixed(float h, const AABB& b);  // one fixed sub-step
};
