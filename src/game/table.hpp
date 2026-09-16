#pragma once
// Regulation table-tennis table: STATIC geometry (surface, edge lines, net,
// legs) plus the ball<->surface AND ball<->net collision. Dimensions are the
// single source of truth shared by both, so the visual mesh and the physics
// footprint can never drift apart. Values match the reference implementation
// (trecnis, ballphys::Table): 2.74 x 1.525 m, surface 76 cm up, a 15.25 cm
// net on the z = 0 plane. +Y up; X is the lateral axis, Z the long one (net
// at z = 0, the two halves are z < 0 and z > 0).

#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>   // bvh::Tri

struct Table {
    float height       = 0.76f;    // playing surface, above the floor (+Y)
    float half_width   = 0.7625f;  // X half-extent (1.525 m wide)
    float half_len     = 1.37f;    // Z half-extent (2.74 m long)
    float net_height   = 0.1525f;  // above the surface
    float net_thickness = 0.006f;  // net box Z half-extent -- single source of
                                    // truth for both append_tris and resolve_net

    // World-space triangles for the surface, edge lines, net and four legs.
    // Appended directly (the table does not move), matching how the arena
    // walls are built in game_rebuild_static.
    void append_tris(std::vector<bvh::Tri>& out) const;

    // Sphere vs the table TOP surface: bounces like the arena floor (same
    // restitution + rest_speed settle rule the ball already uses) but only
    // while the ball's X/Z falls inside the table's footprint AND it was
    // above the surface at `prev_y` (its Y one sub-step ago) — a plane has no
    // underside, so this rejects a ball that is merely passing/resting below
    // the table (e.g. rolling on the room floor underneath it). No side/leg/
    // net collision this checkpoint. Returns true on an actual bounce (not on
    // settling to rest).
    bool resolve(float prev_y, vec3& pos, vec3& vel, float radius,
                 float restitution, float rest_speed) const;

    // Sphere vs the net: a finite thin vertical box centred on z = 0 --
    // x in [-half_width,half_width], y in [height,height+net_height], z in
    // [-net_thickness,net_thickness] -- the SAME box append_tris draws (this
    // struct is the one source of truth for both). Unlike Wall::resolve (an
    // infinite-height panel), the net is finite in Y too: closest-point-on-box
    // naturally lets a ball whose arc clears net_height pass straight over
    // with no contact at all. Response mirrors Obb::resolve (closest point on
    // the box, push the ball out along that normal, reflect the normal
    // velocity component with `restitution` -- the ball's own coefficient,
    // matching how Table::resolve/Wall::resolve already reuse it, no separate
    // "net material") but specialised to a stationary, axis-aligned box.
    // Tangential velocity is untouched (frictionless, same policy as Wall).
    // Returns true on contact.
    bool resolve_net(vec3& pos, vec3& vel, float radius, float restitution) const;
};
