#include "rtt_shot.h"

#include <string.h>

/* SwingType -> attribute set: 8 / 9 ITZ, 10 MaxITZ, else normal (FUN_8009b9fc offsets) */
static int attr_set(int swing_type) {
    if (swing_type == 10) return 2;
    if (swing_type == 8 || swing_type == 9) return 1;
    return 0;
}

static int dominant_axis(Vec3 spin_axis) {          /* 0 top, 1 back, 2 side (FUN_8011b7a8) */
    if (fabsf(spin_axis.y) >= fabsf(spin_axis.x)) return 2;
    return spin_axis.x < 0.0f ? 0 : 1;
}

void shot_reset_point(ShotState* s, int side, float* wobble) {
    s->side = side;
    aim_target_reset_point(&s->aim, side, wobble);
}

/* FUN_8009cfcc: a CPU on a lunge / JIT zone (2, 3, 8, 10, 11, 16) */
static int stretch_zone(int human, int hit_zone) {
    return !human && (hit_zone == 2 || hit_zone == 3 || hit_zone == 8 || hit_zone == 10 ||
                      hit_zone == 11 || hit_zone == 16);
}

static int focus_level_active(const ShotContext* x) {
    return x->focus && x->full && x->full->active[x->player][x->focus->level];
}

void shot_begin(ShotState* s, int side, int serving, int human, float table_err_mag, Rng* rng,
                float* wobble, FocusMeter* focus, int focus_held) {
    AimTarget keep = s->aim;
    memset(s, 0, sizeof(*s));
    s->aim = keep;
    s->active = 1;
    s->serving = serving;
    s->human = human;
    s->side = side;
    s->swing_type = (serving && human) ? 2 : SWING_TYPE_NORMAL;   /* swingcfg Player1SwingType 2 [DATA] */
    if (!serving && focus) {
        if (human) focus->armed = focus_held;                      /* FUN_800ac254 (+0x4bb4 == 0) */
        else focus->armed = focus->soft = 0;                       /* FUN_8009c6fc: FUN_800abcb0 */
        /* the ITZ armed on the swing-input frame is already in StartSwing's shot flags (+0x4c40):
         * live X lines, 26 / 26 human ITZ commits of captures 14-15 [RTT] */
        s->swing_type = swing_base_type(focus->armed, focus->soft);
    }
    s->done_type = serving;
    s->hit_zone = 1;               /* Forehand until select_zone picks the swing set (rtt_match.cpp) */
    aim_target_reset_swing(&s->aim, wobble);
    if (table_err_mag > 0.0f && rng) {                              /* FUN_800aaab8, CPU only */
        float m = table_err_mag * rng_float(rng);
        s->table_err = v3(rng_float(rng) < 0.5f ? -m : m, 0.0f, rng_float(rng) < 0.5f ? -m : m);
    }
}

