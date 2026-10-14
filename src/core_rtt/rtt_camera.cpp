#include "rtt_camera.h"

/* shot indices into CameraDirector.shots, shared by every clip (only 6 gameplay shots exist
 * [RTT], map/camera.md "Only ~6 cameras are actually gameplay") */
enum { SH_SWING_NEAR, SH_SWING_MED, SH_SWING_FAR, SH_SIDE_3Q, SH_SIDE_SIDE, SH_SIDE_TOP };

static void add_shot(CameraDirector* c, const char* name, CamType t, CamTier tier,
                      float back, float h, float side, float fov, int top) {
    CamShot* s = &c->shots[c->shot_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, RTT_NAME_LEN - 1);
    s->type = t; s->tier = tier; s->back_dist = back; s->height = h; s->side_dist = side;
    s->fov = fov; s->top_down = top;
}

/* a SideCam whose placement is the shipped .cm file's SC_LookFrom / SC_LookAt, in world space
 * (X right, Y up, Z along the table, 0 at the net). The file's 36 per-zone FOVs (SC_AA_Fov ..
 * SC_FF_Fov, 0.43..1.77 rad) are not decoded (which zone, absolute or a factor) [OPEN]; the shot
 * uses the common FOV 0.610865 [DATA default] */
static void add_side_shot(CameraDirector* c, const char* name, CamTier tier, Vec3 from, Vec3 at) {
    add_shot(c, name, CAMT_SIDE, tier, 0.0f, 0.0f, 0.0f, 0.610865f, 0);
    CamShot* s = &c->shots[c->shot_count - 1];
    s->fixed = 1;
    s->look_from = from;
    s->look_at = at;
}

/* dests shared by every clip: the rally-event vocabulary from clips.xml (map/camera.md §3),
 * mapped onto the subset of that vocabulary this reconstruction's EventBus actually posts
 * [RTT vocabulary identity, CHOICE which EventId stands for each named Condition] */
static void add_common_dests(CamClip* clip, int swing_shot, int side_shot) {
    static const struct { EventId id; int shot; float bias; } d[] = {
        { EV_BALL_BOUNCE_PADDLE_A,     -1,   0.10f },   /* PaddleHit -- resolved to swing_shot below */
        { EV_BALL_BOUNCE_PADDLE_B,     -1,   0.10f },
        { EV_BALL_COLLIDE_NET,         -1,   0.15f },   /* WindowNetCollideRebound */
        { EV_BALL_BEYOND_TABLE_END,    -1,   0.05f },   /* WindowTableEndHit / miss-or-score */
        { EV_BALL_PAST_PLAYER,         -1,   0.05f },
        { EV_OUT_A,                    -1,   0.00f },   /* WindowMissOrScore */
        { EV_OUT_B,                    -1,   0.00f },
        { EV_POINT_A,                  -1,   0.00f },   /* EndOfPoint */
        { EV_POINT_B,                  -1,   0.00f },
        { EV_RALLY_SHOT_COUNT,         -1,   0.00f },   /* CounterNHits escalation [CHOICE: cuts to the side cam] */
    };
    for (unsigned i = 0; i < sizeof(d) / sizeof(d[0]); i++) {
        CamDest* cd = &clip->dests[clip->dest_count++];
        cd->condition = d[i].id;
        cd->time_bias = d[i].bias;
        /* contact events pull in tight (swing cam); everything else (net/out/point/rally length)
         * cuts wide to the broadcast side cam -- RTT's actual per-condition target instance is
         * authored per clip in clips.xml; this is a representative simplification [CHOICE] */
        cd->target_shot = (d[i].id == EV_BALL_BOUNCE_PADDLE_A || d[i].id == EV_BALL_BOUNCE_PADDLE_B)
                               ? swing_shot : side_shot;
    }
}

static int add_clip(CameraDirector* c, const char* name, int default_inst,
                     const int* insts, int inst_count) {
    CamClip* cl = &c->clips[c->clip_count];
    memset(cl, 0, sizeof(*cl));
    strncpy(cl->name, name, RTT_NAME_LEN - 1);
    /* Attack=25 Decay=1 Sustain=1 Release=5 is the typical shipped envelope (map/camera.md §3) [DATA] */
    cl->env.attack = 25.0f / 60.0f; cl->env.decay = 1.0f / 60.0f;
    cl->env.sustain = 1.0f / 60.0f; cl->env.release = 5.0f / 60.0f;
    for (int i = 0; i < inst_count; i++) cl->insts[i] = insts[i];
    cl->inst_count = inst_count;
    cl->default_inst = default_inst;
    add_common_dests(cl, SH_SWING_MED, SH_SIDE_3Q);
    return c->clip_count++;
}

