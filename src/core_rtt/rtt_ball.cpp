#include "rtt_ball.h"

void ball_default_params(BallParams* p) {
    p->radius = 0.0200f;
    p->gravity_y = -9.8f;
    p->gravity_scale = (-9.8f + 0.9f * (-11.85f + 9.8f)) / -9.8f;   /* 1.18829 */
    p->magnus = 0.004f;
    p->drag = 0.01f;
    p->drag_special = 0.011f;
    p->table_restitution = 0.9f;
    p->table_friction = 0.21f;
    p->serve_gravity_scale = 1.0f;
    p->serve_restitution = 1.0f;
    p->pace_factor = 1.6f;
    p->pace_normal_bias = -0.8f;
    p->grip = 0.40f;
    p->slide_thresh = 0.16f;
    p->spin_coupling = 1.50f;
    p->separation = 0.002f;
    p->cast_skin = 0.001f;
    p->spin_cap_sq = 1.0e6f;
    p->net_restitution = 0.0f;
    p->net_friction = 10.0f;
}

void table_default_geom(TableGeom* g) {
    g->half_width = 0.7612f;
    g->half_length = 1.3720f;
    g->top_y = 0.763f;                   /* contact plane top + r = 0.783 (live 2026-10-10) */
    g->net_half_width = 0.886f;
    g->net_bottom_y = 0.755f;
    g->net_top_y = 0.915f;
    g->net_enabled = 1;
}

/* FUN_8011b488 / GetMagnusCoeff switch: only SwingType 10 takes the second slot (9 falls to the
 * default; live 2026-10-11) [RTT] */
static int is_special_shot(int shot_type) { return shot_type == 10; }

Vec3 ball_accel(const BallParams* p, Vec3 vel, Vec3 spin, int shot_type) {
    /* the optional spin-axis gate (config flag DAT_806d449c, off by default) is omitted [OPEN] */
    Vec3 a = v3_scale(v3_cross(spin, vel), p->magnus);
    float v2 = v3_dot(vel, vel);
    if (v2 > 1.0e-6f) {
        float kd = is_special_shot(shot_type) ? p->drag_special : p->drag;
        a = v3_sub(a, v3_scale(vel, kd * sqrtf(v2)));            /* quadratic drag [RTT] */
    }
    a.y += p->gravity_y * p->gravity_scale;
    return a;
}

void ball_bounce(const BallParams* p, float restitution, float friction, Vec3 n,
                 Vec3* pos, Vec3* vel, Vec3* spin) {
    Vec3 vc = v3_add(*vel, v3_cross(*spin, v3_scale(n, -p->radius)));  /* contact-point velocity */
    float vn = v3_dot(vc, n);
    if (vn < 0.0f) {                                              /* approaching */
        Vec3 vt = v3_sub(vc, v3_scale(n, vn));
        float vt2 = v3_dot(vt, vt);
        Vec3 j = v3_scale(n, -(1.0f + restitution) * vn);
        float mv = friction * vn;
        if (mv * mv < p->slide_thresh * vt2 || mv * mv <= 1.0e-6f) {
            /* sliding: magnitude 2*mu*|vn| along -vt (0 when mu == 0) */
            if (vt2 > 0.0f) j = v3_add(j, v3_scale(vt, 2.0f * friction * vn / sqrtf(vt2)));
        } else {
            /* gripping: remove a fixed share of the tangential contact velocity */
            j = v3_sub(j, v3_scale(vt, p->grip * (1.0f + restitution)));
        }
        *vel = v3_add(*vel, j);
        *spin = v3_add(*spin, v3_scale(v3_cross(n, j), -p->spin_coupling / p->radius));
    }
    *pos = v3_add(*pos, v3_scale(n, p->separation));
}

/* time of impact in [0,1] of a sphere moving a->b against the table top / net, or -1 */
static float sweep(const BallParams* p, const TableGeom* g, Vec3 a, Vec3 b, Surface* surf, Vec3* n) {
    float best = -1.0f;
    *surf = SURF_NONE;
    /* table top: plane y = top + r, moving down, inside the table footprint */
    float ya = a.y - (g->top_y + p->radius), yb = b.y - (g->top_y + p->radius);
    if (ya >= 0.0f && yb < 0.0f) {
        float t = ya / (ya - yb);
        Vec3 c = v3_lerp(a, b, t);
        if (fabsf(c.x) <= g->half_width && fabsf(c.z) <= g->half_length) {
            best = t; *surf = SURF_TABLE; *n = v3(0, 1, 0);
            /* edge balls: RTT rotates the normal by ExtraCornerBounceAngle when 0<n.y<1 [OPEN] */
        }
    }
    /* net: vertical slab z = 0 (+-r), crossed between bottom and top */
    if (g->net_enabled) {
        float za = a.z, zb = b.z;
        if ((za > 0.0f) != (zb > 0.0f) || fabsf(zb) < p->radius) {
            float dz = za - zb;
            float t = (fabsf(dz) > 1e-9f) ? (za - (za > 0 ? p->radius : -p->radius)) / dz : 0.0f;
            t = rtt_clampf(t, 0.0f, 1.0f);
            Vec3 c = v3_lerp(a, b, t);
            if (fabsf(c.x) <= g->net_half_width && c.y >= g->net_bottom_y - p->radius &&
                c.y <= g->net_top_y + p->radius && (best < 0.0f || t < best)) {
                best = t; *surf = SURF_NET; *n = v3(0, 0, za > 0.0f ? 1.0f : -1.0f);
                if (c.y > g->net_top_y) *n = v3_norm(v3(0, 1, n->z));   /* net-cord roll [CHOICE] */
            }
        }
    }
    return best;
}

void ball_step(const BallParams* p, const TableGeom* g, int shot_type,
               const BallSample* prev, BallSample* out, float dt) {
    Vec3 vel = prev->vel, spin = prev->spin, pos = prev->pos;
    Vec3 a = ball_accel(p, vel, spin, shot_type);
    float vz_before = vel.z;
    vel = v3_add(vel, v3_scale(a, dt));                            /* semi-implicit Euler [RTT] */
    if ((vz_before < 0.0f) != (vel.z < 0.0f)) vel.z = 0.0f;        /* z sign guard (sign bit) [RTT] */
    Vec3 npos = v3_add(pos, v3_scale(vel, dt));

    memset(out, 0, sizeof(*out));
    float step_t = dt;
    /* cast segment = the step plus a skin behind its start [RTT] */
    Vec3 disp = v3_sub(npos, pos);
    float len2 = v3_dot(disp, disp), len = 0.0f, cast_len = 0.0f;
    Vec3 dir = v3(0, 0, 0);
    if (len2 > 1.0e-12f) {
        len = sqrtf(len2); dir = v3_scale(disp, 1.0f / len); cast_len = len + p->cast_skin;
    } else if (v3_dot(vel, vel) > 1.0e-12f) {
        dir = v3_norm(vel); cast_len = p->cast_skin;
    }
    Vec3 start = v3_sub(npos, v3_scale(dir, cast_len));
    /* collisions only while the ball is over the table lengthwise [RTT] */
    float zlim = g->half_length + p->radius + 0.01f;
    if (cast_len > 0.0f && (fabsf(start.z) < zlim || fabsf(npos.z) < zlim)) {
        /* [OPEN] RTT also clamps `start` when the previous sample touched the net and the ball is
         * dropping just above the table top; not reproduced. */
        Surface s; Vec3 n;
        float toi = sweep(p, g, start, npos, &s, &n);
        if (toi >= 0.0f) {
            if (len2 > 1.0e-12f) {                                 /* rewind to the impact [RTT] */
                float back = dt * (1.0f + p->cast_skin / len) * (1.0f - toi);
                npos = v3_sub(npos, v3_scale(vel, back));
                vel = v3_sub(vel, v3_scale(a, back));
            }
            out->contact = v3_sub(npos, v3_scale(n, p->radius));
            float rest = (s == SURF_TABLE) ? p->table_restitution : p->net_restitution;
            float fric = (s == SURF_TABLE) ? p->table_friction : p->net_friction;
            /* [OPEN] FUN_8004bce4/bcec scale e on flat table hits in some mode; not read */
            ball_bounce(p, rest, fric, n, &npos, &vel, &spin);
            if (s == SURF_TABLE) out->bounced = 1; else out->net = 1;
            step_t = dt * rtt_clampf(toi, 0.0f, 1.0f);            /* sample sits at the impact [RTT] */
        }
    }
    out->pos = npos; out->vel = vel; out->spin = spin;
    out->t = prev->t + step_t;
}

