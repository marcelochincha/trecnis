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
    /* CAM_SIDE: the broadcast angle, off to one side of the table [OPEN numeric tuning] */
    add_shot(c, "SideThreeQ", CAMT_SIDE,  CAM_TIER_NEAR,   0.0f, 2.0f, 4.5f, 0.61f, 0);
    add_shot(c, "SideSide",   CAMT_SIDE,  CAM_TIER_MEDIUM, 0.0f, 1.4f, 6.0f, 0.55f, 0);
    add_shot(c, "SideTop",    CAMT_SIDE,  CAM_TIER_FAR,    0.0f, 6.0f, 0.0f, 0.70f, 1);

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
    int prev = c->prev_shot < 0 ? c->cur_shot : c->prev_shot;
    shot_eval(&c->shots[prev], ball_pos, player_a_root, player_b_root, &e0, &t0, &f0);

    c->blend_t += dt;
    /* full ADSR weight: ramp in over Attack, hold through Decay+Sustain, ramp out over Release
     * (CMtoCSAdapter__RunEvaluatorAndFill: blend runs until camViewCS phase == 4) [RTT] */
    const CamEnvelope* env = &c->blend_env;
    float w;
    if (c->blend_t <= 0.0f) {
        w = 0.0f;
    } else if (c->blend_t < env->attack) {
        w = env->attack > 0.0f ? c->blend_t / env->attack : 1.0f;
    } else {
        float hold_end = env->attack + env->decay + env->sustain;
        if (c->blend_t < hold_end) {
            w = 1.0f;
        } else {
            float rel_t = c->blend_t - hold_end;
            w = env->release > 0.0f ? rtt_clampf(1.0f - rel_t / env->release, 0.0f, 1.0f) : 0.0f;
            /* once the outgoing shot has fully faded there is nothing left to blend from; lock to
             * the new shot so a very short Release doesn't leave a flicker at w==0 [RTT: phase==4] */
            if (w <= 0.0f) { c->prev_shot = c->cur_shot; w = 1.0f; e0 = e1; t0 = t1; f0 = f1; }
        }
    }
    c->eye = v3_lerp(e0, e1, w);
    c->target = v3_lerp(t0, t1, w);
    c->fov = rtt_lerpf(f0, f1, w);
}