void camera_init(CameraDirector* c) {
    memset(c, 0, sizeof(*c));
    c->active_clip = -1;
    c->cur_shot = c->prev_shot = -1;

    /* CAM_SWING: behind the receiving player, looking toward the net [OPEN numeric tuning] */
    add_shot(c, "SwingNear",  CAMT_SWING, CAM_TIER_NEAR,   2.2f, 1.6f, 0.0f, 0.70f, 0);
    add_shot(c, "SwingMed",   CAMT_SWING, CAM_TIER_MEDIUM, 3.6f, 1.9f, 0.0f, 0.61f, 0);  /* FOV default DAT 0.610865 [DATA] */
    add_shot(c, "SwingFar",   CAMT_SWING, CAM_TIER_FAR,    5.4f, 2.4f, 0.0f, 0.55f, 0);
    /* CAM_SIDE: shots.xml ThreeQuarter / Side / TopDown, placed by sidecam/threequarter.cm,
     * side.cm, topdown.cm [DATA] */
    add_side_shot(c, "SideThreeQ", CAM_TIER_NEAR,   v3(4.0f, 3.5f, 5.0f), v3(0.0f, 0.75f, 0.0f));
    add_side_shot(c, "SideSide",   CAM_TIER_MEDIUM, v3(9.0f, 5.2f, 0.0f), v3(-1.6f, 0.0f, 0.0f));
    add_side_shot(c, "SideTop",    CAM_TIER_FAR,    v3(5.0f, 10.0f, 0.0f), v3(-0.4f, 0.0f, 0.0f));

    static const int all_shots[] = { SH_SWING_NEAR, SH_SWING_MED, SH_SWING_FAR, SH_SIDE_3Q, SH_SIDE_SIDE, SH_SIDE_TOP };
    /* 4 clips mirror gdShotDataMgr__SelectRallyClip's 4-entry table: OnePlayerPZ/NZ, TwoPlayerPZ/NZ
     * (map/camera.md §6) -- default shot is the medium swing cam behind the receiver [RTT structure] */
    add_clip(c, "TwoPlayerPZ", SH_SWING_MED, all_shots, 6);
    add_clip(c, "TwoPlayerNZ", SH_SWING_MED, all_shots, 6);
    add_clip(c, "OnePlayerPZ", SH_SWING_MED, all_shots, 6);
    add_clip(c, "OnePlayerNZ", SH_SWING_MED, all_shots, 6);
}

void camera_rally_start(CameraDirector* c, int player_count, Side receiving_side) {
    int idx = (player_count >= 2 ? 0 : 2) + (receiving_side == SIDE_A ? 0 : 1);  /* [0..3] = PZ/NZ x TwoPlayer/OnePlayer, matches add_clip order [RTT table shape] */
    if (idx < 0 || idx >= c->clip_count) idx = 0;
    c->active_clip = idx;
    c->cur_shot = c->prev_shot = c->clips[idx].default_inst;
    c->blend_t = 1e9f;                                      /* hard cut, no blend-in at rally start [RTT] */
    c->has_from = 0;
    c->blend_env = c->clips[idx].env;
}

void camera_on_event(void* user, const Event* e) {
    CameraDirector* c = (CameraDirector*)user;
    if (c->active_clip < 0) return;
    const CamClip* clip = &c->clips[c->active_clip];
    for (int i = 0; i < clip->dest_count; i++) {
        if (clip->dests[i].condition != e->id) continue;     /* first matching dest wins [RTT] */
        if (clip->dests[i].target_shot == c->cur_shot) return;
        c->prev_shot = c->cur_shot;
        c->cur_shot = clip->dests[i].target_shot;
        c->has_from = 1;
        c->from_eye = c->eye; c->from_target = c->target; c->from_fov = c->fov;
        c->blend_t = -clip->dests[i].time_bias;              /* ConditionTimeBias delays the cut [DATA] */
        c->blend_env = clip->env;
        return;
    }
}

/* evaluate one shot's transform given the live ball and both players' roots [RTT behavior,
 * OPEN: RTT's SwingCam look-at band / approach easing (SW_LookAtBandWidth etc.) and SideCam's
 * exact broadcast placement are not individually decoded -- this is a straight behind-the-player
 * / beside-the-table placement that reproduces the shot FAMILY, not the per-frame easing curve] */