void shot_tick(ShotState* s, const ShotCharacter* c, const ShotContext* x, const ShotFrameInput* in,
               float* pace) {
    /* SwingType: classify until it finalizes, then the Slam-zone swap */
    if (!s->done_type) {
        float hf = s->ch.hit_force;
        int fin;
        if (s->human) {
            SwingHumanInput h;
            h.now = x->now; h.dt = x->dt; h.gesture = in->gesture;
            h.soft_button = in->soft_button; h.power_button = in->power_button;
            h.hit_ready = 1;
            h.itz_level_active = focus_level_active(x);
            h.itz_meter = x->focus ? x->focus->value / FOCUS_MAX : 0.0f;
            fin = swing_classify_human(&s->cls, &s->swing_type, &hf, &s->flags, &h);
        } else {
            fin = swing_classify_ai(&s->cls, &s->swing_type, &hf, &s->flags, x->now, x->dt, in->ai_force,
                                    focus_level_active(x), x->focus ? x->focus->value / FOCUS_MAX : 0.0f);
        }
        if (fin) s->done_type = 1;
    }
    int type = s->serving ? s->swing_type : swing_slam_type(s->swing_type, s->hit_zone);
    int flat_flag = s->hit_zone == 6 || s->hit_zone == 14;          /* player +0x4c46: OTT zones */
    if (in->spin_in[2] > 0.0f) s->latched[0] = 1.0f;
    if (in->spin_in[3] > 0.0f) s->latched[1] = 1.0f;
    if (in->spin_in[0] > 0.0f) s->latched[2] = 1.0f;
    if (in->spin_in[1] > 0.0f) s->latched[3] = 1.0f;
    int charging = s->latched[0] || s->latched[1] || s->latched[2] || s->latched[3];
    int set = attr_set(type);
    int lvl2 = x->full && x->full->active[x->player][2];          /* FUN_80119cf4(gd, p, 2) */
    float sp[4];                                                   /* player +0x4c3a zeroes the charge input */
    for (int i = 0; i < 4; i++) sp[i] = s->stretched ? 0.0f : in->spin_in[i];

    if (s->serving) {
        /* timed branch: SpinAxis set from the serve meter (human) or the truncated timer (CPU) */
        float pct = s->human ? in->serve_meter : (float)(int)in->serve_timer;
        s->ch.spin_axis = swing_timed_spin_axis(in->spin_in, pct, 1.0f);
    } else {
        SwingTickInput t;
        memset(&t, 0, sizeof(t));
        for (int i = 0; i < 4; i++) t.in[i] = sp[i];
        t.attr = &c->attr[set];
        t.zone = &c->zone[s->hit_zone];
        t.mult = 1.0f;
        t.max_charge_time = c->attribs.max_charge_time[set];
        t.charge_rate = c->attribs.max_charge_rate[set];
        t.dt = x->dt;
        t.swing_type = type;
        t.zone_snap = s->hit_zone == 4 || s->hit_zone == 12;
        int listed = s->hit_zone == 2 || s->hit_zone == 3 || s->hit_zone == 8 || s->hit_zone == 10 ||
                     s->hit_zone == 11 || s->hit_zone == 16;
        t.charge_allowed = x->control_mode >= 2 || (charging && !listed);   /* FUN_8009ddfc */
        t.no_charge_flag = flat_flag;
        t.flat = (lvl2 && !flat_flag) || s->flags.flat;
        t.meter = x->focus ? x->focus->value / FOCUS_MAX : 0.0f;
        t.itz_full = x->intensity == 1.0f;
        swing_charge_tick(&s->ch, &t);

        float sum = sp[0] + sp[1] + sp[2] + sp[3];
        *pace = swing_pace_tick(*pace, s->ch.force, t.zone_snap, sum > 0.0f ? sp[2] / sum : 0.0f, lvl2,
                                s->kick_pitch, flat_flag);

        /* the Focus meter half of the tick */
        if (x->focus && x->full) {
            FocusSwingInput fi;
            fi.charge_allowed = t.charge_allowed;
            fi.flat = s->flags.flat;
            fi.point_over = x->point_over;
            fi.intensity = x->intensity;
            fi.momentum = c->momentum;
            fi.charge_delta = c->attribs.meter_charge_delta;
            fi.dt_game = x->dt;
            fi.dt_real = x->dt_real > 0.0f ? x->dt_real : x->dt;
            fi.serve_into_net = x->serve_into_net;
            focus_swing_frame(x->focus, x->full, x->player, &fi);
            s->focus_charge = focus_shot_charge(x->focus, x->full, x->player);
        }

        /* incoming-spin kick (kick__AccumulateIncomingSpin) */
        KickInput k;
        memset(&k, 0, sizeof(k));
        k.have_prediction = x->have_prediction;
        k.in_spin = x->in_spin;
        if (x->in_char) {
            const SwingAttrSet* ha = &x->in_char->attr[0];
            k.spin_attr = kick_spin_attr(x->in_spin, x->in_vel, x->camera_flip, ha->top, ha->back, ha->side);
            k.max_rps = x->in_char->attribs.max_spin_rps[0][dominant_axis(x->in_spin)];   /* level 9 [OPEN] */
        }
        if (k.max_rps <= 0.0f) k.max_rps = 50.0f;
        HitSpinInput hs;
        memset(&hs, 0, sizeof(hs));
        hs.spin_axis = s->ch.spin_axis; hs.ball_pos = x->ball_pos; hs.target = s->world_tgt; hs.swing_type = type;
        hs.flip_side = x->dest_flip;
        Vec3 own = swing_hit_spin(&c->attribs, &hs);
        k.own_spin_x = own.x; k.own_spin_y = own.y;
        k.charging = charging;
        KickPushInput kp;
        kp.in_speed = x->in_speed; kp.in_pace = x->in_pace;
        kp.own_speed = s->post_hit_speed; kp.own_pace = *pace;
        kp.player_z = x->player_z;
        kp.base_z = s->side == 0 ? 1.0f : -1.0f;
        kp.side = s->side;
        kp.table_length = 2.0f * x->table->half_length;
        kp.box_gap = 0.6f; kp.box_depth = 3.4f;                    /* gdGameData +0x88 / +0x84 (live) */
        kp.force_short = s->stretched || stretch_zone(x->human, s->hit_zone);
        k.push = kick_depth_push(&kp);
        k.side = s->side;
        k.in_speed = x->in_speed;
        k.soft = type == SWING_TYPE_SOFT;
        k.flat = flat_flag;
        k.force_short = s->stretched;
        k.reduced = (s->flags.flat && x->focus && x->focus->value > 0.0f) || lvl2;
        s->kick_vec = kick_tick(&s->kick, &c->target_coef, &k, s->kick_vec.y, &s->kick_pitch);

        /* PostHitSpeed sees the target before this frame's aim step */
        ShotSpeedInput sp;
        sp.ball_pos = x->ball_pos; sp.target = s->world_tgt; sp.hit_force = s->ch.hit_force;
        sp.spin_x = s->ch.spin_axis.x; sp.spin_side = s->ch.spin_axis.y;
        sp.swing_type = type; sp.table_top = x->table->top_y;
        s->post_hit_speed = swing_post_hit_speed(&sp);
    }

    /* aim: spin nudge, then the grid tick -> TableTgtPos -> WorldTgtPos */
    int soft = type == SWING_TYPE_SOFT;
    /* FUN_800a9e90: a human's inputs at half weight; the x sign follows the camera for a human
     * (screen-relative) and the grid side for the CPU (the extra case of FUN_8009eb54 is dead:
     * gdGameData +0x14d is never set) */
    float nk = s->human ? 0.5f : 1.0f;                             /* DAT_806de340 */
    int xs = s->human ? (x->camera_flip ? -1 : 1) : (s->aim.grid[soft ? 1 : 0].side ? 1 : -1);
    aim_spin_nudge(&s->aim.grid[soft ? 1 : 0], x->tcfg, nk * in->spin_in[0], nk * in->spin_in[1],
                   nk * in->spin_in[2], nk * in->spin_in[3], s->serving, xs);
    AimTickInput a;
    memset(&a, 0, sizeof(a));
    a.dir = in->aim_dir;
    a.aim_mag = in->aim_mag;
    a.approach = c->attr[set].accuracy * s->ch.accuracy;            /* attribs +0x20 x player +0x1844 */
    a.kick = s->kick_vec;
    a.ball_z = x->ball_pos.z;
    a.prev_world_tgt = s->world_tgt;
    a.soft = soft;
    a.serving = s->serving;
    a.serve_edges = s->serving;
    a.edge_check = !x->point_over || s->serving;
    a.force_short = s->stretched && stretch_zone(x->human, s->hit_zone);
    a.dt = x->dt;
    s->table_tgt = aim_target_tick(&s->aim, x->wobble, x->tcfg, &a, &s->kick_pitch);
    s->world_tgt = v3_add(s->table_tgt, s->table_err);
}

