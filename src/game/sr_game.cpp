#include <game/sr_game.hpp>
#include <game/sr_game_state.hpp>
#include <render/raytrace/sr_raytrace.hpp>
#include <game/sr_hud.hpp>
#include <game/ball_physics.hpp>

#include <render/raytrace/sr_ocl.hpp>
#include <engine/anim/skinned_mesh.hpp>
#include <engine/assets/obj_loader.hpp>
#include <sound/sr_sound.hpp>
#include <sr_config.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iostream>

static const float kMouseSensitivity = 0.0025f;
static const float kMinMoveSpeed     = 0.5f;   // mouse-wheel speed clamp: lower bound
static const float kMaxMoveSpeed     = 60.0f;  // mouse-wheel speed clamp: upper bound

// ---- Scene ------------------------------------------------------------------
// One minimal scene, kept here on purpose: a floor, an overhead area light, and
// a procedurally skinned character. Static geometry (floor + light) goes into
// the static BVH; the character is the single dynamic object, re-folded into the
// dynamic BVH every frame. Materials are explicit — no name-hashing tricks.

// Axis-aligned box from two corners.
static void add_box(std::vector<bvh::Tri>& out, const vec3& lo, const vec3& hi,
                    const vec3& albedo, float roughness) {
    vec3 v[8] = {
        {lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},
        {lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}
    };
    auto face = [&](int a, int b, int c, int d, const vec3& n) {
        out.push_back({ v[a],v[b],v[c], n, albedo, roughness });
        out.push_back({ v[a],v[c],v[d], n, albedo, roughness });
    };
    face(4,5,6,7,{ 0, 1, 0}); face(3,2,1,0,{ 0,-1, 0});
    face(0,3,7,4,{-1, 0, 0}); face(1,5,6,2,{ 1, 0, 0});
    face(0,1,5,4,{ 0, 0,-1}); face(3,7,6,2,{ 0, 0, 1});
}

// A horizontal emissive quad (area light) facing down.
static void add_emissive_quad(std::vector<bvh::Tri>& out, const vec3& center,
                              float hx, float hz, const vec3& emission) {
    vec3 a(center.x-hx, center.y, center.z-hz), b(center.x+hx, center.y, center.z-hz);
    vec3 c(center.x+hx, center.y, center.z+hz), d(center.x-hx, center.y, center.z+hz);
    vec3 n(0.0f, -1.0f, 0.0f);
    bvh::Tri t0{ a, b, c, n, vec3(0,0,0), 1.0f }; t0.emission = emission;
    bvh::Tri t1{ a, c, d, n, vec3(0,0,0), 1.0f }; t1.emission = emission;
    out.push_back(t0); out.push_back(t1);
}

// Place the free camera at a fixed starting pose looking at `target`.
static void set_start_view(Game* e, const vec3& pos, const vec3& target) {
    vec3 d = normalize(target - pos);
    float pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    float yaw   = std::atan2(d.x, -d.z);
    e->position = pos; e->pitch = pitch; e->yaw = yaw;
    e->cam.setPosition(pos);
    e->cam.setRotation(vec3(pitch, yaw, 0.0f));
}

// Mirror a triangle list into a raster mesh (used by the raster backend).
static mesh* make_raster_mesh(const std::vector<bvh::Tri>& tris) {
    mesh* m = new mesh;
    m->vertices.reserve(tris.size() * 3);
    m->faces.reserve(tris.size());
    for (const auto& t : tris) {
        uint32_t b = (uint32_t)m->vertices.size();
        m->vertices.push_back({ t.v0, {0.0f, 0.0f} });
        m->vertices.push_back({ t.v1, {0.0f, 0.0f} });
        m->vertices.push_back({ t.v2, {0.0f, 0.0f} });
        m->faces.push_back({ b, b+1, b+2 });
    }
    m->_modelMatrixDirty = true;
    return m;
}

// Fold a world-space mesh into flat ray-trace triangles with an explicit
// material (or its texture, if any).
static void fold_mesh(std::vector<bvh::Tri>& out, const mesh& m,
                      const vec3& albedo, float roughness) {
    mat4 model = m.modelMatrix();
    for (const triangle& tri : m.faces) {
        const vertex& a = m.vertices[tri.v0];
        const vertex& b = m.vertices[tri.v1];
        const vertex& c = m.vertices[tri.v2];
        vec3 v0 = vec3(model * a.p), v1 = vec3(model * b.p), v2 = vec3(model * c.p);
        vec3 n  = normalize(cross(v1-v0, v2-v0));
        bvh::Tri t{ v0, v1, v2, n, albedo, roughness};
        if (m.tex) { t.tex = m.tex; t.uv0 = a.t; t.uv1 = b.t; t.uv2 = c.t; }
        out.push_back(t);
    }
}

