#pragma once
// Racket: a user-movable oriented paddle (thin box).
//
// This checkpoint the racket can be translated by the player; its orientation
// and dimensions stay as configured. It contributes:
//   - 12 shaded triangles (local space, orientation baked) for the DYNAMIC BVH
//     and a raster mesh — the caller translates them to position() each frame
//   - an Obb collider (its centre tracks position()) for the physics step
//
// It never touches the renderer or the BVH directly. Its velocity is derived
// from the movement for telemetry only — it is NOT fed into the ball's collision
// response this checkpoint.

#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>   // bvh::Tri
#include <game/ball.hpp>             // Obb

class Racket {
public:
    // One-time setup. `euler` is (pitch, yaw, roll) in radians; `size` is the
    // full box extents (width, height, thickness). Orientation and size are
    // fixed after this — only the position moves (step()).
    void configure(const vec3& pos, const vec3& euler, const vec3& size,
                   float restitution);

    // Translate by `dir` (per-axis intent, need not be unit) at `speed` u/s for
    // `dt` seconds, clamped so the centre stays inside `limits`. Recomputes the
    // velocity from the actual displacement and moves the collider centre.
    void step(const vec3& dir, const AABB& limits, float dt, float speed);

    // 12 shaded triangles: box centred at the ORIGIN with the configured
    // orientation baked in. The caller adds position() each frame.
    void append_local_tris(std::vector<bvh::Tri>& out) const;

    const Obb&  collider()    const { return obb_; }
    vec3        position()    const { return pos_; }
    vec3        velocity()    const { return vel_; }   // telemetry only
    vec3        face_normal() const { return obb_.axis[2]; }
    std::size_t tri_count()   const { return 12; }

private:
    vec3 pos_    = vec3(0.0f, 0.0f, 0.0f);
    vec3 vel_    = vec3(0.0f, 0.0f, 0.0f);
    vec3 euler_  = vec3(0.0f, 0.0f, 0.0f);
    vec3 size_   = vec3(1.0f, 1.0f, 0.2f);
    vec3 albedo_ = vec3(0.20f, 0.45f, 0.85f);
    Obb  obb_;
};