/* first table bounce at or after `from` (after at least one non-bounce sample); returns its index
 * if it is on the wanted half, -2 if on the other half, -1 if the ball reaches the floor first,
 * -3 if the path runs out (TestTablePlaneCrossing 0x80123378) [RTT] */
static int find_bounce(const BallPath* path, int from, int want_neg_z) {
    int seen_air = 0;
    for (int i = from; i < path->count; i++) {
        const BallSample* s = &path->s[i];
        if (s->pos.y <= 0.0f) return -1;
        if (!s->bounced) { seen_air = 1; continue; }
        if (seen_air) return ((s->pos.z < 0.0f) == (want_neg_z != 0)) ? i : -2;
    }
    return -3;
}

/* first sample leaving the table box while moving outward (TestOutOfBounds 0x80123700) [RTT] */
static int find_out(const BallPath* path, const TableGeom* g, int from) {
    for (int i = from; i < path->count; i++) {
        Vec3 q = path->s[i].pos, v = path->s[i].vel;
        if ((q.x > g->half_width && v.x > 0.0f) || (q.x < -g->half_width && v.x < 0.0f) ||
            (q.z > g->half_length && v.z > 0.0f) || (q.z < -g->half_length && v.z < 0.0f) ||
            (q.y < g->top_y && v.y < 0.0f))
            return i;
    }
    return -1;
}

/* first net contact before the floor (FindFirstNetSample 0x80123460) [RTT] */
static int find_net(const BallPath* path, int from) {
    for (int i = from; i < path->count; i++) {
        if (path->s[i].pos.y <= 0.0f) return -1;
        if (path->s[i].net) return i;
    }
    return -1;
}

static void set_outcome(BallPath* path, unsigned bits, float t) {
    path->outcome = OUTCOME_KNOWN | bits;
    path->outcome_t = t;
}

/* FUN_80123a28: is extrapolating from the last sample worth it -- the ball is not already falling
 * below the table, not heading away past the end (or toward the wrong end), and over or heading
 * back over the table width. Constants DAT_806df098 = 0.1, 806df084 = 0, 806df08c = 0.5,
 * 806df080 = -1 [RTT] */
static int extrapolation_useful(const BallSample* last, const TableGeom* g, int want_neg_end) {
    Vec3 p = last->pos, v = last->vel;
    if (p.y < g->top_y - 0.1f && v.y < 0.0f) return 0;
    if ((v.z < 0.0f) == (p.z < 0.0f)) {
        if (fabsf(p.z) > 0.1f + g->half_length) return 0;          /* 0.1 + 0.5 * table length */
        if ((p.z < -1.0f) != (want_neg_end != 0)) return 0;
    }
    return (v.x < 0.0f) != (p.x < 0.0f) || fabsf(p.x) <= 0.1f + g->half_width;
}

/* FUN_8004cf34: |x| < 1.36 and |z| < 1.71 (DAT_806d6220 / 6224) [RTT] */
static int near_table(Vec3 p) { return fabsf(p.x) < 1.36f && fabsf(p.z) < 1.71f; }

/* FUN_8004cf74: integrate on from the last sample until a table bounce (1 on +z, 2 on -z) or the
 * ball is below the table top and falling (3); 120 steps at most (0 = nothing) [RTT] */
static int extrapolate_event(const BallParams* p, const TableGeom* g, int shot_type, BallSample s,
                             BallSample* at) {
    float low = g->top_y - p->radius;
    for (int n = 0; n < 120; n++) {
        int ev = 0;
        if (s.bounced) ev = s.pos.z >= 0.0f ? 1 : 2;
        if (s.pos.y < low && s.vel.y < 0.0f) ev = 3;
        if (ev) { *at = s; return ev; }
        BallSample nx;
        ball_step(p, g, shot_type, &s, &nx, RTT_DT);
        s = nx;
    }
    *at = s;
    return 0;
}

/* FUN_8004ce6c: the first sample from the window start at or below the table top (+ r); if it is
 * more than 2 samples in, still hittable and not a bounce, and near the table: NEAR_TABLE [RTT] */
static void near_table_check(BallPath* path, const BallParams* p, const TableGeom* g, int first) {
    int j = -1;
    for (int i = first; i < path->count; i++) if (path->s[i].pos.y <= g->top_y + p->radius) { j = i; break; }
    if (j > first + 2 && path->s[j].hittable && !path->s[j].bounced && near_table(path->s[j].pos))
        path->outcome |= OUTCOME_NEAR_TABLE;
}

/* ballPredict__ExtrapolateUnknownOutcome 0x8004caf4: decides a legal landing [RTT] */
static void extrapolate_outcome(BallPath* path, const BallParams* p, const TableGeom* g, int far_neg_z) {
    const BallSample* last = &path->s[path->count - 1];
    float now = path->s[0].t;                                      /* the point clock at prediction */
    int first = -1;
    for (int i = 0; i < path->count; i++) if (path->s[i].hittable) { first = i; break; }
    if (first < 1) {
        /* no window: the receiver (on -z when far_neg_z) cannot play it */
        if (!extrapolation_useful(last, g, far_neg_z)) { set_outcome(path, OUTCOME_FAULT, last->t); return; }
        BallSample at;
        int ev = extrapolate_event(p, g, path->shot_type, *last, &at);
        if (ev) {
            int fault = ev == 3 || (ev == 1 && far_neg_z) || (ev == 2 && !far_neg_z);
            set_outcome(path, fault ? OUTCOME_FAULT : 0, at.t);
        }
        return;
    }
    int after = -1;
    for (int i = first + 1; i < path->count; i++) if (!path->s[i].hittable) { after = i; break; }
    if (after < 1) {
        /* the window runs to the end of the prediction */
        if (!extrapolation_useful(last, g, path->s[first].pos.z < 0.0f)) {
            set_outcome(path, 0, now);
            near_table_check(path, p, g, first);
            return;
        }
        BallSample at;
        int ev = extrapolate_event(p, g, path->shot_type, *last, &at);
        set_outcome(path, 0, path->s[first].t);
        if (ev == 1 || ev == 2) path->outcome |= OUTCOME_BOUNCES_AGAIN;
        else if (ev == 3 && near_table(at.pos)) path->outcome |= OUTCOME_NEAR_TABLE;
        return;
    }
    if (!path->s[after].bounced) {
        set_outcome(path, 0, now);
        near_table_check(path, p, g, first);
    } else {
        set_outcome(path, OUTCOME_BOUNCES_AGAIN, path->s[after].t);
    }
}

/* ClassifyOutcomeCore 0x8004c848 + MarkHittableFlag 0x80124bd4, then ballPredict__ClassifyOutcome
 * 0x8004c79c: a legal landing leaves the outcome unknown and ExtrapolateUnknownOutcome decides it;
 * still unknown -> KNOWN | FAULT ("Ball prediction outcome unknown!") [RTT] */
