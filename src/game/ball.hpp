#pragma once
// Ball: linear + angular state, integrated with delta time under gravity, drag
// and the Magnus effect. This is the only place ball motion rules live. It knows
// nothing about rendering, the BVH or SDL — pure math over vec3 + AABB.
//
// Integration: a fixed physics sub-step (1/240 s) is consumed from an
// accumulator, so the trajectory is frame-rate independent. Each sub-step:
//
//   a      = -g*y_hat + magnus * (spin x vel)        (gravity + Magnus)
//   vel   += a * h
//   vel    = vel / (1 + drag * |vel| * h)            (quadratic drag, implicit)
//   spin   = spin / (1 + spin_decay * h)             (spin bleeds to the air)
//   pos   += vel * h
//
// The drag step is written in the semi-implicit form v /= (1 + k|v|h): the
// factor is always in (0,1], so speed can only decrease and never overshoots or
// flips sign — unconditionally stable, no CFL-style limit on h or |v|.
//
// A surface contact reflects the normal velocity and scales it by the
// restitution e (|v_n'| = e|v_n|), so normal kinetic energy drops to e^2 per
// bounce. Contact is frictionless this phase: bounces do not change spin and
// spin does not change the bounce.

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>   // AABB

struct Ball {
    vec3  pos    = vec3(0.0f, 2.0f, 0.0f);
    vec3  vel    = vec3(0.0f, 0.0f, 0.0f);
    vec3  spin   = vec3(0.0f, 0.0f, 0.0f);   // angular velocity, rad/s (any axis)
    float radius = 0.5f;

    // Tunables (scene units, seconds, radians).
    float gravity     = 9.81f;   // downward acceleration along -Y
    float restitution = 0.75f;   // fraction of normal speed kept per bounce
    float rest_speed  = 0.50f;   // floor rebound slower than this -> settle (rest)
    float drag        = 0.10f;   // quadratic aerodynamic drag coefficient
    float magnus      = 0.12f;   // Magnus coefficient: a += magnus * (spin x vel)
    float spin_decay  = 0.10f;   // 1/s, mild loss of spin to the air

    // Advance by the real frame dt. Returns the number of rebounds this frame
    // (a settle onto the floor does not count).
    int update(float dt, const AABB& bounds);

    float speed()     const { return magnitude(vel); }
    float spin_rate() const { return magnitude(spin); }

private:
    float accum_ = 0.0f;                       // unconsumed simulated time
    int   step_fixed(float h, const AABB& b);  // one fixed sub-step
};
