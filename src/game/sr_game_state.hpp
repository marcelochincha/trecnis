#pragma once
// Internal header — included only by game sub-modules, not exposed publicly.
#include <SDL2/SDL.h>
#include <render/raster/sr_renderer.hpp>
#include <render/render_scene.hpp>
#include <render/renderer.hpp>
#include <engine/anim/skinned_mesh.hpp>
#include <render/raytrace/bvh.hpp>
#include <vector>
#include <array>

using RTTri = bvh::Tri;

struct Game {
    framebuffer fb;
    camera cam;

    float yaw      = to_radians(0.0f);
    float pitch    = to_radians(-4.0f);
    vec3  position = vec3(0.0f, 1.7f, 1.5f);
    float move_speed = 6.0f;   // camera fly speed, adjusted by the mouse wheel

    float time          = 0.0f;
    bool  show_bvh      = false;
    int   bvh_debug_depth = 12;  // wireframe: draw only nodes up to this depth

    bool show_menu   = false;
    int  menu_cursor = 0;
    bool reflections = true;
    int  max_bounces = 1;
    int  spp         = 1;  // samples per pixel

    float  bob_phase    = 0.0f;
    bool   show_hud     = true;
    bool   hud_simple   = false;
    bool   show_normals = false;
    bool   fly_mode     = true;
    Uint32 last_space_ms = 0;

    std::vector<RTTri> rt_tris;           // dynamic geometry, folded each frame

    // Per-frame draw list handed to the raster backend (floor + character).
    std::vector<RasterItem> raster_items;

    std::vector<bvh::Tri> emissive_tris; // area lights collected from static_bvh


    bvh::BVH           dynamic_bvh;
    bool               use_bvh                = true;

    bvh::BuildStrategy build_strategy         = bvh::SAH;
    bvh::BuildStrategy dynamic_build_strategy = bvh::Morton;
    double             static_build_ms = 0.0;

    mesh*    field_mesh = nullptr;               // static scenery, raster mirror
    vec3     floor_albedo = vec3(0.55f, 0.55f, 0.58f);
    bvh::BVH static_bvh;

    // Skinned character (the one dynamic object). Its per-frame deformed
    // triangles are folded into the DYNAMIC BVH every frame — skinning and the
    // dynamic BVH rebuild coexist.
    SkinnedMesh skin;
    mesh*       character  = nullptr;
    vec3        char_albedo = vec3(0.80f, 0.35f, 0.30f);
    float       char_rough  = 0.6f;
    float       anim_time   = 0.0f;   // skinning clock (seconds)

    std::array<texture, 6> skybox_faces;
    bool skybox_enabled = true;

    // The single render entry point: owns the worker pool and every backend
    // (raster + CPU/Embree/OpenCL ray tracers), and holds the runtime selection.
    Renderer renderer;
    int num_workers = 4;   // configured CPU thread count (from --threads)

    Game(int width, int height) : fb(width, height) {}
};
