#pragma once
#include <array>
#include <vector>

#include <render/raytrace/bvh.hpp>    // bvh::BVH, bvh::Tri
#include <render/raytrace/accel.hpp>  // ISceneAccel
#include <core/sr_camera.hpp>         // camera
#include <core/sr_texture.hpp>        // texture
#include <core/sr_geometry.hpp>       // mesh

// One mesh to draw in the raster backend, with an explicit flat material colour
// and whether it casts a projected planar shadow. The raster path is a peer
// backend, so it consumes the same RenderScene as the ray tracers.
struct RasterItem {
    const mesh* geo    = nullptr;
    uint32_t    color  = 0xFFFFFFFF;
    bool        shadow = false;
};

// A per-frame, read-only view of everything a render backend needs to draw a
// frame. The game app fills one of these each frame; the render/ subsystem
// consumes it and has NO dependency on the Game type. This is the seam that
// keeps rendering decoupled from game logic.
struct RenderScene {
    const camera* cam = nullptr;
    int width  = 0;
    int height = 0;

    // Geometry. The tracer keeps two BVHs: a STATIC tree for scenery and a
    // DYNAMIC tree rebuilt each frame for fast-moving objects. `brute_tris` is
    // the flat dynamic triangle list, used when use_bvh is off (brute force)
    // and as the source geometry for the Embree dynamic scene.
    const bvh::BVH*              static_bvh  = nullptr;
    const bvh::BVH*              dynamic_bvh = nullptr;
    const std::vector<bvh::Tri>* brute_tris = nullptr;
    bool use_bvh = true;

    // Build strategies (SAH / Median / Morton) for the two trees; the Embree
    // backend maps them to its own build-quality levels.
    bvh::BuildStrategy static_strategy  = bvh::SAH;
    bvh::BuildStrategy dynamic_strategy  = bvh::Morton;

    // Active CPU-side acceleration structure (our BVH or Embree). Set by the
    // backend right before it runs the CPU tracer; unused by the GPU backend.
    const ISceneAccel* accel = nullptr;

    // Raster fallback geometry. The raster backend draws these flat-shaded with
    // the skybox; the ray tracers ignore them (they use the BVHs above).
    const std::vector<RasterItem>* raster_items = nullptr;

    // Lighting / environment.
    const std::vector<bvh::Tri>* emissive = nullptr;  // area lights (NEE)
    const std::array<texture, 6>* skybox  = nullptr;
    bool skybox_enabled = true;

    // Shading options.
    bool reflections = true;
    int  max_bounces = 1;
    int  spp         = 1;
};