static void classify(BallPath* path, const BallParams* p, const TableGeom* g) {
    int far_neg_z = path->s[0].pos.z > 0.0f;     /* struck from +Z -> must land on -Z */
    int start = 0;
    path->outcome = 0;
    path->landing = -1;
    if (path->is_serve) {
        int n = find_net(path, 0);
        if (n >= 0) { set_outcome(path, OUTCOME_FAULT | OUTCOME_NET, path->s[n].t); return; }
        int own = find_bounce(path, 0, !far_neg_z);
        if (own < 0) {
            int o = (own == -2) ? find_bounce(path, 0, far_neg_z) : find_out(path, g, 0);
            if (o >= 0) set_outcome(path, OUTCOME_FAULT, path->s[o].t);
            return;
        }
        start = own;
    }
    start += 1;
    int land = find_bounce(path, start, far_neg_z);
    if (land < 0) {
        int o = (land == -2) ? find_bounce(path, start, !far_neg_z) : find_out(path, g, start);
        if (o >= 0) {
            set_outcome(path, OUTCOME_FAULT, path->s[o].t);
            if (find_net(path, start) >= 0) path->outcome |= OUTCOME_NET;
        }
        return;
    }
    path->landing = land;
    /* hittable window: after the landing, until the next bounce or the floor */
    int i = land + 1;
    while (i < path->count && path->s[i].bounced) i++;
    for (; i < path->count; i++) {
        if (path->s[i].pos.y <= p->radius || path->s[i].bounced) break;
        path->s[i].hittable = 1;
    }
    extrapolate_outcome(path, p, g, far_neg_z);
    if (!(path->outcome & OUTCOME_KNOWN)) set_outcome(path, OUTCOME_FAULT, path->s[0].t);
}

float ball_time_step(const BallParams* p, float game_pace) {
    float x = rtt_clampf(game_pace + p->pace_normal_bias, -1.0f, 1.0f);
    float K = p->pace_factor;
    float s = x > 0.0f ? 1.0f + x * (1.0f / K - 1.0f) : 1.0f + (-x) * (K - 1.0f);
    return s * RTT_DT;
}

float ball_hit_elasticity(int swing_type, int is_serve, int hit_2c, float x, int mode) {
    if (hit_2c > 5) return 0.95f;
    if (swing_type == 3) {
        float f = rtt_clampf(x + 0.33333f * (float)mode, 0.0f, 1.0f);
        return 0.875f + f * (0.775f - 0.875f);
    }
    if (swing_type == 6 || swing_type == 11) return 0.85f;
    return is_serve ? 1.0f : 0.9f;
}

void ball_ease_launch_keys(BallPath* path, float paddle_speed, int ring_index_of_s0) {
    if (paddle_speed <= 0.0f || path->count < 2) return;
    Vec3 v = path->s[0].vel;
    if (v3_dot(v, v) > paddle_speed * paddle_speed) return;          /* ball faster than racket */
    float prev_orig = path->s[0].t;
    for (int j = 1; j < path->count; j++) {
        int i = j + ring_index_of_s0;
        float f = rtt_clampf((float)(i - 2) / 15.0f, 0.0f, 1.0f);    /* DAT_806d44b4 = 15 [RTT] */
        float w = f <= 0.0f ? 0.0f : (f >= 1.0f ? 1.0f : sinf(0.5f * 3.14159265f * f));
        float orig = path->s[j].t;
        float at_racket = v3_len(v3_sub(path->s[j].pos, path->s[j - 1].pos)) / paddle_speed;
        path->s[j].t = path->s[j - 1].t + at_racket + w * ((orig - prev_orig) - at_racket);
        prev_orig = orig;
    }
}

void ball_predict(BallPath* path, const BallParams* p, const TableGeom* g,
                  Vec3 pos, Vec3 vel, Vec3 spin, int shot_type, Side striker, int is_serve,
                  float game_pace, float paddle_speed) {
    /* RTT only warns ("postHitAngVel is bad"); clamping is ours [CHOICE] */
    if (v3_dot(spin, spin) >= p->spin_cap_sq)
        spin = v3_scale(v3_norm(spin), sqrtf(p->spin_cap_sq) * 0.999f);
    memset(path, 0, sizeof(*path));
    path->shot_type = shot_type;
    path->striker = striker;
    path->is_serve = is_serve;
    path->key_step = ball_time_step(p, game_pace);
    /* the serve flight uses its own ball state: g scale 1.0, e 1.0 (live 2026-10-10) [RTT] */
    BallParams q = *p;
    if (is_serve) { q.gravity_scale = p->serve_gravity_scale; q.table_restitution = p->serve_restitution; }
    p = &q;
    path->gravity_scale = q.gravity_scale;
    path->restitution = q.table_restitution;
    BallSample* s0 = &path->s[0];
    s0->pos = pos; s0->vel = vel; s0->spin = spin;
    path->count = 1;
    path->cursor = 1;
    /* ProcessHit stop rule [RTT] */
    int same = 0, tail = 0;
    float last_bounce_z = 0.0f;
    while (path->count < RTT_BALL_PATH_SLOTS) {
        BallSample* prev = &path->s[path->count - 1];
        BallSample* nxt = &path->s[path->count];
        ball_step(p, g, shot_type, prev, nxt, RTT_DT);
        nxt->t = prev->t + (nxt->t - prev->t) * (path->key_step / RTT_DT);   /* key time [RTT] */
        if (nxt->bounced) {
            if (!prev->bounced) {
                float z = nxt->pos.z;
                if ((z < 0.0f && last_bounce_z < 0.0f) || (z > 0.0f && last_bounce_z > 0.0f)) same++;
                else same = 1;
            }
            last_bounce_z = nxt->pos.z;
        }
        if (nxt->pos.y <= p->radius) tail++;
        if (same > 1) tail++;
        path->count++;
        if (tail > 6 || same > 2) { path->terminated = 1; break; }   /* FUN_801246a0(.., 1) [RTT] */
    }
    ball_ease_launch_keys(path, paddle_speed, 1);                       /* before classification [RTT] */
    classify(path, p, g);
}

void ball_classify(BallPath* path, const BallParams* p, const TableGeom* g) {
    for (int i = 0; i < path->count; i++) path->s[i].hittable = 0;
    classify(path, p, g);
}

/* pongLerpQueue__SampleLerp: position lerped, velocity carried from the lower sample [RTT] */
static void sample_at(const BallPath* path, float t, Vec3* pos, Vec3* vel) {
    if (path->count == 0) { *pos = *vel = v3(0, 0, 0); return; }
    if (t <= path->s[0].t) { *pos = path->s[0].pos; *vel = path->s[0].vel; return; }
    for (int i = 0; i + 1 < path->count; i++)
        if (t <= path->s[i + 1].t) {
            float dt = path->s[i + 1].t - path->s[i].t;
            float f = dt > 0.0f ? (t - path->s[i].t) / dt : 1.0f;
            *pos = v3_lerp(path->s[i].pos, path->s[i + 1].pos, f);
            *vel = path->s[i].vel;
            return;
        }
    *pos = path->s[path->count - 1].pos;
    *vel = path->s[path->count - 1].vel;
}

Vec3 ball_pos_at(const BallPath* path, float t) {
    Vec3 p, v;
    sample_at(path, t, &p, &v);
    return p;
}

static void post_ball(EventBus* bus, EventId id, Side side, Vec3 at, float speed2) {
    Event e;
    memset(&e, 0, sizeof(e));
    e.id = id; e.side = side;
    e.f[0] = at.x; e.f[1] = at.y; e.f[2] = at.z; e.f[3] = speed2;
    ev_send(bus, &e);
}

/* ApplySampleToLivePhysicsInstance 0x8004a55c: every frame the ball is sampled at the previous
 * and the current clock and each threshold crossing posts one msgBall, in this order [RTT]. */
