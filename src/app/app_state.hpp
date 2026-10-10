#pragma once
// Internal header — included only by app/ sub-modules, not exposed publicly.
#include <cstdint>
#include <vector>

#include <SDL2/SDL.h>
#include <core/sr_framebuffer.hpp>
#include <subsystems/scene/scene_runtime.hpp>
#include <subsystems/scene/world.hpp>
#include <subsystems/input.hpp>
#include <subsystems/debug/debug_vars.hpp>
#include <subsystems/ui/ui_image.hpp>
#include <subsystems/ui/ui_context.hpp>
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

    // Where the framebuffer lands in the window, computed once per present
    // (letterboxed integer scale, or fractional when the window is smaller than
    // the fb). The game UI and the mouse mapping both read it.
    SDL_Rect    present_rect = {0, 0, 0, 0};

    // ---- the frame ---------------------------------------------------------
    SceneRuntime scene;   // owns both BVHs, the folded tris and the skybox
    RenderOpts   opts;    // quality/debug knobs driven by the menu and hotkeys
    World        world;   // what exists this frame, written by the scene
    InputState   input;   // this frame's intent, mapped from SDL

    // ---- the scene ---------------------------------------------------------
    Game* game = nullptr;

    // Tunables game/ exposes to the debugger (pointers into Game). Emptied in
    // app_shutdown before the Game is destroyed.
    DebugVars debug_vars;

    // ---- game UI (GPU-drawn, no framebuffer pixels) ------------------------
    UiImages   ui_images;     // CPU-side registry (font atlas + any game art)
    UiContext  ui;            // immediate-mode builder, rebuilt every present
    std::vector<SDL_Texture*> ui_textures;      // one per ui_images entry
    std::vector<int>          ui_tex_versions;  // uploaded version, for re-upload

    // ---- debug UI ----------------------------------------------------------
    bool show_hud        = true;    bool hud_simple      = false;
    bool show_bvh        = false;
    int  bvh_debug_depth = 12;   // wireframe: draw only nodes up to this depth
    bool show_normals    = false;
    bool show_debugger   = false;   // [M] Dear ImGui debugger

    // Frametime history for the debugger's profiler plot. A ring buffer, not a
    // Profiler change: the profiler itself stays a fixed-section accumulator.
    static constexpr int kFrametimeSamples = 240;
    float frametime_ms[kFrametimeSamples] = {0};
    int   frametime_head = 0;
    float frame_dt = 1.0f / 60.0f;   // last dt handed to app_render, for the HUD

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
