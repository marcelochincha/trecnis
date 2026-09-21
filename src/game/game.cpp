#include <game/game.hpp>

#include <engine/scene/scene_runtime.hpp>
#include <engine/anim/skinned_mesh.hpp>
#include <core/sr_profiler.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

static const float kLookSensitivity = 0.0025f;   // radians per mouse pixel
static const float kMinMoveSpeed    = 0.5f;      // wheel speed clamp: lower bound
static const float kMaxMoveSpeed    = 60.0f;     // wheel speed clamp: upper bound

struct Game {
    // ---- scenery -----------------------------------------------------------
    // With floor_metallic = 1 the albedo stops being a diffuse colour and
    // becomes the mirror's TINT: it is F0, the fraction of each channel the
    // surface reflects head-on. Real measured metals, to swap in:
    //     silver    (0.97, 0.96, 0.92)   aluminium (0.91, 0.92, 0.92)
    //     chrome    (0.55, 0.56, 0.55)   gold      (1.00, 0.77, 0.34)
    //     copper    (0.95, 0.64, 0.54)
    // Anything dark here gives a dim mirror -- that is the physics, not a bug.
    vec3  floor_albedo   = vec3(0.93f, 0.95f, 1.00f);   // cool silver
    float floor_rough    = 0.7f;
    float floor_metallic = 0.1f;

    // // ---- the one mover -----------------------------------------------------
    // SkinnedMesh skin;
    // mesh*       character   = nullptr;
    // vec3        char_albedo = vec3(1.0f, 1.0f, 1.0f);
    // float       char_rough  = 0.00f;
    // float       char_metal  = 0.00f;   // raise to 1 for a chrome character
    // float       anim_time   = 0.0f;   // skinning clock (seconds)

    // ---- the spinning cube -------------------------------------------------
    // Same deal as the character: it rides in the dynamic tree, which is rebuilt
    // every frame anyway, so changing its transform per frame costs nothing extra.
    mesh* cube           = nullptr;
    vec3  cube_center    = vec3(2.6f, 1.2f, 0.0f);
    float cube_rough     = 0.35f;   // matte enough to skip the reflection ray
    float cube_metal     = 0.00f;   // raise to 1 for a chrome cube
    float cube_spin      = 0.9f;    // base angular speed (rad/s)
    float cube_pulse_hz  = 0.55f;   // scale breathing rate (cycles/s)
    float cube_pulse_amt = 0.30f;   // breathing depth, as a fraction of base scale
    float cube_hue_speed = 0.7f;    // colour cycle rate (rad/s)

    // ---- lighting ----------------------------------------------------------
    bool  point_light_enabled = false;
    float point_light_radius  = 2.0f;   // orbit radius (world units)
    float point_light_height  = 3.0f;   // orbit height
    float point_light_speed   = 0.8f;   // angular speed (rad/s)

    // ---- camera rig: a free-flying observer ---------------------------------
    vec3  position   = vec3(0.0f, 1.7f, 1.5f);
    float yaw        = to_radians(0.0f);
    float pitch      = to_radians(-4.0f);
    float move_speed = 6.0f;
    bool  fly_mode   = true;
    float bob_phase  = 0.0f;

    float time = 0.0f;
};

// Point the camera rig at `target` from `pos`.
static void look_from(Game* d, const vec3& pos, const vec3& target) {
    vec3 dir = normalize(target - pos);
    d->position = pos;
    d->pitch    = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    d->yaw      = std::atan2(dir.x, -dir.z);
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
    // Static scenery: one floor slab. Built once, never rebuilt per frame.
    scene.clear_static();
    scene.add_static_box(vec3(-8.0f, -0.02f, -8.0f), vec3(8.0f, 0.0f, 8.0f),
                         d->floor_albedo, d->floor_rough, d->floor_metallic);
    scene.commit_static();

    // // The mover: a procedural skinned character, deformed every frame.
    // d->skin = SkinnedMesh{};
    // mesh* cm = new mesh;
    // build_procedural_character(*cm, d->skin);
    // cm->setPosition(vec3(0.0f, 0.0f, 0.0f));
    // if (d->skin.valid()) d->skin.apply(*cm, 0.0f);
    // delete d->character;
    // d->character = cm;
    // d->anim_time = 0.0f;

    // The second mover: a unit cube that spins, breathes and cycles hue.
    mesh* cb = new mesh;
    create_cube(cb, 1.0f);
    cb->setPosition(d->cube_center);
    delete d->cube;
    d->cube = cb;

    look_from(d, vec3(0.0f, 1.6f, 6.0f), vec3(0.0f, 1.2f, 0.0f));

    // std::cout << "Scene: " << scene.static_bvh().triangle_count()
    //           << " static tris, procedural character ("
    //           << d->skin.bones << " bones, " << d->skin.frames << " frames)\n";
}