void ball_play(BallPath* path, const BallParams* p, const TableGeom* g,
               const float player_abs_z[2], float dt, EventBus* bus) {
    if (path->count < 2) return;
    float t0 = path->clock;
    path->clock += dt;
    if (path->s[0].t < t0) {
        Vec3 a, va, b, vb;
        sample_at(path, t0, &a, &va);
        sample_at(path, path->clock, &b, &vb);
        float sp2 = v3_dot(vb, vb);
        if (va.y > 0.0f && vb.y <= 0.0f)                                       /* 11 */
            post_ball(bus, EV_BALL_AT_APEX, SIDE_NONE, b, sp2);
        /* table / net contacts recorded in the samples the clock passed (1/2, 7) */
        while (path->cursor < path->count && path->s[path->cursor].t <= path->clock) {
            const BallSample* s = &path->s[path->cursor];
            if (s->bounced)
                post_ball(bus, s->contact.z > 0.0f ? EV_BALL_BOUNCE_TABLE_A : EV_BALL_BOUNCE_TABLE_B,
                          SIDE_NONE, s->contact, sp2);
            if (s->net) post_ball(bus, EV_BALL_COLLIDE_NET, SIDE_NONE, s->contact, sp2);
            path->cursor++;
        }
        float end_z = g->half_length + p->radius;
        if (fabsf(a.z) < end_z && fabsf(b.z) >= end_z)                        /* 13 */
            post_ball(bus, EV_BALL_BEYOND_TABLE_END, SIDE_NONE, b, sp2);
        float below_y = g->top_y - p->radius;                                  /* any x/z */
        if (fabsf(a.y) >= below_y && fabsf(b.y) < below_y)                    /* 12 */
            post_ball(bus, EV_BALL_BELOW_TABLE, SIDE_NONE, b, sp2);
        if (a.y > p->radius && b.y <= p->radius) {                             /* 10 + 44 */
            post_ball(bus, EV_BALL_COLLIDE_UNKNOWN, SIDE_NONE, b, sp2);
            post_ball(bus, EV_BALL_FELL_THROUGH_FLOOR, SIDE_NONE, b, sp2);
        }
        /* 14: 1 m behind the player on the ball's half; |player z| is the player object's +0x38
         * [RTT, field meaning inferred] */
        Side half = b.z > 0.0f ? SIDE_A : SIDE_B;
        float past_z = 1.0f + player_abs_z[half];
        if (fabsf(a.z) < past_z && fabsf(b.z) >= past_z)
            post_ball(bus, EV_BALL_PAST_PLAYER, half, b, sp2);
    }
    /* prediction exhausted with nothing deciding the point: end it as a miss [CHOICE] */
    if (path->cursor >= path->count && !path->ended && path->clock > path->s[path->count - 1].t + 0.25f) {
        path->ended = 1;
        post_ball(bus, EV_POINT_TIMEOUT, SIDE_NONE, path->s[path->count - 1].pos, 0.0f);
    }
}

void ball_advance_ring(BallPath* path, const BallParams* p, const TableGeom* g) {
    int n = 0;
    while (path->count - n > 1 && path->s[n + 1].t <= path->clock) n++;
    if (n == 0) return;
    memmove(&path->s[0], &path->s[n], (size_t)(path->count - n) * sizeof(BallSample));
    path->count -= n;
    path->cursor = path->cursor > n ? path->cursor - n : 0;
    path->landing = path->landing >= n ? path->landing - n : -1;
    if (path->terminated || path->fudged) return;
    BallParams q = *p;                                   /* the flight's own ball state (ball_predict) */
    q.gravity_scale = path->gravity_scale;
    q.table_restitution = path->restitution;
    for (int k = 0; k < n && path->count < RTT_BALL_PATH_SLOTS; k++) {
        BallSample* prev = &path->s[path->count - 1];
        BallSample* nxt = &path->s[path->count];
        memset(nxt, 0, sizeof(*nxt));                    /* a fresh sample, flags clear */
        ball_step(&q, g, path->shot_type, prev, nxt, RTT_DT);
        nxt->t = prev->t + (nxt->t - prev->t) * (path->key_step / RTT_DT);
        /* IntegrateStep's tail (0x8004bba8): a table bounce or a sample at / below the floor height
         * clears hittable; otherwise it carries the previous sample's (the integrator starts from a
         * copy of the ring's last sample) [RTT] */
        nxt->hittable = (nxt->bounced || nxt->pos.y <= q.radius) ? 0 : prev->hittable;
        path->count++;
    }
}

int ball_nearest_hittable(const BallPath* path, Vec3 point) {
    int i = path->cursor;
    while (i < path->count && !path->s[i].hittable) i++;
    int best = -1;
    float bd = 1e30f;
    for (; i < path->count && path->s[i].hittable; i++) {
        Vec3 d = v3_sub(path->s[i].pos, point);
        float d2 = v3_dot(d, d);
        if (d2 < bd) { bd = d2; best = i; }
    }
    return best;   /* -1 = "Could not find a hittable samples!" [RTT] */
}

int ball_fudge_to_contact(BallPath* path, int idx, Vec3 target, float contact_t, float now, int snap) {
    if (idx < 1 || idx >= path->count) return 0;
    if (!path->s[idx].hittable || path->s[idx].bounced) return 0;       /* "sample is not hittable" */
    /* run start (FUN_80123888): walk back over hittable samples, but only inside the live queue --
     * RTT has already popped samples before the playback clock, so if the bounce is gone the run
     * starts at the queue head (live frame 746->803 case) [RTT] */
    int lo = path->cursor > 0 ? path->cursor - 1 : 0;
    int st = idx;
    while (st - 1 >= lo && path->s[st - 1].hittable && !path->s[st - 1].bounced) st--;
    if (st - 1 >= lo) st--;
    float t0 = path->s[st].t, t1 = path->s[idx].t;
    if (t1 <= t0) return 0;
    Vec3 delta = v3_sub(target, path->s[idx].pos);
    for (int i = st + 1; i <= idx; i++) {                               /* ApplyPositionFudge [RTT] */
        BallSample* s = &path->s[i];
        float w = (i == idx) ? 1.0f : (s->t - t0) / (t1 - t0);
        s->pos = v3_add(s->pos, v3_scale(delta, w));
        float dt = s->t - path->s[i - 1].t;
        if (dt > 0.0f) s->vel = v3_scale(v3_sub(s->pos, path->s[i - 1].pos), 1.0f / dt);
    }
    path->count = idx + 1;                                              /* FUN_80121d4c [RTT] */
    if (snap) contact_t = now + floorf((contact_t - now) / RTT_DT + 0.5f) * RTT_DT;   /* frame grid [RTT] */
    for (int i = st + 1; i <= idx; i++) {                               /* ApplyTimestampFudge [RTT] */
        float f = (i == idx) ? 1.0f : (path->s[i].t - t0) / (t1 - t0);
        path->s[i].t = t0 + f * (contact_t - t0);
    }
    if (path->count < RTT_BALL_PATH_SLOTS) {                            /* end marker, FUN_80121ff4 [RTT] */
        BallSample* e = &path->s[path->count++];
        *e = path->s[idx];
        e->t += 1.0f;
        e->bounced = e->net = e->hittable = 0;
    }
    if (path->cursor > path->count) path->cursor = path->count;
    return 1;
}

Vec3 ball_solve_launch(const BallParams* p, const TableGeom* g, Vec3 from, Vec3 target,
                       Vec3 spin, float t_flight, int shot_type) {
    /* ballistic first guess, then correct by simulating (shooting method) [CHOICE] */
    Vec3 v = v3((target.x - from.x) / t_flight,
                (g->top_y + p->radius - from.y - 0.5f * p->gravity_y * p->gravity_scale * t_flight * t_flight) / t_flight,
                (target.z - from.z) / t_flight);
    TableGeom open = *g;
    open.net_enabled = 0;
    for (int it = 0; it < 6; it++) {
        BallSample a, b;
        memset(&a, 0, sizeof(a));
        a.pos = from; a.vel = v; a.spin = spin;
        float t = 0.0f;
        while (t < t_flight) { ball_step(p, &open, shot_type, &a, &b, RTT_DT); a = b; t += RTT_DT; if (b.bounced) break; }
        Vec3 err = v3_sub(target, a.pos);
        v.x += err.x / t_flight;
        v.z += err.z / t_flight;
        v.y += (g->top_y + p->radius - a.pos.y) / t_flight;
    }
    return v;
}

