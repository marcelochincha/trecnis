#include "rtt_swing.h"

ShotTier swing_classify(const SwingConfig* c, float speed) {
    if (speed < c->min_swing) return SHOT_NONE;
    if (speed <= c->hard_shot) return SHOT_NORMAL;              /* FUN_8009a734: <= thresholds */
    if (speed <= c->really_hard_shot) return SHOT_HARD;
    return SHOT_REALLY_HARD;
}

void swing_aim_drift(SwingState* s, const SwingConfig* c, AimDir dir) {
    float target = 0.0f;
    switch (dir) {
    case DIR_E:  target = c->aim_e;  break;
    case DIR_W:  target = c->aim_w;  break;
    case DIR_NE: target = c->aim_ne; break;
    case DIR_SE: target = c->aim_se; break;
    case DIR_NW: target = c->aim_nw; break;
    case DIR_SW: target = c->aim_sw; break;
    default: break;                                   /* no input: drift back to center */
    }
    float step = c->dpad_targetting_speed;
    if (s->aim < target)      s->aim = s->aim + step > target ? target : s->aim + step;
    else if (s->aim > target) s->aim = s->aim - step < target ? target : s->aim - step;
}

void swing_charge_tick(SwingState* s, const SwingConfig* c, const PlayerAttribs* a, int axis, float dt) {
    float factor = s->is_ai ? c->ai_spin_charge : c->human_spin_charge;
    s->charge += a->max_charge_rate[s->zone] * factor * dt;
    float cap = a->max_spin_rps[s->zone][axis] * 6.2831853f;   /* rev/s -> rad/s */
    s->charge_spin = s->charge < cap ? s->charge : cap;
}

const SwingBlendRec* swing_find_blend(const SwingSpace* sp, Vec3 p) {
    const SwingBlendRec* best = 0;
    float bd = 1e30f;
    for (int i = 0; i < sp->count; i++) {
        const SwingBlendRec* b = &sp->blends[i];
        float along = p.z - b->contact_point.z;                     /* swing axis = local z [OPEN axis] */
        if (along < -sp->max_dist_behind || along > sp->max_dist_in_front) continue;
        Vec3 d = v3_sub(b->contact_point, p);
        float d2 = v3_dot(d, d);
        if (d2 < bd) { bd = d2; best = b; }
    }
    return best;
}

void swing_start(SwingState* s, const SwingBlendRec* b, float contact_phase, float now, float t_arrive) {
    s->active = 1;
    s->blend = b;
    s->phase = 0.0f;
    s->t_start = now;
    s->t_contact = t_arrive > now ? t_arrive : now + 1e-3f;
    s->contact_phase = contact_phase;
    s->contacted = 0;
}

int swing_update(SwingState* s, float now, float clip_duration) {
    if (!s->active) return 0;
    float before = s->phase;
    if (now < s->t_contact) {
        s->phase = s->contact_phase * (now - s->t_start) / (s->t_contact - s->t_start);
    } else {
        float after = (now - s->t_contact) / (clip_duration > 0 ? clip_duration : 1.0f);
        s->phase = s->contact_phase + after;
    }
    if (s->phase >= 1.0f) { s->phase = 1.0f; s->active = 0; }      /* -> EV_SWING_ANIM_DONE */
    if (!s->contacted && before < s->contact_phase && s->phase >= s->contact_phase) {
        s->contacted = 1;
        return 1;                                                   /* -> EV_PADDLE_CONTACT */
    }
    return 0;
}

/* per-swing-type speed tunables read by the FUN_8011b5a8..0x8011b74c getters (SwingType switch:
 * 7/8/9 -> set 1, 10 -> set 2, anything else -> set 0) [RTT globals 0x806d5f9c.., DATA] */
typedef struct { float flat_lo, flat_hi, top_lo, top_hi, back_lo, back_hi, back_mid; } SpeedTune;
static const SpeedTune k_speed_tune[3] = {
    /* FUN_8011b6f8 74c 5a8 5fc 650 6a4, and 6f8 again as the backspin lo blend end */
    {  8.0f,  9.5f, 10.0f, 11.0f, 7.7f, 9.0f, 8.0f },   /* default */
    {  8.0f, 10.0f, 13.0f, 15.0f, 7.8f, 8.5f, 8.0f },   /* types 7, 8, 9 */
    {  6.5f,  7.5f,  9.5f, 15.0f, 5.7f, 8.7f, 6.5f },   /* type 10 */
};

float swing_post_hit_speed(const ShotSpeedInput* in) {
    Vec3 d = v3_sub(in->ball_pos, in->target);
    float d2 = v3_dot(d, d);
    float lo, hi;
    if (in->swing_type == 3) {
        float a = rtt_lerpf(0.525f, 1.0f, rtt_clampf((d2 / 10.0f) * (d2 / 10.0f), 0.0f, 1.0f));
        lo = 5.5f * a;                       /* DAT_806d5fb4/5fbc, same value with or without backspin */
        hi = 7.5f * a;                       /* DAT_806d5fb0/5fb8 */
    } else if (in->swing_type == 6) {
        lo = 16.0f; hi = 19.0f;
    } else if (in->swing_type == 11) {
        lo = 19.0f; hi = 22.0f;
    } else {
        int t = in->swing_type;
        const SpeedTune* k = &k_speed_tune[(t >= 7 && t <= 9) ? 1 : (t == 10 ? 2 : 0)];
        float side = fabsf(in->spin_side);
        if (in->spin_x > 0.0f) {
            float f = rtt_clampf((d2 / 16.0f) * (d2 / 16.0f), 0.6f, 1.0f);
            float u = rtt_clampf(in->spin_x + side, 0.0f, 1.0f);
            lo = rtt_lerpf(f * k->back_lo, f * k->back_mid, u);
            hi = f * k->back_hi;              /* the FUN_8011b74c read here is discarded [RTT] */
        } else {
            float b = -in->spin_x;          /* topspin amount */
            float dist = rtt_lerpf(0.75f, 1.0f, (d2 / 15.0f) * (d2 / 15.0f));
            float below = in->table_top - in->ball_pos.y;
            if (below < 0.0f) below = 0.0f;
            float low = rtt_lerpf(1.0f, 0.75f, rtt_clampf(below / 0.5f * in->table_top, 0.0f, 1.0f));
            float sd = rtt_lerpf(1.0f, 0.88f, side);   /* DAT_806d44a0 / 44a4 */
            float m = sd * (dist * low);
            lo = rtt_lerpf(m * k->flat_lo, m * k->top_lo, b);
            hi = rtt_lerpf(m * k->flat_hi, m * k->top_hi, b);
        }
    }
    return lo + in->hit_force * in->hit_force * (hi - lo);
}

