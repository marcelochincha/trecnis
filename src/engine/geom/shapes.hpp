#pragma once
// Procedural primitive generators that emit shadeable BVH triangles directly.
// Ported verbatim (namespaced) from raytrec/src/game/sr_scene.cpp. Used to build
// both the static scene (floor + walls) and the dynamic ball geometry.

#include <vector>
#include <render/raytrace/bvh.hpp>   // bvh::Tri, vec3 (via math)

namespace geom {

// Axis-aligned box from two opposite corners -> 12 triangles, outward normals.
void add_box(std::vector<bvh::Tri>& out,
             const vec3& lo, const vec3& hi,
             const vec3& albedo, float roughness = 0.5f,
             float metallic = 0.0f, float ior = 1.5f);

// Same, from centre + half-extents.
void add_box_c(std::vector<bvh::Tri>& out,
               const vec3& center, const vec3& half,
               const vec3& albedo, float roughness = 0.5f,
               float metallic = 0.0f, float ior = 1.5f);

// UV sphere -> slices*stacks*2 triangles. With smooth=true the per-vertex
// normals (n0/n1/n2) make it render round at low tri counts.
void add_sphere(std::vector<bvh::Tri>& out,
                const vec3& center, float radius,
                const vec3& albedo, float roughness, float metallic, float ior,
                int slices, int stacks, bool smooth = true);

} // namespace geom
