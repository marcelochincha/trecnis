// Checkpoint game layer: a static box arena + one ray-traced sphere that moves
// with delta time and bounces off the walls.
//
// This file is the seam between game logic and rendering. It builds a
// RenderScene (a POD of borrowed pointers) and hands it to the Renderer. It
// never traces a ray and never touches BVH internals. Floor + walls live in the
// STATIC BVH (built once); the sphere lives in the DYNAMIC BVH (rebuilt each
// frame). No per-frame heap allocation happens in this file — the world-space
// sphere buffer and the raster draw list keep their capacity across frames.

#include <game/sr_game.hpp>
#include <game/game_state.hpp>
#include <engine/geom/shapes.hpp>

#include <core/sr_text.hpp>     // draw_text (HUD)
#include <sr_config.hpp>        // global_config (--threads)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

// Passed to Renderer::init; mirrors render/raytrace/sr_raytrace.cpp
// (AMBIENT = 0.0f, SHADOW_EPS = 1e-4f) so game/ need not include the tracer.
static constexpr float kAmbient   = 0.0f;
static constexpr float kShadowEps = 1e-4f;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static double ms_since(uint64_t t0) {
    return double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
}

// vec3 colour -> packed 0xAARRGGBB (same rule as pack() in sr_raytrace.cpp).
static uint32_t pack_color(const vec3& c) {
    auto ch = [](float v) { return (uint32_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return 0xFF000000u | (ch(c.x) << 16) | (ch(c.y) << 8) | ch(c.z);
}

// Fill `m` with a world-space triangle-soup mirror of `tris` (identity model
// matrix). Used for the static court so the raster backend draws what the
// tracers trace.
static void fill_raster_mesh(mesh& m, const std::vector<bvh::Tri>& tris) {
    m.vertices.clear();
    m.faces.clear();
    m.vertices.reserve(tris.size() * 3);
    m.faces.reserve(tris.size());
    for (const bvh::Tri& t : tris) {
        uint32_t b = (uint32_t)m.vertices.size();
        m.vertices.push_back({ t.v0, {0.0f, 0.0f} });
        m.vertices.push_back({ t.v1, {0.0f, 0.0f} });
        m.vertices.push_back({ t.v2, {0.0f, 0.0f} });
        m.faces.push_back({ b, b + 1, b + 2 });
    }
    m._modelMatrixDirty = true;
}

// Point the fixed camera from cam_pos towards cam_target (engine euler
// convention: yaw about +Y, pitch about +X, forward = -Z).
static void set_fixed_view(Game* e) {
    vec3  d     = normalize(e->cam_target - e->cam_pos);
    float pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    float yaw   = std::atan2(d.x, -d.z);
    e->cam.setPosition(e->cam_pos);
    e->cam.setRotation(vec3(pitch, yaw, 0.0f));
}

// Rewrite dyn_tris_ = [sphere_local_ + ball.pos | racket_local_ + racket.pos],
// in place (dyn_tris_ is sized once in game_init -> no realloc). Normals and
// material are translation-invariant, so only the three positions change.
static void refresh_dyn_tris(Game* e) {
    const std::size_t ns = e->sphere_local_.size();
    const vec3 bc = e->ball.pos;
    for (std::size_t i = 0; i < ns; ++i) {
        const bvh::Tri& L = e->sphere_local_[i];
        bvh::Tri&       W = e->dyn_tris_[i];
        W = L;  W.v0 = L.v0 + bc;  W.v1 = L.v1 + bc;  W.v2 = L.v2 + bc;
    }
    const std::size_t nr = e->racket_local_.size();
    const vec3 rc = e->racket.position();
    for (std::size_t i = 0; i < nr; ++i) {
        const bvh::Tri& L = e->racket_local_[i];
        bvh::Tri&       W = e->dyn_tris_[ns + i];
        W = L;  W.v0 = L.v0 + rc;  W.v1 = L.v1 + rc;  W.v2 = L.v2 + rc;
    }
}

// ---------------------------------------------------------------------------
// the game <-> render seam
// ---------------------------------------------------------------------------

static RenderScene make_render_scene(Game* e) {
    RenderScene s;
    s.cam    = &e->cam;
    s.width  = e->fb.width;
    s.height = e->fb.height;

    s.static_bvh       = &e->static_bvh;
    s.dynamic_bvh      = &e->dynamic_bvh;
    s.brute_tris       = &e->dyn_tris_;       // brute-force / Embree-dyn source
    s.use_bvh          = true;
    s.static_strategy  = e->static_strategy;
    s.dynamic_strategy = e->dynamic_strategy;

    e->raster_items.clear();                  // capacity kept across frames
    if (e->court_mesh)
        e->raster_items.push_back({ e->court_mesh, pack_color(vec3(0.62f, 0.62f, 0.66f)), false });
    e->raster_items.push_back({ &e->sphere_mesh_, pack_color(e->sphere_albedo), false });
    e->raster_items.push_back({ &e->racket_mesh_, pack_color(e->racket_albedo), false });
    s.raster_items = &e->raster_items;

    s.emissive       = nullptr;               // sun light only this phase
    s.point_lights   = nullptr;
    s.skybox         = &e->skybox_faces;
    s.skybox_enabled = e->skybox_enabled;
    s.bg_color       = e->bg_color;

    s.sun_enabled = e->sun_enabled;
    s.reflections = false;   // performance: no recursion this phase
    s.max_bounces = 1;
    s.gi_enabled  = false;
    return s;
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

Game* game_create(int width, int height) {
    return new Game(width, height);
}

void game_rebuild_static(Game* e) {
    uint64_t t0 = SDL_GetPerformanceCounter();

    // Box arena: floor + back + left + right walls (front + ceiling stay open
    // as invisible reflection planes). Thin boxes, outward normals.
    const vec3 wall_col(0.58f, 0.58f, 0.62f);
    const vec3 floor_col(0.35f, 0.37f, 0.40f);

    std::vector<bvh::Tri> tris;
    tris.reserve(96);
    geom::add_box(tris, vec3(-4.0f, -0.10f, -4.0f), vec3(4.0f, 0.0f, 4.0f), floor_col, 0.85f); // floor
    geom::add_box(tris, vec3(-4.0f,  0.0f, -4.10f), vec3(4.0f, 6.0f, -4.0f), wall_col, 0.80f); // back
    geom::add_box(tris, vec3(-4.10f, 0.0f, -4.0f),  vec3(-4.0f, 6.0f, 4.0f), wall_col, 0.80f); // left
    geom::add_box(tris, vec3( 4.0f,  0.0f, -4.0f),  vec3(4.10f, 6.0f, 4.0f), wall_col, 0.80f); // right

    // The racket is dynamic now (movable) -> it lives in the DYNAMIC BVH, not
    // here. Only the immovable arena is static.
    e->static_tri_count = tris.size();

    delete e->court_mesh;
    e->court_mesh = new mesh;
    fill_raster_mesh(*e->court_mesh, tris);

    e->static_bvh.build(std::move(tris), e->static_strategy);   // SAH, once
    e->static_build_ms = ms_since(t0);

    e->emissive_tris.clear();   // none, but keep the pattern for later phases
    e->renderer.reload_scene(e->static_bvh, e->skybox_faces, e->emissive_tris);

    std::cout << "Static arena: " << e->static_bvh.triangle_count() << " tris, "
              << e->static_bvh.node_count() << " nodes, SAH build "
              << e->static_build_ms << " ms\n";
}

void game_init(Game* e) {
    e->cam = camera(e->cam_pos, vec3(0.0f, 0.0f, 0.0f), 55.0f,
                    float(e->fb.width) / float(e->fb.height), 0.01f, 1000.0f);
    set_fixed_view(e);

    load_png_texture("res/textures/skybox3/null_plainsky512_rt.png", e->skybox_faces[0]);
    load_png_texture("res/textures/skybox3/null_plainsky512_bk.png", e->skybox_faces[1]);
    load_png_texture("res/textures/skybox3/null_plainsky512_ft.png", e->skybox_faces[2]);
    load_png_texture("res/textures/skybox3/null_plainsky512_lf.png", e->skybox_faces[3]);
    load_png_texture("res/textures/skybox3/null_plainsky512_up.png", e->skybox_faces[4]);
    load_png_texture("res/textures/skybox3/null_plainsky512_dn.png", e->skybox_faces[5]);

    // --- ball + arena ---
    e->ball.radius      = 0.5f;
    e->ball.gravity     = 9.81f;
    e->ball.restitution = 0.75f;                 // normal KE -> e^2 = 0.56 per bounce
    e->ball.drag        = 0.10f;                 // quadratic air resistance
    e->ball.magnus      = 0.10f;                 // Magnus strength
    e->ball.spin_decay  = 0.08f;                 // spin slowly fades in flight
    // Lofted toward +X with a touch of +Z to keep the ball near the racket's
    // depth against the Magnus (-Z) drift, so its arc crosses the racket zone
    // while still airborne — a natural chance to intercept it.
    e->ball.pos         = vec3(-3.2f, 2.5f, 0.7f);
    e->ball.vel         = vec3(4.0f, 4.0f, 1.4f);
    e->ball.spin        = vec3(0.0f, 14.0f, 0.0f);   // sidespin, rad/s (unchanged)
    e->arena.min        = vec3(-4.0f, 0.0f, -3.5f);
    e->arena.max        = vec3( 4.0f, 6.0f,  3.5f);

    // --- movable racket: a paddle the player translates. Orientation and size
    //     are fixed; only the position moves (step()). Faced nearly toward the
    //     camera (small yaw + slight upward pitch) so its face is clearly
    //     visible, and placed centrally / a bit forward, in the ball's arc. ---
    e->racket.configure(/*pos*/   vec3(0.0f, 2.9f, 0.6f),
                        /*euler*/ vec3(to_radians(8.0f), to_radians(-6.0f), 0.0f),
                        /*size*/  vec3(1.8f, 1.8f, 0.28f),
                        /*restitution*/ 0.85f);
    e->racket_speed     = 5.0f;
    e->racket_limits.min = vec3(-2.6f, 2.0f, -2.6f);   // paddle stays in the flight zone,
    e->racket_limits.max = vec3( 2.6f, 4.0f,  2.4f);   // clear of walls and the floor

    // --- dynamic geometry: sphere + racket, generated ONCE at the origin ---
    e->sphere_local_.clear();
    geom::add_sphere(e->sphere_local_, vec3(0.0f, 0.0f, 0.0f), e->ball.radius,
                     e->sphere_albedo, /*rough*/ 0.55f, /*metal*/ 0.0f, /*ior*/ 1.5f,
                     /*slices*/ 20, /*stacks*/ 14, /*smooth*/ true);
    e->racket_local_.clear();
    e->racket.append_local_tris(e->racket_local_);         // 12 tris, orientation baked

    e->dyn_tris_.clear();
    e->dyn_tris_.reserve(e->sphere_local_.size() + e->racket_local_.size());
    e->dyn_tris_.insert(e->dyn_tris_.end(), e->sphere_local_.begin(), e->sphere_local_.end());
    e->dyn_tris_.insert(e->dyn_tris_.end(), e->racket_local_.begin(), e->racket_local_.end());

    fill_raster_mesh(e->sphere_mesh_, e->sphere_local_);   // local space; moved via setPosition
    fill_raster_mesh(e->racket_mesh_, e->racket_local_);

    e->raster_items.reserve(3);

    // --- build BVHs ---
    game_rebuild_static(e);
    refresh_dyn_tris(e);
    e->dynamic_bvh.build(e->dyn_tris_, e->dynamic_strategy);   // Morton
    e->sphere_mesh_.setPosition(e->ball.pos);
    e->racket_mesh_.setPosition(e->racket.position());

    // --- renderer ---
    int req = global_config.num_workers;
    if (req <= 0) req = SDL_GetCPUCount();
    e->num_workers = std::clamp(req, 1, 64);

    e->renderer.init(e->num_workers, /*max_bounces*/ 1, kAmbient, kShadowEps);
    e->renderer.upload_static(e->static_bvh, e->skybox_faces, e->emissive_tris);

    e->metrics.primary_rays = (std::size_t)e->fb.width * (std::size_t)e->fb.height;

    std::cout << "Dynamic: " << e->sphere_local_.size() << " sphere + "
              << e->racket_local_.size() << " racket tris\n"
              << "Racket: WASD / arrows move (X/Y), Q/E depth (Z)"
              << (e->racket_autopilot ? "  [autopilot]" : "") << "\n"
              << "Renderer: " << e->renderer.count() << " backends, "
              << e->renderer.workers() << " workers, starting on '"
              << e->renderer.current_name() << "'\n";
}

// Player intent for the racket this frame: -1/0/+1 per axis from the existing
// SDL keyboard state (WASD or arrows on X/Y, Q/E on Z). --racket-auto replaces
// it with a scripted oscillation so headless tests are reproducible.
static vec3 racket_input(Game* e, float dt) {
    if (e->racket_autopilot) {
        // Test affordance only: chase the ball in X/Y (per-axis intent, capped
        // by racket_speed + limits) so a mid-air interception is demonstrable in
        // headless runs. The real game input path below is untouched.
        vec3 to_ball = e->ball.pos - e->racket.position();
        vec3 d(0.0f, 0.0f, 0.0f);
        if (std::fabs(to_ball.x) > 0.05f) d.x = to_ball.x > 0.0f ? 1.0f : -1.0f;
        if (std::fabs(to_ball.y) > 0.05f) d.y = to_ball.y > 0.0f ? 1.0f : -1.0f;
        return d;
    }
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    vec3 d(0.0f, 0.0f, 0.0f);
    if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) d.x -= 1.0f;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d.x += 1.0f;
    if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) d.y += 1.0f;
    if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) d.y -= 1.0f;
    if (k[SDL_SCANCODE_Q]) d.z -= 1.0f;
    if (k[SDL_SCANCODE_E]) d.z += 1.0f;
    return d;
}

