#pragma once



#include <SDL2/SDL.h>

struct Game;


Game* game_create(int width, int height);


void  game_init(Game* e);



void  game_update(Game* e, float dt);



void  game_handle_events(Game* e, SDL_Event& event, bool& running);


void  game_render(Game* e, SDL_Texture* sdl_fb_texture, float dt);


void  game_shutdown(Game* e);




void  game_destroy(Game* e);




void  game_set_backend(Game* e, int index);




void  game_set_racket_autopilot(Game* e, int mode);




void  game_set_demo(Game* e, int stage);



void  game_rebuild_static(Game* e);