void game_update(Game* d, const InputState& in, float dt, World& out) {
    d->time += dt;

    // ---- animation ---------------------------------------------------------
    // if (d->character && d->skin.valid()) {
    //     d->anim_time += dt;
    //     float loop = (float)d->skin.frames / d->skin.fps;   // loop the clip
    //     if (loop > 0.0f && d->anim_time > loop) d->anim_time -= loop;
    //     PROF_SCOPE(PROF_SKIN);
    //     d->skin.apply(*d->character, d->anim_time);
    // }

    // ---- camera rig --------------------------------------------------------
    // Scroll up = faster, down = slower. Scale multiplicatively so the step
    // feels even across the range, then clamp to [min, max].
    if (in.wheel != 0.0f) {
        float factor = std::pow(1.15f, in.wheel);
        d->move_speed = std::clamp(d->move_speed * factor, kMinMoveSpeed, kMaxMoveSpeed);
    }

    d->yaw   +=  in.look_x * kLookSensitivity;
    d->pitch += -in.look_y * kLookSensitivity;
    d->pitch  = std::clamp(d->pitch, to_radians(-85.0f), to_radians(85.0f));

    vec3 wish = in.move;
    if (!d->fly_mode) wish.y = 0.0f;

    float s = std::sin(d->yaw), c = std::cos(d->yaw);
    vec3  vel;
    vel.x = (wish.x * c - wish.z * s) * d->move_speed;
    vel.z = (wish.x * s + wish.z * c) * d->move_speed;
    vel.y =  wish.y * d->move_speed;

    d->position = d->position + vel * dt;
    if (!d->fly_mode) d->position.y = 1.7f;

    float horiz = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    if (!d->fly_mode && horiz > 0.001f) d->bob_phase += dt * horiz * 2.0f;
    float bob_amt = d->fly_mode ? 0.0f : std::min(1.0f, horiz / d->move_speed);
    float bob     = std::sin(d->bob_phase) * 0.05f * bob_amt;

    // ---- publish the world -------------------------------------------------
    out.camera.pos   = d->position + vec3(0.0f, bob, 0.0f);
    out.camera.euler = vec3(d->pitch, d->yaw, 0.0f);

    out.dynamic.clear();
    // if (d->character) {
    //     Entity ch;
    //     ch.geo    = d->character;
    //     ch.albedo = d->char_albedo;
    //     ch.rough    = d->char_rough;
    //     ch.metallic = d->char_metal;
    //     ch.shadow = true;
    //     out.dynamic.push_back(ch);
    // }

    if (d->cube) {
        // Spin on three axes at rates that don't divide evenly into each other,
        // so the cube never repeats the same pose -- it reads as solid and 3D
        // instead of as a card flipping.
        d->cube->setRotation(vec3(d->time * d->cube_spin * 0.8f,
                                  d->time * d->cube_spin * 1.3f,
                                  d->time * d->cube_spin * 0.4f));

        float pulse = 1.0f + d->cube_pulse_amt
                           * std::sin(d->time * d->cube_pulse_hz * 6.2831853f);
        d->cube->setScale(vec3(pulse, pulse, pulse));

        Entity cu;
        cu.geo    = d->cube;
        cu.albedo = hue_cycle(d->time * d->cube_hue_speed);
        cu.rough    = d->cube_rough;
        cu.metallic = d->cube_metal;
        cu.shadow = true;
        out.dynamic.push_back(cu);
    }

    out.lights.clear();
    if (d->point_light_enabled) {
        float a = d->time * d->point_light_speed;
        out.lights.push_back(PointLight{
            vec3(std::cos(a) * d->point_light_radius,
                 d->point_light_height,
                 std::sin(a) * d->point_light_radius),
            vec3(1.0f, 0.9f, 0.75f), 12.0f });
    }
}

void game_destroy(Game* d) {
    if (!d) return;
    // delete d->character;
    delete d->cube;
    delete d;
}

const char* game_status(const Game* d) { return d->fly_mode ? "FLY" : "WALK"; }

void game_set_point_light(Game* d, bool on) { d->point_light_enabled = on; }
bool game_point_light(const Game* d)        { return d->point_light_enabled; }
