#pragma once

#include <SDL2/SDL.h>

#include <subsystems/ui/ui_types.hpp>

struct App;

// =============================================================================
// app_ui — submit a UiDrawList through SDL_RenderGeometryRaw.
//
// This is the only place the game UI touches SDL. It owns one SDL_Texture per
// registered UiImage (created/updated on version change), then walks the command
// list applying clip rects and issuing one geometry call per command.
// =============================================================================

namespace app_ui {

void submit(App* e, SDL_Renderer* r, const UiDrawList& list);
void shutdown(App* e);

}  // namespace app_ui
