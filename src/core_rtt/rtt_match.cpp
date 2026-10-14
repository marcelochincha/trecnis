#include "rtt_match.h"

#include <stdio.h>

/* ---------------------------------------------------------------- bus subscribers */
static void referee_sub(void* user, const Event* e) {
    World* w = (World*)user;
    if (e->id <= EV_BALL_FELL_THROUGH_FLOOR || e->id == EV_POINT_TIMEOUT) {
        RallyState before = w->score.state;
        int says_out = (w->path.outcome & (OUTCOME_KNOWN | OUTCOME_FAULT)) == (OUTCOME_KNOWN | OUTCOME_FAULT);
        rules_on_ball_event(&w->score, &w->settings, e, says_out, &w->bus);
        if (before != RS_POINT_SCORED && w->score.state == RS_POINT_SCORED)
            w->post_point_timer = 1.5f;           /* SetPostPointDelay values not read [OPEN] */
        if (w->score.state == RS_LET) w->post_point_timer = 1.0f;
    }
}

/* pongPlayer::Process, the Focus meter's messages [RTT] */
static void focus_sub(void* user, const Event* e) {
    World* w = (World*)user;
    switch (e->id) {
    case EV_GAME_START:                           /* 0x841: FUN_800abbf4 for both players; the meter
                                                     and its level carry over from point to point */
        for (int i = 0; i < RTT_MAX_PLAYERS; i++) focus_reset(&w->players[i].focus, &w->full);
        break;
    case EV_POINT_START:                          /* 0x83b, posted before the serve */
        for (int i = 0; i < RTT_MAX_PLAYERS; i++) w->players[i].point_over = 0;
        break;
    case EV_BALL_BOUNCE_TABLE_A:                  /* msg 1 / 2: the striker's shot landed */
    case EV_BALL_BOUNCE_TABLE_B: {
        int i = e->id == EV_BALL_BOUNCE_TABLE_A ? 1 : 0;
        if (w->last_hitter == i) focus_on_bounce(&w->players[i].focus);
        break;
    }
    case EV_FAULT:                                /* 0x824 */
        for (int i = 0; i < RTT_MAX_PLAYERS; i++) {
            if (e->i[0] == 1 && i == w->last_hitter) focus_on_fault(&w->players[i].focus);
            else focus_animate(&w->players[i].focus);
        }
        break;
    case EV_POINT_A:                              /* 0x82d / 0x82e */
    case EV_POINT_B:
        for (int i = 0; i < RTT_MAX_PLAYERS; i++) {
            int lost = (e->id == EV_POINT_A) == (i == 1);
            focus_on_point(&w->players[i].focus, &w->full, i, lost);
            w->players[i].point_over = 1;
        }
        break;
    default:
        break;
    }
}

/* announcer: builds the cue name at runtime and resolves it by name, as RTT does [RTT] */
static void announcer_sub(void* user, const Event* e) {
    World* w = (World*)user;
    if (!w->audio) return;
    char name[RTT_NAME_LEN];
    name[0] = 0;
    switch (e->id) {
    case EV_POINT_A: case EV_POINT_B:
        snprintf(name, sizeof(name), "Announcer_Point_%s", w->players[e->id == EV_POINT_A ? 0 : 1].announcer_name); break;
    case EV_DEUCE:          snprintf(name, sizeof(name), "DEUCE"); break;
    case EV_GAME_POINT_A: case EV_GAME_POINT_B:   snprintf(name, sizeof(name), "GAMEPOINT"); break;
    case EV_MATCH_POINT_A: case EV_MATCH_POINT_B: snprintf(name, sizeof(name), "MATCHPOINT"); break;
    case EV_POINT_START:
        snprintf(name, sizeof(name), "Announcer_%dserving%d",
                 w->score.points[w->score.server == SIDE_A ? 0 : 1],
                 w->score.points[w->score.server == SIDE_A ? 1 : 0]); break;
    default: return;
    }
    audio_play_named(w->audio, name, 0.0f, &w->rng);
}

