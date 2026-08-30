#pragma once

#include <core/sr_geometry.hpp>
#include <core/sr_texture.hpp>
#include <string>

// Diffuse-texture cache shared by all OBJ meshes. Textures are owned for the
// lifetime of the program. Returns null if the file can't be loaded.
const texture* obj_cached_texture(const std::string& path);

// Load an OBJ into a `mesh` (vertices + faces + src_vertex + diffuse texture).
// Deduplicates by (v,t) pairs like an exporter would, remembers which OBJ 'v'
// each vertex came from so SkinnedMesh::load can bind the rig by index, and
// resolves mtllib->map_Kd into out.tex. Returns false if the file can't open.
bool load_obj_mesh(const std::string& path, mesh& out, float scale = 1.0f);
