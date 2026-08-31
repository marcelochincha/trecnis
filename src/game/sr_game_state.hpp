#pragma once
// Internal header — included only by game sub-modules, not exposed publicly.
#include <SDL2/SDL.h>
#include <render/raster/sr_renderer.hpp>
#include <render/render_scene.hpp>
#include <render/raytrace/backend.hpp>
#include <engine/anim/skinned_mesh.hpp>
#include <render/raytrace/bvh.hpp>
#include <unordered_map>
#include <string>
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
    bool  raytrace_mode = true;
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

    std::unordered_map<std::string, mesh*> meshes;
    std::vector<RTTri> rt_tris;
    std::unordered_map<std::string, float> reflectivity_map;

    std::vector<bvh::Tri> emissive_tris; // area lights collected from static_bvh


    bvh::BVH           dynamic_bvh;
    bool               use_bvh                = true;

    bvh::BuildStrategy build_strategy         = bvh::SAH;
    bvh::BuildStrategy dynamic_build_strategy = bvh::Morton;
    double             static_build_ms = 0.0;

    mesh*    field_mesh = nullptr;
    bvh::BVH static_bvh;

    // Skinned character. The mesh lives in `meshes["character"]`, so
    // build_scene_tris folds its per-frame deformed triangles into the DYNAMIC
    // BVH every frame — skinning + dynamic BVH rebuild coexist.
    SkinnedMesh skin;
    mesh*       skin_mesh = nullptr;
    float       anim_time = 0.0f;   // skinning clock (seconds)

    std::array<texture, 6> skybox_faces;
    bool skybox_enabled = true;

    // Ray-trace render subsystem: owns the worker pool and the CPU/Embree/OpenCL
    // backends, and holds the runtime backend selection.
    RaytraceRenderer renderer;
    int num_workers = 4;   // configured CPU thread count (from --threads)

    Game(int width, int height) : fb(width, height) {}
};
