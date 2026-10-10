#include <app/app.hpp>
#include <app/app_state.hpp>
#include <app/app_hud.hpp>
#include <app/app_debug.hpp>
#include <app/app_ui.hpp>

#include <game/game.hpp>
#include <render/raster/sr_raster.hpp>       // render_gizmo
#include <render/raytrace/sr_raytrace.hpp>   // pack()
#include <subsystems/ui/ui_font.hpp>
#include <core/sr_profiler.hpp>
#include <core/sr_dump.hpp>
#include <sound/sr_sound.hpp>
#include <sr_config.hpp>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>

App* app_create(int width, int height) {
    return new App(width, height);
}

// Ordered setup — each step depends on state the previous one left behind:
//   1. attach/init_camera     give the scene a renderer and a camera to build for.
//   2. game_create/init       game/ authors ALL scene content here: skybox,
//                              static geometry (scene.add_static*/commit_static()),
//                              ambient SH (scene.commit_sky()). This is the ONLY
//                              place app/ reaches into "what's in the scene",
//                              and only to hand the SceneRuntime pointer to
//                              game/ -- app/ itself never picks an asset path.
//   3. renderer.init          resolve worker count, build backend state.
//   4. upload_static          must run AFTER game_init committed the static
//                              BVH, or the renderer uploads an empty scene.
void app_init(App* e) {
    SDL_SetRelativeMouseMode(SDL_TRUE);

    if (global_config.gi_override >= 0)
        e->opts.gi_enabled = global_config.gi_override != 0;

    e->scene.attach(e->renderer);
    e->scene.init_camera(float(e->fb.width) / float(e->fb.height), 0.01f, 1000.0f);

    // The scene authors itself into the runtime and then only ever writes the
    // World. Swap these calls for the tennis game and the host is unchanged.
    e->game = game_create();
    game_init(e->game, e->scene);
    game_update(e->game, e->input, 0.0f, e->world);   // pose frame zero
    game_debug_vars(e->game, e->debug_vars);           // tunables for the debugger

    // Headless runs capture a live rally, not the title screen: the dump hash
    // must stay identical to the pre-UI baseline, and the bench should measure
    // a rally. Interactively the game starts on the title screen.
    if (global_config.dump_dir || global_config.bench_frames > 0)
        game_begin_match(e->game);

    // Font atlas for the game UI (id 0); the app owns the registry and uploads.
    ui_build_font_atlas(e->ui_images);

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
    // UI pointer: only meaningful in free-cursor mode. In relative mode there is
    // no absolute pointer, so mark it absent (-1) and the game UI ignores it.
    if (SDL_GetRelativeMouseMode()) {
        e->input.ui.pointer_x = -1.0f;
        e->input.ui.pointer_y = -1.0f;
        e->input.ui.pointer_down = false;
    } else {
        int mx = 0, my = 0;
        const Uint32 btn = SDL_GetMouseState(&mx, &my);
        e->input.ui.pointer_x = (float)mx;
        e->input.ui.pointer_y = (float)my;
        e->input.ui.pointer_down = (btn & SDL_BUTTON_LMASK) != 0;
    }

    // While the debugger owns the mouse/keyboard, the game sees no intent at
    // all: the cursor is free and must not drag the paddle, and hotkeys are
    // consumed by app_handle_events.
    if (app_debug::capture_mouse(e)) {
        e->input.move   = vec3(0.0f, 0.0f, 0.0f);
        e->input.look_x = 0.0f;
        e->input.look_y = 0.0f;
        return;
    }

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
    // The simulation keeps running while the debugger or the HUD is up; only the
    // game's input is muted (see poll_input).
    poll_input(e);
    game_update(e->game, e->input, dt, e->world);
    e->input.wheel = 0.0f;   // consumed
}

void app_render(App* e, SDL_Texture* sdl_fb_texture, float dt) {
    e->frame_dt = dt;
    e->frametime_ms[e->frametime_head] = dt * 1000.0f;
    e->frametime_head = (e->frametime_head + 1) % App::kFrametimeSamples;

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

    // The HUD and the debugger no longer draw into the framebuffer: they are
    // ImGui windows assembled in app_present, after this point. That is what
    // keeps the dump hash pure backend output.

    { PROF_SCOPE(PROF_UPLOAD);
      SDL_UpdateTexture(sdl_fb_texture, NULL, e->fb.colorBuffer,
                        e->fb.width * sizeof(uint32_t)); }
}