/* ---------------------------------------------------------------- setup */
void world_init(World* w, const MatchSettings* s) {
    memset(w, 0, sizeof(*w));
    ev_init(&w->bus);
    ball_default_params(&w->ball_params);
    table_default_geom(&w->table);
    swing_config_default(&w->swing_cfg);
    table_target_cfg_default(&w->tcfg);
    shot_character_sweden(&w->default_chr);
    swing_data_sweden(&w->default_swings);
    w->settings = *s;
    w->rng.s = 12345u;
    w->players[0].side = SIDE_A;
    w->players[1].side = SIDE_B;
    for (int i = 0; i < RTT_MAX_PLAYERS; i++) {
        w->players[i].chr = &w->default_chr;
        w->players[i].swings = &w->default_swings;
        w->players[i].control_mode = 2;           /* re-set to 0 for a CPU by the caller [live check] */
    }
    w->last_hitter = -1;
    ev_subscribe(&w->bus, EV_COUNT_SENTINEL, referee_sub, w);
    ev_subscribe(&w->bus, EV_COUNT_SENTINEL, focus_sub, w);
    ev_subscribe(&w->bus, EV_COUNT_SENTINEL, announcer_sub, w);
    if (w->camera) ev_subscribe(&w->bus, EV_COUNT_SENTINEL, camera_on_event, w->camera);
    rules_start_match(&w->score, &w->settings, &w->bus);
    rules_start_point(&w->score, &w->bus);
    w->last_state = (RallyState)-1;
}

/* ---------------------------------------------------------------- shot pipeline */
/* SIDE_A stands on +z and plays toward -z: its target grid is side 1 (area on the -z half) */
static int grid_side(const Player* p) { return p->side == SIDE_A ? 1 : 0; }

static void shot_context(World* w, Player* p, Vec3 ball_pos, int idx, ShotContext* x) {
    memset(x, 0, sizeof(*x));
    x->bp = &w->ball_params;
    x->table = &w->table;
    x->tcfg = &w->tcfg;
    x->cfg = &w->swing_cfg;
    x->now = w->time;
    x->dt = RTT_DT;
    x->wobble = &w->wobble;
    /* the two camera flips follow the human's end (capture 10, both sets): flip byte 1 and
     * destination +0x30 0 while the human plays +z, the reverse on -z. With no human the view
     * is taken from player 0 [CHOICE] */
    const Player* viewer = !w->players[0].is_ai || w->players[1].is_ai ? &w->players[0] : &w->players[1];
    x->camera_flip = viewer->side == SIDE_A;
    x->dest_flip = !x->camera_flip;
    x->human = !p->is_ai;
    x->control_mode = p->is_ai ? 0 : p->control_mode;
    int pi = p == &w->players[0] ? 0 : 1;
    x->focus = &p->focus;
    x->full = &w->full;
    x->player = pi;
    x->opp_level = w->players[1 - pi].focus.level;
    x->intensity = p->intensity;
    x->point_over = p->point_over;
    x->dt_real = RTT_DT;
    x->serve_into_net = 0;
    x->ball_pos = ball_pos;
    x->player_z = p->root.z;
    x->far_extra = 3.4f + 0.6f + w->table.half_length;            /* gdGameData +0x84 / +0x88 (live) */
    x->net_height = 0.147f;                                        /* DAT_806db760 (live) */
    x->net_margin = 0.05f;                                         /* table cfg net +0x80 (live) */
    if (idx >= 0) {
        const Player* o = &w->players[p == &w->players[0] ? 1 : 0];
        x->have_prediction = w->path.count >= 2;
        x->in_spin = w->path.s[idx].spin;
        x->in_vel = w->path.s[idx].vel;
        x->in_speed = o->shot.post_hit_speed;
        x->in_pace = o->pace;
        x->in_char = o->chr;
    }
}

/* the CPU's per-frame swing inputs. Which inputs a CPU presses is the AI topic (gdai shot
 * selection, not ported): these are stand-ins on top of ai_decide's outputs. [CHOICE] */
static void ai_shot_input(World* w, Player* p, int serving, ShotFrameInput* f) {
    memset(f, 0, sizeof(*f));
    float k = w->swing_cfg.ai_spin_charge;                         /* AiSpinChargeFactor 0.5 [DATA] */
    if (p->ai.out_charge > 0.2f) f->spin_in[2] = k;                /* top */
    else if (p->ai.out_charge < -0.2f) f->spin_in[3] = k;          /* back */
    f->aim_dir = p->ai.out_aim > 0.25f ? 7 : (p->ai.out_aim < -0.25f ? 3 : 0);
    f->aim_mag = 1.0f;
    f->ai_force = 0.5f;
    f->swing_strength = p->ai.kill_shot ? 3.6f : 2.0f;             /* normal, hard on a kill shot */
    if (serving) {
        f->serve_meter = 60.0f + 40.0f * rng_float(&w->rng);
        f->serve_timer = 77.0f;
    }
}

