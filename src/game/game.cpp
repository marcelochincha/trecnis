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

// Seed the camera's live yaw/pitch (e->cam_yaw/cam_pitch) from cam_pos/
// cam_target at startup (engine euler convention: yaw about +Y, pitch about
// +X, forward = -Z), and push that initial state to the camera. Every frame
// afterwards, update_camera() re-applies e->cam_pos/cam_yaw/cam_pitch --
// this function only runs once, in game_init.
static void set_fixed_view(Game* e) {
    vec3 d = normalize(e->cam_target - e->cam_pos);
    e->cam_pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    e->cam_yaw   = std::atan2(d.x, -d.z);
    e->cam.setPosition(e->cam_pos);
    e->cam.setRotation(vec3(e->cam_pitch, e->cam_yaw, 0.0f));
}

static constexpr float kMouseSensitivity = 0.0025f;   // rad per relative pixel
static constexpr float kMaxCamPitch      = 1.4834f;   // ~85 degrees

// Free-fly camera, active only while e->free_cam is set (toggle: C). Adapted
// from the base engine's free-fly control (trecnis src/game/sr_game.cpp) to
// this project's `camera` type, which has no built-in fly controller of its
// own -- it just takes a position + (pitch,yaw,roll) each frame, so the game
// layer owns the running yaw/pitch/position state (e->cam_yaw/cam_pitch/
// cam_pos). WASD/Q/E are read directly here instead of going through
// racket_input, so this and the racket's own WASD/Q/E (see racket_input)
// never fight over the same frame's input -- exactly one of them reads the
// keyboard, gated by free_cam.
static void update_camera(Game* e, float dt) {
    if (e->free_cam) {
        int mdx = 0, mdy = 0;
        SDL_GetRelativeMouseState(&mdx, &mdy);
        e->cam_yaw   += (float)mdx * kMouseSensitivity;
        e->cam_pitch -= (float)mdy * kMouseSensitivity;
        e->cam_pitch  = std::clamp(e->cam_pitch, -kMaxCamPitch, kMaxCamPitch);

        const Uint8* k = SDL_GetKeyboardState(nullptr);
        vec3 in(0.0f, 0.0f, 0.0f);
        if (k[SDL_SCANCODE_W]) in.z -= 1.0f;
        if (k[SDL_SCANCODE_S]) in.z += 1.0f;
        if (k[SDL_SCANCODE_A]) in.x -= 1.0f;
        if (k[SDL_SCANCODE_D]) in.x += 1.0f;
        if (k[SDL_SCANCODE_Q]) in.y -= 1.0f;
        if (k[SDL_SCANCODE_E]) in.y += 1.0f;
        float il = magnitude(in);
        if (il > 1e-4f) in = in / il;   // no diagonal speed boost

        const float s = std::sin(e->cam_yaw), c = std::cos(e->cam_yaw);
        vec3 move(in.x * c - in.z * s, in.y, in.x * s + in.z * c);
        e->cam_pos = e->cam_pos + move * (e->cam_move_speed * dt);
    }
    e->cam.setPosition(e->cam_pos);
    e->cam.setRotation(vec3(e->cam_pitch, e->cam_yaw, 0.0f));
}

