#include <game/ball.hpp>
#include <algorithm>
#include <cmath>

static constexpr float kFixedStep  = 1.0f / 240.0f;   // physics sub-step (s)
static constexpr float kMaxCatchUp = 0.25f;           // clamp: avoid spiral of death

// ---------------------------------------------------------------------------
// Obb: sphere vs oriented box
// ---------------------------------------------------------------------------

bool Obb::resolve(vec3& c, vec3& v, float r) const {
    // Sphere centre relative to the box, projected onto the box axes.
    vec3  d  = c - center;
    float lx = dot(d, axis[0]);
    float ly = dot(d, axis[1]);
    float lz = dot(d, axis[2]);

    // Closest point of the box to the sphere centre (clamp to the slab widths).
    float qx = std::clamp(lx, -half.x, half.x);
    float qy = std::clamp(ly, -half.y, half.y);
    float qz = std::clamp(lz, -half.z, half.z);
    vec3  closest = center + axis[0] * qx + axis[1] * qy + axis[2] * qz;

    vec3  delta = c - closest;
    float dist2 = dot(delta, delta);
    if (dist2 >= r * r) return false;                 // no contact

    vec3  n;
    float dist = std::sqrt(dist2);
    if (dist > 1e-6f) {
        n = delta / dist;                            // centre is outside the box
    } else {
        // Centre inside the box: eject along the least-penetrated face.
        float px = half.x - std::fabs(lx);
        float py = half.y - std::fabs(ly);
        float pz = half.z - std::fabs(lz);
        if      (px <= py && px <= pz) n = axis[0] * (lx < 0.0f ? -1.0f : 1.0f);
        else if (py <= pz)             n = axis[1] * (ly < 0.0f ? -1.0f : 1.0f);
        else                           n = axis[2] * (lz < 0.0f ? -1.0f : 1.0f);
        dist = 0.0f;
    }

    // Positional correction: put the sphere just outside the surface.
    c = c + n * (r - dist);

    // Velocity response: reflect only the component moving into the surface.
    float vn = dot(v, n);
    if (vn < 0.0f) v = v - n * ((1.0f + restitution) * vn);
    return true;
}

// ---------------------------------------------------------------------------
// Ball integration
// ---------------------------------------------------------------------------

int Ball::update(float dt, const AABB& b, const Obb* racket) {
    if (dt < 0.0f) dt = 0.0f;
    accum_ += std::min(dt, kMaxCatchUp);

    int contacts = 0;
    while (accum_ >= kFixedStep) {
        contacts += step_fixed(kFixedStep, b, racket);
        accum_   -= kFixedStep;
    }
    return contacts;
}

int Ball::step_fixed(float h, const AABB& b, const Obb* racket) {
    // --- forces -> acceleration (gravity + Magnus), then velocity ---
    // Magnus is perpendicular to vel, so it curves the path without adding speed.
    vec3 a = vec3(0.0f, -gravity, 0.0f) + magnus * cross(spin, vel);
    vel = vel + a * h;

    // Quadratic aerodynamic drag, semi-implicit: v /= (1 + k|v|h). The factor is
    // always in (0,1] -> speed only decreases, never overshoots (unconditionally
    // stable, no limit on h or |v|).
    float sp = magnitude(vel);
    if (sp > 1e-6f) vel = vel / (1.0f + drag * sp * h);

    // Spin slowly bleeds to the air (no contact friction this phase).
    if (spin_decay > 0.0f) spin = spin / (1.0f + spin_decay * h);

    pos = pos + vel * h;

    // --- collisions ---
    int c = 0;

    // Side walls (X).
    if (pos.x - radius < b.min.x)      { pos.x = b.min.x + radius; vel.x = -vel.x * restitution; ++c; }
    else if (pos.x + radius > b.max.x) { pos.x = b.max.x - radius; vel.x = -vel.x * restitution; ++c; }

    // Front / back planes (Z).
    if (pos.z - radius < b.min.z)      { pos.z = b.min.z + radius; vel.z = -vel.z * restitution; ++c; }
    else if (pos.z + radius > b.max.z) { pos.z = b.max.z - radius; vel.z = -vel.z * restitution; ++c; }

    // Ceiling (Y max).
    if (pos.y + radius > b.max.y)      { pos.y = b.max.y - radius; vel.y = -vel.y * restitution; ++c; }

    // Floor / table (Y min): reflect with restitution, or settle to rest once
    // the rebound would be negligible (normal kinetic energy fully dissipated).
    if (pos.y - radius < b.min.y) {
        pos.y = b.min.y + radius;
        float v_in = -vel.y;                        // incoming downward speed (>= 0)
        if (v_in > rest_speed) { vel.y = v_in * restitution; ++c; }
        else                     vel.y = 0.0f;
    }

    // Static racket (oriented box). Resolved per sub-step so the ball cannot
    // tunnel through it.
    if (racket && racket->resolve(pos, vel, radius)) { ++c; ++racket_hits; }

    return c;
}
