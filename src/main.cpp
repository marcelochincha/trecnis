// PingPong RT — entry point.
//
// Thin driver: owns the SDL window + present surface and the frame clock, then
// delegates every frame to the game layer (game/sr_game.hpp). All scene
// construction, the render dispatch and the HUD live behind that API; main.cpp
// knows nothing about the ray tracer, the BVH or which backend is active.
//
// Phase 1 scene: a fixed camera looking at one static cube. No gameplay.

#define SDL_MAIN_HANDLED

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <SDL2/SDL.h>

#include <sr_config.hpp>
#include <game/sr_game.hpp>

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

int main(int argc, char* argv[])
{
    global_config = parse_args(argc, argv);
    print_config(global_config);

    const int W = global_config.window_width;
    const int H = global_config.window_height;

    init_sdl(W, H);

    Game* game = game_create(W, H);
    game_init(game);

    // Optional: pin a backend for measurement, e.g. --backend 2 (OCL GPU).
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], "--backend") == 0)
            game_set_backend(game, atoi(argv[i + 1]));

    const float  target_delta_ms = 1000.0f / global_config.target_fps;
    float        dt   = 1.0f / 60.0f;
    uint64_t     last = SDL_GetTicks64();
    bool         running = true;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            game_handle_events(game, event, running);

        game_update(game, dt);
        game_render(game, g_fb_texture, dt);

        SDL_RenderClear(g_sdlrender);
        SDL_RenderCopy(g_sdlrender, g_fb_texture, nullptr, nullptr);
        SDL_RenderPresent(g_sdlrender);

        uint64_t now = SDL_GetTicks64();
        if (now - last < (uint64_t)target_delta_ms)
        {
            SDL_Delay((uint32_t)(target_delta_ms - (now - last)));
            now = SDL_GetTicks64();
        }
        dt   = (now - last) / 1000.0f;
        last = now;
    }

    game_shutdown(game);
    game_destroy(game);

    SDL_DestroyTexture(g_fb_texture);
    SDL_DestroyRenderer(g_sdlrender);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
