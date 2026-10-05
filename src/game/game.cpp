#include <game/game.hpp>
#include <game/game_ball.hpp>

#include <subsystems/scene/scene_runtime.hpp>
#include <subsystems/anim/skinned_mesh.hpp>
#include <core/sr_profiler.hpp>
#include <core/sr_texture.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

static const float kLookSensitivity = 0.0025f;   // radians per mouse pixel
static const float kMinMoveSpeed    = 0.5f;      // wheel speed clamp: lower bound
static const float kMaxMoveSpeed    = 60.0f;     // wheel speed clamp: upper bound

static const float kPaddleMouseSens = 0.004f;    // paddle metres per mouse pixel
static const float kPaddlePlaneX    = 1.6f;      // how far behind the table the
                                                 // paddles sit, off-table
static const float kPaddleReachY    = 2.0f;      // upper reach of a paddle
static const float kPaddleReachZ    = tt::kHalfWidZ; // lateral reach matches the table
static const float kServePause      = 1.2f;      // seconds between points and the
                                                 // next serve, so a reset is readable

struct Game {
    // ---- table tennis -------------------------------------------------------
    mesh* ball           = nullptr;
    tt::Ball ball_phys;

    // player's paddle (-x side), driven by mouse deltas
    mesh* paddle          = nullptr;
    vec3  paddle_pos      = vec3(-kPaddlePlaneX, 1.5f, 0.0f);
    float paddle_width    = 0.3f;
    float paddle_height   = 0.5f;
    float paddle_depth    = 0.04f;
    vec3  paddle_albedo   = vec3(0.2f, 0.2f, 0.8f);

    // AI paddle (+x side): chases the predicted ball position
    mesh* paddle_ai       = nullptr;
    vec3  paddle_ai_pos   = vec3(kPaddlePlaneX, 1.5f, 0.0f);
    float paddle_ai_speed = 2.5f;   // m/s lateral + vertical reach speed
    vec3  paddle_ai_albedo= vec3(0.8f, 0.2f, 0.2f);

    // table surface (just for reference, static geometry added in game_init)
    vec3  table_albedo   = vec3(0.2f, 0.7f, 0.2f);
    float table_rough    = 0.3f;

    // floor
    vec3  floor_albedo   = vec3(0.5f, 0.5f, 0.5f);
    float floor_rough    = 0.7f;
    float floor_metallic = 0.1f;

    // score
    int   points_player  = 0;
    int   points_ai      = 0;
    float serve_hold     = 0.0f;      // seconds to wait between points before serving
    tt::Side next_server = tt::Side::Player;

    // ---- camera rig: above and behind the player's paddle -------------
    vec3  cam_pos  = vec3(-3.5f, 2.4f, 0.0f);   // current (smoothed) position
    vec3  cam_look = vec3(0.0f, tt::kPlateTopY, 0.0f); // what it watches
    vec3  position = cam_pos;
    float pitch    = to_radians(-20.0f);
    float yaw      = 0.0f;

    float time = 0.0f;
};

// One frame of the camera rig: move `pos` towards the anchor above/behind the
// player's paddle, then point at the table.  Lerp factor `k` keeps the motion
// smooth but responsive; look-at is recomputed directly from the anchor so the
// view direction never lags behind the paddle.
static void update_camera_rig(Game* d) {
    const vec3 anchor = d->paddle_pos +
                        vec3(-1.2f, 1.0f, 0.0f);   // behind + above the paddle
    const float k = 1.0f - std::exp(-8.0f * (1.0f / 60.0f));
    d->cam_pos = d->cam_pos + (anchor - d->cam_pos) * k;

    const vec3 look = d->ball_phys.dead ? vec3(0.0f, tt::kPlateTopY, 0.0f)
                                        : lerp(d->ball_phys.pos,
                                               vec3(0.0f, tt::kPlateTopY, 0.0f),
                                               0.5f);
    d->cam_look = d->cam_look + (look - d->cam_look) * k;

    d->position = d->cam_pos;
    vec3 dir = normalize(d->cam_look - d->cam_pos);
    d->pitch   = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    d->yaw     = std::atan2(dir.x, -dir.z);
}