static void shot_eval(const CamShot* s, Vec3 ball, Vec3 receiver_root, Vec3 striker_root,
                       Vec3* eye, Vec3* tgt, float* fov) {
    if (s->type == CAMT_SWING) {
        /* behind the receiving player, looking at the net / incoming ball [RTT structure] */
        Vec3 dir = v3_norm(v3_sub(receiver_root, striker_root));   /* receiver -> away from opponent */
        if (v3_len(dir) < 1e-5f) dir = v3(0, 0, 1);
        *eye = v3_add(receiver_root, v3(dir.x * s->back_dist, s->height, dir.z * s->back_dist));
        *tgt = v3_lerp(receiver_root, ball, 0.5f);                  /* tracking: half-receiver, half-ball [OPEN] */
    } else if (s->fixed) {
        *eye = s->look_from;
        *tgt = s->look_at;
    } else {
        /* side-on broadcast angle, net height, offset along X [RTT structure] */
        Vec3 mid = v3_scale(v3_add(receiver_root, striker_root), 0.5f);
        *eye = s->top_down ? v3(mid.x, s->height, mid.z)
                            : v3(mid.x + s->side_dist, s->height, mid.z);
        *tgt = s->top_down ? v3(mid.x, 0.0f, mid.z) : mid;
    }
    *fov = s->fov;
}

void camera_update(CameraDirector* c, Vec3 ball_pos, Vec3 player_a_root, Vec3 player_b_root, float dt) {
    if (c->cur_shot < 0) return;
    Vec3 e1, t1, e0, t0; float f1, f0;
    shot_eval(&c->shots[c->cur_shot], ball_pos, player_a_root, player_b_root, &e1, &t1, &f1);
    if (c->has_from) {
        e0 = c->from_eye; t0 = c->from_target; f0 = c->from_fov;
    } else {
        int prev = c->prev_shot < 0 ? c->cur_shot : c->prev_shot;
        shot_eval(&c->shots[prev], ball_pos, player_a_root, player_b_root, &e0, &t0, &f0);
    }

    c->blend_t += dt;
    /* blend weight of the new shot: ramps in over Attack, then the cut locks (see CamEnvelope)
     * (CMtoCSAdapter__RunEvaluatorAndFill: blend runs until camViewCS phase == 4) [RTT] */
    const CamEnvelope* env = &c->blend_env;
    float w;
    if (c->blend_t <= 0.0f) {
        w = 0.0f;
    } else if (c->blend_t < env->attack) {
        w = env->attack > 0.0f ? c->blend_t / env->attack : 1.0f;
    } else {
        /* attack done: the blend is over, lock to the new shot (camViewCS phase 4) [RTT lock,
         * OPEN what Decay / Sustain / Release do -- see CamEnvelope] */
        c->prev_shot = c->cur_shot;
        c->has_from = 0;
        w = 1.0f;
    }
    c->eye = v3_lerp(e0, e1, w);
    c->target = v3_lerp(t0, t1, w);
    c->fov = rtt_lerpf(f0, f1, w);
}

/* ---------------------------------------------------------------- follow cam */
void follow_cam_init(FollowCam* c, Side side) {
    memset(c, 0, sizeof(*c));
    c->side = side;
    c->back = 3.2f; c->height = 2.7f;
    c->look_z = 0.2f; c->look_y = 0.5f;
    c->eye_follow = 0.8f; c->look_follow = 1.0f;
    c->rate = 3.0f;
    c->fov = 0.586f;
}

void follow_cam_update(FollowCam* c, Vec3 ball_pos, Vec3 player_root, float dt) {
    float s = c->side == SIDE_A ? 1.0f : -1.0f;               /* +z for SIDE_A */
    float rest = fabsf(player_root.z) > 1.0f ? fabsf(player_root.z) : 2.0f;
    Vec3 eye = v3(player_root.x * c->eye_follow, c->height, s * (rest + c->back));
    Vec3 tgt = v3(ball_pos.x * c->look_follow, c->look_y, -s * c->look_z);
    if (!c->started) { c->eye = eye; c->target = tgt; c->started = 1; return; }
    float k = 1.0f - expf(-c->rate * dt);                      /* frame-rate independent approach */
    c->eye = v3_add(c->eye, v3_scale(v3_sub(eye, c->eye), k));
    c->target = v3_add(c->target, v3_scale(v3_sub(tgt, c->target), k));
}
