









#include <game/sr_game.hpp>
#include <game/game_state.hpp>
#include <engine/geom/shapes.hpp>

#include <core/sr_text.hpp>
#include <sr_config.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>



static constexpr float kAmbient   = 0.0f;
static constexpr float kShadowEps = 1e-4f;





static double ms_since(uint64_t t0) {
    return double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
}


static uint32_t pack_color(const vec3& c) {
    auto ch = [](float v) { return (uint32_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return 0xFF000000u | (ch(c.x) << 16) | (ch(c.y) << 8) | ch(c.z);
}




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






static void set_fixed_view(Game* e) {
    vec3 d = normalize(e->cam_target - e->cam_pos);
    e->cam_pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    e->cam_yaw   = std::atan2(d.x, -d.z);
    e->cam.setPosition(e->cam_pos);
    e->cam.setRotation(vec3(e->cam_pitch, e->cam_yaw, 0.0f));
}

static constexpr float kMouseSensitivity = 0.0025f;
static constexpr float kMaxCamPitch      = 1.4834f;










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
        if (il > 1e-4f) in = in / il;

        const float s = std::sin(e->cam_yaw), c = std::cos(e->cam_yaw);
        vec3 move(in.x * c - in.z * s, in.y, in.x * s + in.z * c);
        e->cam_pos = e->cam_pos + move * (e->cam_move_speed * dt);
    }
    e->cam.setPosition(e->cam_pos);
    e->cam.setRotation(vec3(e->cam_pitch, e->cam_yaw, 0.0f));
}





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





static RenderScene make_render_scene(Game* e) {
    RenderScene s;
    s.cam    = &e->cam;
    s.width  = e->fb.width;
    s.height = e->fb.height;

    s.static_bvh       = &e->static_bvh;
    s.dynamic_bvh      = &e->dynamic_bvh;
    s.brute_tris       = &e->dyn_tris_;
    s.use_bvh          = true;
    s.static_strategy  = e->static_strategy;
    s.dynamic_strategy = e->dynamic_strategy;

    e->raster_items.clear();
    if (e->court_mesh)
        e->raster_items.push_back({ e->court_mesh, pack_color(vec3(0.62f, 0.62f, 0.66f)), false });
    e->raster_items.push_back({ &e->sphere_mesh_, pack_color(e->sphere_albedo), false });
    if (e->racket_enabled)
        e->raster_items.push_back({ &e->racket_mesh_, pack_color(e->racket_albedo), false });
    s.raster_items = &e->raster_items;

    s.emissive       = nullptr;
    s.point_lights   = nullptr;
    s.skybox         = &e->skybox_faces;
    s.skybox_enabled = e->skybox_enabled;
    s.bg_color       = e->bg_color;

    s.sun_enabled = e->sun_enabled;
    s.reflections = false;
    s.max_bounces = 1;
    s.gi_enabled  = false;
    return s;
}







static const vec3 kRacketHome(0.0f, 0.95f, -1.55f);






static const char* kRacketModelPath = "res/models/racket.obj";











static constexpr float kMouseReachX = 1.0f;
static constexpr float kMouseReachY = 0.30f;



static constexpr float kHitWindowTimeScale = 0.2f;
static constexpr float kTimeScaleLerpRate  = 8.0f;
static constexpr float kChargeRate         = 1.0f;


static constexpr float kHitReach           = 0.45f;
static constexpr float kMinHitForce        = 2.5f;
static constexpr float kMaxHitForce        = 8.0f;
static constexpr float kRacketVelInfluence = 0.5f;
static constexpr float kHitUpBias          = 0.25f;
static constexpr float kMinHitForwardZ     = 1.5f;
static constexpr float kMaxBallHitSpeed    = 10.0f;
static constexpr float kHitFlashSeconds    = 0.6f;






static constexpr float kAimReachX          = 1.0f;
static constexpr float kAimReachY          = 0.30f;
static constexpr float kAimSidewaysStrength = 0.45f;
static constexpr float kAimVerticalStrength = 0.20f;






static constexpr float kSpinFromRacketVel  = 4.0f;
static constexpr float kMaxSpinComponent   = 20.0f;