// Sweep the colour wheel with three phase-shifted cosines -- a cheap stand-in
// for an HSV->RGB conversion. Each channel peaks a third of a cycle after the
// previous one, so the result rides the hue circle at full saturation, and the
// 0.5 bias keeps every channel inside [0, 1] without a clamp.
static vec3 hue_cycle(float t) {
    const float k = 2.0943951f;   // 2*pi/3
    return vec3(0.5f + 0.5f * std::cos(t),
                0.5f + 0.5f * std::cos(t + k),
                0.5f + 0.5f * std::cos(t + 2.0f * k));
}

Game* game_create() { return new Game; }

void game_init(Game* d, SceneRuntime& scene) {
    // Skybox
    std::array<texture, 6>& sky = scene.skybox();
    load_png_texture("res/textures/skybox3/null_plainsky512_rt.png", sky[0]);
    load_png_texture("res/textures/skybox3/null_plainsky512_bk.png", sky[1]);
    load_png_texture("res/textures/skybox3/null_plainsky512_ft.png", sky[2]);
    load_png_texture("res/textures/skybox3/null_plainsky512_lf.png", sky[3]);
    load_png_texture("res/textures/skybox3/null_plainsky512_up.png", sky[4]);
    load_png_texture("res/textures/skybox3/null_plainsky512_dn.png", sky[5]);
    scene.commit_sky();

    // Static geometry: floor and table
    scene.clear_static();
    scene.add_static_box(vec3(-8.0f, -0.02f, -8.0f), vec3(8.0f, 0.0f, 8.0f),
                         d->floor_albedo, d->floor_rough, d->floor_metallic);

    // Ping pong table: 2.74m x 1.525m x 0.76m high (real dimensions, scaled down)
    // Simplified as a rectangular slab at y=1.2
    scene.add_static_box(vec3(-tt::kHalfLenX, tt::kPlateTopY - 0.01f, -tt::kHalfWidZ),
                         vec3(tt::kHalfLenX, tt::kPlateTopY,         tt::kHalfWidZ),
                         d->table_albedo, d->table_rough, 0.0f);

    // Net: a thin box across the middle
    scene.add_static_box(vec3(-0.01f, tt::kPlateTopY, -tt::kHalfWidZ),
                         vec3( 0.01f, tt::kNetTopY,    tt::kHalfWidZ),
                         vec3(0.9f, 0.9f, 0.9f), 1.0f, 0.0f);
    scene.commit_static();

    // Ball: a small sphere
    mesh* ball_mesh = new mesh;
    create_sphere(ball_mesh, tt::kBallRadius, 8, 8);
    ball_mesh->setPosition(d->ball_phys.pos);
    delete d->ball;
    d->ball = ball_mesh;

    tt::serve(d->ball_phys, tt::Side::Player);
    d->next_server = tt::Side::Player;
    d->serve_hold  = 0.0f;   // first serve goes out immediately

    // Player paddle: a flat rectangular slab facing the incoming ball. The broad
    // face must be perpendicular to the travel axis (x): thin in x, wide in z,
    // tall in y.
    const vec3 paddle_scale(d->paddle_depth, d->paddle_height, d->paddle_width);
    mesh* paddle_mesh = new mesh;
    create_cube(paddle_mesh, 1.0f);
    paddle_mesh->setScale(paddle_scale);
    paddle_mesh->setPosition(d->paddle_pos);
    delete d->paddle;
    d->paddle = paddle_mesh;

    // AI paddle: same shape, red, on the +x side
    mesh* ai_mesh = new mesh;
    create_cube(ai_mesh, 1.0f);
    ai_mesh->setScale(paddle_scale);
    ai_mesh->setPosition(d->paddle_ai_pos);
    delete d->paddle_ai;
    d->paddle_ai = ai_mesh;
}