Vec3 swing_hit_spin(const PlayerAttribs* a, const HitSpinInput* in) {
    /* dominant axis (FUN_8011b7a8; DAT_806def74 = -1 makes topspin positive) */
    float x = in->spin_axis.x, y = in->spin_axis.y;
    float fx = x > 0.0f ? -x : (x < 0.0f ? -x : 0.0f);   /* back -> negative, top -> positive */
    float fy = y > 0.0f ? -y : (y < 0.0f ? -y : 0.0f);
    if (fabsf(fx) > 0.0f && fabsf(fy) > 0.0f) {
        if (fabsf(fx) <= fabsf(fy)) fx = 0.0f; else fy = 0.0f;
    }
    int axis = 0;                                         /* 0 top, 1 back, 2 side */
    if (fabsf(fx) > 0.0f) axis = fx > 0.0f ? 0 : 1;
    else if (fabsf(fy) > 0.0f) axis = 2;
    int set = in->is_serve ? 3 : (in->swing_type == 10 ? 2 : (in->swing_type == 8 || in->swing_type == 9 ? 1 : 0));
    float k = 2.0f * 3.14159265f * a->max_spin_rps[set][axis];
    Vec3 s = v3_scale(in->spin_axis, k);
    float c = cosf(in->roll), sn = sinf(in->roll), sy = s.y, sz = s.z;   /* FUN_8020245c */
    s.z = sy * sn + sz * c;
    s.y = sy * c - sz * sn;
    if (in->ball_pos.z <= 0.0f) s = v3_scale(s, -1.0f);
    if (in->flip_side) s.y = -s.y;
    /* FUN_8004605c: the x (top/back) component turns about the horizontal perpendicular */
    float dx = in->target.x - in->ball_pos.x, dz = in->target.z - in->ball_pos.z;
    float l = sqrtf(dx * dx + dz * dz);
    if (l > 0.0f) { dx /= l; dz /= l; }
    float px, pz;
    if (in->ball_pos.z > 0.0f) { px = -dz; pz = dx; } else { px = dz; pz = -dx; }
    return v3(s.x * px, s.y, s.x * pz);
}

void swing_charge_tick(SwingChargeState* st, const SwingTickInput* in) {
    static const SwingAttrSet k_flat_attr = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    static const PowerZone k_flat_zone = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    const SwingAttrSet* a = in->flat ? &k_flat_attr : in->attr;
    const PowerZone* z = in->flat ? &k_flat_zone : in->zone;
    st->force = in->flat ? (in->no_charge_flag ? 0.0f : 1.0f)
                         : in->mult * a->strength;                   /* FUN_8011c028 -> hit +0x14 */
    float sum = in->in[0] + in->in[1] + in->in[2] + in->in[3];
    float coef[4];
    float mult = in->flat ? 1.0f : in->mult;
    coef[0] = coef[1] = rtt_clampf(mult * a->side * z->side_spin, 0.0f, 0.99f);
    coef[2] = rtt_clampf(mult * a->top * z->vert_spin, 0.0f, 0.99f);
    coef[3] = rtt_clampf(mult * a->back * z->vert_spin, 0.0f, 0.99f);
    if (sum > 0.0f) {
        for (int i = 0; i < 4; i++) {
            float frac = in->in[i] / sum;
            float target = in->dt * (1000.0f * (frac * in->in[i])) / (in->max_charge_time * (1.0f - coef[i]));
            float step = in->charge_rate * coef[i] * in->dt;
            float c = st->charge[i];
            if (c < target) { c += step; if (c > target) c = target; }
            else if (c > target) { c -= step; if (c < target) c = target; }
            if (c < in->in[i]) c = in->in[i];
            st->charge[i] = c;
        }
    } else {
        st->charge[0] = st->charge[1] = st->charge[2] = st->charge[3] = 0.0f;
    }
    st->hit_force = z->strength * st->force;                         /* FUN_8011c030 -> hit +0x18 */
    st->accuracy = z->accuracy * (mult * a->accuracy);

    int damp = !in->charge_allowed || in->zone_snap || in->no_charge_flag || in->swing_type == 3;
    if (in->meter <= 0.0f && in->swing_type > 7) damp = 1;
    if (damp)
        for (int i = 0; i < 4; i++) if (st->charge[i] > 0.0f) st->charge[i] = 0.1f;
    swing_axis_step(st, in);
}

void swing_axis_step(SwingChargeState* st, const SwingTickInput* in) {
    float scale = (in->meter <= 0.0f && in->swing_type > 7) ? 0.1f : 1.0f;
    float cap = (in->swing_type >= 8 && !in->itz_full) ? 0.8f : 1.0f;
    if (st->charge[0] > 0.0f || st->charge[1] > 0.0f)
        cap = rtt_lerpf(0.25f, 1.0f, rtt_clampf(1.0f - in->side_cap, 0.0f, 1.0f));
    /* FUN_8011ad40: accumulate, then clamp the length to [0, cap] */
    Vec3 v = st->spin_axis;
    v.x += scale * st->charge[3] / 1000.0f - scale * st->charge[2] / 1000.0f;
    v.y += scale * st->charge[0] / 1000.0f - scale * st->charge[1] / 1000.0f;
    float l2 = v3_dot(v, v);
    if (l2 > cap * cap) v = v3_scale(v, cap / sqrtf(l2));
    st->spin_axis = v;
}

float swing_contact_hit_force(const SwingConfig* c, float hit_force, ShotTier tier) {
    if (tier == SHOT_REALLY_HARD) return hit_force * c->force_mult[2];
    if (tier == SHOT_HARD) return hit_force * c->force_mult[1];
    return hit_force;
}

Vec3 swing_timed_spin_axis(const float in[4], float pct, float mult) {
    float sum = in[0] + in[1] + in[2] + in[3];
    float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (sum > 0.0f) {
        float k = pct / 100.0f;                                    /* DAT_806de328 = 100 */
        for (int i = 0; i < 4; i++) p[i] = k * ((in[i] / sum) * mult);
    }
    Vec3 v = v3(p[3] - p[2], p[0] - p[1], 0.0f);
    float l2 = v3_dot(v, v);
    if (l2 > 1.0f) v = v3_scale(v, 1.0f / sqrtf(l2));
    return v;
}

/* ---------------------------------------------------------------- SwingType */
int swing_base_type(int meter_armed, int meter_soft) {
    int t = SWING_TYPE_NORMAL;
    if (meter_armed) t = SWING_TYPE_ITZ;
    if (meter_soft) t = SWING_TYPE_SOFT;
    return t;
}

int swing_specialty_type(int type, int sample_specialty) {
    return sample_specialty && type < 8 ? SWING_TYPE_SPECIALTY : type;
}

int swing_finalize_type(int type, int itz_level_active, float itz_meter, SwingTypeFlags* fl) {
    if (!itz_level_active || type == SWING_TYPE_SOFT) {
        if (type >= 8 && itz_meter <= 0.0f) {
            fl->itz_shot = 0;
            type = type == SWING_TYPE_ITZ_SLAM ? SWING_TYPE_SLAM : SWING_TYPE_NORMAL;
        }
    } else {
        type = SWING_TYPE_ITZ;
        fl->itz_shot = 1;
    }
    return type;
}

/* FUN_8011c038: HitForce = clamp(HitForce + add, lo, hi) */
static float add_hit_force(float hf, float add, float lo, float hi) { return rtt_clampf(hf + add, lo, hi); }

int swing_classify_human(SwingClassifyState* st, int* type, float* hit_force, SwingTypeFlags* fl,
                         const SwingHumanInput* in) {
    if (st->done) return 0;
    if (*type >= 8) goto finalize;
    if (st->win_start <= 0.0f) {
        if (in->gesture <= 0.0f && !in->soft_button && !in->power_button) {
            *type = SWING_TYPE_NORMAL;
            goto finalize;
        }
        if (in->soft_button) {
            st->win_start = in->now;
            *type = SWING_TYPE_SOFT;
        } else if (in->power_button) {
            st->win_start = in->now;
            *type = SWING_TYPE_ITZ;
            fl->flat = 1;
            fl->itz_shot = 1;
            goto finalize;
        } else if (in->gesture > 0.0f) {
            st->win_start = in->now;
            st->gesture_seen = 1;
        }
    }
    if (st->gesture_max < in->gesture) st->gesture_max = in->gesture;
    if (in->now - st->win_start <= 2.0f * in->dt) {                 /* DAT_806de370 = 2 frames */
        if (!st->gesture_seen) *hit_force = add_hit_force(*hit_force, 0.5f, 0.0f, 0.6f);
        return 0;
    }
    st->win_start = 0.0f;
    if (in->hit_ready) {
        if (!st->gesture_seen) {
            *type = SWING_TYPE_SOFT;
            *hit_force = add_hit_force(*hit_force, 0.5f, 0.0f, 0.6f);
        } else if (in->gesture >= 0.0f && in->gesture < 0.5f) {
            /* a weak gesture becomes a soft shot: HitForce from the strongest sample */
            *type = SWING_TYPE_SOFT;
            *hit_force = rtt_clampf(st->gesture_max * 0.5f, 0.5f, 0.6f);
        }
    }
finalize:
    *type = swing_finalize_type(*type, in->itz_level_active, in->itz_meter, fl);
    st->done = 1;
    return 1;
}