// Rebuild the DYNAMIC BVH from the (skinned) character and the ball. Called
// every frame.
static void build_dynamic(Game* e) {
    e->rt_tris.clear();
    if (e->character) fold_mesh(e->rt_tris, *e->character, e->char_albedo, e->char_rough);
    if (e->ball_mesh) {
        e->ball_mesh->setPosition(e->ball_pos);
        fold_mesh(e->rt_tris, *e->ball_mesh, e->ball_albedo, 0.25f);
    }
    e->dynamic_bvh.build(e->rt_tris, e->dynamic_build_strategy);
}

// The spin the serve is struck with. The ball travels along -Z, so topspin is a
// rotation about -X (the top of the ball moving the way the ball moves) and the
// sidespins are about +/-Y. See ballphys::compute_acceleration for what each one
// then does to the flight.
static vec3 serve_spin(int sel) {
    const float w = 300.0f;   // rad/s: a firm but not maximal stroke
    switch (sel) {
        case 1:  return vec3(-w, 0.0f, 0.0f);   // top
        case 2:  return vec3( w, 0.0f, 0.0f);   // back
        case 3:  return vec3(0.0f,  w, 0.0f);   // left
        case 4:  return vec3(0.0f, -w, 0.0f);   // right
        case 0:
        default: return vec3(0.0f, 0.0f, 0.0f); // none
    }
}

static const char* spin_name(int sel) {
    switch (sel) {
        case 1:  return "top";
        case 2:  return "back";
        case 3:  return "left";
        case 4:  return "right";
        default: return "none";
    }
}

// Strike the ball. This is the ONLY place the trajectory is computed: one burst
// at the moment of the hit, then a post-pass that bends it toward the aim target
// without touching the physics. Everything afterwards just reads the store.
static void serve_ball(Game* e) {
    // Launch from a fixed point tuned to land in the far court (independent of
    // wherever the character model is placed for looks — the velocity/spin
    // below were tuned against this exact origin, so it doesn't follow the
    // character around).
    const vec3 server = vec3(e->table.half_width + 0.34f, 0.0f, e->table.half_len - 0.12f);
    ballphys::BallState b;
    b.pos  = vec3(server.x - 0.25f, e->table.height + 0.30f, server.z - 0.05f);
    b.vel  = vec3(-1.7f, 1.5f, -4.5f);   // swept across so every spin lands in
    b.spin = serve_spin(e->ball_spin_sel);

    ballphys::process_hit(b, e->table, e->ball_pred, +1.0f);

    if (e->ball_autoaim) {
        ballphys::apply_autoaim(e->ball_pred, e->ball_target,
                                ballphys::fudge_for(ballphys::Stroke::Normal), 1.0f / 60.0f);
    }

    e->ball_clock  = 0.0f;
    e->ball_active = e->ball_pred.queue.count() > 1;
    e->ball_pos    = b.pos;

    std::cout << "serve: spin=" << spin_name(e->ball_spin_sel)
              << "  outcome=" << ballphys::outcome_name(e->ball_pred.outcome)
              << " at t=" << e->ball_pred.outcome_time << "s"
              << "  samples=" << e->ball_pred.queue.count() << "\n";
}

