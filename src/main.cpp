// PingPong RT — minimal bootstrap (Fase 0).
//
// This is NOT the game. It brings up the inherited trecnis engine end to end:
// SDL window, framebuffer, camera, skybox, and the Renderer with its backends
// (RASTER / CPU SOFTWARE / OCL GPU). The scene is empty — two empty BVHs — so
// every primary ray misses and returns the skybox (or the solid background).
//
// No Player / Ball / Racket / Physics / Collision / gameplay. Those come in
// later phases, wired through a game/ layer owned by the integration agent.

#define SDL_MAIN_HANDLED

#include <array>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <algorithm>

#include <SDL2/SDL.h>

#include <sr_config.hpp>
#include <core/sr_framebuffer.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_texture.hpp>
#include <render/render_scene.hpp>
#include <render/renderer.hpp>
#include <render/raytrace/bvh.hpp>

// Lighting constants for Renderer::init. These mirror the values defined in
// render/raytrace/sr_raytrace.cpp (AMBIENT = 0.0f, SHADOW_EPS = 1e-4f); kept
// literal here so the bootstrap does not pull in the tracer header.
static constexpr float kAmbient   = 0.0f;
static constexpr float kShadowEps = 1e-4f;

static SDL_Window*   g_window     = nullptr;
static SDL_Renderer* g_sdlrender  = nullptr;
static SDL_Texture*  g_fb_texture = nullptr;

static void init_sdl(int w, int h)
{
    SDL_Init(SDL_INIT_VIDEO);
    g_window = SDL_CreateWindow(
        "PingPong RT",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_RESIZABLE);
    g_sdlrender = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(g_sdlrender, w, h);
    SDL_RenderSetIntegerScale(g_sdlrender, SDL_TRUE);
    g_fb_texture = SDL_CreateTexture(
        g_sdlrender, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
}

// Load the 6 cubemap faces. Returns true only if all 6 decoded, in which case
// the skybox is used; otherwise the renderer falls back to bg_color.
static bool load_skybox(std::array<texture, 6>& faces)
{
    const char* paths[6] = {
        "res/textures/skybox3/null_plainsky512_rt.png",
        "res/textures/skybox3/null_plainsky512_bk.png",
        "res/textures/skybox3/null_plainsky512_ft.png",
        "res/textures/skybox3/null_plainsky512_lf.png",
        "res/textures/skybox3/null_plainsky512_up.png",
        "res/textures/skybox3/null_plainsky512_dn.png",
    };
    bool ok = true;
    for (int i = 0; i < 6; ++i)
        ok &= load_png_texture(paths[i], faces[i]);
    return ok;
}

int main(int argc, char* argv[])
{
    global_config = parse_args(argc, argv);
    print_config(global_config);

    const int W = global_config.window_width;
    const int H = global_config.window_height;

    init_sdl(W, H);

    framebuffer fb(W, H);

    camera cam(vec3(0.0f, 1.6f, 6.0f), vec3(0.0f, 0.0f, 0.0f),
               90.0f, float(W) / float(H), 0.01f, 1000.0f);
    cam.lookAt(vec3(0.0f, 1.0f, 0.0f));

    std::array<texture, 6> skybox_faces;
    bool skybox_ok = load_skybox(skybox_faces);
    printf("Skybox: %s\n", skybox_ok ? "loaded (6/6)" : "missing -> solid background");

    // Empty scene: both BVHs built from an empty triangle list. intersect() and
    // occluded() early-return on an empty tree, so every ray misses.
    bvh::BVH static_bvh;
    bvh::BVH dynamic_bvh;
    static_bvh.build({}, bvh::SAH);
    dynamic_bvh.build({}, bvh::Morton);
    std::vector<bvh::Tri>    brute_tris;      // empty
    std::vector<RasterItem>  raster_items;    // empty
    std::vector<bvh::Tri>    emissive;        // empty

    int workers = global_config.num_workers;
    if (workers <= 0) workers = SDL_GetCPUCount();
    workers = std::clamp(workers, 1, 64);

    Renderer renderer;
    renderer.init(workers, /*max_bounces*/ 1, kAmbient, kShadowEps);
    renderer.upload_static(static_bvh, skybox_faces, emissive);
    printf("Renderer: %d backends, %d workers, starting on '%s'\n",
           renderer.count(), renderer.workers(), renderer.current_name());

    const float target_delta_ms = 1000.0f / global_config.target_fps;
    uint64_t last = SDL_GetTicks64();
    bool running = true;

    while (running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN)
            {
                switch (e.key.keysym.sym)
                {
                    case SDLK_ESCAPE: running = false;            break;
                    case SDLK_TAB:    renderer.cycle(-1);          break;
                    case SDLK_g:      renderer.cycle(+1);          break;
                    default: break;
                }
                if (e.key.keysym.sym == SDLK_TAB || e.key.keysym.sym == SDLK_g)
                    printf("Backend: %s%s\n", renderer.current_name(),
                           renderer.current_available() ? "" : " (unavailable)");
            }
        }

        // Per-frame render view. POD of borrowed pointers — the seam between
        // (a future) game world and render/.
        RenderScene scene;
        scene.cam            = &cam;
        scene.width          = fb.width;
        scene.height         = fb.height;
        scene.static_bvh     = &static_bvh;
        scene.dynamic_bvh    = &dynamic_bvh;
        scene.brute_tris     = &brute_tris;
        scene.raster_items   = &raster_items;
        scene.emissive       = nullptr;
        scene.point_lights   = nullptr;
        scene.skybox         = &skybox_faces;
        scene.skybox_enabled = skybox_ok;
        scene.bg_color       = vec3(0.05f, 0.06f, 0.08f);
        scene.sun_enabled    = true;
        scene.reflections    = false;
        scene.max_bounces    = 1;
        scene.gi_enabled     = false;

        fb.clear(skybox_ok ? 0xFF000000u : 0xFF0D0F14u);
        renderer.render(scene, fb);

        SDL_UpdateTexture(g_fb_texture, nullptr, fb.colorBuffer, fb.width * sizeof(uint32_t));
        SDL_RenderClear(g_sdlrender);
        SDL_RenderCopy(g_sdlrender, g_fb_texture, nullptr, nullptr);
        SDL_RenderPresent(g_sdlrender);

        uint64_t now = SDL_GetTicks64();
        if (now - last < (uint64_t)target_delta_ms)
        {
            SDL_Delay((uint32_t)(target_delta_ms - (now - last)));
            now = SDL_GetTicks64();
        }
        last = now;
    }

    renderer.shutdown();
    SDL_DestroyTexture(g_fb_texture);
    SDL_DestroyRenderer(g_sdlrender);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