// Ballistics: return the launch velocity that lands the ball legally on the
// opponent's table half. We do not invert a formula here -- the module already
// owns the exact bounce/net/floor rules, so we scan vy and run a tiny untouched
// simulation of tt::step to find a velocity whose first table bounce is a clean,
// deep opponent-half landing. vz aims the ball back to z = 0.
static vec3 return_velocity(const vec3& from, bool toward_player) {
    constexpr float dt = 1.0f / 60.0f;
    const float dir   = toward_player ? -1.0f : 1.0f;   // x direction of travel
    for (float vy = 1.2f; vy <= 4.5f; vy += 0.2f) {
        for (float vx : {2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f}) {
            const float sx = dir * vx;
            tt::Ball tmp;
            tmp.pos   = from;
            tmp.radius = tt::kBallRadius;
            tmp.vel   = vec3(sx, vy, -tmp.pos.z * 0.1f); // gentle aim toward z=0
            tmp.phase = tt::Phase::Rally;
            tmp.for_side = toward_player ? tt::Side::Player : tt::Side::AI;
            tmp.dead  = false;
            bool took_bounce = false;
            float min_net_clear = 1e9f;
            for (int f = 0; f < int(4.0f / dt); ++f) {
                const vec3 prev = tmp.pos;
                const tt::Event e = tt::step(tmp, dt);
                if (e == tt::Event::Bounce) {
                    took_bounce = true;
                    break;
                }
                if (e == tt::Event::PointPlayer || e == tt::Event::PointAI) break;
                // swept net clearance, so a hair-clear on a coarse frame is rejected
                if ((prev.x < 0.0f && tmp.pos.x >= 0.0f) &&
                    std::fabs(tmp.pos.z) <= tt::kHalfWidZ) {
                    const float t = -prev.x / (tmp.pos.x - prev.x);
                    const float yc = prev.y + (tmp.pos.y - prev.y) * t;
                    min_net_clear = std::min(min_net_clear,
                                             yc - tt::kNetTopY - tmp.radius);
                }
            }
            if (!took_bounce) continue;
            // Keep enough clearance so the 1/60 game Euler cannot clip the net.
            if (min_net_clear < 0.03f) continue;
            // A first bounce on the opponent's half, deep enough to be a real shot.
            const bool on_half = toward_player ? (tmp.pos.x < 0.0f) : (tmp.pos.x > 0.0f);
            const bool deep    = std::fabs(tmp.pos.x) > 0.45f &&
                                 std::fabs(tmp.pos.x) < tt::kHalfLenX - 0.05f;
            const bool in_z    = std::fabs(tmp.pos.z) < tt::kHalfWidZ - 0.05f;
            if (on_half && deep && in_z) {
                return vec3(sx, vy, -tmp.pos.z * 0.1f);
            }
        }
    }
    // Fallback: never reached; keep something plausible.
    return vec3(dir * 4.0f, 2.0f, 0.0f);
}

// Predict where the ball crosses a paddle plane x = plane_x, advancing a clone
// of the ball with the real module physics (table bounces included) so the AI
// aims at the true crossing of the *post-bounce* trajectory, not a pure
// parabola. Returns false if the rally dies before the plane is reached.
static bool predict_crossing(const tt::Ball& b, float plane_x, vec3& out) {
    constexpr float dt = 1.0f / 60.0f;
    tt::Ball tmp = b;
    for (int f = 0; f < 600; ++f) {
        const vec3 prev = tmp.pos;
        if (tmp.dead) return false;
        const tt::Event e = tt::step(tmp, dt);
        // crossed the plane this frame?
        if ((prev.x < plane_x && tmp.pos.x >= plane_x) ||
            (prev.x > plane_x && tmp.pos.x <= plane_x)) {
            const float t = (plane_x - prev.x) / (tmp.pos.x - prev.x);
            // forward Euler: interpolate x/y/z across the frame like the game
            out = vec3(prev.x + (tmp.pos.x - prev.x) * t,
                       prev.y + (tmp.pos.y - prev.y) * t,
                       prev.z + (tmp.pos.z - prev.z) * t);
            return true;
        }
    }
    return false;
}

