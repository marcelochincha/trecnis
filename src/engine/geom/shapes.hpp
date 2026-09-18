#pragma once




#include <vector>
#include <render/raytrace/bvh.hpp>

namespace geom {


void add_box(std::vector<bvh::Tri>& out,
             const vec3& lo, const vec3& hi,
             const vec3& albedo, float roughness = 0.5f,
             float metallic = 0.0f, float ior = 1.5f);


void add_box_c(std::vector<bvh::Tri>& out,
               const vec3& center, const vec3& half,
               const vec3& albedo, float roughness = 0.5f,
               float metallic = 0.0f, float ior = 1.5f);



void add_sphere(std::vector<bvh::Tri>& out,
                const vec3& center, float radius,
                const vec3& albedo, float roughness, float metallic, float ior,
                int slices, int stacks, bool smooth = true);

}
