#pragma once

#include <subsystems/scene/world.hpp>
#include <subsystems/input.hpp>
#include <subsystems/debug/debug_vars.hpp>

class SceneRuntime;
class UiContext;
struct Game;

// =============================================================================
// The game runtime — scene authoring, simulation and the camera rig.
//
// A floor, a warm point light orbiting the middle, one procedurally skinned
// character, a spinning colour-cycling cube, and a free camera. It exists to
// prove the seam: a scene authors
// its static geometry once, then every frame reads intent and writes a World.
// It never sees a framebuffer, an acceleration structure, a render backend or
// SDL, and the tennis game that replaces it will not either.
// =============================================================================

Game* game_create();

// Author the scene. Static scenery goes straight into the runtime (built once);
// everything that moves is published through the World every frame instead.
void game_init(Game* d, SceneRuntime& scene);

// One simulation step: advance the scene by `dt` under `in`, then describe the
// result in `out`. `out` is rewritten, not appended to.
void game_update(Game* d, const InputState& in, float dt, GameWorld& out);

// Frees the scene, including the Game itself.
void game_destroy(Game* d);

// Expose the game's tunables (roughness, serve pacing, AI speed, ...) to the
// debugger. Registers pointers into Game; call once after game_init and empty
// the registry before game_destroy.
void game_debug_vars(Game* d, DebugVars& out);

// Build the game's own UI (title, scoreboard, pause, point banner) into the
// immediate-mode context. Pure geometry: no pixels, no SDL.
void game_ui(Game* d, UiContext& ui);

// True once the player chose "Salir" in the title screen. app/ reads it and
// stops the loop; the game itself never calls SDL.
bool game_wants_quit(const Game* d);

// Leave the title screen and serve: the state the interactive "Jugar" button
// reaches. Also used by headless --dump/--bench so they capture a live rally
// (the dump hash must not change just because a title screen now exists).
void game_begin_match(Game* d);

// ---- host hooks -------------------------------------------------------------
// One line of scene state for the host HUD ("FLY" / "WALK").
const char* game_status(const Game* d);

// The offline benchmark measures the cost of the scene's point light, so it
// needs to be able to switch it off from outside.
void game_set_point_light(Game* d, bool on);
bool game_point_light(const Game* d);
