#include <game/sr_game.hpp>
#include <game/sr_game_state.hpp>
#include <render/raytrace/sr_raytrace.hpp>
#include <game/sr_scene.hpp>
#include <game/sr_hud.hpp>

#include <render/raytrace/sr_ocl.hpp>
#include <sound/sr_sound.hpp>
#include <sr_config.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

static const float kMouseSensitivity = 0.0025f;
static const float kMinMoveSpeed     = 0.5f;   // mouse-wheel speed clamp: lower bound
static const float kMaxMoveSpeed     = 60.0f;  // mouse-wheel speed clamp: upper bound

// Fill the per-frame render view the ray-trace backends consume. This is the
// seam between the game world and render/: everything the tracer needs, and
// nothing of the Game type leaks across it.
static RenderScene make_render_scene(Game* e) {
    RenderScene s;
    s.cam    = &e->cam;
    s.width  = e->fb.width;
    s.height = e->fb.height;

    s.static_bvh  = &e->static_bvh;
    s.dynamic_bvh = &e->dynamic_bvh;
    s.brute_tris  = &e->rt_tris;
    s.use_bvh     = e->use_bvh;
    s.static_strategy  = e->build_strategy;
    s.dynamic_strategy = e->dynamic_build_strategy;

    s.emissive        = &e->emissive_tris;
    s.skybox          = &e->skybox_faces;
    s.skybox_enabled  = e->skybox_enabled;

    s.reflections = e->reflections;
    s.max_bounces = e->max_bounces;
    s.spp         = e->spp;
    return s;
}

Game* game_create(int width, int height) {
    return new Game(width, height);
}

void game_init(Game* e) {
    SDL_SetRelativeMouseMode(SDL_TRUE);

    e->cam = camera(e->position, vec3(e->pitch, e->yaw, 0.0f), 90.0f,
                    float(e->fb.width) / float(e->fb.height), 0.01f, 1000.0f);

    load_png_texture("res/textures/skybox3/null_plainsky512_rt.png", e->skybox_faces[0]);
    load_png_texture("res/textures/skybox3/null_plainsky512_bk.png", e->skybox_faces[1]);
    load_png_texture("res/textures/skybox3/null_plainsky512_ft.png", e->skybox_faces[2]);
    load_png_texture("res/textures/skybox3/null_plainsky512_lf.png", e->skybox_faces[3]);
    load_png_texture("res/textures/skybox3/null_plainsky512_up.png", e->skybox_faces[4]);
    load_png_texture("res/textures/skybox3/null_plainsky512_dn.png", e->skybox_faces[5]);


    rebuild_field(e);
    build_scene_tris(e);

    // Resolve the worker count from --threads: -1 (or 0) = all hardware threads.
    int req = global_config.num_workers;
    if (req <= 0) req = SDL_GetCPUCount();
    e->num_workers = std::clamp(req, 1, 64);

    e->renderer.init(e->num_workers, e->max_bounces, AMBIENT, SHADOW_EPS);
    e->renderer.upload_static(e->static_bvh, e->skybox_faces, e->emissive_tris);
}