/* ---------------------------------------------------------------- shot solver (SolveHitAim) */
/* One candidate launch flown to the table plane y = top + r, no bounce, no table/net collision
 * (FUN_8004729c with bounce mode off). Semi-implicit Euler at 1/round(fps). Optionally tracks a
 * ball that starts below the plane outside the table: `edge` = it would enter the footprint while
 * still below the plane (the table edge is in the way), edge_pos = the step before. [RTT] */
typedef struct { Vec3 land; int edge; Vec3 edge_pos; } ShotLanding;

static ShotLanding shot_fly(const BallParams* p, const TableGeom* g, int shot_type, Vec3 pos, Vec3 vel,
                            Vec3 spin, Vec3 target, int track_edge, int stop_above) {
    ShotLanding r;
    memset(&r, 0, sizeof(r));
    const float plane = g->top_y + p->radius;
    const float hl = g->half_length, hw = g->half_width;
    /* retarget mode: a target past the table end only flies until the ball rises to the plane */
    int beyond = stop_above && fabsf(target.z) > hl;
    int tracking = track_edge && pos.y < plane && !beyond && !(fabsf(pos.z) <= hl && fabsf(pos.x) <= hw);
    Vec3 prev = pos;
    float t = 0.0f;
    while ((pos.y > plane || vel.y > 0.0f) && t < 24.0f && (!beyond || pos.y < plane)) {
        Vec3 a = ball_accel(p, vel, spin, shot_type);
        t += RTT_DT;
        vel = v3_add(vel, v3_scale(a, RTT_DT));
        prev = pos;
        pos = v3_add(pos, v3_scale(vel, RTT_DT));
        if (tracking) {
            if (prev.y >= plane) {
                if (pos.y >= plane) tracking = 0;
            } else if ((hw < fabsf(prev.x) && fabsf(pos.x) < hw && (fabsf(prev.z) <= hl || fabsf(pos.z) < hl)) ||
                       (hl < fabsf(prev.z) && fabsf(pos.z) < hl && (fabsf(prev.x) <= hw || fabsf(pos.x) < hw))) {
                r.edge = 1; r.edge_pos = prev; tracking = 0;
            } else if (pos.y >= plane) {
                tracking = 0;
            }
        }
    }
    if (pos.y <= plane && plane < prev.y) {                       /* back up onto the plane */
        float f = vel.y < -1.0e-6f ? (pos.y - plane) / vel.y : 0.0f;
        pos = v3_sub(pos, v3_scale(vel, f));
    }
    r.land = pos;
    return r;
}

static float wrap_pi(float a) {
    if (a > 3.14159265f) return a - 6.28318531f;
    if (a < -3.14159265f) return a + 6.28318531f;
    return a;
}

/* direction from (azimuth, elevation): azimuth is measured from the z axis toward x, and the
 * z component points away from the hitter's half (negated when the ball is at z > 0) [RTT] */
static Vec3 shot_vel(float speed, float az, float el, float zsign) {
    float ce = cosf(el);
    return v3(ce * speed * sinf(az), speed * sinf(el), ce * speed * cosf(az) * zsign);
}

/* ComputePostHitVel 0x800492d4, the two modes SolveHitAim uses (the speed mode with retries,
 * param_7 != 0, is not reached from the hit path and is not ported):
 *   fixed_elev < 0 : speed fixed, Newton on (azimuth, elevation)       -> the direct shot
 *   fixed_elev >= 0: elevation fixed, Newton on (azimuth, speed)        -> one arc-table row
 * Newton: finite differences with step 0.1 (rad or m/s), 2x2 solve (sum fallback when the
 * determinant is ~0), steps clamped to +-0.5 rad / [max(-5,-speed), 5] m/s, elevation kept in
 * [min_elev, max_elev]; a worse landing halves the last step (5 halvings in a row, or 5 halving
 * episodes, end it); stop under 1 cm (err^2 < 1e-4) or after 64 steps; returns the best found.
 * A direct shot whose ball starts below the table plane outside the table and would clip the
 * edge is retried once toward a point just past the edge. [RTT] */
static Vec3 post_hit_vel(const BallParams* p, const TableGeom* g, int shot_type, float speed,
                         float fixed_elev, float max_elev, Vec3 from, Vec3 target, Vec3 spin, int retargeted) {
    const float plane = g->top_y + p->radius;
    const float zsign = from.z > 0.0f ? -1.0f : 1.0f;
    const int elev_mode = fixed_elev < 0.0f;
    float dx = target.x - from.x, dz = target.z - from.z;
    float hd = sqrtf(dx * dx + dz * dz);
    float az = hd > 1.0e-6f ? asinf(rtt_clampf(dx / hd, -1.0f, 1.0f)) : 0.0f;
    float el, min_el = -1.5707963f;
    if (elev_mode) {
        Vec3 aim = target;
        if (!retargeted) aim.y += 1.0f;                              /* first guess: 1 m over the target */
        Vec3 d = v3_sub(aim, from);
        el = asinf(rtt_clampf(d.y / v3_len(d), -1.0f, 1.0f));
        if (el > max_elev) el = max_elev;
        if (from.y <= plane) {                                      /* below the plane: clear the edge */
            float h = plane - from.y;
            min_el = (hd != 0.0f || h != 0.0f) ? atan2f(h, hd) : 0.0f;
        }
    } else {
        el = fixed_elev;
    }
    float spd = speed;
    float best = 3.4028235e38f, prev_err = 1.0e8f;
    Vec3 best_v = v3(0, 0, 0);
    int best_edge = 0;
    Vec3 best_edge_pos = v3(0, 0, 0);
    float step_a = 0.0f, step_b = 0.0f;
    int iter = 0, halvings = 0, episodes = 0;
    for (;;) {
        int done = 0;
        while (!done) {
            if (iter > 63) { done = 1; break; }
            Vec3 v = shot_vel(spd, az, el, zsign);
            ShotLanding L = shot_fly(p, g, shot_type, from, v, spin, target, 1, retargeted);
            float ex = target.x - L.land.x, ez = target.z - L.land.z;
            float err = ex * ex + ez * ez;
            if (err >= prev_err && iter != 0) break;                   /* worse: halve the step */
            if (err < best) {
                best = err; best_v = v; best_edge = L.edge; best_edge_pos = L.edge_pos;
                if (err < 1.0e-4f) { done = 1; break; }
            }
            ShotLanding La = shot_fly(p, g, shot_type, from, shot_vel(spd, wrap_pi(az + 0.1f), el, zsign),
                                      spin, target, 0, retargeted);
            float jxa = 10.0f * (La.land.x - L.land.x), jza = 10.0f * (La.land.z - L.land.z);
            ShotLanding Lb = elev_mode
                ? shot_fly(p, g, shot_type, from, shot_vel(spd, az, wrap_pi(el + 0.1f), zsign), spin, target, 0, retargeted)
                : shot_fly(p, g, shot_type, from, shot_vel(spd + 0.1f, az, el, zsign), spin, target, 0, retargeted);
            float jxb = 10.0f * (Lb.land.x - L.land.x), jzb = 10.0f * (Lb.land.z - L.land.z);
            float det = jxa * jzb - jza * jxb, da, db;
            if (fabsf(det) <= 1.0e-6f) {
                da = fabsf(jxa + jxb) > 1.0e-12f ? ex / (jxa + jxb) : 0.0f;
                db = fabsf(jza + jzb) > 1.0e-12f ? ez / (jza + jzb) : 0.0f;
                if (fabsf(da) < 1.0e-12f && db < 1.0e-12f) { done = 1; break; }   /* (sic: db not abs) */
            } else {
                float inv = 1.0f / det;
                da = inv * (ex * jzb - ez * jxb);
                db = inv * (ez * jxa - ex * jza);
            }
            step_a = rtt_clampf(da, -0.5f, 0.5f);
            az = wrap_pi(az + step_a);
            if (elev_mode) {
                float hi = max_elev - el < 0.5f ? max_elev - el : 0.5f;
                float lo = min_el - el > -0.5f ? min_el - el : -0.5f;
                step_b = db < lo ? lo : (db > hi ? hi : db);
                el = wrap_pi(el + step_b);
            } else {
                float lo = -spd > -5.0f ? -spd : -5.0f;
                spd += db < lo ? lo : (db > 5.0f ? 5.0f : db);
            }
            prev_err = err;
            halvings = 0;
            iter++;
        }
        if (done) break;
        step_a *= 0.5f; step_b *= 0.5f;
        az = wrap_pi(az - step_a);
        el = wrap_pi(el - step_b);
        if (halvings == 0 && ++episodes > 4) break;
        if (++halvings >= 5) break;
    }
    if (elev_mode && best_edge && !retargeted) {
        /* aim just past the table edge the ball would clip, then solve again [RTT] */
        float ex = p->radius + g->half_width, ez = p->radius + g->half_length;
        Vec3 t2;
        t2.x = best_edge_pos.x < ex ? (best_edge_pos.x <= -ex ? -(0.002f + ex) : best_edge_pos.x) : 0.002f + ex;
        t2.y = target.y;
        float zz = best_edge_pos.z < ez ? (best_edge_pos.z < -ez ? -(0.002f + ez) : best_edge_pos.z) : 0.002f + ez;
        float m = fmaxf(2.0f * fabsf(from.x - t2.x), 0.5f * (t2.y - from.y));
        if (zz > 0.0f)      t2.z = zz <= from.z - m ? zz : from.z - m;    /* at least m back from the ball */
        else if (zz < 0.0f) t2.z = zz < from.z + m ? from.z + m : zz;
        else                t2.z = zz;
        return post_hit_vel(p, g, shot_type, speed, fixed_elev, max_elev, from, t2, spin, 1);
    }
    return best_v;
}