// Build the static scene (floor + light) into the static BVH, mirror it for the
// raster backend, collect area lights, and re-push everything to the backends.
void game_rebuild_static(Game* e) {
    uint64_t t0 = SDL_GetPerformanceCounter();

    std::vector<bvh::Tri> tris;
    add_box(tris, vec3(-8.f, -0.02f, -8.f), vec3(8.f, 0.f, 8.f), e->floor_albedo, 0.01f);
    //add_emissive_quad(tris, vec3(0.0f, 5.0f, 0.0f), 2.0f, 2.0f, vec3(6.f, 6.f, 6.f));

    // Regulation table, sized from the same numbers the ball physics uses so the
    // geometry and the simulation can never drift apart: 2.74 x 1.525 m, surface
    // 76 cm up, a 15.25 cm net on the z = 0 plane.
    const ballphys::Table& tb = e->table;
    const vec3 top_albedo(0.06f, 0.20f, 0.35f);
    add_box(tris, vec3(-tb.half_width, tb.height - 0.02f, -tb.half_len),
                  vec3( tb.half_width, tb.height,          tb.half_len), top_albedo, 0.35f);
    // White edge lines, laid just proud of the surface.
    const vec3 line(0.90f, 0.90f, 0.92f);
    const float lw = 0.02f, eps = 0.001f;
    add_box(tris, vec3(-tb.half_width, tb.height, -tb.half_len),
                  vec3(-tb.half_width + lw, tb.height + eps, tb.half_len), line, 0.35f);
    add_box(tris, vec3( tb.half_width - lw, tb.height, -tb.half_len),
                  vec3( tb.half_width, tb.height + eps, tb.half_len), line, 0.35f);
    // Net.
    add_box(tris, vec3(-tb.half_width, tb.height, -0.006f),
                  vec3( tb.half_width, tb.height + tb.net_height, 0.006f),
                  vec3(0.85f, 0.85f, 0.88f), 0.8f);
    // Four legs, so the table is not floating.
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            float cx = sx * (tb.half_width - 0.10f);
            float cz = sz * (tb.half_len   - 0.15f);
            add_box(tris, vec3(cx - 0.03f, 0.0f, cz - 0.03f),
                          vec3(cx + 0.03f, tb.height - 0.02f, cz + 0.03f),
                          vec3(0.15f, 0.15f, 0.17f), 0.5f);
        }
    }

    delete e->field_mesh;
    e->field_mesh = make_raster_mesh(tris);
    e->static_bvh.build(std::move(tris), e->build_strategy);
    e->static_build_ms = (SDL_GetPerformanceCounter()-t0)*1000.0/SDL_GetPerformanceFrequency();

    e->emissive_tris.clear();
    for (int i = 0; i < (int)e->static_bvh.triangle_count(); ++i) {
        const bvh::Tri& t = e->static_bvh.tri(i);
        if (t.emission.x + t.emission.y + t.emission.z > 0.0f)
            e->emissive_tris.push_back(t);
    }
    // Invalidate cached backend trees and re-push static geometry / lights.
    // A no-op during initial setup (renderer not yet initialized).
    e->renderer.reload_scene(e->static_bvh, e->skybox_faces, e->emissive_tris);
}

// One-time scene construction: static geometry + the player character.
static void init_scene(Game* e) {
    e->skin = SkinnedMesh{};
    mesh* cm = new mesh;
    bool loaded = load_obj_mesh("res/man_slim/man.obj", *cm);
    if (!loaded) {
        std::cout << "init_scene: could not load res/man_slim/man.obj, falling back "
                     "to the procedural character\n";
        build_procedural_character(*cm, e->skin);
        e->char_base_pos = vec3(e->table.half_width + 0.34f, 0.0f, e->table.half_len - 0.12f);
        e->char_base_yaw = 0.0f;
    } else {
        // Slim CC0 model (Quaternius "Man"), baked from its "Idle" pose (arms
        // down) rather than the rest T-pose — see res/man_slim/man.obj header
        // and the bake script. No skinning at runtime: the pose is static, and
        // a flat per-body-part palette texture stands in for its untextured
        // materials (res/man_slim/man_palette.png). Raw bbox: height 4.812,
        // feet at y ~-0.004; scaled to ~1.575 m (a ~1.75 m adult, sized down
        // 10%, proportions unchanged since the scale is uniform).
        const float kRawHeight = 4.812f;
        const float kRawFeetY  = -0.004f;
        float charScale = (1.75f * 0.9f) / kRawHeight;
        cm->setScale(vec3(charScale, charScale, charScale));
        // Centered on one short end of the table (serving position), standing
        // just behind the end line, facing down the table toward the net.
        // serve_ball() reads this position, so it's also where the ball launches.
        e->char_base_pos = vec3(0.0f, -kRawFeetY * charScale, e->table.half_len + 0.35f);
        e->char_base_yaw = 3.14159265f; // model faces +Z (the camera) at yaw 0; flip to face the table
    }
    cm->setPosition(e->char_base_pos);
    if (e->skin.valid()) e->skin.apply(*cm, 0.0f);
    delete e->character;
    e->character = cm;
    e->anim_time = 0.0f;

    // One dynamic point light, warm-white, orbiting the character.
    e->point_lights.clear();
    e->point_lights.push_back(PointLight{ vec3(e->point_light_radius, e->point_light_height, 0.0f),
                                          vec3(1.0f, 0.9f, 0.75f), 12.0f });

    // The ball: a real 40 mm sphere, folded into the dynamic BVH each frame.
    delete e->ball_mesh;
    e->ball_mesh = new mesh;
    create_sphere(e->ball_mesh, ballphys::kBallRadius, 10, 14);

    game_rebuild_static(e);
    serve_ball(e);
    set_start_view(e, vec3(0.0f, 1.45f, 3.30f), vec3(0.0f, 0.90f, -0.40f));
    std::cout << "Scene: " << e->static_bvh.triangle_count() << " static tris, "
              << e->character->faces.size() << " character tris"
              << (e->skin.valid() ? " (skinned)" : " (static, idle sway only)") << "\n";
}

