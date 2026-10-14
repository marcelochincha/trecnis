#include <game/game.hpp>

#include <core_rtt/rtt_match.h>

#include <subsystems/scene/scene_runtime.hpp>
#include <subsystems/ui/ui_context.hpp>
#include <core/sr_geometry.hpp>

#include <cstdio>
#include <cstring>

// =============================================================================
// game/ implemented on top of the RTT core (src/core_rtt/), a CPU-vs-CPU demo:
// real ball physics, swing/HitZone selection, match rules and rtt_camera's shot
// director, all unmodified from the RTT reversing reconstruction. Both players
// are AI; there is no human device, no locomotion and no real animation yet —
// the three moving things are placeholder primitives positioned from the
// core's own state every frame.
//
// core_rtt brings its own `World` (rtt_match.h); TRECNIS's own type is named
// GameWorld (subsystems/scene/world.hpp) precisely so the two can coexist
// here without a name collision.
//
// `Vec3` (core_rtt) and `vec3` (TRECNIS) are kept as two distinct types for
// now; see TRECNIS-STEP2-PLAN.md for why and the planned follow-up. The
// one-line converter below is the only bridge between them.
// =============================================================================

static inline vec3 to_vec3(Vec3 v) { return vec3(v.x, v.y, v.z); }

// CameraDirector.fov is in radians (rtt_camera.cpp's shot table: 0.55..0.70,
// lerp'd at cut time); CameraPose.fov is documented in degrees (world.hpp).
static inline float fov_to_degrees(float radians) { return to_degrees(radians); }

struct Game {
    World           w;
    CameraDirector  cam;

    mesh* ball      = nullptr;
    mesh* player_a  = nullptr;
    mesh* player_b  = nullptr;

    enum class Screen { Title, Playing };
    Screen screen     = Screen::Title;
    bool   wants_quit = false;

    float  sim_acc    = 0.0f;   // real time not yet simulated, in seconds
};

using Screen = Game::Screen;

// Mirrors the static scenery authored in game_init() so gameplay and the
// vision cannot drift apart: table + floor sized from the core's own
// TableGeom, the same way the old placeholder matched its own tt:: constants.
// Fixed view for now: rtt_camera's cuts are not right yet (its ADSR release
// swings back toward the old shot and then jumps, and the side shots look at
// the floor), so the view is pinned to RTT's ThreeQuarter side cam as shipped
// in tune/camera/sidecam/threequarter.cm (SC_LookFrom / SC_LookAt). That file
// has only per-zone FOVs (SC_AA_Fov ...), not decoded yet, so the FOV is the
// camera's default 0.610865 rad. Set to false to see the director again.
static const bool kFixedCamera = true;
static const Vec3 kFixedEye    = { 4.0f, 3.5f, 5.0f };
static const Vec3 kFixedTarget = { 0.0f, 0.75f, 0.0f };
static const float kFixedFov   = 0.610865f;

static void publish_world(Game* d, GameWorld& out) {
    const CameraDirector& c = d->cam;
    out.camera.pos            = to_vec3(kFixedCamera ? kFixedEye : c.eye);
    out.camera.target         = to_vec3(kFixedCamera ? kFixedTarget : c.target);
    out.camera.look_at_target = true;
    out.camera.fov            = fov_to_degrees(kFixedCamera ? kFixedFov : c.fov);

    out.dynamic.clear();

    Vec3 ball_pos = ball_pos_at(&d->w.path, d->w.path.clock);
    if (d->ball) {
        d->ball->setPosition(to_vec3(ball_pos));
        Entity e;
        e.geo = d->ball; e.albedo = vec3(1.0f, 1.0f, 1.0f);
        e.rough = 0.2f; e.shadow = true;
        out.dynamic.push_back(e);
    }

    if (d->player_a) {
        d->player_a->setPosition(to_vec3(d->w.players[0].root));
        Entity e;
        e.geo = d->player_a; e.albedo = vec3(0.2f, 0.2f, 0.8f);
        e.rough = 0.4f; e.shadow = true;
        out.dynamic.push_back(e);
    }

    if (d->player_b) {
        d->player_b->setPosition(to_vec3(d->w.players[1].root));
        Entity e;
        e.geo = d->player_b; e.albedo = vec3(0.8f, 0.2f, 0.2f);
        e.rough = 0.4f; e.shadow = true;
        out.dynamic.push_back(e);
    }

    out.lights.clear();
}

Game* game_create() { return new Game; }