Vec3 ball_solve_hit_aim(const BallParams* p, const TableGeom* g, const HitAimInput* in, float* out_elev) {
    Vec3 tgt = in->target;
    tgt.y = g->top_y + p->radius;
    Vec3 v0 = post_hit_vel(p, g, in->swing_type, in->speed, -1.0f, in->max_elev, in->ball_pos, tgt, in->spin, 0);
    if (out_elev) *out_elev = 0.0f;
    if (in->swing_type == 6 || in->swing_type == 11) return v0;     /* direct shot, no arc tables */

    float hv = sqrtf(v0.x * v0.x + v0.z * v0.z), sv = v3_len(v0);
    float el0 = (hv > 0.0f && sv > 0.0f) ? acosf(rtt_clampf(hv / sv, -1.0f, 1.0f)) : 0.0f;

    /* low table: elevations el0 -> max_elev, rows not clamped */
    Vec3 low_v[32]; float low_a[32]; int n_low = 0;
    float step = (in->max_elev - el0) / 31.0f - 1.0e-6f;
    for (float a = el0; a < in->max_elev && n_low < 32; a += step) {
        low_a[n_low] = a;
        low_v[n_low] = post_hit_vel(p, g, in->swing_type, in->speed, a, in->max_elev, in->ball_pos, tgt, in->spin, 0);
        n_low++;
        if (step <= 0.0f) break;
    }
    /* high table: elevations el0 -> 0 (0.01 when the ball is below the plane), speed capped */
    Vec3 high_v[32]; float high_a[32]; int n_high = 0;
    float lo = in->ball_pos.y > g->top_y + p->radius ? 0.0f : 0.01f;
    float hstep = (el0 - lo) / 31.0f - 1.0e-6f;
    for (float a = el0; lo < a && n_high < 32; a -= hstep) {
        Vec3 v = post_hit_vel(p, g, in->swing_type, in->speed, a, in->max_elev, in->ball_pos, tgt, in->spin, 0);
        float l = v3_len(v);
        if (l > in->speed) v = v3_scale(v, in->speed / l);          /* ClampVec3ToMaxLen */
        /* High-table path. Capture 2's #37 (kick -3) left a 2.6e-4 rad azimuth residual; it did not
         * reproduce (capture 4: kick -1/-1/-3/-5, all within 1.5e-5), closed 2026-10-11 as a capture
         * artifact (a late / mid-rebuild snapshot, see map/ball-physics.md). */
        high_a[n_high] = a; high_v[n_high] = v; n_high++;
        if (hstep <= 0.0f) break;
    }
    if (n_low < 1) { if (out_elev) *out_elev = el0; return v0; }

    /* steepest allowed arc: 62 deg at the table end, 32 deg once far back [RTT] */
    float hl = g->half_length, near_z = 0.2f + hl, far_z = hl + in->far_extra;
    float z = fabsf(in->ball_pos.z);
    z = z < near_z ? near_z : (z > far_z ? far_z : z);
    float cap = (62.0f - 30.0f * rtt_clampf((z - near_z) / (far_z - near_z), 0.0f, 1.0f)) * 0.017453292f;

    int k = in->kick_pitch < 0 ? -in->kick_pitch : in->kick_pitch;
    if (in->kick_pitch < 0) {                                       /* negative: the flatter table */
        if (k >= n_high) k = n_high - 1;                            /* RTT does not bound-check [OPEN] */
        if (k < 0) return v0;
        if (out_elev) *out_elev = high_a[k];
        return high_v[k];
    }
    if (k >= n_low) k = n_low - 1;                                  /* RTT does not bound-check [OPEN] */
    int pick = 0;
    for (int i = k; i >= 0; i--) if (low_a[i] < cap) { pick = i; break; }
    if (out_elev) *out_elev = low_a[pick];
    return low_v[pick];
}

/* ---------------------------------------------------------------- serve launch (ServeHitVel) */

/* SimulateShotLanding's record (param_9) as the serve solver uses it */
typedef struct {
    Vec3  bounce;            /* first plane crossing [0..2] */
    Vec3  edge_pos;          /* [4..6] */
    float max_h;             /* [8], a running max over every flight of one solve (never reset) */
    float net_y;             /* [9], y when crossing the net plane below the net test height */
    int   bounce_ok;         /* +0x28: first bounce on the server's half, 1 cm inside */
    int   edge;              /* +0x29: entered the table footprint below the plane */
    int   net;               /* +0x2a: crossed z = 0 below the net test height (FUN_8004723c) */
} ServeFlightRec;

static void serve_net_check(ServeFlightRec* r, float net_y, Vec3 cur, Vec3 prev) {
    if (r && ((cur.z < 0.0f) != (prev.z < 0.0f)) && (cur.y < net_y || prev.y < net_y)) {
        r->net = 1;
        r->net_y = cur.y;
    }
}