// Rewrite dyn_tris_ = [sphere + ball.pos | racket + racket.pos] in place. The
// racket half is dropped (resize down, no realloc — capacity is fixed in
// game_init) when the racket is disabled by a demo stage. Normals/material are
// translation-invariant, so only the three positions change.
static void refresh_dyn_tris(Game* e) {
    const std::size_t ns = e->sphere_local_.size();
    const std::size_t nr = e->racket_enabled ? e->racket_local_.size() : 0;
    e->dyn_tris_.resize(ns + nr);

    const vec3 bc = e->ball.pos;
    for (std::size_t i = 0; i < ns; ++i) {
        const bvh::Tri& L = e->sphere_local_[i];
        bvh::Tri&       W = e->dyn_tris_[i];
        W = L;  W.v0 = L.v0 + bc;  W.v1 = L.v1 + bc;  W.v2 = L.v2 + bc;
    }
    if (nr) {
        const vec3 rc = e->racket.position();
        for (std::size_t i = 0; i < nr; ++i) {
            const bvh::Tri& L = e->racket_local_[i];
            bvh::Tri&       W = e->dyn_tris_[ns + i];
            W = L;  W.v0 = L.v0 + rc;  W.v1 = L.v1 + rc;  W.v2 = L.v2 + rc;
        }
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
    if (e->racket_enabled)
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
// Technical Progress / Physics Evolution demo
// ---------------------------------------------------------------------------

// Just behind the table's near edge (half_len 1.37), at paddle height above
// the surface -- the player's side, where the character placeholder stands.
static const vec3 kRacketHome(0.0f, 0.95f, -1.55f);

// Descriptions shown in the demo panel (index 0 = stage 1).
static const char* kStageTitle[4] = {
    "STAGE 1 - BASIC BALL",
    "STAGE 2 - BASIC PHYSICS",
    "STAGE 3 - ADVANCED PHYSICS",
    "STAGE 4 - BALL + RACKET",
};
static const char* kStageBlurb[4] = {
    "A 3D sphere, ray-traced in real time, moving at\nconstant velocity and bouncing perfectly (no energy loss).",
    "Gravity and a restitution coefficient: the ball falls\nand loses energy on every bounce until it settles.",
    "Same ball and arena, now with aerodynamic drag, angular\nvelocity (spin) and the Magnus effect curving the path.",
    "The full game state: all of the above plus a movable,\ncontrollable racket and ball<->racket collision (dynamic BVH).",
};

// Switch the demo to `stage` (1..4): flip the physics feature set and re-seed
// the ball with initial conditions that make that stage obvious. Stage 4 == the
// full current game. The scene keeps running live; no historical code is rebuilt
// — only the existing Ball tunables + the racket_enabled flag are toggled.
static void apply_demo_stage(Game* e, int stage) {
    e->demo_stage = std::clamp(stage, 1, 4);
    Ball& b = e->ball;
    // Regulation-table scale. The sphere's BVH/raster geometry is generated
    // ONCE in game_init from whatever radius is active at that moment and
    // never rebuilt (see sphere_local_), so every stage MUST share one
    // radius here — otherwise a stage switch would desync the visible
    // sphere size from its (now different) collision radius.
    b.radius     = 0.06f;
    b.spin_decay = 0.08f;
    b.rest_speed = 0.5f;

    switch (e->demo_stage) {
        case 1:  // constant velocity, perfectly elastic, no forces
            b.gravity = 0.0f;  b.drag = 0.0f;  b.magnus = 0.0f;
            b.restitution = 1.0f;  b.rest_speed = 0.0f;      // never settle
            b.reset(vec3(-2.0f, 3.2f, 0.0f), vec3(3.4f, 2.4f, 1.7f), vec3(0.0f, 0.0f, 0.0f));
            e->racket_enabled = false;
            e->table_enabled  = false;
            break;
        case 2:  // gravity + restitution
            b.gravity = 9.81f; b.drag = 0.0f;  b.magnus = 0.0f;
            b.restitution = 0.75f;
            b.reset(vec3(-1.5f, 5.2f, 0.0f), vec3(2.6f, 0.4f, 0.5f), vec3(0.0f, 0.0f, 0.0f));
            e->racket_enabled = false;
            e->table_enabled  = false;
            break;
        case 3:  // + drag + spin + Magnus
            b.gravity = 9.81f; b.drag = 0.10f; b.magnus = 0.10f;
            b.restitution = 0.75f;
            b.reset(vec3(-3.4f, 3.8f, 0.2f), vec3(3.4f, 0.8f, 0.0f), vec3(0.0f, 20.0f, 0.0f));
            e->racket_enabled = false;
            e->table_enabled  = false;
            break;
        case 4:  // full current game: ball crosses the table net under gravity +
                 // drag + spin + Magnus, lands on the far half and bounces.
        default:
            b.gravity = 9.81f; b.drag = 0.10f; b.magnus = 0.10f;
            b.restitution = 0.75f;
            // Serve from the player's side (z < 0, near the racket/character),
            // arcing over the net (z = 0) with clearance, landing on the far
            // half and continuing toward the backdrop wall. Lower and flatter
            // than the previous checkpoint's launch (was y=2.0, a floaty lob
            // unrelated to the racket/character height) -- only the initial
            // position/velocity/spin changed here, Ball::step_fixed itself
            // (gravity/drag/spin/Magnus/restitution) is untouched.
            b.reset(vec3(0.0f, 1.5f, -1.0f), vec3(0.0f, 0.9f, 3.0f), vec3(6.0f, 0.0f, 0.0f));
            e->racket_enabled = true;
            e->table_enabled  = true;
            break;
    }
    e->racket.recenter(kRacketHome);
    e->bounces_total = 0;
    e->hits_reported_ = 0;
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

Game* game_create(int width, int height) {
    return new Game(width, height);
}

void game_rebuild_static(Game* e) {
    uint64_t t0 = SDL_GetPerformanceCounter();

    // Open scene, not a closed arena: a floor for context/shadows, the table,
    // and ONE backdrop wall behind the far end of the table (+Z, the ball's
    // travel direction) -- no side walls, no ceiling, so the table stays the
    // visual centrepiece instead of a box around it.
    const vec3 wall_col(0.58f, 0.58f, 0.62f);
    const vec3 floor_col(0.35f, 0.37f, 0.40f);
    const vec3 shirt_col(0.20f, 0.35f, 0.55f);
    const vec3 pants_col(0.15f, 0.15f, 0.18f);
    const vec3 skin_col (0.80f, 0.62f, 0.50f);

    std::vector<bvh::Tri> tris;
    tris.reserve(160);
    geom::add_box(tris, vec3(-3.0f, -0.10f, -2.8f), vec3(3.0f, 0.0f, 2.6f), floor_col, 0.85f); // floor

    // Backdrop wall, just past the table's far edge, aligned with the ball's
    // primary launch direction. Visual only this checkpoint -- no physics
    // collider yet (that is a later checkpoint).
    const float wall_z = e->table.half_len + 0.9f;
    geom::add_box(tris, vec3(-2.4f, 0.0f, wall_z), vec3(2.4f, 2.4f, wall_z + 0.10f), wall_col, 0.80f);

    // Regulation table (surface + edge lines + net + legs), centred at the
    // scene origin. Dimensions come from e->table so the visual mesh and the
    // ball's surface collider (Table::resolve) can never drift apart.
    e->table.append_tris(tris);

    // Player character: a simple blocky placeholder (legs + torso + head, no
    // skinning/animation) standing on the near side, behind the racket, so
    // the composition reads as a table-tennis scene rather than an empty
    // paddle floating in space.
    geom::add_box(tris, vec3(-0.14f, 0.0f,  -2.16f), vec3(0.14f, 0.90f, -1.94f), pants_col, 0.60f); // legs
    geom::add_box(tris, vec3(-0.18f, 0.90f, -2.18f), vec3(0.18f, 1.55f, -1.92f), shirt_col, 0.55f); // torso
    geom::add_box(tris, vec3(-0.10f, 1.55f, -2.15f), vec3(0.10f, 1.75f, -1.95f), skin_col,  0.50f); // head

    // The racket is dynamic now (movable) -> it lives in the DYNAMIC BVH, not
    // here. Only the immovable floor + wall + table + character are static.
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

    // --- arena (invisible physics bound; the visible walls are gone) ---
    // Z max is pushed well past the backdrop wall (table.half_len + 0.9) so
    // the ball's forward flight is never reflected back by this bound -- the
    // wall itself has no collider yet (visual only this checkpoint).
    e->arena.min = vec3(-4.0f, 0.0f, -3.5f);
    e->arena.max = vec3( 4.0f, 6.0f, 12.0f);

    // --- movable racket: a paddle the player translates. Orientation and size
    //     are fixed; only the position moves (step()). Table-tennis-paddle
    //     scale (was oversized at 1.8 units -- comically large next to the
    //     0.06-radius ball), held at the player's side just behind the
    //     table's near edge. ---
    e->racket.configure(/*pos*/   kRacketHome,
                        /*euler*/ vec3(to_radians(8.0f), to_radians(-6.0f), 0.0f),
                        /*size*/  vec3(0.32f, 0.34f, 0.03f),
                        /*restitution*/ 0.85f);
    e->racket_speed     = 5.0f;
    e->racket_limits.min = vec3(-1.0f, 0.70f, -1.90f);   // stays near the player's side of
    e->racket_limits.max = vec3( 1.0f, 1.30f, -1.20f);   // the table, clear of the floor

    // --- ball: seed with the full-game (stage 4) feature set ---
    apply_demo_stage(e, 4);

    // --- dynamic geometry: sphere + racket, generated ONCE at the origin ---
    e->sphere_local_.clear();
    geom::add_sphere(e->sphere_local_, vec3(0.0f, 0.0f, 0.0f), e->ball.radius,
                     e->sphere_albedo, /*rough*/ 0.55f, /*metal*/ 0.0f, /*ior*/ 1.5f,
                     /*slices*/ 20, /*stacks*/ 14, /*smooth*/ true);
    e->racket_local_.clear();
    e->racket.append_local_tris(e->racket_local_);         // 12 tris, orientation baked

    e->dyn_tris_.clear();
    e->dyn_tris_.reserve(e->sphere_local_.size() + e->racket_local_.size());
    // refresh_dyn_tris (below) resizes within this capacity and fills it — the
    // racket half is included only while racket_enabled.

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
              << (e->racket_autopilot == 1 ? "  [auto: chase]"
                  : e->racket_autopilot == 2 ? "  [auto: recede]" : "") << "\n"
              << "Camera: C toggles free-fly (mouse-look, WASD/Q/E move, wheel = speed)\n"
              << "Renderer: " << e->renderer.count() << " backends, "
              << e->renderer.workers() << " workers, starting on '"
              << e->renderer.current_name() << "'\n";
}

// Player intent for the racket this frame: -1/0/+1 per axis from the existing
// SDL keyboard state (WASD or arrows on X/Y, Q/E on Z). --racket-auto /
// --racket-flee replace it with an automatic move so headless tests are
// reproducible.
static vec3 racket_input(Game* e, float /*dt*/) {
    if (e->racket_autopilot != 0) {
        // Test affordance only (headless runs). Both modes track the ball in Y.
        //   mode 1 "chase":  close in on the ball in X       -> paddle moving into the ball
        //   mode 2 "recede": give ground in X as it closes   -> paddle moving away at contact
        // Per-axis intent, capped by racket_speed + limits. The real keyboard
        // input path below is untouched.
        vec3 to_ball = e->ball.pos - e->racket.position();
        vec3 d(0.0f, 0.0f, 0.0f);
        if (std::fabs(to_ball.y) > 0.05f) d.y = to_ball.y > 0.0f ? 1.0f : -1.0f;
        if (e->racket_autopilot == 1) {
            // chase: close in on the ball -> paddle moving INTO the ball
            if (std::fabs(to_ball.x) > 0.05f) d.x = to_ball.x > 0.0f ? 1.0f : -1.0f;
        } else {
            // recede: keep aligned in Z, but stay ~1.6 u ahead of the ball in
            // its X travel direction, so the paddle runs the same way the ball
            // flies and is moving AWAY from it at contact.
            if (std::fabs(to_ball.z) > 0.05f) d.z = to_ball.z > 0.0f ? 1.0f : -1.0f;
            float lead = (e->ball.vel.x >= 0.0f) ? 1.6f : -1.6f;
            float dx   = (e->ball.pos.x + lead) - e->racket.position().x;
            if (std::fabs(dx) > 0.05f) d.x = dx > 0.0f ? 1.0f : -1.0f;
        }
        return d;
    }
    // Free-fly camera mode (toggle: C) takes WASD/Q/E for itself (see
    // update_camera) -- freeze the racket rather than have both read the
    // same keys the same frame.
    if (e->free_cam) return vec3(0.0f, 0.0f, 0.0f);
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    // While the demo panel is open the arrow keys drive stage navigation, so the
    // racket then only responds to WASD + Q/E.
    const bool arrows = !e->demo_open;
    vec3 d(0.0f, 0.0f, 0.0f);
    if ((arrows && k[SDL_SCANCODE_LEFT])  || k[SDL_SCANCODE_A]) d.x -= 1.0f;
    if ((arrows && k[SDL_SCANCODE_RIGHT]) || k[SDL_SCANCODE_D]) d.x += 1.0f;
    if ((arrows && k[SDL_SCANCODE_UP])    || k[SDL_SCANCODE_W]) d.y += 1.0f;
    if ((arrows && k[SDL_SCANCODE_DOWN])  || k[SDL_SCANCODE_S]) d.y -= 1.0f;
    if (k[SDL_SCANCODE_Q]) d.z -= 1.0f;
    if (k[SDL_SCANCODE_E]) d.z += 1.0f;
    return d;
}

void game_update(Game* e, float dt) {
    update_camera(e, dt);   // independent of physics; fixed view unless free_cam

    // Move the racket first, so the ball resolves against its new position.
    // Demo stages 1-3 disable the racket entirely.
    if (e->racket_enabled)
        e->racket.step(racket_input(e, dt), e->racket_limits, dt, e->racket_speed);

    // Ball::update consumes the real dt with an internal fixed physics sub-step,
    // so the trajectory is frame-rate independent (and it clamps a hitching dt
    // itself — no spiral of death). The racket collider carries its velocity, so
    // the ball's contact response depends on the ball-vs-racket relative motion.
    uint64_t tp = SDL_GetPerformanceCounter();
    e->bounces_total += e->ball.update(dt, e->arena,
                                       e->racket_enabled ? &e->racket.collider() : nullptr,
                                       e->table_enabled  ? &e->table            : nullptr);
    refresh_dyn_tris(e);                           // in-place, no allocation
    e->sphere_mesh_.setPosition(e->ball.pos);      // raster mirrors follow
    e->racket_mesh_.setPosition(e->racket.position());
    e->metrics.physics_ms = Metrics::ema(e->metrics.physics_ms, ms_since(tp));

    // One line per racket contact (--debug): incoming/outgoing ball speed and
    // the racket speed at impact, to see the velocity transfer.
    if (global_config.debug_mode && e->ball.racket_hits != e->hits_reported_) {
        e->hits_reported_ = e->ball.racket_hits;
        std::printf("[hit] racket #%ld  ball |v| %.2f -> %.2f  (dV %+.2f)  racket |v|=%.2f\n",
                    e->ball.racket_hits, e->ball.hit_speed_in, e->ball.hit_speed_out,
                    e->ball.hit_speed_out - e->ball.hit_speed_in, e->ball.hit_racket_speed);
        std::fflush(stdout);
    }

    // Frame order: update -> build dynamic BVH -> render.
    uint64_t tb = SDL_GetPerformanceCounter();
    e->dynamic_bvh.build(e->dyn_tris_, e->dynamic_strategy);   // Morton, per frame
    e->metrics.dyn_build_ms = Metrics::ema(e->metrics.dyn_build_ms, ms_since(tb));
}

// The Technical Progress panel (T). Explains what each stage achieved and shows
// live readouts for the currently selected stage. Uses only the existing text
// HUD; no new UI system.
static void draw_demo_panel(Game* e, double fps) {
    const Ball& b = e->ball;
    const int   s = e->demo_stage;                    // 1..4
    auto chk = [](bool on) { return on ? "[x]" : "[ ]"; };

    // Feature set per stage (matches apply_demo_stage).
    const bool f_grav   = (s >= 2);
    const bool f_rest   = (s >= 2);
    const bool f_drag   = (s >= 3);
    const bool f_spin   = (s >= 3);
    const bool f_magnus = (s >= 3);
    const bool f_racket = (s >= 4);

    char p[1400];
    int n = std::snprintf(p, sizeof(p),
        "=== TECHNICAL PROGRESS  -  Physics Evolution ===\n"
        "  stage %d / 4    LEFT / RIGHT change    1-4 select    T / ESC close\n"
        "\n%s\n%s\n\n"
        " %s Gravity\n"
        " %s Restitution (energy loss)\n"
        " %s Aerodynamic drag\n"
        " %s Spin (angular velocity)\n"
        " %s Magnus effect\n"
        " %s Movable racket + collision\n\n",
        s, kStageTitle[s - 1], kStageBlurb[s - 1],
        chk(f_grav), chk(f_rest), chk(f_drag), chk(f_spin), chk(f_magnus), chk(f_racket));

    if (s == 1) {
        n += std::snprintf(p + n, sizeof(p) - n,
            " Ball speed : %.2f  (constant, elastic e = 1.0)\n"
            " Ball pos   : (%.1f, %.1f, %.1f)\n"
            " Bounces    : %d\n",
            b.speed(), b.pos.x, b.pos.y, b.pos.z, e->bounces_total);
    } else if (s == 2) {
        n += std::snprintf(p + n, sizeof(p) - n,
            " Gravity     : %.2f      Restitution : %.2f\n"
            " Ball speed  : %.2f\n"
            " Ball pos    : (%.1f, %.1f, %.1f)\n"
            " Bounces     : %d\n",
            b.gravity, b.restitution, b.speed(),
            b.pos.x, b.pos.y, b.pos.z, e->bounces_total);
    } else if (s == 3) {
        n += std::snprintf(p + n, sizeof(p) - n,
            " Gravity %.2f  Restitution %.2f  Drag %.2f  Magnus %.2f\n"
            " Ball speed       : %.2f\n"
            " Angular velocity : (%.1f, %.1f, %.1f)   |w| %.1f\n"
            " Ball pos         : (%.1f, %.1f, %.1f)\n",
            b.gravity, b.restitution, b.drag, b.magnus, b.speed(),
            b.spin.x, b.spin.y, b.spin.z, b.spin_rate(),
            b.pos.x, b.pos.y, b.pos.z);
    } else {
        const vec3 rp = e->racket.position();
        n += std::snprintf(p + n, sizeof(p) - n,
            " Gravity %.2f  Restitution %.2f  Drag %.2f  Magnus %.2f\n"
            " Ball speed : %.2f    |w| %.1f    bounces %d\n"
            " Racket pos : (%.1f, %.1f, %.1f)   ball<->racket hits: %ld\n"
            " Racket control : WASD + Q/E   (arrows navigate stages)\n",
            b.gravity, b.restitution, b.drag, b.magnus,
            b.speed(), b.spin_rate(), e->bounces_total,
            rp.x, rp.y, rp.z, e->ball.racket_hits);
    }
    std::snprintf(p + n, sizeof(p) - n,
        "\n Backend %s   FPS %.0f\n",
        e->renderer.current_name(), fps);

    draw_text(e->fb, 13, 13, p, 0xCC000000, 0xCC000000);
    draw_text(e->fb, 12, 12, p, 0xFFFFFFFF);
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

    if (e->demo_open) {
        draw_demo_panel(e, fps);
    } else if (e->show_hud) {

        vec3 rp = e->racket.position(), rv = e->racket.velocity();
        char hud[800];
        std::snprintf(hud, sizeof(hud),
            "PingPong RT  -  racket velocity transfer\n"
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
            "Impact  : ball |v| %.2f -> %.2f   racket |v| %.2f\n"
            "Racket  : WASD / arrows = X/Y   Q/E = Z\n"
            "Camera  : C = toggle free-fly (mouse-look, WASD/Q/E, wheel)   [ESC] quit",
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
            e->racket_autopilot == 1 ? "  [chase]" : e->racket_autopilot == 2 ? "  [recede]" : "",
            e->ball.hit_speed_in, e->ball.hit_speed_out, e->ball.hit_racket_speed);

        draw_text(e->fb, 11, 11, hud, 0xAA000000, 0xAA000000);
        draw_text(e->fb, 10, 10, hud, 0xFFFFFFFF);
    }

    SDL_UpdateTexture(sdl_fb_texture, nullptr, e->fb.colorBuffer,
                      e->fb.width * sizeof(uint32_t));
}

void game_handle_events(Game* e, SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) { running = false; return; }

    // Mouse wheel adjusts the free-fly camera's move speed (a no-op, and
    // otherwise unused, while the racket owns WASD/Q/E).
    if (event.type == SDL_MOUSEWHEEL) {
        if (e->free_cam && event.wheel.y != 0) {
            float factor = event.wheel.y > 0 ? 1.15f : 1.0f / 1.15f;
            e->cam_move_speed = std::clamp(e->cam_move_speed * factor, 0.5f, 20.0f);
        }
        return;
    }
    if (event.type != SDL_KEYDOWN) return;

    const SDL_Keycode k = event.key.keysym.sym;

    // C toggles the free-fly camera (mouse-look + WASD/Q/E move it instead of
    // the racket; wheel adjusts its speed). Captures/releases the cursor so
    // mouse-look deltas are relative, matching the base engine's free-fly.
    if (k == SDLK_c) {
        e->free_cam = !e->free_cam;
        SDL_SetRelativeMouseMode(e->free_cam ? SDL_TRUE : SDL_FALSE);
        if (e->free_cam) SDL_GetRelativeMouseState(nullptr, nullptr);   // discard stale delta
        std::cout << "Camera: " << (e->free_cam
            ? "FREE  (mouse-look, WASD move, Q/E up/down, wheel = speed)"
            : "FIXED (WASD / Q/E control the racket)") << "\n";
        return;
    }

    // T toggles the Technical Progress demo. Leaving it restores the full game
    // (stage 4); the last stage is remembered for the next open.
    if (k == SDLK_t) {
        e->demo_open = !e->demo_open;
        apply_demo_stage(e, e->demo_open ? e->demo_stage : 4);
        std::cout << (e->demo_open ? "Demo: open  " : "Demo: closed  ")
                  << (e->demo_open ? kStageTitle[e->demo_stage - 1] : "") << "\n";
        return;
    }

    if (e->demo_open) {
        if (k == SDLK_ESCAPE) {
            e->demo_open = false;
            apply_demo_stage(e, 4);
            std::cout << "Demo: closed\n";
            return;
        }
        if (k == SDLK_LEFT  || k == SDLK_RIGHT) {
            apply_demo_stage(e, e->demo_stage + (k == SDLK_RIGHT ? 1 : -1));
            std::cout << kStageTitle[e->demo_stage - 1] << "\n";
            return;
        }
        if (k >= SDLK_1 && k <= SDLK_4) {
            apply_demo_stage(e, (k - SDLK_1) + 1);
            std::cout << kStageTitle[e->demo_stage - 1] << "\n";
            return;
        }
        // TAB / G still cycle the backend while the demo is open.
    }

    switch (k) {
        case SDLK_ESCAPE: running = false;        break;   // (demo closed only)
        case SDLK_TAB:    e->renderer.cycle(-1);  break;
        case SDLK_g:      e->renderer.cycle(+1);  break;
        default: break;
    }
    if (k == SDLK_TAB || k == SDLK_g) {
        std::cout << "Backend: " << e->renderer.current_name()
                  << (e->renderer.current_available() ? "" : " (unavailable)") << "\n";
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

void game_set_racket_autopilot(Game* e, int mode) {
    e->racket_autopilot = mode;
}

void game_set_demo(Game* e, int stage) {
    if (stage < 1 || stage > 4) return;
    e->demo_open = true;
    apply_demo_stage(e, stage);
    std::cout << "Demo: open  " << kStageTitle[e->demo_stage - 1] << "\n";
}