/* ---------------------------------------------------------------- HitZone selection */
/* the player frame (FacingDownNegZ: the player on +z faces -z as is, the one on -z is turned by pi;
 * FUN_800ed9d0 multiplies by the inverse of the player matrix) */
static Vec3 to_local(const Player* p, Vec3 wpos) {
    Vec3 d = v3_sub(wpos, p->root);
    return p->side == SIDE_A ? d : v3(-d.x, d.y, -d.z);
}

static Vec3 to_world(const Player* p, Vec3 l) {
    Vec3 d = p->side == SIDE_A ? l : v3(-l.x, l.y, -l.z);
    return v3_add(p->root, d);
}

static const Player* g_vol_player;
static const World* g_vol_world;
static int in_table_volume(Vec3 c) {                              /* FUN_800ee1d4 */
    return swing_contact_in_table_volume(c, g_vol_player->root, 2.0f * g_vol_world->table.half_length,
                                         g_vol_world->ball_params.radius);
}

/* pcrSwingSolver__SelectAimTarget for the hitter's coming ball: the hittable run of the prediction
 * in the player frame (FUN_800ed9d0), the outcome masks 4 / 0x10 from the predicted path, the
 * solver from the shot flags (ts / bs / *_itz / sft, with the ITZ / soft fallback), every eligible
 * set ranked (map/gameplay.md HitZone selection). Returns 1 with a target. */
