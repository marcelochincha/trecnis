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

    // Velocity response about a possibly-MOVING surface. Apply restitution to
    // the normal component of the ball velocity RELATIVE to the surface:
    //   vn_rel = (v - vel) . n           (< 0 while the two approach)
    //   vn_rel -> -restitution * vn_rel
    //   => v -= n * (1 + restitution) * vn_rel
    // The surface is infinite mass (kinematic racket): `vel` is unchanged and
    // only the ball's normal component moves; its tangential velocity is left
    // as is (frictionless). Stationary surface (vel == 0) reduces exactly to the
    // previous |v_n'| = e|v_n| bounce.
    float vn_rel = dot(v - vel, n);
    if (vn_rel < 0.0f) v = v - n * ((1.0f + restitution) * vn_rel);
    return true;
}

// ---------------------------------------------------------------------------
// Ball integration
// ---------------------------------------------------------------------------

int Ball::update(float dt, const AABB& b, const Obb* racket, const Table* table, const Wall* wall) {
    if (dt < 0.0f) dt = 0.0f;
    accum_ += std::min(dt, kMaxCatchUp);

    int contacts = 0;
    while (accum_ >= kFixedStep) {
        contacts += step_fixed(kFixedStep, b, racket, table, wall);
        accum_   -= kFixedStep;
    }
    return contacts;
}

int Ball::step_fixed(float h, const AABB& b, const Obb* racket, const Table* table, const Wall* wall) {
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

    const float prev_y = pos.y;   // table needs this: only a top-down arrival counts
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

    // Table surface (only inside its footprint; a no-op elsewhere, so the ball
    // still falls through to the room floor below once it clears the table).
    if (table && table->resolve(prev_y, pos, vel, radius, restitution, rest_speed)) ++c;

    // Net: a finite box centred on z = 0, straddling the table's surface
    // (Table::resolve_net). A no-op for any ball whose arc clears net_height
    // or that is off to the side -- only an actual intersection responds.
    if (table && table->resolve_net(pos, vel, radius, restitution)) { ++c; ++net_hits; }

    // Backdrop wall, flush against the table's far edge: reflects the
    // approaching (+Z) ball back toward -Z with restitution, confined to the
    // wall's finite panel (Wall::resolve). A no-op elsewhere, so a ball that
    // never reaches the wall's X/Y footprint (or the far side of the table)
    // is unaffected.
    if (wall && wall->resolve(pos, vel, radius, restitution)) ++c;

    // Floor / table (Y min): reflect with restitution, or settle to rest once
    // the rebound would be negligible (normal kinetic energy fully dissipated).
    if (pos.y - radius < b.min.y) {
        pos.y = b.min.y + radius;
        float v_in = -vel.y;                        // incoming downward speed (>= 0)
        if (v_in > rest_speed) { vel.y = v_in * restitution; ++c; }
        else                     vel.y = 0.0f;
    }

    // Racket (oriented box, may be moving). Resolved per sub-step so the ball
    // cannot tunnel through it; the response uses the ball-vs-racket relative
    // normal velocity (Obb::resolve).
    if (racket) {
        float sp_before = magnitude(vel);
        if (racket->resolve(pos, vel, radius)) {
            ++c;
            ++racket_hits;
            hit_speed_in     = sp_before;
            hit_speed_out    = magnitude(vel);
            hit_racket_speed = magnitude(racket->vel);
        }
    }

    return c;
}
