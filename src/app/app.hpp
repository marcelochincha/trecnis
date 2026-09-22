#pragma once

#include <SDL2/SDL.h>

struct App;

// =============================================================================
// app/ — the runtime. main.cpp calls exactly these five functions, in this
// order, and owns nothing about rendering or gameplay itself; App (app_state.hpp)
// holds all of it. This header is the entire contract between main.cpp and the
// host, and the host's entire contract with game/ and core/ is these two lines:
//
//   game/  — App calls game_create/game_init once, then game_update every frame
//            inside app_update. game/ never sees SDL, core/, or render/; it only
//            reads InputState and writes World (see subsystems/scene/world.hpp).
//   core/  — App owns a framebuffer (core/sr_framebuffer.hpp) and writes into it
//            through Renderer::render (render/), then blits that buffer to SDL
//            itself in app_render. core/ has no idea App exists; it is just the
//            pixel/camera/text/profiler primitives app/ and render/ both use.
//
// Call order and why:
//   app_create   allocates the App (and its framebuffer, sized up front).
//   app_init     one-time setup: attach the scene to the renderer, THEN create
//                and init the game (game_init authors ALL scene content --
//                skybox, static geometry, ambient SH -- app/ never picks an
//                asset path itself), THEN upload the static BVH. Each step
//                depends on the one before it; reordering silently uploads an
//                empty or stale scene.
//   app_update   per frame: poll SDL input -> InputState -> game_update -> World.
//   app_render   per frame: World -> RenderScene -> Renderer::render -> pixels,
//                then debug overlays/HUD, then the SDL texture upload.
//   app_shutdown tears down the renderer and destroys the game.
// =============================================================================

App* app_create(int width, int height);
void app_init(App* e);

void app_update(App* e, float dt);
void app_handle_events(App* e, SDL_Event& event, bool& running);
void app_render(App* e, SDL_Texture* sdl_fb_texture, float dt);
void app_shutdown(App* e);