// True if the ball crossed the paddle's x-plane this frame, inside the face
// rectangle, moving towards the striker's own side. A swept (plane-crossing)
// test so a fast ball cannot tunnel through the thin slab between frames.
static bool paddle_contact(const tt::Ball& b, const vec3& prev,
                           const vec3& paddle, tt::Side striker,
                           float pwidth, float pheight, float pdepth) {
    (void)pdepth;
    const bool crossed =
        (prev.x < paddle.x && b.pos.x >= paddle.x) ||
        (prev.x > paddle.x && b.pos.x <= paddle.x);
    if (!crossed) return false;
    const float t = (paddle.x - prev.x) / (b.pos.x - prev.x);
    const float yc = prev.y + (b.pos.y - prev.y) * t;
    const float zc = prev.z + (b.pos.z - prev.z) * t;
    const bool in_y = std::fabs(yc - paddle.y) < pheight * 0.5f + b.radius;
    const bool in_z = std::fabs(zc - paddle.z) < pwidth  * 0.5f + b.radius;
    const bool toward = striker == tt::Side::Player ? (b.vel.x < 0.0f) : (b.vel.x > 0.0f);
    const bool bounced_once = b.last_bounce == striker;   // one bounce per side
    return in_y && in_z && toward && bounced_once;
}