/* ball__SimulateShotLanding 0x8004729c with bounce mode on (param_6), param_8 = 0 */
static Vec3 serve_fly(const BallParams* p, const TableGeom* g, int shot_type, float net_y, Vec3 pos,
                      Vec3 vel, Vec3 spin, ServeFlightRec* r) {
    const float plane = g->top_y + p->radius;
    const float hl = g->half_length, hw = g->half_width;
    const Vec3 from = pos;
    int tracking = r && pos.y < plane && !(fabsf(pos.z) <= hl && fabsf(pos.x) <= hw);
    Vec3 prev = pos, a = v3(0, 0, 0);
    float t = 0.0f;
    while ((pos.y > plane || vel.y > 0.0f) && t < 24.0f) {
        a = ball_accel(p, vel, spin, shot_type);
        t += RTT_DT;
        vel = v3_add(vel, v3_scale(a, RTT_DT));
        prev = pos;
        pos = v3_add(pos, v3_scale(vel, RTT_DT));
        serve_net_check(r, net_y, pos, prev);
        if (r && pos.y > r->max_h) r->max_h = pos.y;
        if (tracking) {
            if (prev.y >= plane) {
                if (pos.y >= plane) { r->edge = 0; tracking = 0; }
            } else if ((hw < fabsf(prev.x) && fabsf(pos.x) < hw && (hl >= fabsf(prev.z) || fabsf(pos.z) < hl)) ||
                       (hl < fabsf(prev.z) && fabsf(pos.z) < hl && (hw >= fabsf(prev.x) || fabsf(pos.x) < hw))) {
                r->edge = 1; r->edge_pos = prev; tracking = 0;
            } else if (pos.y >= plane) {
                r->edge = 0; tracking = 0;
            }
        }
    }
    if (pos.y <= plane && plane < prev.y) {
        float f = vel.y < -1.0e-6f ? (pos.y - plane) / vel.y : 0.0f;
        pos = v3_sub(pos, v3_scale(vel, f));
        if (r) {
            r->bounce = pos;
            r->bounce_ok = ((pos.z < 0.0f) == (from.z < 0.0f)) && fabsf(pos.z) <= hl - 0.01f &&
                           fabsf(pos.x) <= hw - 0.01f;                     /* DAT_806dd988 */
        }
        vel = v3_sub(vel, v3_scale(a, f));
        ball_bounce(p, p->serve_restitution, p->table_friction, v3(0, 1, 0), &pos, &vel, &spin);
        a = ball_accel(p, vel, spin, shot_type);
        vel = v3_add(vel, v3_scale(a, f));
        pos = v3_add(pos, v3_scale(vel, f));
        serve_net_check(r, net_y, pos, prev);
        while ((pos.y > plane || vel.y > 0.0f) && t < 24.0f) {
            a = ball_accel(p, vel, spin, shot_type);
            t += RTT_DT;
            vel = v3_add(vel, v3_scale(a, RTT_DT));
            prev = pos;
            pos = v3_add(pos, v3_scale(vel, RTT_DT));
            serve_net_check(r, net_y, pos, prev);
            if (r && pos.y > r->max_h) r->max_h = pos.y;
        }
        float f2 = (pos.y - plane) / vel.y;
        pos = v3_sub(pos, v3_scale(vel, f2));
    }
    return pos;
}

/* one serve-table row: ComputePostHitVel speed mode (param_7 = 1) with the record param_11 */
typedef struct {
    Vec3  vel, target;
    float max_h, net_y;
    int   valid, net;
    float score, elev;
} ServeRow;

static void serve_solve_row(const BallParams* p, const TableGeom* g, const ServeSolveInput* in,
                            float net_test_y, float el, Vec3 target, int no_edge_retarget, ServeRow* row) {
    const Vec3 from = in->from;
    const float zsign = from.z > 0.0f ? -1.0f : 1.0f;
    float dx = target.x - from.x, dz = target.z - from.z;
    float hd = sqrtf(dx * dx + dz * dz);
    float az = hd > 1.0e-6f ? asinf(rtt_clampf(dx / hd, -1.0f, 1.0f)) : 0.0f;
    float spd = 5.0f;                                              /* DAT_806dda00 */
    float best = 3.4028235e38f, prev_err = 1.0e8f;
    Vec3 best_v = v3(0, 0, 0);
    float best_maxh = 0.0f, best_nety = 0.0f;
    int best_ok = 1, best_edge = 0, best_net = 0;
    ServeFlightRec r;
    memset(&r, 0, sizeof(r));
    float step_a = 0.0f;
    int iter = 0, halvings = 0, episodes = 0;
    for (;;) {
        int done = 0;
        while (!done) {
            if (iter > 63) { done = 1; break; }
            Vec3 v = shot_vel(spd, az, el, zsign);
            r.bounce_ok = 1; r.edge = 0; r.net = 0;
            Vec3 L = serve_fly(p, g, in->swing_type, net_test_y, from, v, in->spin, &r);
            float ex = target.x - L.x, ez = target.z - L.z;
            float err = ex * ex + ez * ez;
            if (err >= prev_err && iter != 0) break;
            if (err < best) {
                best = err; best_v = v; best_maxh = r.max_h; best_nety = r.net_y;
                best_ok = r.bounce_ok; best_edge = r.edge; best_net = r.net;
                if (err < 1.0e-4f) { done = 1; break; }
            }
            Vec3 La = serve_fly(p, g, in->swing_type, net_test_y, from, shot_vel(spd, wrap_pi(az + 0.1f), el, zsign),
                                in->spin, NULL);
            Vec3 Lb = serve_fly(p, g, in->swing_type, net_test_y, from, shot_vel(spd + 0.1f, az, el, zsign),
                                in->spin, NULL);
            float jxa = 10.0f * (La.x - L.x), jza = 10.0f * (La.z - L.z);
            float jxb = 10.0f * (Lb.x - L.x), jzb = 10.0f * (Lb.z - L.z);
            float det = jxa * jzb - jza * jxb, da, db;
            if (fabsf(det) <= 1.0e-6f) {
                da = fabsf(jxa + jxb) > 1.0e-12f ? ex / (jxa + jxb) : 0.0f;
                db = fabsf(jza + jzb) > 1.0e-12f ? ez / (jza + jzb) : 0.0f;
                if (fabsf(da) < 1.0e-12f && db < 1.0e-12f) { done = 1; break; }
            } else {
                float inv = 1.0f / det;
                da = inv * (ex * jzb - ez * jxb);
                db = inv * (ez * jxa - ex * jza);
            }
            step_a = rtt_clampf(da, -0.5f, 0.5f);
            az = wrap_pi(az + step_a);
            float lo = -spd > -5.0f ? -spd : -5.0f;
            spd += db < lo ? lo : (db > 5.0f ? 5.0f : db);
            prev_err = err;
            halvings = 0;
            iter++;
        }
        if (done) break;
        step_a *= 0.5f;                                            /* the speed step is not undone */
        az = wrap_pi(az - step_a);
        if (halvings == 0 && ++episodes > 4) break;
        if (++halvings >= 5) break;
    }
    Vec3 t = target;
    if (!best_edge || no_edge_retarget) {
        if ((el < 0.0f && !best_ok) || (el >= 0.0f && best_net)) {
            if (best_net && fabsf(target.z) > 0.2f) t.z = target.z * 0.75f;   /* DAT_806dd9f8 / d908 */
        }
        if (!best_ok) {                                            /* last flight's bounce (sic) */
            float hw = g->half_width, hl = g->half_length;
            if (fabsf(r.bounce.x) > hw) t.x -= r.bounce.x - (r.bounce.x <= 0.0f ? 0.01f - hw : hw - 0.01f);
            if (fabsf(r.bounce.z) <= hl) {
                if ((from.z < 0.0f) != (r.bounce.z < 0.0f)) t.z -= r.bounce.z;
            } else {
                t.z -= r.bounce.z - (r.bounce.z <= 0.0f ? 0.01f - hl : hl - 0.01f);
            }
        }
    } else {
        /* the edge retarget of the hit solver, recorded instead of re-solved */
        float ex = p->radius + g->half_width, ez = p->radius + g->half_length;
        t.x = r.edge_pos.x < ex ? (r.edge_pos.x <= -ex ? -(0.002f + ex) : r.edge_pos.x) : 0.002f + ex;
        float zz = r.edge_pos.z < ez ? (r.edge_pos.z < -ez ? -(0.002f + ez) : r.edge_pos.z) : 0.002f + ez;
        float m = fmaxf(2.0f * fabsf(from.x - t.x), 0.5f * (target.y - from.y));
        if (zz > 0.0f)      t.z = zz <= from.z - m ? zz : from.z - m;
        else if (zz < 0.0f) t.z = zz < from.z + m ? from.z + m : zz;
        else                t.z = zz;
    }
    row->vel = best_v;
    row->target = t;
    row->max_h = best_maxh;
    row->net_y = best_nety;
    row->valid = !best_edge && best_ok && !best_net;
    row->net = best_net;
}

typedef struct {
    ServeRow a[32], b[32];
    ServeRow* la[32];        /* +0xd00, sorted by score, high first */
    ServeRow* lb[32];        /* +0xd80 */
    float start, end, step;
    int n;
} ServeTable;

static void serve_insert(ServeRow** list, int i, ServeRow* row) {   /* FUN_800491bc / 80049248 */
    int k = 0;
    while (k < i && !(list[k]->score <= row->score)) k++;
    for (int j = i; j > k; j--) list[j] = list[j - 1];
    list[k] = row;
}

