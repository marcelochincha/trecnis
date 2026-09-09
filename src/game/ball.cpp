#include <game/ball.hpp>
#include <algorithm>
#include <cmath>

static constexpr float kFixedStep  = 1.0f / 240.0f;   // physics sub-step (s)
static constexpr float kMaxCatchUp = 0.25f;           // clamp: avoid spiral of death

int Ball::update(float dt, const AABB& b) {
    if (dt < 0.0f) dt = 0.0f;
    accum_ += std::min(dt, kMaxCatchUp);

    int contacts = 0;
    while (accum_ >= kFixedStep) {
        contacts += step_fixed(kFixedStep, b);
        accum_   -= kFixedStep;
    }
    return contacts;
}

int Ball::step_fixed(float h, const AABB& b) {
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

    // --- collisions: gravity + restitution, unchanged from checkpoint 11ae276 ---
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

    return c;
}