void game_update(Game* e, float dt) {
    if (e->show_menu) return;

    // Advance the character skinning; its deformed mesh feeds the dynamic BVH.
    if (e->skin_mesh && e->skin.valid()) {
        e->anim_time += dt;
        float loop = (float)e->skin.frames / e->skin.fps;   // loop the clip
        if (loop > 0.0f && e->anim_time > loop) e->anim_time -= loop;
        e->skin.apply(*e->skin_mesh, e->anim_time);
    }

    const Uint8* keys = SDL_GetKeyboardState(NULL);
    int mdx = 0, mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);

    e->yaw   += float(mdx) * kMouseSensitivity;
    e->pitch += -float(mdy) * kMouseSensitivity;
    e->pitch  = std::clamp(e->pitch, to_radians(-85.0f), to_radians(85.0f));

    vec3 inputDir(0.0f, 0.0f, 0.0f);
    if (keys[SDL_SCANCODE_W]) inputDir.z -= 1.0f;
    if (keys[SDL_SCANCODE_S]) inputDir.z += 1.0f;
    if (keys[SDL_SCANCODE_A]) inputDir.x -= 1.0f;
    if (keys[SDL_SCANCODE_D]) inputDir.x += 1.0f;
    if (e->fly_mode) {
        if (keys[SDL_SCANCODE_LSHIFT]) inputDir.y -= 1.0f;
        if (keys[SDL_SCANCODE_SPACE])  inputDir.y += 1.0f;
    }

    float s = sinf(e->yaw), c = cosf(e->yaw);
    const float moveSpeed = e->move_speed;
    vec3 moveDir;
    moveDir.x = (inputDir.x*c - inputDir.z*s) * moveSpeed;
    moveDir.z = (inputDir.x*s + inputDir.z*c) * moveSpeed;
    moveDir.y =  inputDir.y * moveSpeed;

    e->position = e->position + moveDir * dt;
    if (!e->fly_mode) e->position.y = 1.7f;

    float horizSpeed = std::sqrt(moveDir.x*moveDir.x + moveDir.z*moveDir.z);
    if (!e->fly_mode && horizSpeed > 0.001f) e->bob_phase += dt * horizSpeed * 2.0f;
    float bobAmt = e->fly_mode ? 0.0f : std::min(1.0f, horizSpeed / moveSpeed);
    float bob    = std::sin(e->bob_phase) * 0.05f * bobAmt;

    e->cam.setPosition(e->position + vec3(0.0f, bob, 0.0f));
    e->cam.setRotation(vec3(e->pitch, e->yaw, 0.0f));

    e->time += dt;
}