// Clear, blit the framebuffer and present. All the SDL present work lives here
// so the three main.cpp call sites share one implementation. The destination is
// letterboxed: an integer scale when it fits, a fractional one when the window
// is smaller than the framebuffer, never a crop. `present_rect` is cached on App
// because the game UI and the mouse mapping need the same mapping.
void app_present(App* e, SDL_Renderer* r, SDL_Texture* sdl_fb_texture) {
    PROF_SCOPE(PROF_PRESENT);

    SDL_RenderClear(r);

    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(r, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) { SDL_RenderPresent(r); return; }

    const int fw = e->fb.width, fh = e->fb.height;
    int scale = std::min(win_w / fw, win_h / fh);
    int dst_w, dst_h;
    if (scale >= 1) {
        dst_w = fw * scale;
        dst_h = fh * scale;
    } else {
        const float f = std::min((float)win_w / fw, (float)win_h / fh);
        dst_w = (int)std::lround(fw * f);
        dst_h = (int)std::lround(fh * f);
    }
    e->present_rect = { (win_w - dst_w) / 2, (win_h - dst_h) / 2, dst_w, dst_h };

    SDL_RenderCopy(r, sdl_fb_texture, NULL, &e->present_rect);

    // Game UI: immediate-mode geometry submitted through SDL_RenderGeometryRaw.
    // It never touches the framebuffer, so the dump hash stays pure backend
    // output. While the debugger is open the UI draws but receives no input.
    { PROF_SCOPE(PROF_UI);
      int win_w = 0, win_h = 0;
      SDL_GetRendererOutputSize(r, &win_w, &win_h);

      UiInput uin = e->input.ui;
      if (e->show_debugger) uin = UiInput{};
      e->ui.begin_frame(uin, e->ui_images, win_w, win_h);
      game_ui(e->game, e->ui);
      const UiDrawList& list = e->ui.end_frame();
      app_ui::submit(e, r, list);

      // Pointer mode: menus want the cursor free, a rally wants it relative.
      // The debugger always wins.
      const bool want_free = e->ui.pointer_requested() || e->show_debugger;
      SDL_SetRelativeMouseMode(want_free ? SDL_FALSE : SDL_TRUE);
    }

    // Consume the one-frame UI edges only after the UI has seen them.
    e->input.ui.pointer_pressed = false;
    e->input.ui.nav_up = e->input.ui.nav_down = false;
    e->input.ui.accept = e->input.ui.back = false;

    // The debugger draws last, always on top.
    app_debug::frame(e, r);

    SDL_RenderPresent(r);
}

void app_handle_events(App* e, SDL_Event& event, bool& running) {
    // ImGui first: it may consume the event (text fields, window drags, ...),
    // and the host keys below are checked against what it captured.
    app_debug::handle_event(e, event);

    if (event.type == SDL_QUIT) { running = false; return; }

    // ESC closes the debugger if it is open, otherwise quits.
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        if (e->show_debugger) { e->show_debugger = false; SDL_SetRelativeMouseMode(SDL_TRUE); }
        else running = false;
        return;
    }

#ifdef WITH_IMGUI
    // M toggles the debugger. Opening it frees the cursor so ImGui can be used.
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_m) {
        e->show_debugger = !e->show_debugger;
        SDL_SetRelativeMouseMode(e->show_debugger ? SDL_FALSE : SDL_TRUE);
        return;
    }
#endif

    if (event.type == SDL_MOUSEWHEEL) {
        // Accumulated here because the wheel arrives as events, not as state;
        // app_update hands it to the scene and clears it. While a UI is using
        // the mouse the wheel is its scroll input, not the game's.
        if (!app_debug::capture_mouse(e))
            e->input.wheel += (float)event.wheel.y;
        return;
    }

    // UI intent for the game's own menus. These are edges, consumed in
    // app_present after game_ui has seen them.
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:      case SDLK_w:        e->input.ui.nav_up = true;  break;
            case SDLK_DOWN:    case SDLK_s:        e->input.ui.nav_down = true; break;
            case SDLK_RETURN:  case SDLK_KP_ENTER:
            case SDLK_SPACE:                       e->input.ui.accept = true; break;
            case SDLK_BACKSPACE:                   e->input.ui.back = true;  break;
            default: break;
        }
    }
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        e->input.ui.pointer_pressed = true;

    // Standalone hotkeys do not fire while ImGui is capturing the keyboard
    // (typing in a field must not toggle the sun).
    if (event.type == SDL_KEYDOWN && !app_debug::capture_keyboard(e)) {
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
    // DebugVars hold pointers into Game; drop them before the Game dies.
    e->debug_vars.vars.clear();
    app_ui::shutdown(e);
    e->renderer.shutdown();
    e->world.dynamic.clear();
    game_destroy(e->game);
    e->game = nullptr;
}