void shot_contact(ShotState* s, const ShotCharacter* c, const ShotContext* x, const ShotFrameInput* in,
                  float* pace, ShotLaunch* out) {
    memset(out, 0, sizeof(*out));
    if (!s->serving && x->focus && x->full)                        /* FUN_8009cdf4 */
        s->stretched = focus_launch_stretch(x->focus, x->full, x->player, !x->human, s->hit_zone,
                                            s->swing_type == SWING_TYPE_SOFT, x->opp_level);
    Vec3 serve_axis = s->ch.spin_axis;
    shot_tick(s, c, x, in, pace);                                  /* LaunchSwing's extra tick */
    /* on the serve's launch frame the timed SpinAxis is not recomputed: RTT launches the previous
     * frame's axis (capture 10: 8/8 CPU serves, the timer two ticks back) */
    if (s->serving) s->ch.spin_axis = serve_axis;
    int type = s->serving ? s->swing_type : swing_slam_type(s->swing_type, s->hit_zone);
    out->is_serve = s->serving;
    out->pos = x->ball_pos;

    if (s->serving && s->human && in->swing_strength <= 0.0f) {
        out->whiff = 1;                                            /* ReachAnticipation's pop-up */
        out->vel = out->spin = ball_serve_whiff_vel(x->camera_flip);
        out->swing_type = type;
        out->restitution = 1.0f;
        s->active = 0;
        return;
    }

    /* hard-swing applier FUN_8009a734: spin, pace and HitForce scaled, then N more aim steps */
    ShotTier tier = swing_classify(x->cfg, in->swing_strength);
    if (tier < SHOT_NORMAL) tier = SHOT_NORMAL;
    out->tier = tier;
    if (tier >= SHOT_HARD) {
        float m = x->cfg->spin_mult[tier];
        Vec3 v = v3(s->ch.spin_axis.x * m, s->ch.spin_axis.y * m, 0.0f);
        float l2 = v3_dot(v, v);
        if (l2 > 1.0f) v = v3_scale(v, 1.0f / sqrtf(l2));
        s->ch.spin_axis = v;
        *pace = rtt_clampf(*pace + *pace * x->cfg->pace_mult[tier], 0.0f, tier == SHOT_REALLY_HARD ? 1.2f : 1.0f);
        s->ch.hit_force = swing_contact_hit_force(x->cfg, s->ch.hit_force, tier);
    }
    for (int i = 0; i < x->cfg->aim_sampling[tier]; i++) {
        AimTickInput a;
        memset(&a, 0, sizeof(a));
        a.dir = in->aim_dir; a.aim_mag = in->aim_mag;
        a.approach = c->attr[attr_set(type)].accuracy * s->ch.accuracy;
        a.kick = s->kick_vec; a.ball_z = x->ball_pos.z; a.prev_world_tgt = s->world_tgt;
        a.soft = type == SWING_TYPE_SOFT; a.serving = s->serving; a.serve_edges = s->serving;
        a.edge_check = !x->point_over || s->serving;
        a.force_short = s->stretched && stretch_zone(x->human, s->hit_zone);
        a.dt = x->dt;
        s->table_tgt = aim_target_tick(&s->aim, x->wobble, x->tcfg, &a, &s->kick_pitch);
        s->world_tgt = v3_add(s->table_tgt, s->table_err);
    }

    /* launch spin (FUN_8011bd78) */
    HitSpinInput hs;
    memset(&hs, 0, sizeof(hs));
    hs.spin_axis = s->ch.spin_axis; hs.ball_pos = x->ball_pos; hs.target = s->world_tgt;
    hs.swing_type = type; hs.is_serve = s->serving;
    hs.flip_side = x->dest_flip;
    out->spin = swing_hit_spin(&c->attribs, &hs);
    out->swing_type = type;

    if (s->serving) {
        ServeSolveInput si;
        si.from = x->ball_pos; si.target = s->world_tgt; si.spin = out->spin;
        si.meter = in->serve_meter;
        si.serve_t = in->serve_timer / 100.0f;
        si.swing_type = type;
        si.max_elev = HIT_MAX_ELEVATION;
        si.net_height = x->net_height;
        si.net_margin = x->net_margin;
        ServeSolveResult r;
        ball_solve_serve(x->bp, x->table, &si, &r);
        if (r.net_target) {
            s->table_tgt = r.net_target_pos;
            s->world_tgt = v3_add(r.net_target_pos, s->table_err);
            s->swing_type = type = 2;
            out->swing_type = 2;
        }
        out->vel = r.vel;
        out->elev = r.elev;
        float a = 0.0f;                                            /* player +0x4c0c (FUN_80096cc0) */
        for (int i = 0; i < 4; i++) if (s->latched[i]) { a = 1.0f - c->serve_spin_attr[i]; break; }
        out->game_pace = serve_game_pace(in->serve_meter, a, in->serve_timer, type, 0.0f);
        out->restitution = 1.0f;
    } else {
        HitAimInput hi;
        hi.ball_pos = x->ball_pos; hi.target = s->world_tgt; hi.spin = out->spin;
        hi.speed = s->post_hit_speed; hi.max_elev = HIT_MAX_ELEVATION;
        hi.swing_type = type; hi.kick_pitch = s->kick_pitch; hi.far_extra = x->far_extra;
        out->vel = ball_solve_hit_aim(x->bp, x->table, &hi, &out->elev);
        int level = x->focus ? x->focus->level : 0;                /* Focus meter level +0x2c */
        float bonus = x->full ? focus_pace_bonus(x->full, x->player, level) : 0.1f;
        out->game_pace = rtt_clampf(*pace + bonus, 0.0f, 1.0f);
        out->restitution = ball_hit_elasticity(type, 0, s->kick_pitch, s->focus_charge, level);
        if (x->focus) focus_launch(x->focus, x->point_over);
    }
    s->active = 0;
}