int swing_classify_ai(SwingClassifyState* st, int* type, float* hit_force, SwingTypeFlags* fl,
                      float now, float dt, float ai_force, int itz_level_active, float itz_meter) {
    if (st->done) return 0;
    if (*type == SWING_TYPE_SOFT) {
        if (st->win_start == 0.0f) { st->win_start = now; return 0; }
        if (now - st->win_start <= 2.0f * dt) return 0;
        st->win_start = 0.0f;
        *hit_force = ai_force;
    }
    *type = swing_finalize_type(*type, itz_level_active, itz_meter, fl);
    st->done = 1;
    return 1;
}

int swing_slam_type(int type, int hit_zone) {
    if (hit_zone != 4 && hit_zone != 12) return type;
    if (type == SWING_TYPE_NORMAL) return SWING_TYPE_SLAM;
    if (type == 9 || type == 10) return SWING_TYPE_ITZ_SLAM;
    return type;
}

/* ---------------------------------------------------------------- HitZone selection */
unsigned swing_shot_flags(int itz, int soft, int top, int back, int side_a, int side_b) {
    unsigned f = itz ? (SHOT_FLAG_VALID | SHOT_FLAG_ITZ) : SHOT_FLAG_VALID;
    if (soft) f |= SHOT_FLAG_SOFT;
    if (top) f |= SHOT_FLAG_TOP;
    if (back) f |= SHOT_FLAG_BACK;
    if (side_a) f |= SHOT_FLAG_SIDE_A;
    if (side_b) f |= SHOT_FLAG_SIDE_B;
    return f;
}

int swing_solver_slot(unsigned f, int* fallback, int* fb_itz, int* fb_soft) {
    int topish = !(f & SHOT_FLAG_BACK);                 /* uVar3: no backspin -> the ts variant */
    int kind = (f & SHOT_FLAG_ITZ) ? 1 : ((f & SHOT_FLAG_SOFT) ? 2 : 0);
    /* local_38 = { +0x68 bs, +0x64 ts, +0x70 bs_itz, +0x6c ts_itz, +0x74 sft, +0x74 sft } */
    static const int k_slot[6] = { SOLVER_BS, SOLVER_TS, SOLVER_BS_ITZ, SOLVER_TS_ITZ, SOLVER_SFT, SOLVER_SFT };
    *fallback = kind ? k_slot[topish] : -1;
    *fb_itz = kind == 1;
    *fb_soft = kind == 2;
    return k_slot[kind * 2 + topish];
}

int swing_set_eligible(const SwingSetDef* set, const ZoneSelectInput* in, int fb_itz, int fb_soft) {
    if (!set->category_active) return 0;
    if (set->specialty_blocked) return 0;
    if (in->all_eligible) return 1;                     /* solver +0x168 (IsEligible) */
    if (fb_itz && !set->allow_fallback_itz) return 0;
    if (fb_soft && !set->allow_fallback_soft) return 0;
    if (set->forward_lunge) return 1;
    if (set->short_and_low) {
        if (in->short_ball) return 0;
        float lo = -1.5f, hi = 1.0f;                     /* DAT_806d57e8 / 57f0 */
        if (set->hit_zone == 10) { lo = -1.0f; hi = 1.5f; }   /* BH_Lunge: 57ec / 57f4 */
        return in->reach.z > 2.8f || in->reach.x < lo || in->reach.x > hi;   /* 57e4 = 2.8 */
    }
    if (set->hit_zone == 7 || set->hit_zone == 15) {     /* FH_SOT / BH_SOT */
        if (in->short_ball) return 0;
        if (set->hit_zone == 7) return in->reach.x + set->swingdata_min_x > in->sot_limit;
        return in->reach.x + set->swingdata_max_x < -in->sot_limit;
    }
    if (in->short_ball || in->near_table) return 1;
    return set->hit_zone != 6 && set->hit_zone != 14;   /* OTT sets only for short / near balls */
}

/* pcrSwingSpace__IsOutOfMoverBounds (0x800db740, on: DAT_806d5998 = 1): out on an axis when the
 * mover translation leaves the reach box there AND moves more than 0.01 on it (DAT_806debdc), so a
 * swing that barely moves always fits [RTT] */
static int out_of_mover_bounds(const SwingBlendRec* b, const ZoneSelectInput* in) {
    const float k = 0.01f;
    Vec3 m = b->total_mover_trans;
    if ((m.x < in->mover_min.x || m.x > in->mover_max.x) && fabsf(m.x) > k) return 1;
    if ((m.z < in->mover_min.z || m.z > in->mover_max.z) && fabsf(m.z) > k) return 1;
    return 0;
}

/* a blend that is one clip alone (weight 1): its TotalMoverTrans is that clip's root translation at
 * its end, which FUN_800d3f20 samples (every clip of every set has one in swingspace.xml) */
static int pure_blend(const SwingBlendRec* b) {
    for (int k = 0; k < 3; k++) if (b->anim[k] >= 0 && b->w[k] == 1.0f) return 1;
    return 0;
}

/* FUN_801fb7e8 + SquaredDistanceToContactPoint: closest point of the segment p0 + t*d, t in [0,1] */
static float seg_dist2(Vec3 p0, Vec3 d, Vec3 c) {
    float dd = v3_dot(d, d), t = 0.0f;
    if (dd > 0.0f) t = rtt_clampf(v3_dot(v3_sub(c, p0), d) / dd, 0.0f, 1.0f);
    Vec3 q = v3_add(p0, v3_scale(d, t));
    Vec3 e = v3_sub(q, c);
    return v3_dot(e, e);
}

/* pcrSwingSpace__FindNearestBlend (segment) / FUN_800db5ec (point, d = 0) */
static const SwingBlendRec* nearest_blend(const SwingSetDef* set, const ZoneSelectInput* in, Vec3 p0,
                                          Vec3 d, float* out_d2) {
    int check_volume = set->hit_zone != 6 && set->hit_zone != 14;     /* FUN_800d411c: not OTT */
    const SwingBlendRec* best = 0;
    float bd = 0.0f;
    for (int i = 0; i < set->blend_count; i++) {
        const SwingBlendRec* b = &set->blends[i];
        if (out_of_mover_bounds(b, in)) continue;
        if (check_volume && in->in_table_volume && in->in_table_volume(b->contact_point)) continue;
        float d2 = seg_dist2(p0, d, b->contact_point);
        if (!best || d2 < bd) { best = b; bd = d2; }
    }
    if (best) *out_d2 = bd;
    return best;
}

/* FUN_800edc1c: the contiguous run of samples inside the box, scanning back from hi; with none
 * inside, the single sample nearest the box. Returns the count, [*lo, *hi] the range. */
