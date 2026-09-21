#include <app/app.hpp>
#include <app/app_state.hpp>
#include <app/app_hud.hpp>

#include <game/game.hpp>
#include <render/raster/sr_raster.hpp>       // render_gizmo
#include <render/raytrace/sr_raytrace.hpp>   // pack()
#include <render/raytrace/sr_ocl.hpp>
#include <core/sr_profiler.hpp>
#include <core/sr_dump.hpp>
#include <core/sr_texture.hpp>
#include <core/sr_text.hpp>
#include <sound/sr_sound.hpp>
#include <sr_config.hpp>

#include <algorithm>
#include <cstring>
#include <cstdio>

App* app_create(int width, int height) {
    return new App(width, height);
}

void app_init(App* e) {
    SDL_SetRelativeMouseMode(SDL_TRUE);

    if (global_config.gi_override >= 0)
        e->opts.gi_enabled = global_config.gi_override != 0;

    e->scene.attach(e->renderer);
    e->scene.init_camera(float(e->fb.width) / float(e->fb.height), 0.01f, 1000.0f);

    std::array<texture, 6>& sky = e->scene.skybox();
    load_png_texture("res/textures/skybox3/null_plainsky512_rt.png", sky[0]);
    load_png_texture("res/textures/skybox3/null_plainsky512_bk.png", sky[1]);
    load_png_texture("res/textures/skybox3/null_plainsky512_ft.png", sky[2]);
    load_png_texture("res/textures/skybox3/null_plainsky512_lf.png", sky[3]);
    load_png_texture("res/textures/skybox3/null_plainsky512_up.png", sky[4]);
    load_png_texture("res/textures/skybox3/null_plainsky512_dn.png", sky[5]);
    e->scene.commit_sky();   // project the cubemap into the ambient SH, once

    // The scene authors itself into the runtime and then only ever writes the
    // World. Swap these calls for the tennis game and the host is unchanged.
    e->game = game_create();
    game_init(e->game, e->scene);
    game_update(e->game, e->input, 0.0f, e->world);   // pose frame zero

    // Resolve the worker count from --threads: -1 (or 0) = all hardware threads.
    int req = global_config.num_workers;
    if (req <= 0) req = SDL_GetCPUCount();
    e->num_workers = std::clamp(req, 1, 64) * 0.75f;

    e->renderer.init(e->num_workers, e->opts.max_bounces, AMBIENT, SHADOW_EPS);
    e->renderer.upload_static(e->scene.static_bvh(), e->scene.skybox(),
                              e->scene.emissive_tris());
}

// Map the devices we have onto this frame's intent. Everything SDL-shaped stops
// here: nothing below app/ sees a scancode or an SDL_Event.
static void poll_input(App* e) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);

    e->input.move = vec3(0.0f, 0.0f, 0.0f);
    if (keys[SDL_SCANCODE_W])      e->input.move.z -= 1.0f;
    if (keys[SDL_SCANCODE_S])      e->input.move.z += 1.0f;
    if (keys[SDL_SCANCODE_A])      e->input.move.x -= 1.0f;
    if (keys[SDL_SCANCODE_D])      e->input.move.x += 1.0f;
    if (keys[SDL_SCANCODE_LSHIFT]) e->input.move.y -= 1.0f;
    if (keys[SDL_SCANCODE_SPACE])  e->input.move.y += 1.0f;

    int mdx = 0, mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    e->input.look_x = (float)mdx;
    e->input.look_y = (float)mdy;
    // input.wheel is accumulated in app_handle_events and cleared below.
}

void app_update(App* e, float dt) {
    if (e->show_menu) return;

    poll_input(e);
    game_update(e->game, e->input, dt, e->world);
    e->input.wheel = 0.0f;   // consumed
}