static vec3 mouse_racket_target(Game* e) {
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    float nx = e->fb.width  > 0 ? (float)mx / (float)e->fb.width  * 2.0f - 1.0f : 0.0f;
    float ny = e->fb.height > 0 ? (float)my / (float)e->fb.height * 2.0f - 1.0f : 0.0f;
    nx = std::clamp(nx, -1.0f, 1.0f);
    ny = std::clamp(ny, -1.0f, 1.0f);


    return vec3(kRacketHome.x + nx * kMouseReachX,
                kRacketHome.y - ny * kMouseReachY,
                e->racket.position().z);
}










static vec3 compute_hit_direction(Game* e) {
    const vec3  offset = e->racket.position() - kRacketHome;
    const float aim_x  = std::clamp(offset.x / kAimReachX, -1.0f, 1.0f);
    const float aim_y  = std::clamp(offset.y / kAimReachY, -1.0f, 1.0f);

    vec3 dir = e->racket.face_normal();
    dir.x += aim_x * kAimSidewaysStrength;
    dir.y += aim_y * kAimVerticalStrength + kHitUpBias;
    const float dl = magnitude(dir);
    if (dl > 1e-5f) dir = dir / dl;
    return dir;
}









static vec3 compute_hit_spin(Game* e) {
    const vec3 rv = e->racket.velocity();
    const float sx = std::clamp(rv.y * kSpinFromRacketVel, -kMaxSpinComponent, kMaxSpinComponent);
    const float sy = std::clamp(rv.x * kSpinFromRacketVel, -kMaxSpinComponent, kMaxSpinComponent);
    return vec3(sx, sy, 0.0f);
}









static bool perform_hit(Game* e) {
    const float dist = magnitude(e->ball.pos - e->racket.position());
    if (dist > kHitReach) {
        if (global_config.debug_mode)
            std::printf("[swing] miss -- ball %.2f away (reach %.2f)\n", dist, kHitReach);
        return false;
    }

    const float charge_used = e->charge;
    const vec3  dir         = compute_hit_direction(e);
    const vec3  spin        = compute_hit_spin(e);




    const float hit_strength = kMinHitForce + (kMaxHitForce - kMinHitForce) * charge_used;
    vec3 new_vel = dir * hit_strength + e->racket.velocity() * kRacketVelInfluence;




    if (new_vel.z < kMinHitForwardZ) new_vel.z = kMinHitForwardZ;

    const float sp = magnitude(new_vel);
    if (sp > kMaxBallHitSpeed) new_vel = new_vel * (kMaxBallHitSpeed / sp);






    const float clearance = e->racket.collider().half.z + e->ball.radius + 0.02f;
    e->ball.pos = e->racket.position() + dir * clearance;
    e->ball.apply_hit(new_vel);
    e->ball.spin = spin;

    e->charging = false;
    e->charge   = 0.0f;
    e->hit_flash_timer = kHitFlashSeconds;

    if (global_config.debug_mode)
        std::printf("[swing] HIT charge=%.2f strength=%.2f dist=%.2f -> ball |v|=%.2f (%.2f,%.2f,%.2f) spin=(%.1f,%.1f) rkt|v|=%.2f\n",
                    charge_used, hit_strength, dist, sp, new_vel.x, new_vel.y, new_vel.z,
                    spin.x, spin.y, magnitude(e->racket.velocity()));
    return true;
}


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





static void apply_demo_stage(Game* e, int stage) {
    e->demo_stage = std::clamp(stage, 1, 4);
    Ball& b = e->ball;





    b.radius     = 0.06f;
    b.spin_decay = 0.08f;
    b.rest_speed = 0.5f;

    switch (e->demo_stage) {
        case 1:
            b.gravity = 0.0f;  b.drag = 0.0f;  b.magnus = 0.0f;
            b.restitution = 1.0f;  b.rest_speed = 0.0f;
            b.reset(vec3(-2.0f, 3.2f, 0.0f), vec3(3.4f, 2.4f, 1.7f), vec3(0.0f, 0.0f, 0.0f));
            e->racket_enabled = false;
            e->table_enabled  = false;
            e->wall_enabled   = false;
            break;
        case 2:
            b.gravity = 9.81f; b.drag = 0.0f;  b.magnus = 0.0f;
            b.restitution = 0.75f;
            b.reset(vec3(-1.5f, 5.2f, 0.0f), vec3(2.6f, 0.4f, 0.5f), vec3(0.0f, 0.0f, 0.0f));
            e->racket_enabled = false;
            e->table_enabled  = false;
            e->wall_enabled   = false;
            break;
        case 3:
            b.gravity = 9.81f; b.drag = 0.10f; b.magnus = 0.10f;
            b.restitution = 0.75f;
            b.reset(vec3(-3.4f, 3.8f, 0.2f), vec3(3.4f, 0.8f, 0.0f), vec3(0.0f, 20.0f, 0.0f));
            e->racket_enabled = false;
            e->table_enabled  = false;
            e->wall_enabled   = false;
            break;
        case 4:

        default:
            b.gravity = 9.81f; b.drag = 0.10f; b.magnus = 0.10f;
            b.restitution = 0.88f;



            b.reset(vec3(0.0f, 1.5f, -1.2f), vec3(0.0f, 2.1f, 6.8f), vec3(0.0f, 0.0f, 0.0f));
            e->racket_enabled = true;
            e->table_enabled  = true;
            e->wall_enabled   = true;
            break;
    }
    e->racket.recenter(kRacketHome);
    e->bounces_total = 0;
    e->hits_reported_ = 0;
}