static int samples_in_box(const LocalBallSample* s, int n, Vec3 mn, Vec3 mx, int* lo, int* hi) {
    int top = *hi < 0 ? n - 1 : *hi, bot = *lo < 0 ? 0 : *lo;
    if (top > n - 1) top = n - 1;
    if (bot > top) bot = top;
    int r_lo = -1, r_hi = -1, nearest = -1, inside_seen = 0;
    float nd = 0.0f;
    for (int i = top; i >= bot; i--) {
        Vec3 p = s[i].pos;
        float ox = p.x < mn.x ? mn.x - p.x : (p.x > mx.x ? mx.x - p.x : 0.0f);
        float oy = p.y < mn.y ? mn.y - p.y : (p.y > mx.y ? mx.y - p.y : 0.0f);
        float oz = p.z < mn.z ? mn.z - p.z : (p.z > mx.z ? mx.z - p.z : 0.0f);
        if (ox == 0.0f && oy == 0.0f && oz == 0.0f) {
            if (r_hi < 0) r_hi = i;
            r_lo = i;
            inside_seen = 1;
        } else {
            if (inside_seen) break;
            float d2 = ox * ox + oy * oy + oz * oz;
            if (nearest < 0 || d2 < nd) { nearest = i; nd = d2; }
        }
    }
    if (r_lo < 0) { r_lo = r_hi = nearest; }
    *lo = r_lo; *hi = r_hi;
    return r_lo < 0 ? 0 : r_hi - r_lo + 1;
}

/* FUN_800d0880: fill the target for a blend met at sample k */
static void make_target(const SwingSetDef* set, int index, const ZoneSelectInput* in, int k,
                        const SwingBlendRec* b, float d2, ZoneTarget* t) {
    const LocalBallSample* sm = &in->s[k];
    memset(t, 0, sizeof(*t));
    t->set = index;
    t->hit_zone = set->hit_zone;
    t->sample_valid = 1;
    t->hittable = sm->hittable;
    t->t_sample = sm->t;
    t->contact_frame = set->contact_frame;
    t->blend = b;
    t->dist2 = d2;
    /* FUN_800d331c: (sample time - now) - frames still to play / 30 - lookahead (DAT_806dea6c = 30;
     * the lookahead is BuildTargetResult's f1, stored at +0x11c) */
    t->time_key = (sm->t - in->now) - (float)(set->contact_frame - in->start_frame) / 30.0f - in->lookahead;
    /* FUN_800d2c88: contact further away than the start-to-contact limit */
    if (in->max_start_to_contact < sm->t - in->now && t->time_key > RTT_DT) t->flags |= TGT_TOO_EARLY;
    /* FUN_8014ac40 / FUN_800d2bf8: inside the aim-assist reach at that time */
    const BallFudgeZone* fz = &in->fudge[set->hit_zone];
    float reach = rtt_clampf((sm->t - in->t_first_hittable) * fz->dist_per_sec, fz->min_dist, fz->max_dist);
    if (d2 < reach * reach) t->flags |= TGT_IN_REACH;
}

int swing_target_valid(const ZoneTarget* t) {
    return t->sample_valid && (t->flags & TGT_IN_REACH) && t->time_key >= 0.0f && t->hittable &&
           !(t->flags & (TGT_FLAG4 | TGT_FLAG8));
}

int swing_target_better(const ZoneTarget* a, const ZoneTarget* b, float now) {
    if (a->sample_valid && !b->sample_valid) return 1;
    if (!a->sample_valid) return 0;
    int va = swing_target_valid(a), vb = swing_target_valid(b);
    if (va && !vb) return 1;
    if (va && vb)
        return a->t_sample - now < 0.4f &&                       /* DAT_806dea80 */
               (a->contact_frame < b->contact_frame || a->dist2 < b->dist2);
    if (vb) return 0;
    /* neither valid */
    int a8 = (a->flags & TGT_FLAG8) != 0, b8 = (b->flags & TGT_FLAG8) != 0;
    if (a8 != b8) return a8;
    if (a->hittable != b->hittable) return a->hittable;
    int a1 = (a->flags & TGT_IN_REACH) != 0, b1 = (b->flags & TGT_IN_REACH) != 0;
    if (a1 != b1) return a1;
    if (!a1) return a->dist2 < b->dist2;
    int ka = a->time_key >= 0.0f, kb = b->time_key >= 0.0f;
    if (ka != kb) return ka;
    if (!ka) return b->time_key < a->time_key;
    return 0;
}

int swing_set_target(const SwingSetDef* set, int index, const ZoneSelectInput* in, ZoneTarget* out) {
    const int k_max = 10;
    if (in->n < 2) return 0;
    /* FUN_800d3e8c: some clip of the set (pcrSwingData +0x5c) must fit the reach box, its root
     * translation at the clip's end (FUN_800d3f20) [RTT] */
    int any = 0;
    for (int i = 0; i < set->blend_count && !any; i++)
        any = pure_blend(&set->blends[i]) && !out_of_mover_bounds(&set->blends[i], in);
    if (!any) return 0;
    /* FUN_800d1108 (DAT_806d57e1 = 1): the box widened by half a sample step + MaxFudgeDist */
    float grow = 0.5f * v3_len(v3_sub(in->s[0].pos, in->s[1].pos)) + in->fudge[set->hit_zone].max_dist;
    Vec3 g = v3(grow, grow, grow);
    int lo = -1, hi = -1;
    int cnt = samples_in_box(in->s, in->n, v3_sub(set->box_min, g), v3_add(set->box_max, g), &lo, &hi);
    if (cnt > k_max) {
        int lo2 = lo, hi2 = hi;
        int c2 = samples_in_box(in->s, in->n, set->box_min, set->box_max, &lo2, &hi2);
        if (c2 > k_max) { lo = hi2 - k_max + 1; hi = hi2; }
        else {
            int extra = k_max - c2, up = (extra + 1) / 2, down = extra / 2;
            if (hi - hi2 < up) up = hi - hi2;
            if (lo2 - lo < down) down = lo2 - lo;
            hi = hi2 + up; lo = lo2 - down;
        }
        cnt = hi - lo + 1;
    }
    if (cnt <= 0) return 0;
    int found = 0;
    ZoneTarget cur;
    if (cnt == 1) {
        float d2;
        const SwingBlendRec* b = nearest_blend(set, in, in->s[lo].pos, v3(0, 0, 0), &d2);
        if (!b) return 0;
        make_target(set, index, in, lo, b, d2, &cur);
        if ((!set->short_and_low && !set->forward_lunge) || swing_target_valid(&cur)) { *out = cur; found = 1; }
        return found;
    }
    /* segments from the latest back; the later end only moves on when a blend was found */
    Vec3 later = in->s[hi].pos;
    for (int k = hi - 1; k >= lo; k--) {
        Vec3 p = in->s[k].pos;
        float d2;
        const SwingBlendRec* b = nearest_blend(set, in, p, v3_sub(later, p), &d2);
        if (!b) continue;
        make_target(set, index, in, k + 1, b, d2, &cur);
        if ((!found || swing_target_better(&cur, out, in->now)) &&
            ((!set->short_and_low && !set->forward_lunge) || swing_target_valid(&cur))) {
            *out = cur;
            found = 1;
        }
        later = p;
    }
    return found;
}

