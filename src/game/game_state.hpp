#pragma once
// Internal header — included only by game/ sub-modules, never by render/.
//
// Checkpoint scene: a static box arena (floor + 3 walls) in the STATIC BVH, and
// in the DYNAMIC BVH a ray-traced sphere plus a user-movable racket (tilted
// paddle). The ball flies under gravity + air drag + Magnus and bounces off the
// arena and the racket with a restitution coefficient, integrated frame-rate
// independently with a fixed sub-step. The racket is translated by player input
// and stays inside racket_limits; its velocity is tracked for telemetry but is
// NOT fed into the ball's bounce yet (no velocity transfer, no impact spin, no
// contact friction). No full Player, animation, score, AI.

#include <SDL2/SDL.h>
#include <vector>
#include <array>
#include <cstddef>

#include <core/sr_framebuffer.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_texture.hpp>
#include <core/sr_geometry.hpp>          // mesh, AABB
#include <render/render_scene.hpp>       // RenderScene, RasterItem, PointLight
#include <render/renderer.hpp>           // Renderer
#include <render/raytrace/bvh.hpp>       // bvh::BVH, bvh::Tri, bvh::BuildStrategy

#include <game/ball.hpp>
#include <game/racket.hpp>

// game/ deliberately does NOT include sr_raytrace.hpp / sr_ocl.hpp /
// cpu_tracer.hpp. The seam to render/ is RenderScene + Renderer + bvh.hpp only.

// Rolling performance numbers (exponential moving average, so the HUD is
// readable instead of flickering every frame).
struct Metrics {
    double frame_ms   = 0.0;   // wall-clock, whole frame
    double physics_ms  = 0.0;  // Ball::update + sphere world-space refresh
    double dyn_build_ms = 0.0; // dynamic BVH rebuild (Morton)
    double render_ms   = 0.0;  // Renderer::render (traversal + shading dominate)
    std::size_t primary_rays = 0;   // width * height per frame

    static double ema(double prev, double sample) {
        return prev <= 0.0 ? sample : prev * 0.9 + sample * 0.1;
    }
};

struct Game {
    framebuffer fb;
    camera      cam;

    // Fixed camera (no free-fly). Looks into the arena from the open front.
    vec3 cam_pos    = vec3(0.0f, 3.0f, 12.0f);
    vec3 cam_target = vec3(0.0f, 2.6f, 0.0f);

    // --- STATIC geometry: floor + back / left / right walls -------------------
    bvh::BVH           static_bvh;
    bvh::BuildStrategy static_strategy = bvh::SAH;
    mesh*              court_mesh = nullptr;     // raster mirror, world-space, identity model
    std::size_t        static_tri_count = 0;
    double             static_build_ms = 0.0;

    // --- DYNAMIC geometry: the sphere + the movable racket -----------------
    // *_local_  : generated once, centred at the origin
    // dyn_tris_ : [sphere | racket], each part rewritten in place every frame
    //             (translated to its owner's position). Capacity fixed in
    //             game_init -> no per-frame allocation in the game layer.
    std::vector<bvh::Tri> sphere_local_;
    std::vector<bvh::Tri> racket_local_;
    std::vector<bvh::Tri> dyn_tris_;
    bvh::BVH              dynamic_bvh;
    bvh::BuildStrategy    dynamic_strategy = bvh::Morton;
    mesh                 sphere_mesh_;          // raster mirror, local space + setPosition
    mesh                 racket_mesh_;          // raster mirror, local space + setPosition
    vec3                 sphere_albedo = vec3(0.90f, 0.30f, 0.20f);
    vec3                 racket_albedo = vec3(0.20f, 0.45f, 0.85f);

    // --- physics state -----------------------------------------------------
    Ball   ball;
    Racket racket;                              // user-movable paddle (dynamic BVH)
    AABB   arena;                               // ball centre stays inside this
    AABB   racket_limits;                       // racket centre stays inside this
    float  racket_speed = 5.0f;                 // u/s, player move speed
    int    racket_autopilot = 0;                // 0 = keyboard, 1 = chase ball, 2 = recede (headless tests)
    long   hits_reported_ = 0;                  // last racket-hit count logged
    int    bounces_total = 0;

    // --- per-frame draw list for the raster backend ----------------------
    std::vector<RasterItem> raster_items;

    // --- lighting / environment (minimal: sun + skybox) ------------------
    std::vector<bvh::Tri>   emissive_tris;      // empty
    std::vector<PointLight> point_lights;       // empty
    std::array<texture, 6>  skybox_faces;
    bool skybox_enabled = true;
    vec3 bg_color       = vec3(0.05f, 0.06f, 0.08f);
    bool sun_enabled    = true;

    Metrics metrics;
    bool    show_hud = true;

    Renderer renderer;
    int      num_workers = 4;

    Game(int width, int height) : fb(width, height) {}
};