static int select_zone(World* w, Player* p, const PlayerInput* pi, ZoneTarget* out) {
    const BallPath* path = &w->path;
    static LocalBallSample ls[RTT_BALL_PATH_SLOTS];
    /* the live ring: pongLerpQueue__PopExpiredSamples drops the head while the next sample has
     * passed, so the ring starts at the last sample with t <= now (live X lines s0) [RTT] */
    int head = 0;
    for (int i = 0; i + 1 < path->count && path->s[i + 1].t <= path->clock; i++) head = i + 1;
    /* FUN_800d17e8: time of the ring's first hittable, non-bounce sample (the aim-assist reach) */
    float t_first = -1.0f;
    for (int i = head; i < path->count; i++)
        if (path->s[i].hittable && !path->s[i].bounced) { t_first = path->s[i].t; break; }
    /* pcrSwingSolver__CopyPathToPlayerFrame: the window = FindFirstHittableSample ..
     * FindFirstNonHittableSample of the ring, "hittable" = +0x0a set and not a bounce (+0x08) [RTT] */
    int first = -1, end = path->count;
    for (int i = head; i < path->count; i++)
        if (path->s[i].hittable && !path->s[i].bounced) { first = i; break; }
    if (first < 0 || !p->swings) return 0;
    for (int i = first; i < path->count; i++)
        if (!(path->s[i].hittable && !path->s[i].bounced)) { end = i; break; }
    /* the predicted outcome: a fault (out / net) walks the abort sets -- the receiver lets it go,
     * no HitZone (pcrSwingSolver__SelectAimTarget: known && FUN_80120f8c -> +0x160) [RTT] */
    if ((path->outcome & (OUTCOME_KNOWN | OUTCOME_FAULT)) == (OUTCOME_KNOWN | OUTCOME_FAULT)) return 0;
    /* masks 4 (bounces again) and 0x10 (drops near the table) are the outcome bits, read only while
     * the outcome is known (FUN_800d1e8c / FUN_800d1f10; set by ExtrapolateUnknownOutcome) [RTT] */
    int known = (path->outcome & OUTCOME_KNOWN) != 0;
    int short_ball = known && (path->outcome & OUTCOME_BOUNCES_AGAIN);
    int near_table = known && (path->outcome & OUTCOME_NEAR_TABLE);
    /* the window moves to its first descending sample on the raw bits 4 / 0x10 (FUN_80120f9c /
     * FUN_80120fac, no known test there; vel.y <= DAT_806dedc0 = 0) [RTT] */
    if (path->outcome & (OUTCOME_BOUNCES_AGAIN | OUTCOME_NEAR_TABLE))
        for (int i = first; i < end; i++) if (path->s[i].vel.y <= 0.0f) { first = i; break; }
    int n = 0;
    for (int i = first; i < end; i++) {
        ls[n].pos = to_local(p, path->s[i].pos);
        ls[n].t = path->s[i].t;
        ls[n].hittable = 1;
        n++;
    }
    if (n < 2) return 0;

    ShotState* s = &p->shot;
    ZoneSelectInput in;
    memset(&in, 0, sizeof(in));
    in.s = ls; in.n = n;
    in.now = path->clock;
    in.t_first_hittable = t_first >= 0.0f ? t_first : ls[0].t;
    in.short_ball = short_ball;
    in.near_table = near_table;
    float m = p->root.z >= 0.0f ? 1.0f : -1.0f;                    /* side byte gd+0x40+p */
    in.reach = v3(p->root.x * m, p->root.y, p->root.z * m);
    in.sot_limit = 0.5f * w->table.half_width * 2.0f;              /* DAT_806dea44 * table +8 */
    /* reach box FUN_8011a5c4: x +-0.5 * gd+0x80 (5.4), z from the table end + gd+0x88 (0.6) for
     * gd+0x84 (3.4) on the player's half, then into the player frame */
    float z0 = w->table.half_length + 0.6f, z1 = z0 + 3.4f;
    Vec3 a = v3(-2.7f, 0.0f, m * z0), b = v3(2.7f, 0.0f, m * z1);
    Vec3 la = to_local(p, a), lb = to_local(p, b);
    in.mover_min = v3(fminf(la.x, lb.x), 0.0f, fminf(la.z, lb.z));
    in.mover_max = v3(fmaxf(la.x, lb.x), 0.0f, fmaxf(la.z, lb.z));
    in.fudge = p->swings->fudge;
    g_vol_player = p; g_vol_world = w;
    in.in_table_volume = in_table_volume;
    in.per_set = 1;
    in.human = !p->is_ai;
    /* SoftShotMultiplier when the INCOMING shot is soft: FUN_800d0880 reads SwingType of the last
     * striker's hit object (FUN_80045934 -> obj +0xb4) */
    const Player* o = &w->players[p == &w->players[0] ? 1 : 0];
    in.soft = o->shot.swing_type == SWING_TYPE_SOFT;
    in.swing_started = 1;            /* DAT_806db30d: StartSwing sets it before selecting, nothing clears it [RTT] */
    /* FUN_800ed5ec "near" = |DAT_806db6a0[p]| < 2.7 (DAT_806d5be4): DAT_806db6a0[p] is the player's
     * depth z (live F lines spd, +-1.97 at rest), so far = standing 2.7 m or more back [RTT] */
    in.far = fabsf(p->root.z) >= 2.7f;
    static EarlyOutCfg early;
    early.min_early_out = w->swing_cfg.min_early_out;
    early.max_early_out = w->swing_cfg.max_early_out;
    early.soft_shot_mult = w->swing_cfg.soft_shot_mult;
    in.early = &early;

    /* shot flags (player +0x4c40, FUN_8014b1ec) and the solver */
    unsigned flags = swing_shot_flags(s->swing_type >= 8, s->swing_type == SWING_TYPE_SOFT,
                                      s->latched[0] > 0.0f,
                                      s->latched[1] > 0.0f, s->latched[2] > 0.0f, s->latched[3] > 0.0f);
    int fb, fb_itz, fb_soft;
    int slot = swing_solver_slot(flags, &fb, &fb_itz, &fb_soft);
    CategoryInput ci;
    memset(&ci, 0, sizeof(ci));
    ci.all_active = p->is_ai || p->control_mode == 2 || p->control_mode == 4;   /* FUN_8009ead4 */
    ci.latch = s->cat_latch;
    ci.stick_x = pi ? pi->move_stick[0] : 0.0f;                    /* DAT_804ce830[p] */
    ci.stick_y = pi ? pi->move_stick[1] : 0.0f;
    ci.speed = p->root.z;                                          /* DAT_806db6a0[p], FUN_800ed5ec */
    /* gdBall__ResolveSwingAimTarget: the slot's solver first; for an ITZ or soft shot without a valid
     * target the ts / bs solver again with the fallback filter, kept when the first found nothing
     * or it ranks higher (CompareAimTargetPriority) */
    static SwingSetDef sets[2][64];
    ZoneTarget r[2];
    int found[2] = { 0, 0 };
    for (int pass = 0; pass < 2; pass++) {
        int sv = pass == 0 ? slot : fb;
        if (sv < 0 || (pass == 1 && found[0] && swing_target_valid(&r[0]))) break;
        int cnt = p->swings->count[sv];
        if (cnt > 64) cnt = 64;
        for (int i = 0; i < cnt; i++) {
            sets[pass][i] = p->swings->sets[sv][i];
            sets[pass][i].category_active = swing_category_active(sets[pass][i].category, &ci);
            sets[pass][i].specialty_blocked = swing_specialty_blocked(sets[pass][i].specialty,
                                                                      sets[pass][i].disable_if_specialty_fails, flags);
        }
        found[pass] = swing_select_set(sets[pass], cnt, &in, pass == 1 && fb_itz, pass == 1 && fb_soft, &r[pass]);
    }
    int k = 0;
    if (found[1] && (!found[0] || swing_target_better(&r[1], &r[0], in.now))) k = 1;
    if (!found[k]) return 0;
    *out = r[k];
    int cat = sets[k][out->set].category;
    if (swing_target_valid(out) && (cat == ACT_CAT_EXT_LEFT || cat == ACT_CAT_EXT_RIGHT))
        s->cat_latch[cat] = 1;                                     /* FUN_800ed178 */
    return 1;
}