int swing_category_active(int category, const CategoryInput* in) {
    if (in->all_active) return 1;
    if (in->latch && (category == ACT_CAT_EXT_LEFT || category == ACT_CAT_EXT_RIGHT) && in->latch[category])
        return 1;                                                  /* FUN_800ed194, table 0x80462d10 */
    float x = in->stick_x, y = in->stick_y, l2 = x * x + y * y;
    int zero = l2 == 0.0f;                                         /* FUN_800ed590 */
    float k = zero ? 0.0f : 1.0f / sqrtf(l2);
    const float c = 0.70700002f;                                   /* DAT_806deda8 */
    int fwd = !zero && y * k > c;                                  /* FUN_800ed260: (0, 1) */
    int left = !zero && -x * k > c;                                /* FUN_800ed4c4: (-1, 0) */
    int right = !zero && x * k > c;                                /* FUN_800ed3f8: (1, 0) */
    int back = !zero && -y * k > c;                                /* FUN_800ed32c: (0, -1) */
    int neg_x = x < 0.0f;                                          /* FUN_800ed5c8 */
    int near_ = fabsf(in->speed) < 2.7f;                           /* FUN_800ed5ec, DAT_806d5be4 */
    switch (category) {
    case ACT_CAT_ALWAYS:      return 1;
    case ACT_CAT_NORMAL:      return !fwd;
    case ACT_CAT_NORMAL_NEAR: return !fwd && near_;
    case ACT_CAT_NORMAL_FAR:  return !fwd && !near_;
    case ACT_CAT_EXT_LEFT:    return left;
    case ACT_CAT_EXT_RIGHT:   return right;
    case ACT_CAT_DEF_NEAR:    return fwd && near_;
    case ACT_CAT_DEF_FAR:     return fwd && !near_;
    case ACT_CAT_FORWARD:     return back;
    case ACT_CAT_DEF_LEFT:    return fwd && neg_x;
    case ACT_CAT_DEF_RIGHT:   return fwd && !neg_x;
    case ACT_CAT_PROT_LEFT:   return !zero && !fwd && neg_x && !near_;
    case ACT_CAT_PROT_RIGHT:  return !zero && !fwd && !neg_x && !near_;
    default:              return 0;
    }
}

int swing_specialty_blocked(int specialty, int disable_if_fails, unsigned f) {
    if (!(f & 1) || !specialty || !disable_if_fails) return 0;
    int top = (f & 8) != 0, back = (f & 0x10) != 0, side = (f & 0x60) != 0;
    int match = specialty == 1 ? (top && !back && !side) :
                specialty == 2 ? (!top && back && !side) :
                specialty == 3 ? (!top && !back && side) : 0;
    return !match;
}

int swing_select_set(const SwingSetDef* sets, int count, const ZoneSelectInput* in, int fb_itz,
                     int fb_soft, ZoneTarget* best) {
    int found = 0;
    memset(best, 0, sizeof(*best));
    best->set = -1;
    for (int i = 0; i < count; i++) {
        const ZoneSelectInput* use = in;
        ZoneSelectInput li;
        if (in->per_set) {                                         /* FUN_800d3e08 / FUN_800d0880 */
            li = *in;
            li.start_frame = swing_start_frame(sets[i].contact_frame, sets[i].contact_tol_pct, in->human);
            li.max_start_to_contact = swing_too_early_limit(in->early, sets[i].contact_frame, in->soft,
                                                            in->swing_started, sets[i].max_start_to_contact,
                                                            sets[i].far_max_start_to_contact, in->far);
            use = &li;
        }
        if (!swing_set_eligible(&sets[i], use, fb_itz, fb_soft)) continue;
        ZoneTarget t;
        if (!swing_set_target(&sets[i], i, use, &t)) continue;
        if (!found || swing_target_better(&t, best, in->now)) { *best = t; found = 1; }
    }
    return found;
}

void swing_space_box(const SwingBlendRec* blends, int n, Vec3* mn, Vec3* mx) {
    *mn = *mx = n > 0 ? blends[0].contact_point : v3(0, 0, 0);
    for (int i = 1; i < n; i++) {
        Vec3 c = blends[i].contact_point;
        if (c.x < mn->x) mn->x = c.x;
        if (c.x > mx->x) mx->x = c.x;
        if (c.y < mn->y) mn->y = c.y;
        if (c.y > mx->y) mx->y = c.y;
        if (c.z < mn->z) mn->z = c.z;
        if (c.z > mx->z) mx->z = c.z;
    }
}

int swing_start_frame(int contact_frame, float contact_tol_pct, int human) {
    float tol = human ? 0.6f : contact_tol_pct;                   /* DAT_806deab4 */
    float f = 1.0f / (1.0f - tol);                                 /* DAT_806dea9c = 1 */
    if (f < 1.0f) f = 1.0f;
    int s = contact_frame - (int)((float)contact_frame / f);
    if (s < 0) s = 0;
    else if (s > contact_frame - 1) s = contact_frame - 1;
    return s;
}

float swing_too_early_limit(const EarlyOutCfg* c, int contact_frame, int soft, int swing_started,
                            float max_start_to_contact, float far_max_start_to_contact, int far) {
    if (swing_started) return far ? far_max_start_to_contact : max_start_to_contact;
    float m = soft ? c->soft_shot_mult : 1.0f;                     /* else DAT_806dea38 = 1 */
    float t_contact = (float)contact_frame / 30.0f;                /* FUN_800d35f4 */
    if (t_contact <= c->min_early_out / c->max_early_out) return c->min_early_out * m;
    return m * (c->max_early_out * t_contact);
}

int swing_contact_in_table_volume(Vec3 contact, Vec3 player_pos, float table_length, float ball_radius) {
    Vec3 p = player_pos;
    if (p.z < 0.0f) { p.x = -p.x; p.z = -p.z; }                   /* RotateVec2ByAngle(pi) */
    float hx = 0.5f * 2.3f;                                        /* 0.5 * DAT_806d5c00 */
    float x0 = -hx - p.x, x1 = hx - p.x;
    float y0 = 0.0f - p.y, y1 = 0.8f + ball_radius - p.y;          /* DAT_806d5c04 */
    float z0 = 0.0f - p.z, z1 = 0.5f * table_length + 0.3f - p.z;  /* DAT_806d5c08 */
    float z2 = z1 + 1.0f;                                          /* DAT_806d5c0c */
    Vec3 c = contact;
    if (c.x >= x0 && c.x <= x1 && c.y >= y0 && c.y <= y1 && c.z >= z0 && c.z <= z1) return 1;
    if (c.x >= x0 && c.x <= x1 && c.y >= y0 && c.y <= y1 && c.z >= z1 && c.z <= z2) {
        float frac = z1 != z2 ? (c.z - z2) / (z1 - z2) : 1.0f;     /* DAT_806dedfc */
        return c.y <= frac * y1;
    }
    return 0;
}

/* ---------------------------------------------------------------- TableTgtPos: target grid */

static const float AIM_DIR_X[9] = { 0, 0, -0.707f, -1, -0.707f, 0, 0.707f, 1, 0.707f };  /* DAT_806de590/594 */
static const float AIM_DIR_Z[9] = { 0, 1, 0.707f, 0, -0.707f, -1, -0.707f, 0, 0.707f };

static float clampf_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void aim_grid_reset_swing(AimGrid* g) {
    g->serve_frozen = 0;
    g->offset = g->drift = g->drift_prev = v3(0, 0, 0);
    g->spin_x = g->spin_z = 0.0f;
    g->pitch = 0;
    g->slow = 0;
}

void aim_target_reset_point(AimTarget* a, int side, float* wobble) {
    for (int k = 0; k < 2; k++) {
        AimGrid* g = &a->grid[k];
        g->side = side;
        g->area_set = k;                                           /* FUN_800aad64: +0x60 -> 1 */
        aim_grid_reset_swing(g);
        g->cell_depth = g->cell_lat = 1;
    }
    *wobble = 0.001f;                                              /* DAT_806d52a0 */
}