void game_update(Game* d, const InputState& in, float dt, World& out) {
    d->time += dt;

    const float table_top = tt::kPlateTopY;
    const vec3  ball_prev = d->ball_phys.pos;

    // ---- serve pacing: after a point the ball sits dead for a beat, then the
    // next server launches it. Without this the reset is a visible teleport.
    if (d->ball_phys.dead && d->serve_hold > 0.0f) {
        d->serve_hold -= dt;
        if (d->serve_hold <= 0.0f) {
            tt::serve(d->ball_phys, d->next_server);
            d->serve_hold = 0.0f;
        }
    }

    // ---- physics: ball step -> events ---
    const tt::Event ev = tt::step(d->ball_phys, dt);

    // ---- paddle control: player paddle follows mouse deltas ----
    // SDL reports +look_y when the mouse moves DOWN, so subtracting keeps
    // "mouse up = paddle up".
    d->paddle_pos.z += in.look_x * kPaddleMouseSens;
    d->paddle_pos.y -= in.look_y * kPaddleMouseSens;
    d->paddle_pos.z = std::clamp(d->paddle_pos.z, -kPaddleReachZ, kPaddleReachZ);
    d->paddle_pos.y = std::clamp(d->paddle_pos.y, tt::kFloorY + 0.1f, kPaddleReachY);

    // ---- AI paddle: chase the ball, off-table on the +x side ----
    if (!d->ball_phys.dead) {
        const float plane_x = kPaddlePlaneX;
        const bool approaching = d->ball_phys.vel.x > 1e-4f &&
                                 d->ball_phys.pos.x < plane_x;
        if (approaching) {
            // Ball is coming our way: move to where it will cross our plane.
            vec3 cp;
            if (predict_crossing(d->ball_phys, plane_x, cp)) {
                cp.z = std::clamp(cp.z, -kPaddleReachZ, kPaddleReachZ);
                cp.y = std::clamp(cp.y, tt::kFloorY + 0.1f, kPaddleReachY);
            } else {
                cp = d->paddle_ai_pos;   // rally ends before us: hold position
            }
            // ease toward the predicted spot
            d->paddle_ai_pos.y += (cp.y - d->paddle_ai_pos.y) *
                                  std::min(1.0f, d->paddle_ai_speed * dt);
            d->paddle_ai_pos.z += (cp.z - d->paddle_ai_pos.z) *
                                  std::min(1.0f, d->paddle_ai_speed * dt);
        }
    }

    // ---- struck ball: AI paddle return ----
    if (paddle_contact(d->ball_phys, ball_prev, d->paddle_ai_pos, tt::Side::AI,
                       d->paddle_width, d->paddle_height, d->paddle_depth)) {
        // return it towards the player's half, simulated from the true hit point
        tt::struck(d->ball_phys, tt::Side::AI,
                   return_velocity(d->ball_phys.pos, /*toward_player=*/true));
    }

    // ---- struck ball: player paddle return ----
    if (paddle_contact(d->ball_phys, ball_prev, d->paddle_pos, tt::Side::Player,
                       d->paddle_width, d->paddle_height, d->paddle_depth)) {
        tt::struck(d->ball_phys, tt::Side::Player,
                   return_velocity(d->ball_phys.pos, /*toward_player=*/false));
    }

    // ---- point scored -> pause, then serve again ---
    if (ev == tt::Event::PointPlayer || ev == tt::Event::PointAI) {
        if (ev == tt::Event::PointPlayer) d->points_player++;
        else                              d->points_ai++;
        d->next_server = other(d->ball_phys.server);
        d->serve_hold  = kServePause;
    }

    // ---- update mesh positions ---
    if (d->ball) {
        d->ball->setPosition(d->ball_phys.pos);
    }
    if (d->paddle) {
        d->paddle->setPosition(d->paddle_pos);
    }
    if (d->paddle_ai) {
        d->paddle_ai->setPosition(d->paddle_ai_pos);
    }

    // ---- camera rig: follow the player's paddle, point at the ball/table ---
    update_camera_rig(d);

    // ---- publish the world -------------------------------------------------
    out.camera.pos   = d->position;
    out.camera.euler = vec3(d->pitch, d->yaw, 0.0f);

    out.dynamic.clear();

    // Ball
    if (d->ball) {
        Entity ball_ent;
        ball_ent.geo    = d->ball;
        ball_ent.albedo = vec3(1.0f, 1.0f, 1.0f);
        ball_ent.rough  = 0.2f;
        ball_ent.metallic = 0.0f;
        ball_ent.shadow = true;
        out.dynamic.push_back(ball_ent);
    }

    // Player paddle
    if (d->paddle) {
        Entity paddle_ent;
        paddle_ent.geo    = d->paddle;
        paddle_ent.albedo = d->paddle_albedo;
        paddle_ent.rough  = 0.4f;
        paddle_ent.metallic = 0.0f;
        paddle_ent.shadow = true;
        out.dynamic.push_back(paddle_ent);
    }

    // AI paddle
    if (d->paddle_ai) {
        Entity ai_ent;
        ai_ent.geo    = d->paddle_ai;
        ai_ent.albedo = d->paddle_ai_albedo;
        ai_ent.rough  = 0.4f;
        ai_ent.metallic = 0.0f;
        ai_ent.shadow = true;
        out.dynamic.push_back(ai_ent);
    }

    out.lights.clear();
}

void game_destroy(Game* d) {
    if (!d) return;
    delete d->ball;
    delete d->paddle;
    delete d->paddle_ai;
    delete d;
}

const char* game_status(const Game* d) {
    static char buf[96];
    const char* server = d->next_server == tt::Side::Player ? "P" : "AI";
    const char* state  = "rally";
    if (d->ball_phys.dead) state = d->serve_hold > 0.0f ? "serving" : "dead";
    snprintf(buf, sizeof(buf), "TT  %d - %d   %s serves [%s]",
             d->points_player, d->points_ai, server, state);
    return buf;
}

void game_set_point_light(Game* d, bool on) { (void)d; (void)on; }
bool game_point_light(const Game* d) { (void)d; return false; }