/* ---------------------------------------------------------------- shipped data */
void shot_character_sweden(ShotCharacter* c) {
    memset(c, 0, sizeof(*c));
    PlayerAttribs* a = &c->attribs;
    static const float rps[4][3] = { { 50, 30, 20 }, { 100, 40, 40 }, { 100, 50, 55 }, { 15, 10, 25 } };
    memcpy(a->max_spin_rps, rps, sizeof(rps));
    a->max_charge_time[0] = 0.57f; a->max_charge_time[1] = 1.0f; a->max_charge_time[2] = 0.75f;
    a->max_charge_rate[0] = 1552.798828f; a->max_charge_rate[1] = 1500.0f; a->max_charge_rate[2] = 2000.0f;
    a->max_move_speed = 4.0f; a->max_move_accel = 0.0f; a->max_srv_move_speed = 1.1f;
    a->tgt_error_x = 0.1f; a->tgt_error_z = 0.05f;
    a->meter_charge_delta = 20.0f;
    /* gdPlyrAttr: TopSpin, BackSpin, Strength, Sidespin, Accuracy (normal / ITZ / MaxITZ) */
    SwingAttrSet n = { 0.899992f, 0.099992f, 1.0f, 0.199992f, 0.9f };
    SwingAttrSet i = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    c->attr[0] = n; c->attr[1] = i; c->attr[2] = i;
    /* powerZoneData: Strength, VertSpin, SideSpin, Accuracy, SpinDef, SpeedDef */
    static const PowerZone z[HIT_ZONE_COUNT] = {
        { 0.65f, 0.75f, 0.75f, 1.0f, 0.55f, 0.2f },   /* Push */
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },       /* Forehand */
        { 0.4f, 0.0f, 0.0f, 1.0f, 0.2f, 0.2f },       /* Forehand Lunge */
        { 0.4f, 0.0f, 0.0f, 1.0f, 0.2f, 0.2f },       /* Forehand Lunge Far */
        { 1.0f, 0.0f, 0.0f, 0.9f, 0.2f, 0.0f },       /* Forehand Smash */
        { 0.7f, 0.1f, 0.8f, 0.9f, 1.0f, 0.9f },       /* Forehand Scoop */
        { 0.0f, 0.02f, 0.02f, 1.0f, 0.0f, 0.0f },     /* Forehand OTT */
        { 0.4f, 0.1f, 0.1f, 0.9f, 0.3f, 0.0f },       /* Forehand SOT */
        { 0.4f, 0.0f, 0.0f, 0.8f, 0.2f, 0.3f },       /* Forehand JIT */
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },       /* Backhand */
        { 0.4f, 0.0f, 0.0f, 1.0f, 0.2f, 0.2f },       /* Backhand Lunge */
        { 0.4f, 0.0f, 0.0f, 1.0f, 0.2f, 0.2f },       /* Backhand Lunge Far */
        { 1.0f, 0.0f, 0.0f, 0.9f, 0.2f, 0.0f },       /* Backhand Smash */
        { 0.7f, 0.1f, 0.8f, 0.9f, 1.0f, 0.9f },       /* Backhand Scoop */
        { 0.0f, 0.02f, 0.02f, 1.0f, 0.0f, 0.0f },     /* Backhand OTT */
        { 0.4f, 0.1f, 0.1f, 0.9f, 0.3f, 0.0f },       /* Backhand SOT */
        { 0.4f, 0.0f, 0.0f, 0.8f, 0.2f, 0.3f },       /* Backhand JIT */
        { 0.65f, 0.75f, 0.75f, 1.0f, 0.2f, 0.2f },    /* SoftPush */
        { 0.65f, 0.75f, 0.75f, 1.0f, 0.15f, 0.2f },   /* SoftMid */
    };
    memcpy(c->zone, z, sizeof(z));
    for (int k = 0; k < 4; k++) c->serve_spin_attr[k] = 0.3f;   /* ServeTop/Back/Left/RightSpin */
    c->momentum = 0.075f;                                          /* Momentum */
    c->target_coef.vert_spin_coef = 0.022f;
    c->target_coef.side_spin_coef = 0.022f;
}