void game_init(Game* d, SceneRuntime& scene) {
    MatchSettings settings;
    rules_make_settings(&settings, POINTS_11, GAMES_1_OF_1, 0, SIDE_A);
    world_init(&d->w, &settings);

    camera_init(&d->cam);
    d->w.camera = &d->cam;
    ev_subscribe(&d->w.bus, EV_COUNT_SENTINEL, camera_on_event, &d->cam);

    // The whole CPU-vs-CPU setup (AI tier table, difficulty, rest positions)
    // lives in the core, so this adapter never re-creates it by hand.
    world_setup_cpu_match(&d->w, 8.0f);

    // Static geometry: floor and table, sized from the core's own TableGeom so
    // gameplay and the visuals cannot drift apart.
    scene.clear_static();
    scene.add_static_box(vec3(-8.0f, -0.02f, -8.0f), vec3(8.0f, 0.0f, 8.0f),
                         vec3(0.5f, 0.5f, 0.5f), 0.7f, 0.1f);

    const TableGeom& t = d->w.table;
    scene.add_static_box(vec3(-t.half_width, t.top_y - 0.01f, -t.half_length),
                         vec3( t.half_width, t.top_y,          t.half_length),
                         vec3(0.2f, 0.7f, 0.2f), 0.3f, 0.0f);
    scene.add_static_box(vec3(-t.net_half_width, t.net_bottom_y, -0.01f),
                         vec3( t.net_half_width, t.net_top_y,     0.01f),
                         vec3(0.9f, 0.9f, 0.9f), 1.0f, 0.0f);
    scene.commit_static();

    mesh* ball_mesh = new mesh;
    create_sphere(ball_mesh, d->w.ball_params.radius, 8, 8);
    delete d->ball;
    d->ball = ball_mesh;

    mesh* a_mesh = new mesh;
    create_cube(a_mesh, 1.0f);
    a_mesh->setScale(vec3(0.3f, 1.6f, 0.3f));
    delete d->player_a;
    d->player_a = a_mesh;

    mesh* b_mesh = new mesh;
    create_cube(b_mesh, 1.0f);
    b_mesh->setScale(vec3(0.3f, 1.6f, 0.3f));
    delete d->player_b;
    d->player_b = b_mesh;
}

void game_update(Game* d, const InputState& in, float dt, GameWorld& out) {
    (void)in;   // both players are AI; no human device feeds world_tick

    if (d->screen == Screen::Title) {
        publish_world(d, out);
        return;
    }

    // world_tick is one fixed RTT frame (RTT_DT = 1/60 s). The render rate is
    // whatever the tracer manages, so run as many core frames as real time
    // elapsed; the cap keeps a long stall from turning into a burst.
    PlayerInput pin[RTT_MAX_PLAYERS];
    memset(pin, 0, sizeof(pin));
    d->sim_acc += dt;
    if (d->sim_acc > 0.25f) d->sim_acc = 0.25f;
    while (d->sim_acc >= RTT_DT) {
        world_tick(&d->w, pin);
        d->sim_acc -= RTT_DT;
    }

    publish_world(d, out);
}

void game_destroy(Game* d) {
    if (!d) return;
    delete d->ball;
    delete d->player_a;
    delete d->player_b;
    delete d;
}

void game_debug_vars(Game* d, DebugVars& out) {
    (void)d; (void)out;   // nothing tunable exposed yet; the core owns its own tuning
}

void game_ui(Game* d, UiContext& ui) {
    const vec2 vs = ui.virtual_size();

    if (d->screen == Screen::Title) {
        ui.request_pointer(true);
        ui.rect({ 0.0f, 0.0f, vs.x, vs.y }, 0x66000000);
        ui.text(vec2(vs.x * 0.5f, vs.y * 0.28f), "TENIS DE MESA (RTT core)", 3,
                0xFFFFFFFFu, UiAlign::Center);

        const float bw = 200.0f, bh = 36.0f;
        const UiRect play{ (vs.x - bw) * 0.5f, vs.y * 0.52f, bw, bh };
        const UiRect quit{ (vs.x - bw) * 0.5f, vs.y * 0.52f + 56.0f, bw, bh };

        if (ui.button(1, play, "Jugar")) game_begin_match(d);
        if (ui.button(2, quit, "Salir")) d->wants_quit = true;
        return;
    }

    char score[32];
    snprintf(score, sizeof(score), "%d  -  %d", d->w.score.points[0], d->w.score.points[1]);
    const UiRect panel = ui.anchor(UiAnchor::Top, vec2(200.0f, 44.0f), vec2(0.0f, 16.0f));
    ui.rect(panel, 0x99000000);
    ui.text(vec2(panel.x + panel.w * 0.5f, panel.y + 10.0f), score, 3,
            0xFFFFFFFFu, UiAlign::Center);
}

bool game_wants_quit(const Game* d) { return d->wants_quit; }

void game_begin_match(Game* d) {
    d->screen = Screen::Playing;
}

const char* game_status(const Game* d) {
    static char buf[96];
    snprintf(buf, sizeof(buf), "RTT core  %d - %d  games %d - %d",
             d->w.score.points[0], d->w.score.points[1],
             d->w.score.games[0], d->w.score.games[1]);
    return buf;
}

void game_set_point_light(Game* d, bool on) { (void)d; (void)on; }
bool game_point_light(const Game* d) { (void)d; return false; }
