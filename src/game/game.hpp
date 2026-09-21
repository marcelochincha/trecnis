#pragma once

#include <engine/scene/world.hpp>
#include <engine/input.hpp>

class SceneRuntime;
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
void game_update(Game* d, const InputState& in, float dt, World& out);

// Frees the scene, including the Game itself.
void game_destroy(Game* d);

// ---- host hooks -------------------------------------------------------------
// One line of scene state for the host HUD ("FLY" / "WALK").
const char* game_status(const Game* d);

// The offline benchmark measures the cost of the scene's point light, so it
// needs to be able to switch it off from outside.
void game_set_point_light(Game* d, bool on);
bool game_point_light(const Game* d);