void aim_target_reset_swing(AimTarget* a, float* wobble) {
    for (int k = 0; k < 2; k++) {
        aim_grid_reset_swing(&a->grid[k]);
        a->grid[k].cell_depth = a->grid[k].cell_lat = 1;           /* FUN_80097d08 */
    }
    a->edge_risk = 0;
    *wobble = 0.001f;
}

int aim_dir_cell(int dir, int* depth, int* lat) {
    static const int D[9] = { -1, 2, 2, 1, 0, 0, 0, 1, 2 };
    static const int L[9] = { -1, 1, 0, 0, 0, 1, 2, 2, 2 };
    if (dir < 1 || dir > 8) return 0;
    *depth = D[dir];
    *lat = L[dir];
    return 1;
}

Vec3 aim_cell_point(const TableTargetCfg* t, int side, int area_set, int depth, int lat) {
    const float* f = &t->frac[area_set ? 6 : 3];                   /* FUN_8011d08c.. / d0a4.. */
    float cz = t->length * 0.25f * (side ? -1.0f : 1.0f);          /* FUN_8011cc50 */
    float hl = t->length * 0.5f;
    float ax = -f[0] * t->width, bx = f[0] * t->width;
    float az, bz;
    if (side == 0) { bz = cz + hl * f[2]; az = cz - hl * f[1]; }
    else           { bz = cz + hl * f[1]; az = cz - hl * f[2]; }
    float tx = ((float)lat / 3.0f + (float)(lat + 1) / 3.0f) * 0.5f;   /* FUN_800a8cf0 */
    float tz = ((float)depth / 3.0f + (float)(depth + 1) / 3.0f) * 0.5f;
    Vec3 p;
    if (side == 0) { p.x = ax + tx * (bx - ax); p.z = az + tz * (bz - az); }
    else           { p.x = bx + tx * (ax - bx); p.z = bz + tz * (az - bz); }
    p.y = 0.01f + t->height;                                       /* DAT_806d529c */
    return p;
}

void aim_spin_nudge(AimGrid* g, const TableTargetCfg* t, float side_a, float side_b, float top,
                    float back, int serving, int x_sign) {
    const float d = 0.0015f;                                       /* DAT_806de57c */
    float dz = d * (serving ? 0.75f : 1.0f);
    g->spin_x += (float)x_sign * (side_a - side_b) * d;
    g->spin_z += (g->side ? (back - top) : (top - back)) * dz;
    /* FUN_800a7b30: keep the nudged point inside the chosen cell (cell corners are t +- 1/6) */
    const float* f = &t->frac[g->area_set ? 6 : 3];
    float hx = f[0] * t->width * 2.0f / 6.0f;
    float hz = t->length * 0.5f * (f[1] + f[2]) / 6.0f;
    g->spin_x = clampf_(g->spin_x, -hx, hx);
    g->spin_z = clampf_(g->spin_z, -hz, hz);
}

/* FUN_800a7ff4: the direction points away from the table centre, seen from the current point */
static int aim_dir_outward(const AimGrid* g, const TableTargetCfg* t, int dir) {
    Vec3 p = aim_cell_point(t, g->side, g->area_set, g->cell_depth, g->cell_lat);
    p = v3(p.x + g->offset.x + g->spin_x, p.y + g->offset.y, p.z + g->offset.z + g->spin_z);
    float n = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    if (n <= 0.0f || dir < 1 || dir > 8) return 0;
    float dot = (p.x * AIM_DIR_X[dir] + p.z * AIM_DIR_Z[dir]) / n;
    return g->side == 0 ? dot >= 0.0f : dot <= 0.0f;
}

/* FUN_800a8378 */
static void aim_grid_move(AimGrid* g, float* wobble, const TableTargetCfg* t, const AimTickInput* in,
                          int edge, int serving) {
    g->drift_prev = g->drift;
    if (serving) g->serve_frozen = 1;
    if (!g->serve_frozen || edge || g->slow) {
        float s = clampf_(in->aim_mag, 0.25f, 1.0f);
        if (!g->slow || (edge && !aim_dir_outward(g, t, in->dir))) {
            float goal = 0.01f * s;                                /* DAT_806d52a4 */
            float r = 0.25f * in->dt;                              /* DAT_806d52ac */
            if (goal > *wobble) { *wobble += r; if (*wobble > goal) *wobble = goal; }
            else if (goal < *wobble) { *wobble -= r; if (*wobble < goal) *wobble = goal; }
        } else {
            *wobble = s * 0.01f * (1.0f + 0.015f - in->approach); /* DAT_806d52a8 */
        }
        float step = (g->side ? -*wobble : *wobble) * s;
        if (in->dir >= 1 && in->dir <= 8) {
            g->drift.x = g->drift_prev.x + AIM_DIR_X[in->dir] * step;
            g->drift.z = g->drift_prev.z + AIM_DIR_Z[in->dir] * step;
        }
    }
    g->offset = v3_add(in->kick, g->drift);
    if (g->offset.x > 1.0f || g->offset.z > 1.25f) g->drift = g->drift_prev;  /* offset kept */
    /* FUN_800a8ad4: aiming short against the kick lowers the launch pitch */
    g->pitch = 0;
    if (in->ball_z >= 0.0f) {
        if (in->kick.z > 0.0f) {
            float v = clampf_(in->kick.z + g->drift.z, 0.0f, 0.5f);
            if (v > 0.0f) g->pitch = (int)(-15.0f * clampf_(v / 0.5f, 0.0f, 1.0f));
        }
    } else if (in->kick.z < 0.0f) {
        float v = clampf_(in->kick.z + g->drift.z, -0.5f, 0.0f);
        if (v < 0.0f) g->pitch = (int)(-15.0f * clampf_(fabsf(v / 0.5f), 0.0f, 1.0f));
    }
    g->offset.x = clampf_(g->offset.x, -0.75f, 0.75f);
    if (in->ball_z <= 0.0f) g->offset.z = clampf_(g->offset.z, -0.5f, 1.25f);
    else                    g->offset.z = clampf_(g->offset.z, -1.25f, 0.5f);
}

Vec3 aim_target_tick(AimTarget* a, float* wobble, const TableTargetCfg* t, const AimTickInput* in,
                     int* kick_pitch) {
    AimGrid* g = &a->grid[in->soft ? 1 : 0];
    int depth = 1, lat = 1;
    int have = aim_dir_cell(in->dir, &depth, &lat);
    if (in->force_short) { depth = 0; have = 1; }
    if (in->edge_check) {
        float hw = 0.5f * t->width, hl = 0.5f * t->length;
        float ax = fabsf(in->prev_world_tgt.x), az = fabsf(in->prev_world_tgt.z);
        float sx = in->serve_edges ? 0.975f : 0.85f;               /* DAT_806de5c8 / 5cc / 5d0 */
        float sz = in->serve_edges ? 0.975f : 0.9f;
        if (ax - hw * 0.995f > 0.0f || az - hl * 0.991f > 0.0f) a->edge_risk = 1;
        if (ax - sx * hw > 0.0f || az - sz * hl > 0.0f) g->slow = 1;
    }
    aim_grid_move(g, wobble, t, in, a->edge_risk, in->soft ? 0 : in->serving);
    if (have && !a->edge_risk) { g->cell_depth = depth; g->cell_lat = lat; }
    if (a->grid[0].pitch < 0) *kick_pitch = a->grid[0].pitch;      /* reads +0xce4 for both grids */
    Vec3 p = aim_cell_point(t, g->side, g->area_set, g->cell_depth, g->cell_lat);
    return v3(p.x + g->offset.x + g->spin_x, p.y + g->offset.y, p.z + g->offset.z + g->spin_z);
}

