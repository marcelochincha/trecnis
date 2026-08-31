#pragma once
#include <game/sr_game_state.hpp>
#include <cstdint>

extern const int MENU_ITEMS;

double tick_ms(uint64_t& since);
void   render_raster_shadows(Game* e);
void   draw_bvh_debug(Game* e);
void   draw_normals_debug(Game* e);
void   draw_menu(Game* e);
void   menu_apply(Game* e, int dir);
