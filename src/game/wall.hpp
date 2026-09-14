#pragma once
// Backdrop wall behind the table's far edge: STATIC geometry (a single box)
// plus the ball<->wall collision. Same pattern as Table (one struct owns
// both the visual triangles and the collider, from the same fields, so they
// can never drift apart). The wall's near face (inner_z) is the physical
// collision plane; its normal, from the ball's side, points toward -Z.

#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>   // bvh::Tri

struct Wall {
    float inner_z    = 0.0f;   // near face z (the collision plane) -- set once
                                // in game_init from table.half_len + a small gap
    float thickness  = 0.10f;  // Z extent, cosmetic depth only
    float half_width = 0.85f;  // X half-extent
    float height     = 1.1f;   // Y extent, from the floor (y = 0) up

    // World-space triangles for the wall box. Built from the same fields the
    // collider below uses -- no separate physics-only coordinate.
    void append_tris(std::vector<bvh::Tri>& out) const;

    // Sphere vs the wall's near face: a finite vertical panel at z = inner_z,
    // spanning x in [-half_width,half_width] and y in [0,height]. Unlike
    // Table::resolve, this needs no previous-substep tracking: the table's
    // plane has a legitimate "underside" (the room floor beneath it) that a
    // resting ball can validly occupy, so it must check the ball was already
    // above the surface. The wall has no such far side in this scene -- the
    // half-space check (current position past the plane -> push back out)
    // is exactly the pattern the arena's own Z-bound already uses in
    // Ball::step_fixed, just confined to a finite panel instead of an
    // infinite plane, so it corrects the position on any single sub-step
    // that would tunnel past inner_z, however far it moved that step.
    // Reflects the normal (Z) velocity component with `restitution` (a
    // stationary-surface reduction of the racket's relative-velocity
    // formula -- restitution is the ball's own coefficient, not a separate
    // wall material, matching how the table/arena bounds already reuse it);
    // X and Y velocity are left untouched (frictionless, tangential).
    // Returns true on contact.
    bool resolve(vec3& pos, vec3& vel, float radius, float restitution) const;
};