void app_render(App* e, SDL_Texture* sdl_fb_texture, float dt) {
    { PROF_SCOPE(PROF_CLEAR);
      e->fb.clear(e->opts.skybox_enabled ? 0xFF000000 : pack(e->opts.bg_color)); }

    // One dispatch point for every mode. The scene runtime turns the world into
    // the view the backends consume; the active backend (raster fallback or
    // CPU BVH / Embree / OpenCL tracer) draws it. No raster/raytrace branch
    // here, and no acceleration structure in sight.
    RenderScene scene = e->scene.build_frame(e->world, e->opts, e->fb.width, e->fb.height);
    { PROF_SCOPE(PROF_TRACE);
      e->renderer.render(scene, e->fb); }

    // Every frame dump lands here and nowhere else: this is the last moment the
    // frame is pure backend output. Everything below draws the gizmo, the debug
    // overlays and a HUD full of per-frame timings, which would make two dumps
    // of the same scene differ for reasons that have nothing to do with
    // rendering — and would make the hash useless as an identity.
    if (e->dump_next) {
        e->dump_next = false;
        char tag[160];
        if (e->dump_tag[0])
            snprintf(tag, sizeof(tag), "%s", e->dump_tag);
        else
            snprintf(tag, sizeof(tag), "%s_%03d",
                     e->renderer.current_name(), e->dump_counter++);
        e->dump_pixels = dump::capture(e->fb);
        e->dump_hash   = dump::write(e->fb, e->dump_dir, tag);
        e->dump_tag[0] = '\0';
    }

    { PROF_SCOPE(PROF_GIZMO);
      render_gizmo(e->fb, e->scene.cam(), vec3{}, 1.0f); }

    { PROF_SCOPE(PROF_DEBUG);
      if (e->show_bvh)     draw_bvh_debug(e);
      if (e->show_normals) draw_normals_debug(e); }

    // The HUD and the menu are timed together, but they sit in separate `if`s
    // with the text formatting between them, so bracket them by hand rather
    // than wrapping both in one scope.
    uint64_t hud_t0 = SDL_GetPerformanceCounter();

    // Micro profiler readout: one line per section in fixed enum order (stable
    // to read frame to frame), skipping sections that cost nothing because the
    // feature is off, so the block shrinks instead of showing rows of 0.00.
    char prof[512];
    {
        int off = 0;
        prof[0] = 0;
        for (int i = 0; i < PROF_COUNT && off < (int)sizeof(prof) - 1; ++i) {
            double v = g_prof.ms((ProfSection)i);
            if (v < 0.005) continue;
            int n = snprintf(prof + off, sizeof(prof) - off, "%-6s %5.2f\n",
                             Profiler::name((ProfSection)i), v);
            if (n < 0 || n >= (int)sizeof(prof) - off) break;
            off += n;
        }
        snprintf(prof + off, sizeof(prof) - off, "%-6s %5.2f\n", "TOTAL",
                 g_prof.total_ms());
    }

    if (e->show_hud) {
        char backend[192];
        const char* bname = e->renderer.current_name();
        if (!e->renderer.current_available()) {
            snprintf(backend, sizeof(backend), "%s (unavailable)", bname);
        } else if (std::strcmp(bname, "CPU SOFTWARE") == 0) {
            snprintf(backend, sizeof(backend), "%s (%d workers)", bname, e->renderer.workers());
        } else if (ocl::available() && std::strcmp(bname, "OCL GPU") == 0) {
            snprintf(backend, sizeof(backend), "%s: %s", bname, ocl::device_name());
        } else {
            snprintf(backend, sizeof(backend), "%s", bname);
        }

        if (e->hud_simple) {
            char line[192];
            snprintf(line, sizeof(line),
                "FPS: %.0f  |  %s  |  BVH: %s  |  [H] expand",
                1.0f/dt, backend, e->opts.use_bvh ? "On" : "Off");
            draw_text(e->fb, 12, 12, line, 0x88000000, 0x88000000);
            draw_text(e->fb, 10, 10, line, 0xFFFFFFFF);
        } else {
            const bvh::BVH& sb = e->scene.static_bvh();
            const bvh::BVH& db = e->scene.dynamic_bvh();
            char HUD[1280] = {0};
            snprintf(HUD, sizeof(HUD),
                "=== SR-LEC ===\n"
                "Frametime: %.2fms   FPS: %.2f\n"
                "prof ms\n"
                "%s"
                "--- Static BVH (scene) ---\n"
                "  tris: %zu  nodes: %zu  build: %.2fms\n"
                "--- Dynamic BVH (movers) ---\n"
                "  tris: %zu  nodes: %zu\n"
                "Accel: %s   Static: %s  Dyn: %s\n"
                "Backend: %s\n"
                "Move: %s\n"
                "[TAB/G] backend [B] BVH [V] vis [N] normals [L] sun\n[M] menu  [SPACE x2] walk/fly\n[H] compact\n",
                dt*1000.0f, 1.0f/dt, prof,
                sb.triangle_count(), sb.node_count(), e->scene.static_build_ms(),
                db.triangle_count(), db.node_count(),
                e->opts.use_bvh ? "BVH (fast)" : "BRUTE FORCE (slow)",
                strategy_name(e->scene.static_strategy()),
                strategy_name(e->opts.dynamic_strategy),
                backend, game_status(e->game));
            draw_text(e->fb, 22, 22, HUD, 0x88000000, 0x88000000);
            draw_text(e->fb, 20, 20, HUD, 0xFFFFFFFF);
        }
    }

    if (e->show_menu) draw_menu(e);
    g_prof.add(PROF_HUD, (double)(SDL_GetPerformanceCounter() - hud_t0) * 1000.0
                         / (double)SDL_GetPerformanceFrequency());

    { PROF_SCOPE(PROF_UPLOAD);
      SDL_UpdateTexture(sdl_fb_texture, NULL, e->fb.colorBuffer,
                        e->fb.width * sizeof(uint32_t)); }
}