/* FUN_800483b0: a clean net fault; FUN_8004840c the same with max height < 2.5 */
static int serve_net_fault(const ServeRow* r, float hmax) {
    return r->net && r->net_y < 0.89f && r->net_y >= 0.8f && r->max_h >= 0.99f && r->max_h < hmax;
}

/* FUN_800488f4: table A (scorer FUN_80048f20) or N (FUN_80048fd4); returns the rows counted by
 * FUN_80048a5c (valid, max height <= 1.25) */
static int serve_build(const BallParams* p, const TableGeom* g, const ServeSolveInput* in, float net_test_y,
                       ServeTable* T, Vec3 target, int net_table, int retarget) {
    const float h = in->net_height + g->top_y;
    int n_good = 0, i = 0;
    for (float el = T->start; el < T->end && i < 32; i++, el += T->step) {
        ServeRow* r = &T->a[i];
        Vec3 tg = retarget ? r->target : target;
        serve_solve_row(p, g, in, net_test_y, el, tg, 0, r);
        if (!net_table) {
            r->score = h / r->max_h - (r->max_h > 1.25f ? 2.5f : 0.0f);
            if (!r->valid) {
                r->score = r->score - 5.0f + r->net_y / h;
                if (r->net) r->score += 2.0f;
            }
        } else {
            r->score = h;
            int early = 0;
            if (!r->valid || r->net_y < 1.01f * h) {
                r->score -= 5.0f;
                r->valid = 0;
                if (r->max_h > 1.2f) r->score -= 10.0f;
                if (!r->net) early = 1; else r->score += 2.0f;
            }
            if (!early && r->max_h > 1.8f) r->score -= 50.0f;
        }
        r->elev = el;
        serve_insert(T->la, i, r);
        if (r->valid && r->max_h <= 1.25f) n_good++;
    }
    T->n = i;
    return n_good;
}

/* FUN_80048a8c: table L into the second half of A's storage (scorer FUN_800490b8) */
static void serve_build_long(const BallParams* p, const TableGeom* g, const ServeSolveInput* in,
                             float net_test_y, ServeTable* T, Vec3 target) {
    Vec3 d = v3_sub(target, in->from);
    float l2 = v3_dot(d, d);
    float k = l2 != 0.0f ? 1.0f / sqrtf(l2) : 0.0f;
    target = v3_add(target, v3_scale(d, k * 1.5f));               /* DAT_806dd938 */
    int i = 0;
    for (float el = T->start; el < T->end && i < 32; i++, el += T->step) {
        ServeRow* r = &T->b[i];
        serve_solve_row(p, g, in, net_test_y, el, target, 1, r);
        r->elev = el;
        r->score = 0.0f;
        if (!r->valid) {
            r->score = 1.0f;
            if (r->max_h < 2.5f) {
                if (serve_net_fault(r, 2.5f)) {
                    r->score += 2.0f;
                    if (r->max_h < 1.75f) r->score += 0.5f;
                    if (serve_net_fault(r, 1.25f)) r->score += 2.0f;
                } else if (r->net) {
                    r->score += 1.0f + r->net_y;
                }
            }
        }
        if (i == 0) T->lb[0] = r; else serve_insert(T->lb, i, r);
    }
    T->n = i;
}

static int serve_row_index(float t, int n) {
    int i = (int)(0.0f + t * (float)(n - 1));                     /* FUN_8004835c */
    return i < 0 ? 0 : (i > 32 ? 32 : i);
}

void ball_solve_serve(const BallParams* p0, const TableGeom* g, const ServeSolveInput* in,
                      ServeSolveResult* out) {
    BallParams p = *p0;
    p.gravity_scale = p0->serve_gravity_scale;
    const float hw = g->half_width;
    const float net_test_y = in->net_margin + in->net_height + g->top_y;
    static ServeTable A, N;                                        /* ~7 KB each */
    memset(&A, 0, sizeof(A));
    memset(&N, 0, sizeof(N));
    A.start = -1.0471976f; A.end = 1.0471976f;                     /* DAT_806dd98c / 990 */
    A.step = (A.end - A.start) / 31.0f - 1.0e-6f;
    N.start = -0.34906585f; N.end = 0.52359878f;                   /* DAT_806dd998 / 99c */
    N.step = (N.end - N.start) / 31.0f - 1.0e-6f;
    memset(out, 0, sizeof(*out));

    Vec3 tg = in->target;
    tg.y = g->top_y + p.radius;
    float m = fabsf(in->meter);
    if (m < 10.0f) m = 0.0f;
    float depth = 0.025f + rtt_clampf(m * 0.03125f, 0.0f, 1.0f) * (0.5f - 0.025f);
    if (m >= 27.5f && m < 37.0f) depth = 0.475f;                   /* DAT_806d448c */
    Vec3 tn = tg;
    tn.z = in->from.z > 0.0f ? -depth : (in->from.z < 0.0f ? depth : tg.z);
    tn.x = tn.x < 0.0f ? rtt_clampf(tn.x, -0.5f * hw, 0.0f) : rtt_clampf(tn.x, 0.0f, 0.5f * hw);

    int n_good = serve_build(&p, g, in, net_test_y, &A, tg, 0, 0);
    serve_build(&p, g, in, net_test_y, &N, tn, 1, 0);
    Vec3 moved = tg;
    for (int k = 0; !A.la[0]->valid && k < 2; k++) {
        moved = A.la[0]->target;
        n_good = serve_build(&p, g, in, net_test_y, &A, moved, 0, 1);
    }
    int found = A.la[0]->valid;
    float dist = v3_len(v3_sub(moved, tg));
    serve_build_long(&p, g, in, net_test_y, &A, A.la[0]->target);

    ServeRow* pick = A.la[0];
    out->pick = 0; out->row = 0;
    int far = fabsf(in->from.z) >= 2.25f || fabsf(in->from.x) >= 1.75f * hw || in->serve_t < 0.0f;
    int ok_meter = m >= 70.0f && m <= 100.0f;                      /* DAT_806dd9c4 / 9c8 */
    if (far) {
        if (!ok_meter || n_good < 1) {
            pick = A.lb[0]; out->pick = 2;
            if (m > 25.0f && m < 70.0f && n_good > 0) {
                out->row = serve_row_index((m - 25.0f) / 45.0f, n_good);
                pick = A.la[out->row]; out->pick = 0;
            }
        }
    } else if (!ok_meter) {
        if (m < 37.0f || m >= 70.0f || n_good < 1) {
            out->net_target = 1;
            out->net_target_pos = tn;
            pick = N.la[0]; out->pick = 1;
        } else {
            out->row = serve_row_index((m - 37.0f) / 33.0f, n_good);
            pick = A.la[out->row];
        }
    }
    if (dist > 1.0f || !found) {
        int hit = 0;
        for (int i = 0; i < 32 && !hit; i++) {
            pick = A.la[i]; out->pick = 0; out->row = i;
            if (pick && serve_net_fault(pick, 1.25f)) hit = 1;
        }
        if (!hit && !far) {
            for (int i = 0; i < 32 && !hit; i++) {
                pick = A.lb[i]; out->pick = 2; out->row = i;
                if (pick && serve_net_fault(pick, 1.25f)) hit = 1;
            }
        }
        if (!hit && far) { pick = A.lb[0] ? A.lb[0] : &A.b[0]; out->pick = 2; out->row = 0; }
    }
    if (!pick) pick = &A.a[0];
    out->vel = pick->vel;
    out->elev = pick->elev;
    out->good = found;
    if (found) out->post_hit_speed = rtt_clampf(v3_len(pick->vel), 0.0f, 50.0f);   /* DAT_806dd9d8 */
}

Vec3 ball_serve_whiff_vel(int camera_flip) {
    return v3(0.0f, 2.0f, -0.75f * (camera_flip ? 1.0f : -1.0f));   /* DAT_806d5230 / 5234 */
}