/* ---------------------------------------------------------------- incoming-spin kick */

float kick_spin_attr(Vec3 spin, Vec3 vel, int camera_flip, float a10, float a14, float a1c) {
    float sgn = vel.z > 0.0f ? 1.0f : -1.0f;                       /* DAT_806def5c / def74 */
    float x = spin.x * sgn, y = spin.y;
    if ((sgn < 0.0f ? 1 : 0) == (camera_flip ? 1 : 0)) y = -y;
    if (x > 0.0f) return y > 0.0f ? (a10 + a1c) * 0.5f : (y < 0.0f ? (a1c + a10) * 0.5f : a10);
    if (x < 0.0f) return y != 0.0f ? (a14 + a1c) * 0.5f : a14;
    return y != 0.0f ? a1c : 0.0f;
}

float kick_depth_push(const KickPushInput* in) {
    float near_z = 0.5f * in->table_length + in->box_gap;          /* FUN_8011a5c4, DAT_806def14 */
    float dz = 0.2f * in->box_depth;                               /* DAT_806de5c0 */
    /* side 0: box z in [-near - depth, -near], zref = hi - dz; side 1: [near, near + depth],
     * zref = lo + dz */
    float zref = in->side == 0 ? -near_z - dz : near_z + dz;
    float t = (in->player_z - zref) / dz;
    float f = 0.0f;
    if (in->in_speed >= 18.0f) {                                   /* DAT_806de5fc */
        f = clampf_((in->in_speed - 10.0f) / 12.0f, 0.0f, 1.0f) + 0.5f * 0.5f * in->in_pace;
        f = f * f;
    }
    float s = clampf_((in->own_speed - 10.0f) / 12.0f, 0.0f, 1.0f);   /* DAT_806de5c4 / 5f8 */
    float v = clampf_(f + s * s * f + 0.5f * in->own_pace * f, 0.0f, 1.0f);
    float out = 0.0f;
    if (in->base_z >= 0.0f ? t >= 0.0f : t <= 0.0f) out = 5.0f * v * t;   /* lerp(v, 0, 5) */
    return clampf_(fabsf(out), 0.0f, in->force_short ? 0.005f : 5.0f);   /* DAT_806de600 */
}

static float sign0_(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

Vec3 kick_tick(KickState* k, const ShotTargetCoef* c, const KickInput* in, float old_kick_y,
               int* kick_pitch) {
    float d_vert = 0.0f, d_side = 0.0f;
    if (in->have_prediction) {
        Vec3 w = in->in_spin;
        float wmax = 3.14159265f * (2.0f * in->max_rps);           /* DAT_806de3f0 * 2 * RPS */
        float r = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z) / wmax;
        float m = r * r;
        float scale = in->reduced ? 0.05f : 2.0f * in->spin_attr;  /* DAT_806de32c / de370 */
        float sx = sign0_(w.x), sy = sign0_(w.y);
        float f8 = -in->own_spin_x, f4 = in->own_spin_y;
        float sum = fabsf(w.x) + fabsf(w.y);
        if (in->charging && sum > 5.0f) {                          /* DAT_806de364 */
            float fx = fabsf(w.x) / sum, fy = fabsf(w.y) / sum;
            if (fabsf(f8) > 0.0f) {
                if (sx * f8 < 0.0f) k->vert += 0.1f * scale * m * fx;          /* against it */
                if (f4 == 0.0f && fabsf(w.y) > 0.0f) k->side += 0.275f * scale * m * fy;
            }
            if (fabsf(f4) > 0.0f) {
                if (sy * f4 < 0.0f) k->side += 0.1f * scale * m * fy;
                if (f8 == 0.0f && fabsf(w.x) > 0.0f) k->vert += 0.5f * scale * m * fx;
            }
        }
        k->vert += in->push * scale;
        d_vert = sx < 0.0f ? -k->vert : k->vert;
        d_side = sy < 0.0f ? -k->side : k->side;
        if (in->side) d_side = -d_side;
    }
    /* FUN_800ab3ac: its pitch term is fed 0 by this caller, so only the forced-short rule and
     * the flat clamp are live */
    int pitch = 0;
    if (in->force_short) {
        if (in->in_speed > 10.0f) pitch = 31;                      /* DAT_806de5c4 */
        else if (in->soft && pitch >= 0) pitch = 0;
    }
    if (in->flat && pitch < -4) pitch = -4;
    *kick_pitch = pitch;
    return v3(c->side_spin_coef * d_side, old_kick_y, -c->vert_spin_coef * d_vert);
}

/* ---------------------------------------------------------------- launch inputs: elevation, paddle, pace */

Vec3 swing_paddle_vel(Vec3 post_hit_paddle_vel, float time_key, int instance_flag, int contact_frame,
                      float t_contact, float t_start, int side) {
    Vec3 v = post_hit_paddle_vel;
    if (time_key >= 0.0f && !instance_flag && t_contact != t_start) {   /* FUN_800d2780 / d60e8 */
        float k = ((float)contact_frame / 30.0f) / (t_contact - t_start);
        v = v3(v.x * k, v.y * k, v.z * k);
    }
    if (side == 0) { v.x = -v.x; v.z = -v.z; }                    /* RotateVec2ByAngle(pi) */
    return v;
}

float swing_pace_tick(float pace, float force, int slam_zone, float top_share, int zone_flag,
                      int kick_pitch, int flat) {
    if ((slam_zone || top_share >= 0.5f || zone_flag) && kick_pitch < 1 && !flat)
        pace = clampf_(pace + 0.003f * force * force, 0.0f, 1.0f);
    return pace;
}

float serve_game_pace(float meter, float a, float timer, int swing_type, float normal_bias) {
    float g = (fabsf(meter) == 100.0f ? 0.5f : 0.0f);              /* DAT_806def40 */
    g += (a == 1.0f && swing_type != 2) ? 0.5f : 0.25f * a;        /* DAT_806def34 */
    float t = clampf_((float)(int)timer / 100.0f, 0.0f, 1.0f);
    float v = clampf_(clampf_(g * t, -1.0f, 1.0f), 0.0f, 1.0f);
    return clampf_(v + normal_bias, -1.0f, 1.0f);
}

/* ---------------------------------------------------------------- Focus meter */

static const float FOCUS_DURATION[3] = { 5.0f, 10.0f, 15.0f };   /* 0x80469ce0 */

int full_focus_any(const FullFocus* f, int p) {
    return f->active[p][0] || f->active[p][1] || f->active[p][2];
}

void full_focus_off(FullFocus* f, int p) {
    f->remaining[p] = 0.0f;                                        /* DAT_806def18 */
    for (int l = 0; l < 3; l++) f->active[p][l] = 0;
    f->started[p] = 0.0f;
}

/* FUN_80119d64 */
static void full_focus_on(FullFocus* f, int p, int level) {
    f->remaining[p] = FOCUS_DURATION[level];
    f->active[p][level] = 1;
}

/* FUN_800abc4c -> FUN_800aeaa0: gauge set to 0 (the callers pass 0), level replaced */
static void focus_gauge_reset(FocusMeter* m, int level) {
    m->value = m->ghost = m->target = 0.0f;
    m->level = level;
}

static void focus_clear_flags(FocusMeter* m) {                     /* FUN_800ac4e0 */
    m->filling = m->spending = m->animating = 0;
}