void swing_config_default(SwingConfig* c) {
    memset(c, 0, sizeof(*c));
    c->min_early_out = 0.7f; c->max_early_out = 1.8f; c->soft_shot_mult = 1.5f;
    c->human_spin_charge = 1.0f; c->ai_spin_charge = 0.5f;
    c->serve_toss_threshold = 1.0f; c->serve_toss_time = 1.25f;
    c->dpad_targetting_speed = 0.05f;
    c->min_swing = 1.4f; c->hard_shot = 3.4f; c->really_hard_shot = 4.1f;
    c->aim_sampling[0] = 15; c->aim_sampling[1] = 25; c->aim_sampling[2] = 40;
    c->pace_mult[0] = 1.0f; c->pace_mult[1] = 1.5f; c->pace_mult[2] = 2.0f;
    c->force_mult[0] = 1.0f; c->force_mult[1] = 1.5f; c->force_mult[2] = 2.0f;
    c->spin_mult[0] = 1.0f; c->spin_mult[1] = 1.5f; c->spin_mult[2] = 2.0f;
    c->aim_e = 0.2f; c->aim_w = -0.2f; c->aim_ne = -0.15f; c->aim_se = 0.1f; c->aim_nw = -0.15f; c->aim_sw = 0.1f;
}

void table_target_cfg_default(TableTargetCfg* t) {
    static const TableTargetCfg d = { 1.5224f, 0.763f, 2.744f,
        { 0.489f, 0.325f, 0.481f, 0.489f, 0.097f, 0.482f, 0.306f, 0.200f, 0.150f } };
    *t = d;
}
