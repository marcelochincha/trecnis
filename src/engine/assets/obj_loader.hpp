#pragma once

#include <core/sr_geometry.hpp>
#include <core/sr_texture.hpp>
#include <render/raytrace/bvh.hpp>
#include <string>
#include <vector>



const texture* obj_cached_texture(const std::string& path);





bool load_obj_mesh(const std::string& path, mesh& out, float scale = 1.0f);





void load_obj_tris(const char* path, const vec3& base, float scale,
                   const vec3& albedo_def, float roughness_def,
                   std::vector<bvh::Tri>& out,
                   float metallic_def = 0.0f, float ior_def = 1.5f);