void focus_reset(FocusMeter* m, FullFocus* f) {
    focus_gauge_reset(m, 0);
    full_focus_off(f, 0);
    full_focus_off(f, 1);
    m->auto_request = 0;
    m->full_posted = 0;
}

void focus_fill(FocusMeter* m, float amount) {
    if ((float)(int)m->ghost >= FOCUS_MAX) m->overflow += amount;  /* msg 0x1021 the first time */
    float a = clampf_(fabsf(amount), -FOCUS_MAX, FOCUS_MAX);       /* DAT_806de658 / 644 */
    m->ghost = clampf_(m->ghost + a, 0.0f, FOCUS_MAX);              /* FUN_800aec54 */
}

void focus_drain(FocusMeter* m, float amount) {
    float a = clampf_(fabsf(amount), -FOCUS_MAX, FOCUS_MAX);
    m->value = clampf_(m->value - a, 0.0f, FOCUS_MAX);              /* FUN_800aecec */
}

void focus_mark(FocusMeter* m, int spend, int force) {
    if (force) focus_clear_flags(m);
    else if (m->filling || m->spending) return;
    if (spend) m->spending = 1; else m->filling = 1;
}

void focus_animate(FocusMeter* m) {
    m->animating = 1;
    if (m->filling) {
        int v = (int)m->value, t = (int)m->target;
        if (v + 30 < t) m->value = (float)(v + 30);
        else { m->value = (float)t; focus_clear_flags(m); }
    } else if (m->spending) {
        int v = (int)m->value, g = (int)m->ghost;
        m->overflow = 0.0f;
        if (v < g - 30) m->ghost = (float)(g - 30);
        else {
            m->ghost = (float)v;
            if (m->hold_t <= 0.0f) focus_clear_flags(m);
            else { m->hold = 1; m->spending = 0; }
        }
    } else if (m->hold) {
        m->hold = 0;
        m->hold_t = 0.0f;
    } else {
        focus_clear_flags(m);
    }
}

void focus_swing_frame(FocusMeter* m, const FullFocus* f, int p, const FocusSwingInput* in) {
    if (!in->charge_allowed || in->point_over) return;
    if (!in->flat) {                                               /* FUN_8009ded0 */
        if (full_focus_any(f, p)) return;
        static const float k_level[3] = { 0.8f, 0.7f, 0.6f };      /* DAT_806de3a4 / 3ec / 36c */
        float ratio = in->dt_real > 0.0f ? clampf_(in->dt_game / in->dt_real, 0.0f, 1.0f) : 1.0f;
        float a = k_level[m->level] * (0.5f + 0.5f * in->intensity) *
                  (0.25f + 0.75f * in->momentum) * (in->charge_delta * ratio);
        focus_fill(m, a);
        focus_mark(m, 0, 0);
    } else {
        int d = (int)m->ghost - (int)m->value;
        if (d >= 0 && (float)d < 280.0f && !full_focus_any(f, p) && !in->serve_into_net) {
            focus_drain(m, 10.0f);                                 /* DAT_806d519c / 51a0 */
            focus_mark(m, 1, 0);
        }
    }
}

float focus_shot_charge(const FocusMeter* m, const FullFocus* f, int p) {
    if (f->active[p][2]) return 1.0f;
    return clampf_(m->ghost / FOCUS_MAX - m->value / FOCUS_MAX, 0.0f, 1.0f);
}

int focus_launch_stretch(FocusMeter* m, const FullFocus* f, int p, int cpu, int hit_zone, int soft,
                         int opp_level) {
    int listed = hit_zone == 2 || hit_zone == 3 || hit_zone == 8 || hit_zone == 10 || hit_zone == 11 ||
                 hit_zone == 16;                                   /* FUN_8009cfcc */
    if (!(cpu && listed) || full_focus_any(f, p))
        return soft && m->value / FOCUS_MAX <= 0.0f && m->level < 1;
    if (m->value / FOCUS_MAX <= 0.9999f) {                         /* DAT_806de3d8 */
        int d = m->level - opp_level;
        float pen = d == -1 ? 170.0f : d == -2 ? 160.0f : d == 1 ? 200.0f : d == 2 ? 220.0f : 180.0f;
        float left = (float)(int)m->value - pen;                   /* DAT_806d5188..5198 */
        if (left < 0.0f) pen = fabsf(pen - fabsf(left));           /* everything there is */
        focus_drain(m, pen);
        focus_mark(m, 1, 1);
    }
    return 1;
}

void focus_launch(FocusMeter* m, int point_over) {
    m->armed = 0;                                                  /* FUN_800abcb0 */
    m->soft = 0;
    m->target = (float)(int)m->ghost;                              /* FUN_800aec4c */
    if (point_over) focus_mark(m, 1, 1);
}

void focus_on_bounce(FocusMeter* m) { focus_animate(m); }

void focus_on_fault(FocusMeter* m) {
    focus_drain(m, 180.0f);                                        /* DAT_806d51a4 */
    focus_mark(m, 1, 1);
    focus_animate(m);
}

void focus_on_point(FocusMeter* m, const FullFocus* f, int p, int lost) {
    if (lost && full_focus_any(f, p)) {
        focus_drain(m, 170.0f);                                    /* DAT_806d51a8 */
        focus_mark(m, 1, 1);
    }
    focus_mark(m, 1, 1);
    focus_animate(m);
}

int focus_frame(FocusMeter* m, FullFocus* f, int p, int auto_activate, int rally_live, int paused,
                float dt_real, float now) {
    int switched_on = 0;
    if (m->animating) focus_animate(m);
    if (m->value / FOCUS_MAX == 1.0f && m->ghost / FOCUS_MAX == 1.0f) {
        if (!m->animating) {
            if ((m->auto_request || auto_activate) && !f->active[p][m->level]) {
                full_focus_on(f, p, m->level);                     /* msg 0x1022 */
                switched_on = 1;
            }
            m->full_posted = 1;                                    /* msg 0x1020 once */
        }
    } else {
        if (m->value <= 0.0f && m->ghost <= 0.0f) {
            int l = m->level;
            if (f->active[p][l]) {
                full_focus_off(f, p);
                focus_gauge_reset(m, l == 0 ? 1 : 2);
            }
        }
        m->full_posted = 0;
    }
    if (m->overflow > 0.0f) {
        if (!m->overflow_hold) m->overflow = 0.0f;
        else m->overflow_hold = 0;
    }
    if (rally_live && !paused && f->active[p][m->level]) {
        float dur = FOCUS_DURATION[m->level];
        if (f->started[p] <= 0.0f) f->started[p] = now;
        float rem = m->value / FOCUS_MAX * dur;
        rem -= (f->active[0][2] && f->active[1][2]) ? 0.25f * dt_real : dt_real;   /* DAT_806de654 */
        float frac = clampf_(rem / dur, 0.0f, 1.0f);
        if (rem <= 0.0f) {
            m->value = 0.0f;
            m->ghost = 0.0f;
        } else {
            f->remaining[p] = rem;
            m->value = m->ghost = FOCUS_MAX * frac;
        }
    }
    m->auto_request = 0;                                           /* +0x45 cleared every tick */
    return switched_on;
}

float focus_pace_bonus(const FullFocus* f, int p, int meter_level) {
    if (f->active[0][2] && f->active[1][2]) return 0.25f;         /* DAT_806def34 */
    if (full_focus_any(f, p)) return 0.1f;                         /* DAT_806def20 */
    return meter_level == 2 ? 0.2f : (meter_level == 1 ? 0.15f : 0.1f);
}