// Fill the per-frame render view the backends consume. This is the seam between
// the game world and render/: everything a backend needs — the two BVHs for the
// tracers and the raster draw list for the raster backend — and nothing of the
// Game type leaks across it.
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

    // Raster draw list: static scenery (no shadow) + the character (casts one).
    e->raster_items.clear();
    if (e->field_mesh)  e->raster_items.push_back({ e->field_mesh,  pack(e->floor_albedo),   false });
    if (e->character)   e->raster_items.push_back({ e->character,  pack(e->char_albedo),    true  });
    if (e->ball_mesh)   e->raster_items.push_back({ e->ball_mesh,   pack(e->ball_albedo),    true  });
    s.raster_items = &e->raster_items;

    s.emissive        = e->emissive_enabled ? &e->emissive_tris : nullptr;
    s.point_lights    = e->point_light_enabled ? &e->point_lights : nullptr;
    s.skybox          = &e->skybox_faces;
    s.skybox_enabled  = e->skybox_enabled;
    s.bg_color        = e->bg_color;

    s.sun_enabled = e->sun_enabled;
    s.reflections = e->reflections;
    s.max_bounces = e->max_bounces;
    s.gi_enabled  = e->gi_enabled;
    s.gi_samples  = e->gi_samples;
    s.gi_strength = e->gi_strength;
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


    init_scene(e);
    build_dynamic(e);

    // Resolve the worker count from --threads: -1 (or 0) = all hardware threads.
    int req = global_config.num_workers;
    if (req <= 0) req = SDL_GetCPUCount();
    e->num_workers = std::clamp(req, 1, 64);

    e->renderer.init(e->num_workers, e->max_bounces, AMBIENT, SHADOW_EPS);
    e->renderer.upload_static(e->static_bvh, e->skybox_faces, e->emissive_tris);
}

void game_update(Game* e, float dt) {
    if (e->show_menu) return;

    // Advance the character, then re-fold it into the dynamic BVH. This happens
    // every frame regardless of backend, so the raster draw and the ray tracers
    // all see the same geometry — no per-mode special cases.
    if (e->character) {
        e->anim_time += dt;
        if (e->skin.valid()) {
            float loop = (float)e->skin.frames / e->skin.fps;   // loop the clip
            if (loop > 0.0f && e->anim_time > loop) e->anim_time -= loop;
            e->skin.apply(*e->character, e->anim_time);
        } else {
            // No rig on this model: a small hand-authored sway + bob on the
            // whole-body transform stands in for "minimal" idle motion,
            // layered on top of the fixed placement from init_scene.
            float sway = std::sin(e->anim_time * 1.3f) * to_radians(2.5f);
            float bob  = std::sin(e->anim_time * 2.6f) * 0.010f;
            e->character->setRotation(vec3(0.0f, e->char_base_yaw + sway, 0.0f));
            e->character->setPosition(e->char_base_pos + vec3(0.0f, bob, 0.0f));
        }
    }
    // Ball playback. No integration here: the flight was solved at the hit, so
    // this only reads the store by time and retires what the clock has passed.
    if (e->ball_active) {
        e->ball_clock += dt;
        ballphys::BallState st;
        if (e->ball_pred.queue.sample_at(e->ball_clock, st)) {
            e->ball_pos = st.pos;
            e->ball_pred.queue.pop_expired(e->ball_clock);
        } else {
            e->ball_active  = false;      // ran past the end of the prediction
            e->ball_respawn = 1.0f;
        }
    } else {
        e->ball_respawn -= dt;
        if (e->ball_respawn <= 0.0f) serve_ball(e);
    }

    build_dynamic(e);

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

    // Orbit the dynamic point light around the scene centre.
    if (!e->point_lights.empty()) {
        float a = e->time * e->point_light_speed;
        e->point_lights[0].pos = vec3(std::cos(a) * e->point_light_radius,
                                      e->point_light_height,
                                      std::sin(a) * e->point_light_radius);
    }
}