void app_handle_events(App* e, SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) { running = false; return; }
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        if (e->show_menu) { e->show_menu = false; SDL_SetRelativeMouseMode(SDL_TRUE); }
        else running = false;
        return;
    }
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_m) {
        e->show_menu = !e->show_menu;
        SDL_SetRelativeMouseMode(e->show_menu ? SDL_FALSE : SDL_TRUE);
        return;
    }
    if (event.type == SDL_MOUSEWHEEL) {
        // Accumulated here because the wheel arrives as events, not as state;
        // app_update hands it to the scene and clears it.
        e->input.wheel += (float)event.wheel.y;
        return;
    }
    if (e->show_menu && event.type == SDL_KEYDOWN) {
        SDL_Keycode k = event.key.keysym.sym;
        if      (k == SDLK_UP)                  { e->menu_cursor = (e->menu_cursor-1+MENU_ITEMS)%MENU_ITEMS; }
        else if (k == SDLK_DOWN)                 { e->menu_cursor = (e->menu_cursor+1)%MENU_ITEMS;   }
        else if (k == SDLK_LEFT)                 { menu_apply(e, -1); }
        else if (k == SDLK_RIGHT || k==SDLK_RETURN) { menu_apply(e, +1); }
        return;
    }
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_TAB: e->renderer.cycle(-1); break;
            case SDLK_b:   e->opts.use_bvh     = !e->opts.use_bvh;     break;
            case SDLK_v:   e->show_bvh         = !e->show_bvh;         break;
            case SDLK_h:   e->hud_simple       = !e->hud_simple;       break;
            case SDLK_n:   e->show_normals     = !e->show_normals;     break;
            case SDLK_l:   e->opts.sun_enabled = !e->opts.sun_enabled; break;
            case SDLK_g:   e->renderer.cycle(+1); break;
            case SDLK_F12: e->dump_next = true; break;
            default: break;
        }
    }
}

void app_shutdown(App* e) {
    e->renderer.shutdown();
    e->world.dynamic.clear();
    game_destroy(e->game);
    e->game = nullptr;
}
