#pragma once

#include <core/sr_geometry.hpp>
#include <core/sr_texture.hpp>
#include <render/raytrace/bvh.hpp>   // bvh::Tri
#include <string>
#include <vector>

// Diffuse-texture cache shared by all OBJ meshes. Textures are owned for the
// lifetime of the program. Returns null if the file can't be loaded.
const texture* obj_cached_texture(const std::string& path);

// Load an OBJ into a `mesh` (vertices + faces + src_vertex + diffuse texture).
// Deduplicates by (v,t) pairs like an exporter would, remembers which OBJ 'v'
// each vertex came from so SkinnedMesh::load can bind the rig by index, and
// resolves mtllib->map_Kd into out.tex. Returns false if the file can't open.
bool load_obj_mesh(const std::string& path, mesh& out, float scale = 1.0f);

// Load an OBJ directly into shadeable ray-trace triangles, appending to `out`.
// Reads the .mtl for per-material PBR (Kd/Ks/Ke/Ns/Pr/Pm/Ni) and diffuse
// textures, and computes smooth per-vertex normals. `base`/`scale` place the
// model; the *_def values are the fallback material for faces without one.
void load_obj_tris(const char* path, const vec3& base, float scale,
                   const vec3& albedo_def, float roughness_def,
                   std::vector<bvh::Tri>& out,
                   float metallic_def = 0.0f, float ior_def = 1.5f);
