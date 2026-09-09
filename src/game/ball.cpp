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
    // Semi-implicit (symplectic) Euler: integrate velocity, then position.
    vel.y -= gravity * h;
    pos    = pos + vel * h;

    int c = 0;

    // Side walls (X) — existing walls, now with restitution.
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
