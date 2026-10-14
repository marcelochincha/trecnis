#include "rtt_ai.h"

float ai_curve(const AiData* d, AiField f, float x, float mod) {
    x = rtt_clampf(x, 0.0f, (float)(AI_TIERS - 1));
    int lo = (int)x, hi = lo + 1 < AI_TIERS ? lo + 1 : lo;
    float v = rtt_lerpf(d->tier[lo][f], d->tier[hi][f], x - (float)lo) + mod;
    return rtt_clampf(v, 0.0f, 1.0f);
}

static int roll(Rng* r, float p) { return rng_float(r) <= p; }

void ai_decide(AiContext* c, const AiData* d, int own, int opp, int has_hit_state, Rng* rng) {
    float x = c->level, ms = d->awareness_mod_shot, mm = d->awareness_mod_move;
    /* 1. caution gate: conservative unless this AI already leads by 3+ [RTT] */
    if (own - opp < 3) c->aggression = 0.1f;
    /* 2. movement */
    if (has_hit_state || c->focus_active) c->offensive_move = 0;
    else c->offensive_move = roll(rng, ai_curve(d, AIF_MOVE_OFFENSIVE, x, mm));
    /* 3. shot selection, gated */
    c->attempt_shot = roll(rng, ai_curve(d, AIF_SS_AWARENESS, x, ms));
    if (!c->attempt_shot) return;
    float aim = 0.0f, charge = 0.0f;
    c->kill_shot = roll(rng, ai_curve(d, AIF_USE_KILL_SHOT, x, ms) * c->aggression);
    if (c->kill_shot) {
        aim = rng_float(rng) < 0.5f ? -1.0f : 1.0f;                 /* SelectKillShot detail [OPEN] */
        charge = 1.0f;
    } else {
        /* cumulative waterfall with one roll [RTT] */
        float wl = ai_curve(d, AIF_ATTACK_LONG, x, ms);
        float ws = ai_curve(d, AIF_ATTACK_SHORT, x, ms);
        float ww = ai_curve(d, AIF_ATTACK_WIDE, x, ms);
        float r = rng_float(rng);
        if (r < wl)                { charge = 1.0f; }               /* long: deep, driven [OPEN detail] */
        else if (r < wl + ws)      { charge = -0.5f; }              /* short */
        else if (r < wl + ws + ww) { aim = rng_float(rng) < 0.5f ? -1.0f : 1.0f; charge = 0.0f; } /* soft wide [RTT] */
        if (roll(rng, ai_curve(d, AIF_USE_SPIN_COUNTER, x, ms))) charge = -charge;  /* [OPEN detail] */
        c->special_flag = roll(rng, ai_curve(d, AIF_USE_KILL_SHOT, x, ms));
    }
    if (roll(rng, ai_curve(d, AIF_AIM_FOR_EDGE, x, ms))) aim = aim >= 0.0f ? 1.0f : -1.0f;
    if (roll(rng, ai_curve(d, AIF_COMBINE_SPIN, x, ms))) charge = rtt_clampf(charge * 1.5f, -1.0f, 1.0f);
    /* commit: clamp, halve aim outside focus mode [RTT] */
    aim = rtt_clampf(aim, -1.0f, 1.0f);
    charge = rtt_clampf(charge, -1.0f, 1.0f);
    if (!c->focus_active) aim *= 0.5f;
    c->out_aim = aim;
    c->out_charge = charge;
}

float ai_reaction_cost(const AiPositionCfg* c, float dist) {
    float cost = c->react_base_time + c->react_accel_coeff * dist * dist;   /* + DAT_806de544 [OPEN] */
    return cost < c->react_time_max ? cost : c->react_time_max;
}
