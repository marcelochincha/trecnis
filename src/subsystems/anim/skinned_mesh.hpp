#pragma once

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// Baked rigid skinning (1 bone per vertex, no runtime weights, one pass per
// frame). Per frame and bone we store a final composed skinning matrix:
//
//     p' = M[frame][ bone[v] ] · restP(v)
//
// restP = the mesh at rest (bind pose). Cost: O(vertices) per frame, no runtime
// hierarchy or inverse. Because the geometry changes each frame, this drives
// the dynamic BVH rebuild (skinning + dynamic BVH coexist).
//
// Index binding (not positional): mesh.src_vertex says which OBJ 'v' each
// deduplicated vertex came from, so the binding (one bone per 'v') maps direct:
//     vertexBone[v_engine] = bone_of[ mesh.src_vertex[v_engine] ]
//
// Two text files ('#'/blank lines skipped):
//   BINDING (weights):              ANIMATION (anim):
//     bones <B>                       fps <f>
//     verts <V>                       bones <B>
//     <V ints: bone per vertex>       frames <F>
//                                     F*B matrices (16 floats column-major,
//                                                   frame -> bone order)
// The binding depends on the mesh; the animation is independent (reusable).
// =============================================================================
struct SkinnedMesh {
    std::vector<vertex> bind;        // rest pose (copy of mesh.vertices)
    std::vector<int>    vertexBone;  // bone per vertex (parallel to bind)
    std::vector<mat4>   skin;        // F*B matrices: skin[f*bones + b]
    int   bones   = 0;
    int   frames  = 0;
    float fps     = 24.0f;
    int   matched = 0; // vertices bound to a valid bone (diagnostic)

    bool valid() const { return frames > 0 && bones > 0 && !bind.empty(); }

    // Load binding + animation (two files) and bind each mesh vertex to its bone
    // by index (via mesh.src_vertex). `m` must be the rigged OBJ (loaded with
    // load_obj_mesh, which fills src_vertex). Returns false on open/parse error
    // or if the bone count mismatches between the two files.
    bool load(const std::string& weights_path, const std::string& anim_path,
              const mesh& m);

    // Write the pose at time `t` (SECONDS, real-time) into the mesh. The bake fps
    // only sets the clip duration (frames/fps); this samples by time and linearly
    // interpolates the two neighbouring frames, so a 30 or 60 fps bake plays the
    // same. Clamped to the last frame (no looping). Marks the mesh dirty so the
    // dynamic BVH rebuilds.
    void apply(mesh& m, float t) const;
};

// Procedural test character: a column of stacked rings bound to a bone chain,
// with a baked travelling-wave animation that bends it. Needs no assets — useful
// to exercise per-vertex skinning + the dynamic BVH without a rigged model.
void build_procedural_character(mesh& out, SkinnedMesh& skin);