void game_update(Game* e, float dt) {
    // Move the racket first, so the ball resolves against its new position.
    e->racket.step(racket_input(e, dt), e->racket_limits, dt, e->racket_speed);

    // Ball::update consumes the real dt with an internal fixed physics sub-step,
    // so the trajectory is frame-rate independent (and it clamps a hitching dt
    // itself — no spiral of death). The racket's velocity is NOT passed in: its
    // motion does not add energy to the ball this checkpoint.
    uint64_t tp = SDL_GetPerformanceCounter();
    e->bounces_total += e->ball.update(dt, e->arena, &e->racket.collider());
    refresh_dyn_tris(e);                           // in-place, no allocation
    e->sphere_mesh_.setPosition(e->ball.pos);      // raster mirrors follow
    e->racket_mesh_.setPosition(e->racket.position());
    e->metrics.physics_ms = Metrics::ema(e->metrics.physics_ms, ms_since(tp));

    // Frame order: update -> build dynamic BVH -> render.
    uint64_t tb = SDL_GetPerformanceCounter();
    e->dynamic_bvh.build(e->dyn_tris_, e->dynamic_strategy);   // Morton, per frame
    e->metrics.dyn_build_ms = Metrics::ema(e->metrics.dyn_build_ms, ms_since(tb));
}

