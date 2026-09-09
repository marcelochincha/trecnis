#pragma once
// Public game API — the only header main.cpp needs. Same shape as the trecnis
// game API so the main loop stays a thin driver. The Game type is opaque here.

#include <SDL2/SDL.h>

struct Game;

// Allocate the game (owns its framebuffer at width x height).
Game* game_create(int width, int height);

// One-time setup: camera, skybox, static scene, Renderer + worker pool.
void  game_init(Game* e);

// Advance one frame of game logic: integrate the ball with delta time, resolve
// wall bounces, refresh its world-space geometry and rebuild the dynamic BVH.
void  game_update(Game* e, float dt);

// Handle one SDL event. ESC / SDL_QUIT set running = false; TAB / G cycle the
// render backend.
void  game_handle_events(Game* e, SDL_Event& event, bool& running);

// Build the RenderScene and draw the frame into the SDL streaming texture.
void  game_render(Game* e, SDL_Texture* sdl_fb_texture, float dt);

// Release the Renderer, worker pool and owned meshes.
void  game_shutdown(Game* e);

// Free the Game itself. Separate from game_shutdown so the shutdown/teardown
// order stays explicit; call it last. (Game is opaque here, so main.cpp cannot
// delete it directly.)
void  game_destroy(Game* e);

// Directly select a render backend by index (0 = RASTER, 1 = CPU SOFTWARE,
// last = OCL GPU). For measurement / CI so a backend can be pinned without the
// TAB/G keys. No-op if the index is out of range or the backend is unavailable.
void  game_set_backend(Game* e, int index);

// Drive the racket on a scripted oscillation instead of reading the keyboard,
// so headless / CI runs can exercise racket movement + collision reproducibly.
void  game_set_racket_autopilot(Game* e, bool on);

// (Re)build the STATIC BVH from the scene geometry (floor + walls) and push it
// to the backends that cache it. Called once from game_init; safe to call again.
void  game_rebuild_static(Game* e);
