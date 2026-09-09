#pragma once
// Ball: linear + angular state, integrated with delta time under gravity, drag
// and the Magnus effect. This is the only place ball motion rules live. It knows
// nothing about rendering, the BVH or SDL — pure math over vec3 + AABB + Obb.
//
// Integration: a fixed physics sub-step (1/240 s) is consumed from an
// accumulator, so the trajectory is frame-rate independent. Each sub-step:
//
//   a      = -g*y_hat + magnus * (spin x vel)        (gravity + Magnus)
//   vel   += a * h
//   vel    = vel / (1 + drag * |vel| * h)            (quadratic drag, implicit)
//   spin   = spin / (1 + spin_decay * h)             (spin bleeds to the air)
//   pos   += vel * h
//   -> resolve arena walls/floor, then the racket (Obb)
//
// The drag step is written in the semi-implicit form v /= (1 + k|v|h): the
// factor is always in (0,1], so speed can only decrease and never overshoots or
// flips sign — unconditionally stable, no CFL-style limit on h or |v|.
//
// A contact applies the restitution e to the NORMAL component of the ball's
// velocity RELATIVE to the surface: (v_rel . n) goes from vn_rel to -e*vn_rel.
// For a stationary surface this is the usual |v_n'| = e|v_n| (normal kinetic
// energy -> e^2). A racket moving into the ball raises the outgoing normal
// speed to (1+e) times its approach; a racket pulling away softens the bounce.
// Contact is frictionless this phase: it does not change spin, and only the
// ball's normal component is touched (its tangential velocity is untouched).

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>   // AABB

// Oriented bounding box obstacle — pure math. The racket produces one of these
// (its centre + velocity track the paddle) and hands it to Ball::update; the
// ball resolves against it per sub-step so it cannot tunnel through.
struct Obb {
    vec3  center      = vec3(0.0f, 0.0f, 0.0f);
    vec3  axis[3]     = { vec3(1,0,0), vec3(0,1,0), vec3(0,0,1) }; // orthonormal
    vec3  half        = vec3(0.5f, 0.5f, 0.5f);                     // half-extents
    vec3  vel         = vec3(0.0f, 0.0f, 0.0f);                     // surface velocity
    float restitution = 0.85f;

    // Sphere (centre c, radius r) vs this box. On penetration: push c out along
    // the contact normal (anti-tunneling) and update the ball's NORMAL velocity
    // component using the ball-relative-to-surface normal speed and restitution
    // (see the header comment). The surface is treated as infinite mass, so
    // `vel` is unchanged. Returns true on contact.
    bool resolve(vec3& c, vec3& v, float r) const;
};

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

    long  racket_hits      = 0;  // cumulative racket contacts (telemetry)
    float hit_speed_in     = 0.0f;  // ball |v| just before the last racket contact
    float hit_speed_out    = 0.0f;  // ball |v| just after
    float hit_racket_speed = 0.0f;  // racket |v| at that contact

    // Advance by the real frame dt. `racket` may be null. Returns the number of
    // contacts this frame (rebounds; a settle onto the floor does not count).
    int update(float dt, const AABB& bounds, const Obb* racket = nullptr);

    float speed()     const { return magnitude(vel); }
    float spin_rate() const { return magnitude(spin); }

private:
    float accum_ = 0.0f;                                          // unconsumed simulated time
    int   step_fixed(float h, const AABB& b, const Obb* racket);  // one fixed sub-step
};
