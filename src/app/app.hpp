#pragma once

#include <SDL2/SDL.h>

struct App;

App* app_create(int width, int height);
void app_init(App* e);

void app_update(App* e, float dt);
void app_handle_events(App* e, SDL_Event& event, bool& running);
void app_render(App* e, SDL_Texture* sdl_fb_texture, float dt);
void app_shutdown(App* e);