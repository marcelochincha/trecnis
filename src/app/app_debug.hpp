#pragma once

#include <SDL2/SDL.h>

struct App;

// =============================================================================
// app_debug — the Dear ImGui debugger, confined to app/.
//
// app_debug.cpp is the ONLY translation unit in the project that includes
// imgui.h. game/, render/ and subsystems/ never see it. When WITH_IMGUI is
// undefined every entry point here is a no-op, so the rest of app/ can call it
// unconditionally.
//
// Lifecycle (driven from main.cpp / app.cpp):
//   init          after SDL and App exist
//   handle_event  first thing in app_handle_events (before app/ consumes it)
//   frame         inside app_present, before SDL_RenderPresent (debugger on top)
//   shutdown      at teardown
// =============================================================================

namespace app_debug {

void init(App* e, SDL_Window* window, SDL_Renderer* renderer);
void handle_event(App* e, const SDL_Event& event);
void frame(App* e, SDL_Renderer* renderer);
void shutdown();

// True when the debugger (or an ImGui widget) wants the mouse / keyboard. app/
// consults these so the game never reads input the UI is using.
bool capture_mouse(App* e);
bool capture_keyboard(App* e);

}  // namespace app_debug
