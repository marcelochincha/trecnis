#pragma once
#include <app/app_state.hpp>
#include <cstdint>

extern const int MENU_ITEMS;

const char* strategy_name(bvh::BuildStrategy s);
double tick_ms(uint64_t& since);
void   draw_bvh_debug(App* e);
void   draw_normals_debug(App* e);
void   draw_menu(App* e);
void   menu_apply(App* e, int dir);
