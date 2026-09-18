#pragma once













#include <SDL2/SDL.h>
#include <vector>
#include <array>
#include <cstddef>

#include <core/sr_framebuffer.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_texture.hpp>
#include <core/sr_geometry.hpp>
#include <render/render_scene.hpp>
#include <render/renderer.hpp>
#include <render/raytrace/bvh.hpp>

#include <game/ball.hpp>
#include <game/racket.hpp>
#include <game/table.hpp>
#include <game/wall.hpp>






struct Metrics {
    double frame_ms   = 0.0;
    double physics_ms  = 0.0;
    double dyn_build_ms = 0.0;
    double render_ms   = 0.0;
    std::size_t primary_rays = 0;

    static double ema(double prev, double sample) {
        return prev <= 0.0 ? sample : prev * 0.9 + sample * 0.1;
    }
};

struct Game {
    framebuffer fb;
    camera      cam;








    vec3  cam_pos    = vec3(3.0f, 2.1f, -3.4f);
    vec3  cam_target = vec3(0.0f, 0.85f, 0.6f);
    float cam_yaw    = 0.0f;
    float cam_pitch  = 0.0f;
    float cam_move_speed = 3.0f;
    bool  free_cam   = false;


    bvh::BVH           static_bvh;
    bvh::BuildStrategy static_strategy = bvh::SAH;
    mesh*              court_mesh = nullptr;
    std::size_t        static_tri_count = 0;
    double             static_build_ms = 0.0;






    std::vector<bvh::Tri> sphere_local_;
    std::vector<bvh::Tri> racket_local_;
    std::vector<bvh::Tri> dyn_tris_;
    bvh::BVH              dynamic_bvh;
    bvh::BuildStrategy    dynamic_strategy = bvh::Morton;
    mesh                 sphere_mesh_;
    mesh                 racket_mesh_;
    vec3                 sphere_albedo = vec3(0.90f, 0.30f, 0.20f);
    vec3                 racket_albedo = vec3(0.20f, 0.45f, 0.85f);


    Ball   ball;
    Racket racket;
    Table  table;
    Wall   wall;
    AABB   arena;
    AABB   racket_limits;
    float  racket_speed = 5.0f;
    int    racket_autopilot = 0;
    bool   racket_mouse_mode = true;
    long   hits_reported_ = 0;
    bool   racket_enabled = true;
    bool   table_enabled  = true;
    bool   wall_enabled   = true;
    int    bounces_total = 0;
    bool   hit_window = false;


    float  time_scale = 1.0f;
    bool   charging   = false;
    float  charge     = 0.0f;
    float  hit_flash_timer = 0.0f;


    bool   demo_open  = false;
    int    demo_stage = 1;


    std::vector<RasterItem> raster_items;


    std::vector<bvh::Tri>   emissive_tris;
    std::vector<PointLight> point_lights;
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
