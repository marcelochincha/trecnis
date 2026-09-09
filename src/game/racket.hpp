#pragma once
// Racket: a static oriented paddle (thin box). This checkpoint it does not move.
//
// It contributes:
//   - shaded triangles for the STATIC BVH / raster mirror (append_tris)
//   - an Obb collider for the physics step (collider)
//
// It never touches the renderer or the BVH directly — game.cpp folds its
// triangles into the same static set as the arena and passes its collider to
// Ball::update. Architecture stays Game -> RenderScene -> Renderer.

#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>   // bvh::Tri
#include <game/ball.hpp>             // Obb

class Racket {
public:
    // Configure the static racket. `euler` is (pitch, yaw, roll) in radians;
    // `size` is the full box extents (width, height, thickness). The local +Z
    // face (thickness axis) is the hitting face.
    void configure(const vec3& pos, const vec3& euler, const vec3& size,
                   float restitution);

    // Append the racket's world-space shaded triangles to `out`.
    void append_tris(std::vector<bvh::Tri>& out) const;

    const Obb& collider()    const { return obb_; }
    vec3       position()    const { return pos_; }
    vec3       face_normal() const { return obb_.axis[2]; }   // local +Z, world
    std::size_t tri_count()  const { return 12; }

private:
    vec3 pos_    = vec3(0.0f, 0.0f, 0.0f);
    vec3 euler_  = vec3(0.0f, 0.0f, 0.0f);
    vec3 size_   = vec3(1.0f, 1.0f, 0.2f);
    vec3 albedo_ = vec3(0.20f, 0.45f, 0.85f);
    Obb  obb_;
};