void game_render(Game* e, SDL_Texture* sdl_fb_texture, float dt) {
    e->metrics.frame_ms = Metrics::ema(e->metrics.frame_ms, dt * 1000.0);

    e->fb.clear(e->skybox_enabled ? 0xFF000000u : pack_color(e->bg_color));

    RenderScene scene = make_render_scene(e);
    uint64_t tr = SDL_GetPerformanceCounter();
    e->renderer.render(scene, e->fb);              // single dispatch point
    e->metrics.render_ms = Metrics::ema(e->metrics.render_ms, ms_since(tr));

    const Metrics& m = e->metrics;
    std::size_t tris = e->static_bvh.triangle_count() + e->dynamic_bvh.triangle_count();
    double fps = m.frame_ms > 0.0 ? 1000.0 / m.frame_ms : 0.0;

    // Headless perf telemetry (enable with --debug): one line every ~2 s of
    // wall-clock so the checkpoint numbers (and dt-independence) are visible
    // without a screenshot.
    static uint64_t t_start = SDL_GetPerformanceCounter();
    static double   next_log = 2.0;
    double elapsed = double(SDL_GetPerformanceCounter() - t_start) / double(SDL_GetPerformanceFrequency());
    if (global_config.debug_mode && elapsed >= next_log) {
        next_log += 2.0;
        vec3 rp = e->racket.position(), rv = e->racket.velocity();
        std::printf("[perf] t=%5.1fs | fps %3.0f | frame %5.2f ms | physics %.3f | dynBVH %.3f | render %5.2f "
                    "| ball(%.2f,%.2f,%.2f) |v|=%.2f bounces %d racket %ld "
                    "| rkt(%.2f,%.2f,%.2f) |vr|=%.2f\n",
                    elapsed, fps, m.frame_ms, m.physics_ms, m.dyn_build_ms, m.render_ms,
                    e->ball.pos.x, e->ball.pos.y, e->ball.pos.z,
                    e->ball.speed(), e->bounces_total, e->ball.racket_hits,
                    rp.x, rp.y, rp.z, magnitude(rv));
        std::fflush(stdout);
    }

    if (e->show_hud) {

        vec3 rp = e->racket.position(), rv = e->racket.velocity();
        char hud[800];
        std::snprintf(hud, sizeof(hud),
            "PingPong RT  -  movable racket + control\n"
            "Backend : %s%s   (TAB / G to cycle)\n"
            "FPS     : %.0f      Frame : %.2f ms\n"
            "Physics : %.3f ms   Dyn BVH build : %.3f ms\n"
            "Render  : %.2f ms   (traversal + shading)\n"
            "Tris    : %zu static + %zu dynamic = %zu\n"
            "Rays    : %zu primary / frame\n"
            "Ball    : p(%.1f, %.1f, %.1f)  speed %.2f  bounces: %d\n"
            "Spin    : (%.1f, %.1f, %.1f) rad/s   |w| %.1f\n"
            "Model   : g %.2f  e %.2f  drag %.2f  magnus %.2f  (1/240 s)\n"
            "Racket  : p(%.1f, %.1f, %.1f)  v(%.1f, %.1f, %.1f)  hits: %ld%s\n"
            "Move    : WASD / arrows = X/Y   Q/E = Z        [ESC] quit",
            e->renderer.current_name(),
            e->renderer.current_available() ? "" : " (n/a)",
            fps, m.frame_ms,
            m.physics_ms, m.dyn_build_ms,
            m.render_ms,
            e->static_bvh.triangle_count(), e->dynamic_bvh.triangle_count(), tris,
            m.primary_rays,
            e->ball.pos.x, e->ball.pos.y, e->ball.pos.z, e->ball.speed(), e->bounces_total,
            e->ball.spin.x, e->ball.spin.y, e->ball.spin.z, e->ball.spin_rate(),
            e->ball.gravity, e->ball.restitution, e->ball.drag, e->ball.magnus,
            rp.x, rp.y, rp.z, rv.x, rv.y, rv.z, e->ball.racket_hits,
            e->racket_autopilot ? "  [auto]" : "");

        draw_text(e->fb, 11, 11, hud, 0xAA000000, 0xAA000000);
        draw_text(e->fb, 10, 10, hud, 0xFFFFFFFF);
    }

    SDL_UpdateTexture(sdl_fb_texture, nullptr, e->fb.colorBuffer,
                      e->fb.width * sizeof(uint32_t));
}

void game_handle_events(Game* e, SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) { running = false; return; }

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_ESCAPE: running = false;        break;
            case SDLK_TAB:    e->renderer.cycle(-1);  break;
            case SDLK_g:      e->renderer.cycle(+1);  break;
            default: break;
        }
        if (event.key.keysym.sym == SDLK_TAB || event.key.keysym.sym == SDLK_g) {
            std::cout << "Backend: " << e->renderer.current_name()
                      << (e->renderer.current_available() ? "" : " (unavailable)") << "\n";
        }
    }
}

void game_shutdown(Game* e) {
    e->renderer.shutdown();
    delete e->court_mesh;
    e->court_mesh = nullptr;
}

void game_destroy(Game* e) {
    delete e;
}

void game_set_backend(Game* e, int index) {
    if (e->renderer.select(index))
        std::cout << "Backend pinned: " << e->renderer.current_name() << "\n";
    else
        std::cout << "Backend " << index << " unavailable, staying on '"
                  << e->renderer.current_name() << "'\n";
}

void game_set_racket_autopilot(Game* e, bool on) {
    e->racket_autopilot = on;
}