/* contact: launch -> ball_predict -> paddle / serve event */
static void launch(World* w, Player* p, const ShotLaunch* L) {
    BallParams bp = w->ball_params;
    if (!L->is_serve) bp.table_restitution = L->restitution;       /* FUN_80047114 (hit Elasticity) */
    /* |PaddleVel| needs the chosen blend's PostHitPaddleVel (swing_paddle_vel); without swing sets
     * the live ratio stands in [CHOICE] */
    float paddle = L->paddle_speed > 0.0f ? L->paddle_speed : 1.6f * v3_len(L->vel);
    ball_predict(&w->path, &bp, &w->table, L->pos, L->vel, L->spin, L->swing_type, p->side, L->is_serve,
                 L->game_pace, paddle);
    w->last_hitter = p == &w->players[0] ? 0 : 1;
    p->point_over = 0;                                             /* msg 3 / 4 (contact) */
    if (L->is_serve) rules_on_serve_struck(&w->score);
    else             ev_post(&w->bus, p->side == SIDE_A ? EV_BALL_BOUNCE_PADDLE_A : EV_BALL_BOUNCE_PADDLE_B, p->side);
}

/* ---------------------------------------------------------------- frame */
void world_tick(World* w, const PlayerInput in[RTT_MAX_PLAYERS]) {
    float dt = RTT_DT;
    w->time += dt;

    /* a new point: grids back to their side, pace accumulators cleared (FUN_8011a258) [RTT] */
    if (w->score.state == RS_PRE_SERVE && w->last_state != RS_PRE_SERVE) {
        for (int i = 0; i < RTT_MAX_PLAYERS; i++) {
            Player* p = &w->players[i];
            shot_reset_point(&p->shot, grid_side(p), &w->wobble);
            p->shot.active = 0;
            p->pace = 0.0f;
            p->serve_toss = 0.0f;
        }
        /* gdShotDataMgr__SelectRallyClip: cut to this point's clip (player count + receiving
         * side), no blend [RTT] */
        if (w->camera) {
            int ai_count = w->players[0].is_ai + w->players[1].is_ai;
            camera_rally_start(w->camera, RTT_MAX_PLAYERS - ai_count, rtt_other(w->score.server));
        }
    }
    w->last_state = w->score.state;

    /* 1. intents: human input or AI decision [RTT order: input/AI before swing] */
    PlayerInput pin[RTT_MAX_PLAYERS];
    for (int i = 0; i < RTT_MAX_PLAYERS; i++) {
        Player* p = &w->players[i];
        pin[i] = in[i];
        if (p->is_ai) {
            int own = w->score.points[i], opp = w->score.points[1 - i];
            ai_decide(&p->ai, &w->ai_data, own, opp, p->shot.active, &w->rng);
            ai_shot_input(w, p, w->score.state == RS_PRE_SERVE, &pin[i].shot);
            pin[i].swing_pressed = p->ai.attempt_shot;
        }
    }

    /* Focus meter frame (FUN_800947d8 -> FUN_800abcc0) */
    for (int i = 0; i < RTT_MAX_PLAYERS; i++) {
        Player* p = &w->players[i];
        RallyState st = w->score.state;
        int live = !p->point_over && st != RS_PRE_SERVE && st != RS_POINT_SCORED && st != RS_LET &&
                   st != RS_MATCH_OVER;                            /* FUN_80097c9c */
        /* activation request +0x45 (FUN_80096ff0): a human's button; the CPU's AI tick
         * (FUN_800a149c) asks as soon as its meter is above 0.9999 while it is in a rally state
         * (player state 1 / 2; here: rally live and not serving) */
        if (p->is_ai) {
            int serving = st == RS_PRE_SERVE && i == (w->score.server == SIDE_A ? 0 : 1);
            if (live && !serving && p->focus.value / FOCUS_MAX > 0.9999f) p->focus.auto_request = 1;
        } else if (pin[i].focus_activate) {
            p->focus.auto_request = 1;
        }
        focus_frame(&p->focus, &w->full, i, w->auto_full_focus[i], live, 0, RTT_DT, w->time);
    }

    /* 2. serve / swing resolution */
    Player* server = &w->players[w->score.server == SIDE_A ? 0 : 1];
    int si = server == &w->players[0] ? 0 : 1;
    if (w->score.state == RS_PRE_SERVE) {
        /* toss position: in front of the server at the captured height [CHOICE until serve.md's
         * toss is wired]; the serve swing ticks while the ball is up */
        Vec3 toss = v3(server->root.x - 0.1f, 0.99f, server->side == SIDE_A ? 1.85f : -1.85f);
        if (!server->shot.active && (server->is_ai || pin[si].swing_pressed)) {
            float err = server->is_ai ? 0.8f * (1.0f - ai_curve(&w->ai_data, AIF_ACCURACY, server->ai.level, 0.0f)) : 0.0f;
            shot_begin(&server->shot, grid_side(server), 1, !server->is_ai, err, &w->rng, &w->wobble,
                       &server->focus, pin[si].shot.focus_held);
            server->serve_toss = 0.0f;
        }
        if (server->shot.active) {
            ShotContext x;
            shot_context(w, server, toss, -1, &x);
            server->serve_toss += dt;
            if (server->serve_toss < 0.5f) {                           /* toss time [CHOICE] */
                shot_tick(&server->shot, server->chr, &x, &pin[si].shot, &server->pace);
            } else {
                ShotLaunch L;
                shot_contact(&server->shot, server->chr, &x, &pin[si].shot, &server->pace, &L);
                launch(w, server, &L);
            }
        }
    } else if (w->score.state == RS_AWAIT_SERVE_RETURN || w->score.state == RS_AWAIT_RETURN ||
               w->score.state == RS_RETURN_HIT || w->score.state == RS_SERVE_HIT ||
               w->score.state == RS_SERVE_BOUNCE || w->score.state == RS_HIT_NET_SERVE) {
        /* the receiver prepares while the ball is still in flight: in the AWAIT states the striker
         * is the expected hitter, otherwise the one who just hit and the other player receives */
        int awaiting = w->score.state == RS_AWAIT_SERVE_RETURN || w->score.state == RS_AWAIT_RETURN;
        Side recv = awaiting ? w->score.striker : rtt_other(w->score.striker);
        Player* hitter = &w->players[recv == SIDE_A ? 0 : 1];
        int hi = hitter == &w->players[0] ? 0 : 1;
        int idx = ball_nearest_hittable(&w->path, hitter->root);
        ShotState* st = &hitter->shot;
        int window = 0;                                            /* a hittable sample still ahead */
        for (int i = 0; i < w->path.count && !window; i++)
            if (w->path.s[i].hittable && w->path.s[i].t > w->path.clock) window = 1;
        if (pin[hi].swing_pressed && window && !st->active) {
            float err = hitter->is_ai ? 0.8f * (1.0f - ai_curve(&w->ai_data, AIF_ACCURACY, hitter->ai.level, 0.0f)) : 0.0f;
            shot_begin(st, grid_side(hitter), 0, !hitter->is_ai, err, &w->rng, &w->wobble,
                       &hitter->focus, pin[hi].shot.focus_held);
        }
        if (st->active) {
            /* gdBall__ResolveSwingAimTarget every frame until the swing starts: the chosen set's
             * HitZone drives the charge rows; StartSwing once the target is valid and not "too
             * early"; the aim assist then bends the ball onto the blend's ContactPoint (AutoAimTick)
             * and contact is that sample's time [RTT] */
            if (!st->started) {
                ZoneTarget zt;
                int got = select_zone(w, hitter, &pin[hi], &zt);
                if (got) {
                    st->zone = zt;
                    st->hit_zone = zt.hit_zone;
                    /* start rule: a CPU or a human in control mode 2 / 4 starts at once
                     * (FUN_8009ead4); otherwise "too early" refuses and FUN_800ca0a8 delays until
                     * the ball is within the aim-assist reach or the time key reaches 0 */
                    int now_ok = hitter->is_ai || hitter->control_mode == 2 || hitter->control_mode == 4;
                    if (!now_ok) {
                        if (zt.flags & TGT_TOO_EARLY) now_ok = 0;
                        else if (zt.flags & TGT_FLAG8) now_ok = 1;
                        else if (!zt.sample_valid) now_ok = 0;
                        else if (zt.flags & TGT_FLAG4) now_ok = 1;
                        else now_ok = (zt.flags & TGT_IN_REACH) || zt.time_key <= 0.0f;
                    }
                    if (now_ok) {
                        st->started = 1;
                        st->t_start = w->path.clock;
                        st->t_contact = zt.t_sample;
                        int k = -1;
                        for (int i = 0; i < w->path.count; i++) if (w->path.s[i].t == zt.t_sample) { k = i; break; }
                        if (k >= 0 && zt.blend && !w->path.fudged) {
                            ball_fudge_to_contact(&w->path, k, to_world(hitter, zt.blend->contact_point),
                                                  zt.t_sample, w->path.clock, 1);
                            w->path.fudged = 1;
                        }
                    }
                }
            }
            int ci = -1;
            if (st->started) {
                for (int i = 0; i < w->path.count; i++) if (w->path.s[i].t >= st->t_contact) { ci = i; break; }
            } else {
                ci = idx;
            }
            if (ci >= 0) {
                ShotContext x;
                shot_context(w, hitter, w->path.s[ci].pos, ci, &x);
                if (st->started && !swing_target_valid(&st->zone)) {
                    /* a miss swing (pcrSwingMissSets): it plays, the ball is not struck */
                    if (w->path.clock > st->t_contact) st->active = 0;
                } else if (st->started && awaiting && w->path.clock + 0.5f * dt >= st->t_contact) {
                    ShotLaunch L;
                    shot_contact(st, hitter->chr, &x, &pin[hi].shot, &hitter->pace, &L);
                    if (st->zone.blend) {                              /* pcrSwingSolver__GetPaddleVel */
                        Vec3 pv = swing_paddle_vel(st->zone.blend->post_hit_paddle_vel, st->zone.time_key, 0,
                                                   st->zone.contact_frame, st->t_contact, st->t_start,
                                                   hitter->side == SIDE_A ? 1 : 0);
                        L.paddle_speed = v3_len(pv);
                    }
                    launch(w, hitter, &L);
                } else {
                    shot_tick(st, hitter->chr, &x, &pin[hi].shot, &hitter->pace);
                }
            } else if (!st->started && !window) {
                st->active = 0;                                    /* no set reached it: a miss */
            }
        }
    }

    /* 3. ball playback -> msgBall events */
    if (w->score.state != RS_PRE_SERVE && w->score.state != RS_POINT_SCORED && w->score.state != RS_LET) {
        float pz[2] = { fabsf(w->players[0].root.z), fabsf(w->players[1].root.z) };
        ball_play(&w->path, &w->ball_params, &w->table, pz, dt, &w->bus);
        ball_advance_ring(&w->path, &w->ball_params, &w->table);   /* FUN_8004ac10 [RTT] */
    }

    /* 4. deliver events: referee, announcer, camera, animation triggers */
    ev_dispatch(&w->bus);

    /* 5. post-point delay, then next point / game */
    if ((w->score.state == RS_POINT_SCORED || w->score.state == RS_LET) && w->post_point_timer > 0.0f) {
        w->post_point_timer -= dt;
        if (w->post_point_timer <= 0.0f) {
            rules_advance(&w->score, &w->settings, &w->bus);
            ev_dispatch(&w->bus);
        }
    }
    if (w->audio) w->audio->now = w->time;
    /* 6. presentation (outside the core): pose = swing/locomotion blend -> FK -> skin palette;
     *    camera; render. No look-at/IK in a RTT match (look-at optional). See rtt_anim.h. */
    if (w->camera) {
        Vec3 ball = w->path.count > 0 ? w->path.s[0].pos : v3(0, 0, 0);
        camera_update(w->camera, ball, w->players[0].root, w->players[1].root, dt);
    }
}
