#pragma once
// Internal header — included only by app/ sub-modules, not exposed publicly.
#include <cstdint>
#include <vector>

#include <SDL2/SDL.h>
#include <core/sr_framebuffer.hpp>
#include <engine/scene/scene_runtime.hpp>
#include <engine/scene/world.hpp>
#include <engine/input.hpp>
#include <render/renderer.hpp>

struct Game;

// =============================================================================
// App — the host, and nothing else.
//
// It owns the pixels, the renderer, the scene runtime and the debug UI, pumps
// SDL, and drives one frame:
//
//     SDL events -> InputState -> game_update -> World
//     World -> SceneRuntime::build_frame -> RenderScene
//     RenderScene -> Renderer::render -> framebuffer -> overlays -> present
//
// It does not know what a BVH is (SceneRuntime does) and it does not know what
// is in the scene (game/ does). Swapping one game module for another means
// changing the four game_* calls in app.cpp and nothing else.
// =============================================================================
struct App {
    // ---- output ------------------------------------------------------------
    framebuffer fb;
    Renderer    renderer;
    int         num_workers = -1;   // configured CPU thread count (from --threads)

    // ---- the frame ---------------------------------------------------------
    SceneRuntime scene;   // owns both BVHs, the folded tris and the skybox
    RenderOpts   opts;    // quality/debug knobs driven by the menu and hotkeys
    World        world;   // what exists this frame, written by the scene
    InputState   input;   // this frame's intent, mapped from SDL

    // ---- the scene ---------------------------------------------------------
    Game* game = nullptr;

    // ---- debug UI ----------------------------------------------------------
    bool show_hud        = true;
    bool hud_simple      = false;
    bool show_bvh        = false;
    int  bvh_debug_depth = 12;   // wireframe: draw only nodes up to this depth
    bool show_normals    = false;
    bool show_menu       = false;
    int  menu_cursor     = 0;

    // Frame dump. Callers only raise the flag; the capture happens inside
    // app_render at the one point where the frame is still pure backend output
    // (before the gizmo, the debug overlays and the HUD). Both users go through
    // it — [F12] interactively and --dump headless — so the two never disagree
    // about what "the frame" means.
    bool     dump_next     = false;
    char     dump_tag[128] = {0};   // empty: auto-numbered <backend>_NNN
    int      dump_counter  = 0;
    uint64_t dump_hash     = 0;     // hash of the last captured frame
    std::vector<uint32_t> dump_pixels;   // ...and its pixels, for diffing
    const char* dump_dir   = "dumps";

    App(int width, int height) : fb(width, height) {}
};