void game_render(Game* e, SDL_Texture* sdl_fb_texture, float dt) {
    uint64_t t = SDL_GetPerformanceCounter();
    double ms_core = 0;

    e->fb.clear(e->skybox_enabled ? 0xFF000000 : pack(e->bg_color));

    // One dispatch point for every mode. The active backend (raster fallback or
    // CPU BVH / Embree / OpenCL ray tracer) renders the frame from a decoupled
    // scene view — no raster/raytrace branch here.
    RenderScene scene = make_render_scene(e);
    e->renderer.render(scene, e->fb);
    ms_core = tick_ms(t);

    render_gizmo(e->fb, e->cam, vec3{}, 1.0f);

    if (e->show_bvh)     draw_bvh_debug(e);
    if (e->show_normals) draw_normals_debug(e);

    double ms_present = tick_ms(t);

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
                1.0f/dt, backend, e->use_bvh ? "On" : "Off");
            draw_text(e->fb, 12, 12, line, 0x88000000, 0x88000000);
            draw_text(e->fb, 10, 10, line, 0xFFFFFFFF);
        } else {
            char HUD[1024] = {0};
            snprintf(HUD, sizeof(HUD),
                "=== SR-LEC ===\n"
                "Frametime: %.2fms   FPS: %.2f\n"
                "render: %.1fms  rest: %.1fms\n"
                "--- Static BVH (scene) ---\n"
                "  tris: %zu  nodes: %zu  build: %.2fms\n"
                "--- Dynamic BVH (character) ---\n"
                "  tris: %zu  nodes: %zu\n"
                "Accel: %s   Static: %s  Dyn: %s\n"
                "Backend: %s\n"
                "Move: %s\n"
                "--- Ball ---\n"
                "  spin: %-5s  autoaim: %-3s  outcome: %s\n"
                "  t: %.2f / %.2fs   samples left: %d\n"
                "[TAB/G] backend [B] BVH [V] vis [N] normals [L] sun\n[M] menu  [SPACE x2] walk/fly\n"
                "[P] serve  [O] spin  [K] autoaim  [H] compact\n",
                dt*1000.0f, 1.0f/dt, ms_core, ms_present,
                e->static_bvh.triangle_count(), e->static_bvh.node_count(), e->static_build_ms,
                e->dynamic_bvh.triangle_count(), e->dynamic_bvh.node_count(),
                e->use_bvh ? "BVH (fast)" : "BRUTE FORCE (slow)",
                e->build_strategy==bvh::SAH ? "SAH" : e->build_strategy==bvh::Median ? "Median" : "Morton",
                e->dynamic_build_strategy==bvh::SAH ? "SAH" : e->dynamic_build_strategy==bvh::Median ? "Median" : "Morton",
                backend, e->fly_mode ? "FLY" : "WALK",
                spin_name(e->ball_spin_sel), e->ball_autoaim ? "on" : "off",
                ballphys::outcome_name(e->ball_pred.outcome),
                e->ball_clock, e->ball_pred.queue.max_t(), e->ball_pred.queue.count());
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
            case SDLK_TAB: e->renderer.cycle(-1); break;
            case SDLK_b:   e->use_bvh       = !e->use_bvh;       break;
            case SDLK_v:   e->show_bvh      = !e->show_bvh;      break;
            case SDLK_h:   e->hud_simple    = !e->hud_simple;    break;
            case SDLK_n:   e->show_normals  = !e->show_normals;  break;
            case SDLK_l:   e->sun_enabled   = !e->sun_enabled;   break;
            case SDLK_p:   serve_ball(e); break;
            case SDLK_o:   e->ball_spin_sel = (e->ball_spin_sel + 1) % 5; serve_ball(e); break;
            case SDLK_k:   e->ball_autoaim  = !e->ball_autoaim;  serve_ball(e); break;
            case SDLK_g:   e->renderer.cycle(+1); break;
            default: break;
        }
    }
}

void game_shutdown(Game* e) {
    e->renderer.shutdown();
    delete e->field_mesh;
    delete e->character;
    delete e->ball_mesh;
}