void game_render(Game* e, SDL_Texture* sdl_fb_texture, float dt) {
    uint64_t t = SDL_GetPerformanceCounter();
    double ms_build = 0, ms_core = 0;

    e->fb.clear(0xFF000000);

    if (e->raytrace_mode) {
        build_scene_tris(e);
        ms_build = tick_ms(t);

        // One dispatch point: the active backend (CPU BVH / Embree / OpenCL)
        // renders the frame from a decoupled scene view.
        RenderScene scene = make_render_scene(e);
        e->renderer.render(scene, e->fb);
        ms_core = tick_ms(t);
    } else {
        renderConfig cfg;
        render_skybox(e->fb, e->cam, e->skybox_faces);
        if (e->field_mesh) {
            cfg.baseColor = pack(vec3(0.55f, 0.55f, 0.58f));
            e->field_mesh->inverseFaces = false; render_mesh(e->fb, e->cam, *e->field_mesh, cfg);
            e->field_mesh->inverseFaces = true;  render_mesh(e->fb, e->cam, *e->field_mesh, cfg);
            e->field_mesh->inverseFaces = false;
        }
        for (auto& [name, mesh_ptr] : e->meshes) {
            cfg.baseColor = pack(color_from_hash(name));
            render_mesh(e->fb, e->cam, *mesh_ptr, cfg);
        }
        render_raster_shadows(e);
        ms_core = tick_ms(t);
    }

    render_gizmo(e->fb, e->cam, vec3{}, 1.0f);

    if (e->show_bvh) {
        if (!e->raytrace_mode) build_scene_tris(e);
        draw_bvh_debug(e);
    }
    if (e->show_normals) {
        if (!e->raytrace_mode) build_scene_tris(e);
        draw_normals_debug(e);
    }

    double ms_present = tick_ms(t);

    if (e->show_hud) {
        char backend[192];
        const char* bname = e->renderer.current_name();
        if (e->raytrace_mode && !e->renderer.current_available()) {
            snprintf(backend, sizeof(backend), "%s (unavailable)", bname);
        } else if (e->raytrace_mode && e->renderer.index() == 0) {
            snprintf(backend, sizeof(backend), "%s (%d workers)", bname, e->renderer.workers());
        } else if (e->raytrace_mode && ocl::available() && std::strcmp(bname, "OCL GPU") == 0) {
            snprintf(backend, sizeof(backend), "%s: %s", bname, ocl::device_name());
        } else {
            snprintf(backend, sizeof(backend), "%s", bname);
        }

        if (e->hud_simple) {
            char line[192];
            snprintf(line, sizeof(line),
                "FPS: %.0f  |  %s  |  %s  |  BVH: %s  |  [H] expand",
                1.0f/dt, e->raytrace_mode ? "Raytrace" : "Raster",
                backend, e->use_bvh ? "On" : "Off");
            draw_text(e->fb, 12, 12, line, 0x88000000, 0x88000000);
            draw_text(e->fb, 10, 10, line, 0xFFFFFFFF);
        } else {
            char HUD[640] = {0};
            snprintf(HUD, sizeof(HUD),
                "=== SR-LEC (%s) ===\n"
                "Frametime: %.2fms   FPS: %.2f\n"
                "render: %.1fms  dyn-build: %.1fms  rest: %.1fms\n"
                "--- Static BVH (scene) ---\n"
                "  tris: %zu  nodes: %zu  build: %.2fms\n"
                "--- Dynamic BVH (objs+peds) ---\n"
                "  tris: %zu  nodes: %zu\n"
                "Accel: %s   Static: %s  Dyn: %s\n"
                "Backend: %s\n"
                "Move: %s\n"
                "[TAB] mode [B] BVH [V] vis [N] normals\n[M] menu  [SPACE x2] walk/fly\n[H] compact  [G] backend\n",
                e->raytrace_mode ? "RAYTRACE" : "RASTER",
                dt*1000.0f, 1.0f/dt, ms_core, ms_build, ms_present,
                e->static_bvh.triangle_count(), e->static_bvh.node_count(), e->static_build_ms,
                e->dynamic_bvh.triangle_count(), e->dynamic_bvh.node_count(),
                e->use_bvh ? "BVH (fast)" : "BRUTE FORCE (slow)",
                e->build_strategy==bvh::SAH ? "SAH" : e->build_strategy==bvh::Median ? "Median" : "Morton",
                e->dynamic_build_strategy==bvh::SAH ? "SAH" : e->dynamic_build_strategy==bvh::Median ? "Median" : "Morton",
                backend, e->fly_mode ? "FLY" : "WALK");
            draw_text(e->fb, 22, 22, HUD, 0x88000000, 0x88000000);
            draw_text(e->fb, 20, 20, HUD, 0xFFFFFFFF);
        }
    }

    if (e->show_menu) draw_menu(e);

    SDL_UpdateTexture(sdl_fb_texture, NULL, e->fb.colorBuffer, e->fb.width * sizeof(uint32_t));
}

void game_handle_events(Game* e, SDL_Event& event, bool& running) {
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
        // Scroll up = faster, down = slower. Scale multiplicatively so the step
        // feels even across the range, then clamp to [min, max].
        float factor = (event.wheel.y > 0) ? 1.15f : (event.wheel.y < 0 ? 1.0f / 1.15f : 1.0f);
        e->move_speed = std::clamp(e->move_speed * factor, kMinMoveSpeed, kMaxMoveSpeed);
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
            case SDLK_TAB: e->raytrace_mode = !e->raytrace_mode; break;
            case SDLK_b:   e->use_bvh       = !e->use_bvh;       break;
            case SDLK_v:   e->show_bvh      = !e->show_bvh;      break;
            case SDLK_h:   e->hud_simple    = !e->hud_simple;    break;
            case SDLK_n:   e->show_normals  = !e->show_normals;  break;
            case SDLK_g:   e->renderer.cycle(+1); break;
            default: break;
        }
    }
}

void game_shutdown(Game* e) {
    e->renderer.shutdown();
    delete e->field_mesh;
}
