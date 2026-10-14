/*
 * rtt_ai.h -- CPU opponent: probability curves over aidata.xml tiers, per-frame decision,
 * positioning-candidate cost model. Docs: reconstruction/ai.md, map/ai.md.
 */
#ifndef RTT_AI_H
#define RTT_AI_H

#include "rtt_core.h"

enum { AI_TIERS = 18, AI_FIELDS = 32 };

/* one difficulty tier, flattened from aidata.xml's 5 blocks [DATA] */
typedef enum {
    AIF_DEF_AWARENESS, AIF_DEF_SPIN_ADJ, AIF_DEFEND_SLAM,
    AIF_OFF_AWARENESS, AIF_CHOOSE_BEST_POS, AIF_MOVE_OFFENSIVE,
    AIF_CHARGE_METER, AIF_CONSERVE_METER, AIF_DO_FOCUS_SHOT,
    AIF_SS_AWARENESS, AIF_CHARGING, AIF_USE_TARGET_COUNTER, AIF_USE_SPIN_COUNTER,
    AIF_ATTACK_OPEN_SIDE, AIF_ATTACK_WEAK_SIDE, AIF_ATTACK_LONG, AIF_ATTACK_SHORT,
    AIF_DEFEND_SOFT_SHOT, AIF_ATTACK_WIDE, AIF_AIM_FOR_EDGE, AIF_USE_TOUCH,
    AIF_MIX_UP_TARGETS, AIF_USE_KILL_SHOT, AIF_COMBINE_SPIN,
    AIF_SHOT_TIMING, AIF_POSITIONING, AIF_REACTION_TIME, AIF_ACCURACY, AIF_TOP_SPEED
} AiField;

typedef struct {
    float tier[AI_TIERS][AI_FIELDS];
    float awareness_mod_shot;          /* gdaiDataMgr+0x3c, added to shot-selection curves [RTT] */
    float awareness_mod_move;          /* gdaiDataMgr+0x40, added to movement curves [RTT] */
} AiData;

/* positioning cost model, config struct 0x806d5260 [RTT] */
typedef struct {
    float zone_margin;                 /* 0.1 */
    float react_accel_coeff;           /* 0.22 */
    float react_base_time;             /* 0.05 */
    float react_time_max;              /* 2.8 s */
    float react_turn_scale;            /* 1.1 */
    int   grid_a, grid_b;              /* 4 x 4 */
} AiPositionCfg;

typedef struct {
    float level;                       /* continuous curve input -- see ai_curve [OPEN semantics] */
    float aggression;                  /* ctx+0x6e0: 0.1 unless leading by 3+ [RTT] */
    float raw_attack_prob;
    int   focus_active;
    /* outputs, written into the AI's virtual controller */
    int   attempt_shot;
    float out_aim;                     /* [-1,1] */
    float out_charge;                  /* [-1,1] */
    int   kill_shot, special_flag;
    int   offensive_move;
} AiContext;

/* lerp(tier[floor(x)], tier[ceil(x)]) + awareness modifier, clamped to [0,1]
 * (shared template of all FUN_8013xxxx curves) [RTT]. RTT names the input "timeInPoint"
 * in reconstruction/ai.md, but the template treats it as a fractional tier index [OPEN]. */
float ai_curve(const AiData* d, AiField f, float x, float awareness_mod);

/* the shipped tiers (aidata.xml via tools/ai_data/gen_ai_data.py -> data/ai_data.inc); each column is
 * the field RTT's curve for it actually reads. Awareness modifiers gdaiDataMgr+0x3c / +0x40 = 0
 * [OPEN: their source]. */
void ai_data_default(AiData* d);

/* one decision tick (gdai_DecisionTick 0x800a2488) [RTT] */
void ai_decide(AiContext* c, const AiData* d, int own_score, int opp_score, int has_hit_state, Rng* rng);

/* reaction-time cost to reach a candidate position:
 * min(base + k + accel * dist^2, max) (gdai_ComputeReactionTimeBase 0x800a7078) [RTT] */
float ai_reaction_cost(const AiPositionCfg* c, float dist);

#endif /* RTT_AI_H */