Game* game_create(int width, int height) {
    return new Game(width, height);
}

void game_rebuild_static(Game* e) {
    uint64_t t0 = SDL_GetPerformanceCounter();





    const vec3 floor_col(0.35f, 0.37f, 0.40f);
    const vec3 shirt_col(0.20f, 0.35f, 0.55f);
    const vec3 pants_col(0.15f, 0.15f, 0.18f);
    const vec3 skin_col (0.80f, 0.62f, 0.50f);

    std::vector<bvh::Tri> tris;
    tris.reserve(160);
    geom::add_box(tris, vec3(-3.0f, -0.10f, -2.8f), vec3(3.0f, 0.0f, 2.6f), floor_col, 0.85f);







    e->wall.append_tris(tris);




    e->table.append_tris(tris);





    geom::add_box(tris, vec3(-0.14f, 0.0f,  -2.16f), vec3(0.14f, 0.90f, -1.94f), pants_col, 0.60f);
    geom::add_box(tris, vec3(-0.18f, 0.90f, -2.18f), vec3(0.18f, 1.55f, -1.92f), shirt_col, 0.55f);
    geom::add_box(tris, vec3(-0.10f, 1.55f, -2.15f), vec3(0.10f, 1.75f, -1.95f), skin_col,  0.50f);



    e->static_tri_count = tris.size();

    delete e->court_mesh;
    e->court_mesh = new mesh;
    fill_raster_mesh(*e->court_mesh, tris);

    e->static_bvh.build(std::move(tris), e->static_strategy);
    e->static_build_ms = ms_since(t0);

    e->emissive_tris.clear();
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






    const float kWallGap = 0.04f;
    e->wall.inner_z = e->table.half_len + kWallGap;





    e->arena.min = vec3(-4.0f, 0.0f, -3.5f);
    e->arena.max = vec3( 4.0f, 6.0f, 12.0f);






    e->racket.configure(   kRacketHome,
                         vec3(to_radians(8.0f), to_radians(-6.0f), 0.0f),
                          vec3(0.32f, 0.34f, 0.03f),
                         0.85f);
    e->racket_speed     = 5.0f;
    e->racket_limits.min = vec3(-1.0f, 0.70f, -1.90f);
    e->racket_limits.max = vec3( 1.0f, 1.30f, -1.20f);


    apply_demo_stage(e, 4);


    e->sphere_local_.clear();
    geom::add_sphere(e->sphere_local_, vec3(0.0f, 0.0f, 0.0f), e->ball.radius,
                     e->sphere_albedo,  0.55f,  0.0f,  1.5f,
                      20,  14,  true);
    e->racket_local_.clear();





    e->racket.append_local_tris_from_obj(kRacketModelPath, e->racket_local_);

    e->dyn_tris_.clear();
    e->dyn_tris_.reserve(e->sphere_local_.size() + e->racket_local_.size());



    fill_raster_mesh(e->sphere_mesh_, e->sphere_local_);
    fill_raster_mesh(e->racket_mesh_, e->racket_local_);

    e->raster_items.reserve(3);


    game_rebuild_static(e);
    refresh_dyn_tris(e);
    e->dynamic_bvh.build(e->dyn_tris_, e->dynamic_strategy);
    e->sphere_mesh_.setPosition(e->ball.pos);
    e->racket_mesh_.setPosition(e->racket.position());


    int req = global_config.num_workers;
    if (req <= 0) req = SDL_GetCPUCount();
    e->num_workers = std::clamp(req, 1, 64);

    e->renderer.init(e->num_workers,  1, kAmbient, kShadowEps);
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





static vec3 racket_input(Game* e, float ) {
    if (e->racket_autopilot != 0) {





        vec3 to_ball = e->ball.pos - e->racket.position();
        vec3 d(0.0f, 0.0f, 0.0f);
        if (std::fabs(to_ball.y) > 0.05f) d.y = to_ball.y > 0.0f ? 1.0f : -1.0f;
        if (e->racket_autopilot == 1) {

            if (std::fabs(to_ball.x) > 0.05f) d.x = to_ball.x > 0.0f ? 1.0f : -1.0f;
        } else {



            if (std::fabs(to_ball.z) > 0.05f) d.z = to_ball.z > 0.0f ? 1.0f : -1.0f;
            float lead = (e->ball.vel.x >= 0.0f) ? 1.6f : -1.6f;
            float dx   = (e->ball.pos.x + lead) - e->racket.position().x;
            if (std::fabs(dx) > 0.05f) d.x = dx > 0.0f ? 1.0f : -1.0f;
        }
        return d;
    }



    if (e->free_cam) return vec3(0.0f, 0.0f, 0.0f);
    const Uint8* k = SDL_GetKeyboardState(nullptr);


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
    uint64_t tu0 = SDL_GetPerformanceCounter();

    update_camera(e, dt);



    if (e->racket_enabled) {










        if (e->racket_autopilot == 0 && !e->free_cam && e->racket_mouse_mode) {
            vec3 to_target = mouse_racket_target(e) - e->racket.position();
            to_target.z = 0.0f;
            float dist = magnitude(to_target);
            vec3  dir  = dist > 1e-4f ? to_target / dist : vec3(0.0f, 0.0f, 0.0f);
            float step_speed = std::min(e->racket_speed, dist / std::max(dt, 1e-6f));
            e->racket.step(dir, e->racket_limits, dt, step_speed);
        } else {
            e->racket.step(racket_input(e, dt), e->racket_limits, dt, e->racket_speed);
        }
    }










    {
        const float target = e->hit_window ? kHitWindowTimeScale : 1.0f;
        const float rate   = std::clamp(kTimeScaleLerpRate * dt, 0.0f, 1.0f);
        e->time_scale += (target - e->time_scale) * rate;
    }
    const float sim_dt = dt * e->time_scale;









    uint64_t tp = SDL_GetPerformanceCounter();
    e->bounces_total += e->ball.update(sim_dt, e->arena,
                                       e->racket_enabled ? &e->racket.collider() : nullptr,
                                       e->table_enabled  ? &e->table            : nullptr,
                                       e->wall_enabled   ? &e->wall             : nullptr);
    refresh_dyn_tris(e);
    e->sphere_mesh_.setPosition(e->ball.pos);
    e->racket_mesh_.setPosition(e->racket.position());
    e->metrics.physics_ms = Metrics::ema(e->metrics.physics_ms, ms_since(tp));









    {
        const float kHitWindowNearZ = e->racket_limits.max.z + 0.30f;
        const float kHitWindowFarZ  = e->racket_limits.min.z - 0.30f;
        e->hit_window = (e->ball.vel.z < 0.0f)
                      && (e->ball.pos.z <= kHitWindowNearZ)
                      && (e->ball.pos.z >= kHitWindowFarZ);
    }









    if (!e->hit_window && e->charging) {
        e->charging = false;
        e->charge   = 0.0f;
    } else if (e->charging) {
        e->charge = std::clamp(e->charge + kChargeRate * dt, 0.0f, 1.0f);
    }


    if (e->hit_flash_timer > 0.0f) e->hit_flash_timer = std::max(0.0f, e->hit_flash_timer - dt);



    if (global_config.debug_mode && e->ball.racket_hits != e->hits_reported_) {
        e->hits_reported_ = e->ball.racket_hits;
        std::printf("[hit] racket #%ld  ball |v| %.2f -> %.2f  (dV %+.2f)  racket |v|=%.2f\n",
                    e->ball.racket_hits, e->ball.hit_speed_in, e->ball.hit_speed_out,
                    e->ball.hit_speed_out - e->ball.hit_speed_in, e->ball.hit_racket_speed);
        std::fflush(stdout);
    }


    e->metrics.update_ms = Metrics::ema(e->metrics.update_ms, ms_since(tu0));

    uint64_t tb = SDL_GetPerformanceCounter();
    e->dynamic_bvh.build(e->dyn_tris_, e->dynamic_strategy);
    e->metrics.dyn_build_ms = Metrics::ema(e->metrics.dyn_build_ms, ms_since(tb));
}




static void draw_demo_panel(Game* e, double fps) {
    const Ball& b = e->ball;
    const int   s = e->demo_stage;
    auto chk = [](bool on) { return on ? "[x]" : "[ ]"; };


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
    e->renderer.render(scene, e->fb);
    e->metrics.render_ms = Metrics::ema(e->metrics.render_ms, ms_since(tr));

    const Metrics& m = e->metrics;
    std::size_t tris = e->static_bvh.triangle_count() + e->dynamic_bvh.triangle_count();
    double fps = m.frame_ms > 0.0 ? 1000.0 / m.frame_ms : 0.0;




    static uint64_t t_start = SDL_GetPerformanceCounter();
    static double   next_log = 2.0;
    double elapsed = double(SDL_GetPerformanceCounter() - t_start) / double(SDL_GetPerformanceFrequency());
    if (global_config.debug_mode && elapsed >= next_log) {
        next_log += 2.0;
        vec3 rp = e->racket.position(), rv = e->racket.velocity();
        std::printf("[perf] t=%5.1fs | fps %3.0f | frame %5.2f ms | update %5.3f | draw %5.3f (bvh %.3f + render %.3f) "
                    "| physics %.3f "
                    "| ball(%.2f,%.2f,%.2f) |v|=%.2f bounces %d racket %ld net %ld swing %ld "
                    "| rkt(%.2f,%.2f,%.2f) |vr|=%.2f ctrl=%s hitwin=%d ts=%.2f charge=%.2f(%d)\n",
                    elapsed, fps, m.frame_ms, m.update_ms, m.dyn_build_ms + m.render_ms, m.dyn_build_ms, m.render_ms,
                    m.physics_ms,
                    e->ball.pos.x, e->ball.pos.y, e->ball.pos.z,
                    e->ball.speed(), e->bounces_total, e->ball.racket_hits, e->ball.net_hits, e->ball.swing_hits,
                    rp.x, rp.y, rp.z, magnitude(rv),
                    e->racket_mouse_mode ? "mouse" : "wasd", e->hit_window ? 1 : 0,
                    e->time_scale, e->charge, e->charging ? 1 : 0);
        std::fflush(stdout);
    }

    if (e->demo_open) {
        draw_demo_panel(e, fps);
    } else if (e->show_hud) {

        vec3 rp = e->racket.position(), rv = e->racket.velocity();






        const vec3  aim_offset = e->racket.position() - kRacketHome;
        const float aim_x = std::clamp(aim_offset.x / kAimReachX, -1.0f, 1.0f);
        const float aim_y = std::clamp(aim_offset.y / kAimReachY, -1.0f, 1.0f);
        const vec3  live_spin = compute_hit_spin(e);



        char power_bar[11];
        {
            int filled = (int)(e->charge * 10.0f + 0.5f);
            for (int i = 0; i < 10; ++i) power_bar[i] = (i < filled) ? '#' : '-';
            power_bar[10] = '\0';
        }

        char hud[1024];
        std::snprintf(hud, sizeof(hud),
            "PingPong RT  -  racket velocity transfer\n"
            "Backend : %s%s   (TAB / G to cycle)\n"
            "FPS     : %.0f      Frame : %.2f ms\n"
            "Update  : %.3f ms   (state + game logic, physics %.3f)\n"
            "Draw    : %.3f ms   (dyn BVH build %.3f + render %.3f)\n"
            "Tris    : %zu static + %zu dynamic = %zu\n"
            "Rays    : %zu primary / frame\n"
            "Ball    : p(%.1f, %.1f, %.1f)  speed %.2f  bounces: %d\n"
            "Spin    : (%.1f, %.1f, %.1f) rad/s   |w| %.1f\n"
            "Model   : g %.2f  e %.2f  drag %.2f  magnus %.2f  (1/240 s)\n"
            "Racket  : p(%.1f, %.1f, %.1f)  v(%.1f, %.1f, %.1f)  hits: %ld%s\n"
            "Impact  : ball |v| %.2f -> %.2f   racket |v| %.2f\n"
            "Control : %s   [M] toggle\n"
            "HIT WINDOW: %s   POWER: [%s] %3.0f%%   TIME SCALE: %.2f / 1.00%s\n"
            "AIM: (%+.2f, %+.2f)   SPIN: (%+.1f, %+.1f) rad/s\n"
            "Camera  : C = toggle free-fly (mouse-look, WASD/Q/E, wheel)   [ESC] quit",
            e->renderer.current_name(),
            e->renderer.current_available() ? "" : " (n/a)",
            fps, m.frame_ms,
            m.update_ms, m.physics_ms,
            m.dyn_build_ms + m.render_ms, m.dyn_build_ms, m.render_ms,
            e->static_bvh.triangle_count(), e->dynamic_bvh.triangle_count(), tris,
            m.primary_rays,
            e->ball.pos.x, e->ball.pos.y, e->ball.pos.z, e->ball.speed(), e->bounces_total,
            e->ball.spin.x, e->ball.spin.y, e->ball.spin.z, e->ball.spin_rate(),
            e->ball.gravity, e->ball.restitution, e->ball.drag, e->ball.magnus,
            rp.x, rp.y, rp.z, rv.x, rv.y, rv.z, e->ball.racket_hits,
            e->racket_autopilot == 1 ? "  [chase]" : e->racket_autopilot == 2 ? "  [recede]" : "",
            e->ball.hit_speed_in, e->ball.hit_speed_out, e->ball.hit_racket_speed,
            e->racket_mouse_mode ? "GAMEPLAY (mouse aims X/Y)" : "DEBUG/MANUAL (WASD/arrows=X/Y, Q/E=Z)",
            e->hit_window ? "YES" : "NO", power_bar, e->charge * 100.0f, e->time_scale,
            e->hit_flash_timer > 0.0f ? "   >>> HIT! <<<" : "",
            aim_x, aim_y, live_spin.x, live_spin.y);

        draw_text(e->fb, 11, 11, hud, 0xAA000000, 0xAA000000);
        draw_text(e->fb, 10, 10, hud, 0xFFFFFFFF);
    }

    SDL_UpdateTexture(sdl_fb_texture, nullptr, e->fb.colorBuffer,
                      e->fb.width * sizeof(uint32_t));
}

void game_handle_events(Game* e, SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) { running = false; return; }



    if (event.type == SDL_MOUSEWHEEL) {
        if (e->free_cam && event.wheel.y != 0) {
            float factor = event.wheel.y > 0 ? 1.15f : 1.0f / 1.15f;
            e->cam_move_speed = std::clamp(e->cam_move_speed * factor, 0.5f, 20.0f);
        }
        return;
    }






    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (e->hit_window && !e->free_cam) e->charging = true;
        return;
    }






    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        if (e->charging) {
            bool hit = e->hit_window && perform_hit(e);
            if (!hit) { e->charging = false; e->charge = 0.0f; }
        }
        return;
    }

    if (event.type != SDL_KEYDOWN) return;

    const SDL_Keycode k = event.key.keysym.sym;




    if (k == SDLK_c) {
        e->free_cam = !e->free_cam;
        SDL_SetRelativeMouseMode(e->free_cam ? SDL_TRUE : SDL_FALSE);
        if (e->free_cam) SDL_GetRelativeMouseState(nullptr, nullptr);
        std::cout << "Camera: " << (e->free_cam
            ? "FREE  (mouse-look, WASD move, Q/E up/down, wheel = speed)"
            : "FIXED (WASD / Q/E control the racket)") << "\n";
        return;
    }






    if (k == SDLK_m) {
        e->racket_mouse_mode = !e->racket_mouse_mode;
        std::cout << "Racket control: " << (e->racket_mouse_mode
            ? "GAMEPLAY (mouse aims X/Y, Z fixed)"
            : "DEBUG/MANUAL (WASD / arrows = X/Y, Q/E = Z)") << "\n";
        return;
    }



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

    }

    switch (k) {
        case SDLK_ESCAPE: running = false;        break;
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
