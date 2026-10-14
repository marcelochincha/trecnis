/*
 * rtt_swing.h -- swing/serve control layer: config, strength ladder, aim drift, spin charge,
 * spatial swing pick (swingspace) and contact-frame retiming.
 * Docs: reconstruction/gameplay.md, input.md, serve.md, animation-runtime.md.
 */
#ifndef RTT_SWING_H
#define RTT_SWING_H

#include "rtt_core.h"
#include "rtt_anim.h"

/* swingcfg.xml (gmSwingData) [DATA] */
typedef struct {
    float min_early_out, max_early_out;          /* 0.70 / 1.80 timing window */
    float soft_shot_mult;                        /* 1.5 */
    float human_spin_charge, ai_spin_charge;     /* 1.0 / 0.5 */
    float serve_toss_threshold, serve_toss_time; /* 1.0 / 1.25 s */
    float dpad_targetting_speed;                 /* 0.05 per frame */
    float min_swing, hard_shot, really_hard_shot;/* 1.4 / 3.4 / 4.1 */
    int   aim_sampling[3];                       /* 15 / 25 / 40 */
    float pace_mult[3], force_mult[3], spin_mult[3]; /* 1, 1.5, 2 */
    float aim_e, aim_w, aim_ne, aim_se, aim_nw, aim_sw; /* +0.2 -0.2 -0.15 +0.1 -0.15 +0.1 */
} SwingConfig;

/* characters.xml per character (gdPlayerAttribs) [DATA] */
typedef struct {
    float max_spin_rps[4][3];                    /* [normal, ITZ, MaxITZ, serve][top, back, side] */
    float max_charge_time[3];                    /* 0.57 / 1.0 / 0.75 */
    float max_charge_rate[3];                    /* 1552.8 / 1500 / 2000 */
    float max_move_speed, max_move_accel, max_srv_move_speed;
    float tgt_error_x, tgt_error_z;              /* aim-error box */
    float meter_charge_delta;                    /* 20 */
} PlayerAttribs;

typedef enum { SHOT_NONE = -1, SHOT_NORMAL = 0, SHOT_HARD = 1, SHOT_REALLY_HARD = 2 } ShotTier;
typedef enum { DIR_NONE, DIR_E, DIR_W, DIR_NE, DIR_SE, DIR_NW, DIR_SW } AimDir;
typedef enum { ZONE_NORMAL, ZONE_ITZ, ZONE_MAX_ITZ } ZoneLevel;

/* one sampled reachable swing in swingspace.xml (pcrSwingBlend) [DATA] */
typedef struct {
    int   anim[3];                               /* clip indices, -1 = unused */
    float w[3];                                  /* W0..W2 */
    Vec3  contact_point, contact_normal;
    Vec3  post_hit_paddle_vel;                   /* feeds the ball launch */
    Vec3  total_mover_trans;
} SwingBlendRec;

typedef struct {
    const SwingBlendRec* blends;
    int   count;
    float max_dist_behind, max_dist_in_front;    /* search bounds along the swing axis */
} SwingSpace;

typedef struct {
    /* input/AI side */
    float aim;                                   /* drifting lateral aim [-1,1]-ish */
    float charge, charge_spin;
    ZoneLevel zone;
    int   is_ai;
    /* active swing */
    int   active;
    const SwingBlendRec* blend;
    float phase;                                 /* 0..1 over the blended clip */
    float t_start, t_contact;                    /* absolute times */
    float contact_phase;                         /* ContactFrame / frames of the lead clip */
    int   contacted;
    ShotTier tier;
} SwingState;

ShotTier swing_classify(const SwingConfig* c, float swing_speed);

/* fixed step toward the pushed direction's offset per frame (input.md; gameplay.md's lerp
 * reading is superseded) [RTT]. Legacy aim path (aim object +0x1360 clear); live swings use the
 * target grid, aim_target_tick below. */
void swing_aim_drift(SwingState* s, const SwingConfig* c, AimDir dir);

/* spin charge while held: rate * (human|ai factor) * dt, capped by the axis max [RTT] */
void swing_charge_tick(SwingState* s, const SwingConfig* c, const PlayerAttribs* a,
                       int spin_axis, float dt);

/* nearest pcrSwingBlend to the required contact point: bounded-box reject then
 * min squared distance (FindNearestBlend 0x800db4d0) [RTT] */
const SwingBlendRec* swing_find_blend(const SwingSpace* sp, Vec3 contact_local);

/* start a swing so ContactFrame lands at ball arrival time t_arrive [RTT] */
void swing_start(SwingState* s, const SwingBlendRec* b, float contact_phase, float now, float t_arrive);

/* linear phase: before contact reach contact_phase exactly at t_contact, then run to 1
 * at the clip's natural rate (AdvancePhaseSolved / FUN_800d6364) [RTT]. Returns 1 on the
 * frame the contact phase is crossed. */
int swing_update(SwingState* s, float now, float clip_duration);

/* inputs of the rally shot speed, all from the hitter's hit state [RTT field names,
 * gdHitState__AddWidgets] */
typedef struct {
    Vec3  ball_pos;          /* BallPos +0x50 */
    Vec3  target;            /* WorldTgtPos +0x1c */
    float hit_force;         /* HitForce +0x18, 0..1 */
    float spin_x;            /* SpinAxis.x +0x30: < 0 topspin, > 0 backspin (Magnus-checked) */
    float spin_side;         /* SpinAxis.y +0x34 */
    int   swing_type;        /* SwingType +0x8c */
    float table_top;         /* table cfg +0xc */
} ShotSpeedInput;

/* PostHitSpeed (hit +0x70) of a rally shot: ball__ComputePostHitSpeed 0x800469c0 rally branch.
 * swing__ResolveShotTick 0x8009d044 runs it EVERY frame of a swing (spin charging, aim drifting),
 * so the value SolveHitAim reads is the contact frame's. Within a frame it sees the current
 * HitForce / SpinAxis / SwingType but WorldTgtPos before that frame's aim step; on the contact
 * frame the aim error is added to the target only after it [RTT, live-validated 2026-10-11 on
 * 283/283 captured swing frames, tests/compare_speed_capture.cpp]:
 *   d2 = |BallPos - WorldTgtPos|^2 (squared metres, not metres)
 *   type 3:  a = lerp(0.525, 1, clamp((d2/10)^2, 0, 1));  lo = 5.5a, hi = 7.5a
 *   type 6:  lo 16, hi 19;   type 11: lo 19, hi 22
 *   backspin (SpinAxis.x > 0):
 *     f = clamp((d2/16)^2, 0.6, 1);  u = clamp(top + |side|, 0, 1)
 *     lo = f * lerp(BackLo, BackMid, u),  hi = f * BackHi
 *   no backspin (t = topspin amount = max(0, -SpinAxis.x)):
 *     k = lerp(0.75, 1, (d2/15)^2)                (not clamped)
 *       * lerp(1, 0.75, clamp((top_y - BallPos.y)^+ / 0.5 * top_y, 0, 1))   ball below the table top
 *       * lerp(1, 0.88, |side|)
 *     lo = k * lerp(FlatLo, TopLo, t),  hi = k * lerp(FlatHi, TopHi, t)
 *   PostHitSpeed = lo + HitForce^2 * (hi - lo)
 * Per-type tunables (globals 0x806d5f90..0x806d6014, three sets: types 7-9 use set "7-9" for
 * the four 0x8011b5a8.. getters, type 10 its own, the rest the default) are in rtt_swing.cpp.
 * Serves take the other branch of FUN_800469c0 (distance to the serve point, gdServe tunables);
 * a serve launches from ServeHitVel, not from SolveHitAim: see ball_solve_serve (rtt_ball.h). */
float swing_post_hit_speed(const ShotSpeedInput* in);

/* launch spin (rad/s, world) of a rally shot from the swing's SpinAxis: FUN_8011bd78 ->
 * FUN_8011b184, applied by ball__OnRacketHit [RTT, decompiled 2026-10-11]:
 *   axis   = dominant SpinAxis component (FUN_8011b7a8): x < 0 top, x > 0 back, y side; a tie
 *            goes to side
 *   max    = attribs MaxSpinRPS[set][axis], set = SwingType 8/9 -> ITZ, 10 -> MaxITZ, serve ->
 *            Serve, else normal (FUN_801265e0); ONE max scales both components
 *   s      = SpinAxis * 2*pi*max, then (y, z) rotated by roll (DAT_806db750, runtime; 0 seen live)
 *   s      = -s when BallPos.z <= 0 (the -z half);  s.y = -s.y when flip_side (FUN_80120c04 == 0)
 *   world  = (s.x * p.x, s.y, s.x * p.z), p = horizontal unit vector perpendicular to
 *            (WorldTgtPos - BallPos): (d.z, -d.x) on the -z half, (-d.z, d.x) on the +z half
 *            (FUN_8004605c) */
typedef struct {
    Vec3  spin_axis;         /* SpinAxis +0x30, components in [-1, 1] */
    Vec3  ball_pos;          /* BallPos +0x50 */
    Vec3  target;            /* WorldTgtPos +0x1c */
    int   swing_type;        /* SwingType +0x8c */
    int   is_serve;          /* hit +0xb6 & 0x1000 */
    float roll;              /* DAT_806db750 (rad) */
    int   flip_side;         /* FUN_80120c04 == 0: the active camera destination (+0x30) flips the
                              * view, so left/right side spin is screen-relative */
} HitSpinInput;
Vec3 swing_hit_spin(const PlayerAttribs* a, const HitSpinInput* in);

/* per-character swing attributes, one set per level (characters.xml gdPlyrAttr: normal / ITZ /
 * MaxITZ; player cfg +0x10.. / +0x2c.. / +0x48.., picked by SwingType 8-9 / 10) [DATA] */
typedef struct { float top, back, strength, side, accuracy; } SwingAttrSet;

/* one hit zone (characters.xml powerZoneData, indexed by HitZone hit +0x88; entry +0x14..) [DATA] */
typedef struct { float strength, vert_spin, side_spin, accuracy, spin_def, speed_def; } PowerZone;

/* what swing__ResolveShotTick 0x8009d044 keeps between frames of one rally swing */
typedef struct {
    float charge[4];         /* player +0x1904..+0x1910: side a, side b, top, back */
    Vec3  spin_axis;         /* SpinAxis hit +0x30 */
    float force;             /* Force hit +0x14 */
    float hit_force;         /* HitForce hit +0x18 */
    float accuracy;          /* player +0x1844 */
} SwingChargeState;

typedef struct {
    float in[4];             /* this frame's charge inputs (ResolveShotTick f1..f4; human: player
                              * +0x1914 while d-pad button 0x34 / 0x35 / 0x32 / 0x33 is held) */
    const SwingAttrSet* attr;/* set for this SwingType */
    const PowerZone* zone;   /* zone[HitZone] */
    float mult;              /* 1, or the ITZ value for SwingType 8 */
    float max_charge_time;   /* MaxSpinChargeTime[set] 0.57 / 1.0 / 0.75 */
    float charge_rate;       /* MaxSpinChargeRate[set] 1552.8 / 1500 / 2000 */
    float dt;                /* frame time (0x804f23a4, or 1/0x804f23a8 in control mode >= 2) */
    int   swing_type;        /* SwingType hit +0x8c (after the zone 4/12 swap) */
    int   zone_snap;         /* HitZone 4 or 12 */
    int   charge_allowed;    /* FUN_8009ddfc: always 1 in control mode >= 2 */
    int   no_charge_flag;    /* player +0x4c46 */
    float meter;             /* FUN_800aea20(player +0x1860) */
    int   itz_full;          /* player +0x4bbc == 1 */
    float side_cap;          /* hit +0x130 */
    int   flat;              /* FUN_80119cf4(gd, player, 2) != 0 (and +0x4c46 clear), or hit +0x12e:
                              * the attributes are skipped -- Force = HitForce = 1, every spin
                              * coefficient 1 (0.99 after the clamp); with +0x4c46 set Force = 0.
                              * Flag 2 = In-The-Zone level 2 active (ITZ meter FUN_800abcc0, 15 s).
                              * Live: 172/1513 CPU swing frames take it */
} SwingTickInput;

/* one frame of a rally swing (swing__ResolveShotTick, rally branch) [RTT, decompiled 2026-10-11]:
 *   Force = mult * attr.strength;  HitForce = zone.strength * Force;  accuracy = zone.accuracy *
 *   mult * attr.accuracy
 *   k = clamp(mult * attr.{side, side, top, back} * zone.{side, side, vert, vert}_spin, 0, 0.99)
 *   frac_i = in_i / sum(in)    (all charges 0 when sum <= 0)
 *   charge_i moves toward dt * 1000 * frac_i * in_i / (max_charge_time * (1 - k_i)) by at most
 *   charge_rate * k_i * dt, then charge_i = max(charge_i, in_i)
 *   charges > 0 drop to 0.1 on HitZone 4/12, the +0x4c46 flag, no charge allowed, or SwingType 3;
 *   also when SwingType > 7 and the meter is empty (then the axis step is scaled by 0.1 too)
 *   SpinAxis += s * (back - top, side_a - side_b, 0) / 1000, then |SpinAxis| <= cap:
 *   cap = 1 (SwingType < 8, or itz_full), 0.8 otherwise; any side charge > 0 -> cap =
 *   lerp(0.25, 1, clamp(1 - side_cap, 0, 1))
 * Live (capture 3, R snapshots): Force/HitForce match 1502/1513 swing frames (1315 normal, 172
 * flat). The other 11 are CPU frames whose SwingType/zone changed later in the same frame.
 * At contact a hard / really hard swing multiplies HitForce by swingcfg HitForceHardIncreaseMult
 * 1.5 / HitForceReallyHardIncreaseMult 2.0 (15 frames live; applying function not traced [OPEN]),
 * see swing_contact_hit_force. */
void swing_charge_tick(SwingChargeState* st, const SwingTickInput* in);

/* the SpinAxis half of the tick, from the charges already in st (FUN_8011c64c -> FUN_8011ad40) */
void swing_axis_step(SwingChargeState* st, const SwingTickInput* in);

/* HitForce at contact: x1.5 for a hard swing, x2.0 for a really hard one (swingcfg.xml) */
float swing_contact_hit_force(const SwingConfig* c, float hit_force, ShotTier tier);

/* player +0x1cc states 3 / 4 / 5 (FUN_8009ea90) take the timed branch of swing__ResolveShotTick
 * instead of the charges [RTT, decompiled 2026-10-11]. Live (capture 4) they are the SERVE: 4 = CPU
 * serving, 5 = human serving, every such frame has IsServe set (rally states are 1 human / 2 CPU).
 *   pct = player +0x191c (human) ? *(player +0x17ec)+0x14 (serve meter, float)
 *                                : (float)(int)player +0x4c2c (serve timer, truncated)
 *   p_i = pct / 100 * in_i / sum(in) * mult   (0 when sum <= 0)
 *   SpinAxis = (p3 - p2, p0 - p1, 0), length clamped to <= 1 (FUN_8011aecc: SET, not added)
 * Force / HitForce are not touched in this branch. Live-validated: CPU 506 non-zero frames (capture
 * 4), human 41/41 non-zero frames with the captured meter (capture 6). On the serve's contact frame
 * (HitForce 0 -> set) SpinAxis is further scaled by swingcfg SpinHardIncreaseMult 1.5 / 2.0. */
Vec3 swing_timed_spin_axis(const float in[4], float pct, float mult);

/* ---------------------------------------------------------------- SwingType (hit +0x8c) */
/* Values seen in the code [RTT, decompiled 2026-10-11]. 8 and 10 are read by switches (spin set,
 * drag slot) but no writer was found and they were never seen live. 1 / 2 are set-up values
 * (FUN_8009c498 / FUN_80047b54) that the swing set-up overwrites. */
enum {
    SWING_TYPE_NONE = 0,
    SWING_TYPE_SOFT = 3,          /* A button (or a weak gesture) */
    SWING_TYPE_NORMAL = 5,
    SWING_TYPE_SLAM = 6,          /* NORMAL hit from HitZone FH_Slam / BH_Slam */
    SWING_TYPE_SPECIALTY = 7,     /* predicted sample flagged specialty */
    SWING_TYPE_ITZ = 9,           /* ITZ meter armed, or the power button */
    SWING_TYPE_ITZ_SLAM = 11      /* ITZ (9/10) hit from a Slam zone */
};

/* swing set-up (FUN_80097d08 / FUN_8009c6fc -> focusMeter__GetBaseSwingType 0x800ac344): Focus
 * meter flag +4 armed -> 9 (posts msg 0x101a; the meter read there is discarded, nothing is spent),
 * flag +6 -> 3 (wins), else 5 */
int swing_base_type(int meter_armed, int meter_soft);

/* plrSwingLogic__StartSwing: a specialty-flagged predicted hit sample (+0x128 bit 4) turns a
 * type < 8 into 7 */
int swing_specialty_type(int type, int sample_specialty);

/* flags the type selection raises besides the type itself */
typedef struct {
    int flat;                     /* hit +0x12e: flat Force/HitForce branch of the charge tick */
    int itz_shot;                 /* player +0x185c */
} SwingTypeFlags;

/* swing__FinalizeSwingType 0x800982e4: an active ITZ level (FUN_80119cf4(gd, p, level)) turns
 * anything but a soft shot into 9 (+itz_shot); otherwise a type >= 8 with an empty ITZ meter
 * drops back to 5 (6 if it was 11) and clears itz_shot */
int swing_finalize_type(int type, int itz_level_active, float itz_meter, SwingTypeFlags* fl);

/* swing__ClassifyHumanInput 0x80097f60, human branch (control mode >= 2). Runs every frame of the
 * swing until it finalizes. Wii input: soft button 0x36 (A), power button 0x37, gesture =
 * pongPlayer__GetSwingGestureMagnitude (max |axis| of the swing stick pair, 0 = no swing).
 * TRECNIS maps its own input onto these three. */
typedef struct {
    float win_start;              /* player +0x18dc, 0 = no window open */
    int   gesture_seen;           /* player +0x18e0 */
    float gesture_max;            /* player +0x18e4 */
    int   done;                   /* finalized (swing__FinalizeSwingType ran) */
} SwingClassifyState;

typedef struct {
    float now;                    /* point clock DAT_806dab74+0x3c (s) */
    float dt;                     /* dt_a 0x804f23a4 */
    float gesture;                /* >= 0 */
    int   soft_button, power_button;
    int   hit_ready;              /* hit object +0x21 (byte before the hit state) [OPEN meaning] */
    int   itz_level_active;
    float itz_meter;
} SwingHumanInput;

/* updates *type / *hit_force / flags; returns 1 on the frame it finalized */
int swing_classify_human(SwingClassifyState* st, int* type, float* hit_force, SwingTypeFlags* fl,
                         const SwingHumanInput* in);

/* the CPU branch of the same function: a soft shot (3) waits the same 2-frame window, then takes
 * the AI's force (player->ai +0x4c8) as HitForce; any other type finalizes at once */
int swing_classify_ai(SwingClassifyState* st, int* type, float* hit_force, SwingTypeFlags* fl,
                      float now, float dt, float ai_force, int itz_level_active, float itz_meter);

/* swing__ResolveShotTick: HitZone FH_Slam (4) / BH_Slam (12) turns 5 into 6 and 9/10 into 11 */
int swing_slam_type(int type, int hit_zone);

/* ---------------------------------------------------------------- HitZone selection */
/* Which pcrSwingSet the solver picks for the coming ball; its HitZone becomes hit +0x88 [RTT,
 * decompiled 2026-10-11: gdBall__ResolveSwingAimTarget 0x800c9db8, FUN_800cfc28, FUN_800d1b80,
 * FUN_800d021c, FUN_800d1108, FUN_800d0880, gdBall__CompareAimTargetPriority; map/gameplay.md].
 * All data comes from the already-loaded tuning (swingsolver_*.xml, swingspace.xml,
 * ballfudge.xml); nothing here parses XML. */

/* the five pcrSwingSolver slots of a pcrCharacter (+0x64..+0x74) */
enum { SOLVER_TS = 0, SOLVER_BS = 1, SOLVER_TS_ITZ = 2, SOLVER_BS_ITZ = 3, SOLVER_SFT = 4, SOLVER_COUNT = 5 };

/* shot flags player +0x4c40, built by swing__FinalizeSwingType (FUN_8014b1ec) */
enum {
    SHOT_FLAG_VALID = 0x01, SHOT_FLAG_ITZ = 0x02, SHOT_FLAG_SOFT = 0x04,
    SHOT_FLAG_TOP = 0x08, SHOT_FLAG_BACK = 0x10, SHOT_FLAG_SIDE_A = 0x20, SHOT_FLAG_SIDE_B = 0x40
};
unsigned swing_shot_flags(int itz, int soft, int top, int back, int side_a, int side_b);

/* ResolveSwingAimTarget: soft -> sft, ITZ -> ts_itz / bs_itz, else ts / bs; the bs variant when
 * backspin was input. *fallback gets the normal solver to retry with when an ITZ / soft pass
 * finds no valid target (-1 if none), *fb_itz / *fb_soft the fallback filter to apply then. */
int swing_solver_slot(unsigned shot_flags, int* fallback, int* fb_itz, int* fb_soft);

/* one pcrSwingSet, as the solver uses it (XML attribute -> field: map/gameplay.md) */
typedef struct {
    int   hit_zone;                    /* HitZone +0x64 */
    int   short_and_low;               /* Name starts "ShortAndLow"  (+0x52, FUN_800d3cd0) */
    int   forward_lunge;               /* Name starts "ForwardLunge" (+0x53, FUN_800d3d28) */
    int   allow_fallback_itz;          /* AllowFallbackFromITZ +0x59 */
    int   allow_fallback_soft;         /* AllowFallbackFromSoft +0x5a */
    int   specialty_blocked;           /* IsSpecialty set, DisableIfSpecialtyCheckFails and the
                                          spin input does not match it (FUN_800d42f8) */
    int   category_active;             /* ActivationCategory test; always 1 for a CPU or a
                                          human in control mode 2 / 4 (FUN_8009ead4) */
    int   contact_frame;               /* ContactFrame +0x1c */
    float swingdata_min_x, swingdata_max_x;   /* min / max of its swing data +0x14 (SOT reach) */
    const SwingBlendRec* blends;       /* its pcrSwingSpace (+0x70) blends */
    int   blend_count;
    Vec3  box_min, box_max;            /* swing space +0x30 / +0x40 (player frame) */
    /* static data the selection reads per set (generated: data/swing_sets_<char>.inc) */
    const char* name;
    int   category;                    /* ActivationCategory enum (0x80462dd8, FUN_800ecebc) */
    int   specialty;                   /* IsSpecialty: 0 None, 1 TopspinOnly, 2 BackspinOnly, 3 SidespinOnly */
    int   disable_if_specialty_fails;  /* +0x58 */
    int   anticipation_frame;          /* +0x18 */
    float contact_tol_pct;             /* +0x20, clamped [0, 0.99] at load */
    float max_start_to_contact;        /* +0x2c, raised to (ContactFrame + 1) / 30 at load */
    float far_max_start_to_contact;    /* +0x30, = +0x2c when <= 0, same floor */
    float max_dist_behind, max_dist_in_front;   /* its swing space +0x28 / +0x2c */
} SwingSetDef;

/* ballfudge.xml row of a zone (gdBallFudgeData +0x10 / +0x14 / +0x18) */
typedef struct { float min_dist, max_dist, dist_per_sec; } BallFudgeZone;

/* swingcfg.xml early-out window (FUN_800d0880) */
typedef struct {
    float min_early_out, max_early_out, soft_shot_mult;   /* swingcfg.xml +0x18 / +0x1c / +0x20 */
} EarlyOutCfg;

/* one predicted ball sample already in the player's frame (FUN_800ed9d0 buffer) */
typedef struct { Vec3 pos; float t; int hittable; } LocalBallSample;

typedef struct {
    const LocalBallSample* s;          /* the hittable window, in time order */
    int   n;
    float now;                         /* point clock */
    float t_first_hittable;            /* FUN_800d17e8: time of the first hittable sample */
    int   short_ball;                  /* outcome mask 4: bounces again on the table (A) */
    int   near_table;                  /* outcome mask 0x10: drops close to the table (B) */
    Vec3  reach;                       /* pcrCharacter +0x16c, already mirrored by side */
    float sot_limit;                   /* 0.5 * table width (DAT_806dea44 * table +8) */
    Vec3  mover_min, mover_max;        /* reach box in the player frame (FUN_800d0b08) */
    int   start_frame;                 /* FUN_800d3e08 [OPEN]: frames already elapsed */
    float max_start_to_contact;        /* "too early" limit (FUN_800d0880 [OPEN] source) */
    const BallFudgeZone* fudge;        /* [19] by HitZone */
    int   (*in_table_volume)(Vec3 contact);   /* FUN_800ee1d4, may be 0 [OPEN volumes] */
    /* per-set timing from the set's own data (RTT); 0 keeps start_frame / max_start_to_contact
     * above for every set (unit tests) */
    int   per_set;
    int   human;                       /* start-frame tolerance 0.6 (FUN_800d3e08) */
    int   soft;                        /* SwingType 3: SoftShotMultiplier on the early-out limit */
    int   swing_started;               /* DAT_806db30d: after StartSwing the set's own limits apply */
    int   far;                         /* |controller +0x48| >= 2.7 (FarMaxStartToContactTime) */
    const EarlyOutCfg* early;          /* swingcfg MinEarlyOut / MaxEarlyOut / SoftShotMultiplier */
    float lookahead;                   /* StartSwing's locomotion lookahead (FUN_800c1894 f1 ->
                                          target +0x11c): taken off every time key [RTT] */
    int   all_eligible;                /* solver +0x168: every set passing category / specialty is
                                          eligible, no fallback / OTT / SOT / ShortAndLow filter
                                          (pcrSwingSet__IsEligible 0x800d1b80) [RTT]. Its writer is
                                          not traced [OPEN]; it is NOT the unknown-outcome state
                                          (that reading drops capture 13 to 87/107). Leave 0. */
} ZoneSelectInput;

/* the result struct the solver ranks (aim target, +0x70..+0x12a) */
enum { TGT_IN_REACH = 0x01, TGT_TOO_EARLY = 0x02, TGT_FLAG4 = 0x04, TGT_FLAG8 = 0x08 };
typedef struct {
    int   set;                         /* index into the set array, -1 = none */
    int   hit_zone;
    int   sample_valid, hittable;
    float t_sample;                    /* time of the sample the swing meets */
    float time_key;                    /* +0x110: slack before the swing must start (>= 0 ok) */
    float dist2;                       /* +0x114: squared contact-point to ball-segment distance */
    int   contact_frame;               /* +0x122 */
    unsigned flags;                    /* +0x128 */
    const SwingBlendRec* blend;
} ZoneTarget;

int  swing_set_eligible(const SwingSetDef* set, const ZoneSelectInput* in, int fb_itz, int fb_soft);
/* FUN_800d021c: best target of one set (0 = none) */
int  swing_set_target(const SwingSetDef* set, int index, const ZoneSelectInput* in, ZoneTarget* out);
int  swing_target_valid(const ZoneTarget* t);                       /* gdBall__HasValidAimTarget */
int  swing_target_better(const ZoneTarget* a, const ZoneTarget* b, float now);   /* Compare... */
/* FUN_800cfc28 over one solver's sets: returns 1 if any target was kept in *best. With none, RTT
 * falls to the pcrSwingMissSets (a miss swing) or, for a ball decided out, the pcrSwingAbortSets
 * (let it go); which miss / abort clip plays is animation, not gameplay. */
/* FUN_800ecebc: is a set of this ActivationCategory usable now. Every category is active for a
 * CPU or a human in control mode 2 / 4 (FUN_8009ead4; a CPU only while serving), or when the
 * category system is off (DAT_806d5be0 = 1: on). Otherwise the stick (DAT_804ce830[p], normalised,
 * cos 45 deg = 0.707 against the axes) and the move speed (|controller +0x48| < 2.7 = near) decide;
 * ExtendedLeft / Right stay active once picked this swing (latch DAT_804ce840, FUN_800ed178, cleared
 * at swing begin FUN_800ed1e0). */
enum {
    ACT_CAT_UNDEFINED, ACT_CAT_ALWAYS, ACT_CAT_NORMAL, ACT_CAT_NORMAL_NEAR, ACT_CAT_NORMAL_FAR, ACT_CAT_EXT_LEFT, ACT_CAT_EXT_RIGHT,
    ACT_CAT_DEF_NEAR, ACT_CAT_DEF_FAR, ACT_CAT_FORWARD, ACT_CAT_DEF_LEFT, ACT_CAT_DEF_RIGHT, ACT_CAT_PROT_LEFT, ACT_CAT_PROT_RIGHT,
    ACT_CAT_COUNT
};
typedef struct {
    int   all_active;                  /* FUN_8009ead4 */
    float stick_x, stick_y;            /* DAT_804ce830[p] */
    float speed;                       /* DAT_806db6a0[p] = controller +0x48: the player's depth z
                                          (live F lines), |z| < 2.7 = near (FUN_800ed5ec) */
    const unsigned char* latch;        /* [ACT_CAT_COUNT], may be 0 */
} CategoryInput;
int  swing_category_active(int category, const CategoryInput* in);

/* FUN_800d42f8 / FUN_800d41e4: with shot flag 1 set, a specialty set flagged
 * DisableIfSpecialtyCheckFails is skipped unless the latched inputs are exactly its kind:
 * 1 top only (8, no 0x10 / 0x20 / 0x40), 2 back only (0x10, no 8 / 0x20 / 0x40), 3 side only
 * (0x20 or 0x40, no 8 / 0x10) */
int  swing_specialty_blocked(int specialty, int disable_if_fails, unsigned shot_flags);

int  swing_select_set(const SwingSetDef* sets, int count, const ZoneSelectInput* in,
                      int fb_itz, int fb_soft, ZoneTarget* best);

/* the swing-space box (+0x30 / +0x40): AABB of its blends' ContactPoints (FUN_800db870) */
void swing_space_box(const SwingBlendRec* blends, int n, Vec3* mn, Vec3* mx);

/* FUN_800d3e08: frames of the clip already skipped by the contact retiming tolerance;
 * tol = ContactFrameTolPct (+0x20), or 0.6 for a human (DAT_806deab4) */
int  swing_start_frame(int contact_frame, float contact_tol_pct, int human);

/* FUN_800d0880 "too early" limit (s from now to contact).
 * Before StartSwing: SwingDataCfg (DAT_806db2f0+0x158) MinEarlyOut 0.7 / MaxEarlyOut 1.8,
 *   limit = MinEarlyOut if ContactFrame/30 <= MinEarlyOut/MaxEarlyOut else MaxEarlyOut *
 *   ContactFrame/30, both x SoftShotMultiplier 1.5 when the shot is soft (SwingType 3).
 * After StartSwing (DAT_806db30d): the set's MaxStartToContactTime, or FarMaxStartToContactTime
 *   when |controller +0x48| >= 2.7 (DAT_806d5be4; likely the move speed [OPEN meaning]). */
float swing_too_early_limit(const EarlyOutCfg* c, int contact_frame, int soft, int swing_started,
                            float max_start_to_contact, float far_max_start_to_contact, int far);

/* FUN_800ee088 / FUN_800ee1d4: the table volumes no non-OTT contact point may fall in, relative to
 * the player (player_pos mirrored onto the +z side). V1: |x| <= 1.15 (DAT_806d5c00 2.3 / 2),
 * 0 <= y <= 0.8 + ball radius, 0 <= z <= table_length/2 + 0.3. V2: the 1.0 m behind it, a wedge
 * whose top falls linearly from V1's to 0. */
int  swing_contact_in_table_volume(Vec3 contact, Vec3 player_pos, float table_length, float ball_radius);

/* ---------------------------------------------------------------- TableTgtPos: target grid */
/* The aim path in use (aim object +0x1360 set on every live swing): FUN_800a9850 per swing tick.
 * A 3x3 grid of cells over a target area of the receiving half; the aim direction picks a cell,
 * the stick held in that direction drifts the point further, charging spin nudges it inside the
 * cell, and the incoming-spin kick shifts it. Two grids per player: normal (aim +0x9e0, area set
 * 0) and soft (aim +0x60, area set 1, SwingType 3). */

/* table +0x8 / +0xc / +0x10 and table cfg +0x18..+0x38 (not in any tune XML; live values
 * W 1.5224, H 0.763, L 2.744, frac 0.489 0.325 0.481 | 0.489 0.097 0.482 | 0.306 0.200 0.150) */
typedef struct {
    float width, height, length;
    float frac[9];                       /* [0..2] WorldTgtPos base area, [3..5] normal grid,
                                            [6..8] soft grid: x half-extent, z toward net, z away */
} TableTargetCfg;

typedef struct {
    int   side;                          /* +0x2e9: 0 = target area on the +z half */
    int   area_set;                      /* +0x2e8: 0 normal, 1 soft */
    int   cell_depth, cell_lat;          /* +0x2ec / +0x2f0: 0..2 (depth 0 = nearest the net) */
    Vec3  drift, drift_prev;             /* +0x2c8 / +0x2d8: stick drift */
    Vec3  offset;                        /* +0x298: kick + drift, clamped */
    float spin_x, spin_z;                /* +0x2a8 / +0x2b0: spin-charge nudge */
    int   serve_frozen;                  /* +0x2f4 */
    int   slow;                          /* +0x300: point went past the soft edge this swing */
    int   pitch;                         /* +0x304: KickPitchOffset override, -15..0 */
} AimGrid;

typedef struct {
    AimGrid grid[2];                     /* [0] normal (+0x9e0), [1] soft (+0x60) */
    int     edge_risk;                   /* aim +0x1369 */
} AimTarget;

/* FUN_800a8c18 via FUN_800aad64 (player reset, every point): side, centre cell (1, 1) */
void aim_target_reset_point(AimTarget* a, int side, float* wobble);
/* swing begin (FUN_80097d08 -> FUN_800aabfc -> FUN_800aac7c): offsets, drift, flags, the cell
 * back to (1, 1); *wobble = 0.001 (DAT_806db368, one global shared by both players) */
void aim_target_reset_swing(AimTarget* a, float* wobble);

/* FUN_800a9850 direction -> cell; returns 0 for no direction (cell kept) */
int  aim_dir_cell(int dir, int* depth, int* lat);

/* cell centre on the table, without offsets (FUN_8011cc84 mode 1 + FUN_800a94ec): t = 1/6, 1/2,
 * 5/6 across the area; y = table top + 0.01 (DAT_806d529c) */
Vec3 aim_cell_point(const TableTargetCfg* t, int side, int area_set, int depth, int lat);

/* FUN_800a78bc + FUN_800a7b30, every swing frame from swing__ReadSpinInputAndTick with the four
 * spin inputs (side a, side b, top, back; x0.0015 per frame, z x0.75 while serving), clamped to
 * the cell (+-1/6 of the area). The live x sign also depends on two player flags (FUN_8009eac4 /
 * FUN_8009eb54) and the camera flip: x_sign is passed in (+1 = side_a - side_b on side 0). */
void aim_spin_nudge(AimGrid* g, const TableTargetCfg* t, float side_a, float side_b, float top,
                    float back, int serving, int x_sign);

typedef struct {
    int   dir;                           /* FUN_8009b2b0: 1..8, 0 = none */
    float aim_mag;                       /* FUN_8009b5dc: 1 for a human (control 2/3), AI value */
    float approach;                      /* attribs Accuracy[set] (+0x20 / +0x3c / +0x58) * player +0x1844 */
    Vec3  kick;                          /* TableTgtKick (hit +0xd4) */
    float ball_z;                        /* BallPos z (hit +0x58) */
    Vec3  prev_world_tgt;                /* WorldTgtPos (hit +0x1c) of the previous tick */
    int   soft;                          /* SwingType 3: soft grid */
    int   serving;                       /* player state 3..5 (FUN_8009ea90) */
    int   serve_edges;                   /* hit +0xb6 & 0x1000: serve edge thresholds */
    int   edge_check;                    /* player +0x4bd0 == 0 || serve_edges */
    int   force_short;                   /* FUN_8009cfcc(HitZone) && player +0x4c3a: depth 0 */
    float dt;                            /* DAT_804f23a4 */
} AimTickInput;

/* one FUN_800a9850 tick (also run 15 / 25 / 40 more times by the hard-swing applier).
 * Returns TableTgtPos; *kick_pitch is set when the normal grid's pitch is < 0 (hit +0x2c). */
Vec3 aim_target_tick(AimTarget* a, float* wobble, const TableTargetCfg* t, const AimTickInput* in,
                     int* kick_pitch);

/* ---------------------------------------------------------------- incoming-spin kick */
/* FUN_8009df24 every swing frame (swing__ResolveShotTick, rally only) -> FUN_800ab3ac.
 * The incoming ball's spin pushes the receiver's target: two accumulators (player +0x4c14
 * "vertical", +0x4c18 "side", reset at swing begin by FUN_80097d08) grow while the player charges
 * spin, and TableTgtKick = (SideSpinCoef * side, 0, -VertSpinCoef * vert) with signs from the
 * incoming spin. The kick enters TableTgtPos through the target grid offset. */

/* shottarget/char_<c>.txt, copied into the aim object +0x24 / +0x28 (0.022 / 0.022) */
typedef struct { float vert_spin_coef, side_spin_coef; } ShotTargetCoef;

typedef struct { float vert, side; } KickState;      /* player +0x4c14 / +0x4c18 */

/* FUN_8011c38c + FUN_80045960: the hitter's spin attribute for the incoming spin's category.
 * x = spin.x * (vel.z > 0 ? 1 : -1); y = spin.y, negated when (vel.z <= 0) == camera flip.
 * Category (x, y signs) -> attribute: (+, 0) a10; (-, 0) a14; (0, +-) a1c; diagonals average
 * a1c with a10 (x > 0) or a14 (x < 0) (x 0.5, DAT_806dd918); no spin -> 0 (DAT_806dd914).
 * a10 / a14 / a1c = the hitter's attribs +0x28 struct +0x10 / +0x14 / +0x1c, probably the top /
 * back / side spin attributes [hypothesis]. */
float kick_spin_attr(Vec3 spin, Vec3 vel, int camera_flip, float a10, float a14, float a1c);

/* FUN_800ab07c: extra "vertical" push against a fast ball when the receiver stands close to the
 * table. Player box (FUN_8011a5c4): z from L/2 + gd88 (0.6) to that + gd84 (3.4) on the player's
 * half; zref is 20 % of the box depth in from its table end. t = (player_z - zref) / (0.2 * gd84).
 *   f = in_speed >= 18 ? (clamp((in_speed - 10) / 12, 0, 1) + 0.25 * in_pace)^2 : 0
 *   v = clamp(f + s^2 f + 0.5 * own_pace * f, 0, 1),  s = clamp((own_speed - 10) / 12, 0, 1)
 *   push = |5 v t| when t is on the table side (sign of the target base z), clamped to [0, 5],
 *          or to [0, 0.005] for a forced-short shot (player +0x4c3a or FUN_8009cfcc). */
typedef struct {
    float in_speed, in_pace;       /* incoming shot: hitter's PostHitSpeed (+0x70), pace (+0x10c) */
    float own_speed, own_pace;     /* receiver's hit +0x70 / +0x10c */
    float player_z;                /* gamedata player entity +0x30 z */
    float base_z;                  /* WorldTgtPos base z (FUN_8011c658 mode 0) */
    int   side;                    /* gamedata +0x40 + p (FUN_8009cc50): 0 = player on -z */
    float table_length, box_gap, box_depth;   /* table +0x10, gamedata +0x88, +0x84 */
    int   force_short;
} KickPushInput;
float kick_depth_push(const KickPushInput* in);

typedef struct {
    int   have_prediction;         /* live set sample count >= 2 (FUN_80124dbc) */
    Vec3  in_spin;                 /* live prediction data +0x0 (FUN_80123b8c), rad/s */
    float max_rps;                 /* GetMaxSpinRPS(char, dominant axis of the incoming SpinAxis,
                                      9, serve bit); normalises |in_spin| by 2*pi*max_rps */
    float spin_attr;               /* kick_spin_attr */
    int   reduced;                 /* (flat hit +0x12e and meter player +0x1860 > 0) or zone flag
                                      (FUN_80119cf4(p, 2)): scale 0.05 instead of 2 * spin_attr */
    float own_spin_x, own_spin_y;  /* receiver's launch spin x, y (FUN_8011bd78(hit, out, 1)) */
    int   charging;                /* any latched spin input player +0x4c24..+0x4c27 */
    float push;                    /* kick_depth_push */
    int   side;                    /* FUN_8009cc50 */
    float in_speed;                /* hitter's PostHitSpeed (pitch rule) */
    int   force_short;             /* player +0x4c3a */
    int   soft;                    /* SwingType 3 */
    int   flat;                    /* player +0x4c46 */
} KickInput;

/* one FUN_8009df24 tick: updates the accumulators, returns TableTgtKick (y = old_kick_y) and
 * the KickPitchOffset this function writes (the grid may then override a negative one). */
Vec3 kick_tick(KickState* k, const ShotTargetCoef* c, const KickInput* in, float old_kick_y,
               int* kick_pitch);

/* ---------------------------------------------------------------- launch inputs: elevation, paddle, pace */

/* hit +0x124, the steepest arc SolveHitAim's low table reaches: set to 65 deg (DAT_806def78) by the
 * hit-state constructor FUN_8011b944 and never written again (871/871 captured frames) */
#define HIT_MAX_ELEVATION 1.13446f

/* PaddleVel (hit +0x60) for the launch ease-in: FUN_800cc21c, written at StartSwing and again at
 * the anticipation reach (FUN_8009868c). It is the chosen blend's PostHitPaddleVel (pcrSwingBlend
 * +0x48, swingspace.xml), scaled by the retiming speed-up (ContactFrame / 30) / (t_contact -
 * t_start) (FUN_800d6364) when the target's time key >= 0 and the swing instance's +0x80 flag is
 * clear, then turned by pi about y (x, z negated) for a player on side 0. An abort swing takes its
 * velocity from the abort set instead (FUN_800d8714). */
Vec3 swing_paddle_vel(Vec3 post_hit_paddle_vel, float time_key, int instance_flag, int contact_frame,
                      float t_contact, float t_start, int side);

/* hit +0x10c, the game-pace accumulator, every swing__ResolveShotTick:
 *   += 0.003 * Force^2 (DAT_806de3e8), clamped [0, 1], when (slam zone or top input share >= 0.5
 *   or the zone flag FUN_80119cf4(p, 2)) and KickPitchOffset < 1 and not flat (+0x4c46).
 * Force is the frame's hit +0x14 (0 on a serve). Hard swings then scale it by (1 + mult)
 * (swing_hard applier). Reset every point (FUN_8011a258). */
float swing_pace_tick(float pace, float force, int slam_zone, float top_share, int zone_flag,
                      int kick_pitch, int flat);

/* serve GamePace (hit +0x74), FUN_8009868c at the serve contact -> FUN_8011a144 ->
 * gdHitState__SetGamePace:
 *   g = (a == 1 and SwingType != 2 ? 0.5 : 0.25 a) + (|meter| == 100 ? 0.5 : 0)
 *   v = clamp(g * clamp((int)timer / 100, 0, 1), -1, 1), then clamped [0, 1]
 *   GamePace = clamp(v + GamePaceNormalBias (-0.8), -1, 1)
 * a = player +0x4c0c = 1 - attribs[+0x88 + 4 * latched input] (side a, side b, top, back; written
 * by FUN_80096cc0 while charging; CPU 0.2), meter = serve meter (*(player +0x17ec) + 0x14),
 * timer = spin timer player +0x4c2c. (Rally shots: GamePace = clamp(+0x10c + pace bonus) - 0.8.) */
float serve_game_pace(float meter, float a, float timer, int swing_type, float normal_bias);

/* ---------------------------------------------------------------- shipped swing data */
/* one character's five swing solvers (pcrSwingSolver slots +0x64 ts, +0x68 bs, +0x6c ts_itz,
 * +0x70 bs_itz, +0x74 sft; swing_solver_slot picks one) and the ballfudge.xml rows, as RTT holds
 * them after load. Generated literals (tools/swing_data/gen_swing_sets.py ->
 * data/swing_sets_<char>.inc), no parsing at runtime. */
typedef struct {
    const SwingSetDef* sets[SOLVER_COUNT];
    int   count[SOLVER_COUNT];
    const BallFudgeZone* fudge;        /* [19] by HitZone */
    const char* const* anims;          /* clip names of SwingBlendRec.anim */
    int   anim_count;
} CharacterSwingData;
void swing_data_sweden(CharacterSwingData* d);
void swing_data_usa(CharacterSwingData* d);
void swing_data_austria(CharacterSwingData* d);

/* ---------------------------------------------------------------- Focus meter */
/* The "ITZ meter" of the code is the game's Focus meter (strings FocusMeterCharged / Filled,
 * stxt_auto_activate_full_focus). Player +0x1858 (FUN_800abb00); its gauge at +0x1860 holds two
 * values on a 0..1000 scale [RTT, decompiled 2026-10-11, map/gameplay.md]:
 *   value  (gauge +0x0)   the committed meter
 *   ghost  (gauge +0xc)   value plus the charge of the swing in flight
 *   target (gauge +0x18)  what value climbs to once the shot lands (= ghost at launch)
 * Charging spin during a rally swing raises ghost; a landed shot commits it (value climbs 30 per
 * animation step); a point end, a fault or an uncharged swing gives the pending part back. A full
 * meter (value = ghost = 1000) can switch on "full focus" (gdGameData +0xac levels, 5 / 10 / 15 s),
 * which drains the meter over its duration and raises the meter's level (0 -> 1 -> 2) when it ends.
 * The meter and its level carry over from point to point; EV_GAME_START (0x841, every game) resets
 * them (capture 10, 2026-10-13). */
#define FOCUS_MAX 1000.0f

typedef struct {
    float value, ghost, target;
    int   filling, spending, animating;  /* +0x28 / +0x29 / +0x2a */
    int   hold;                          /* +0x38 */
    float hold_t;                        /* +0x3c (only read and cleared by the game) */
    int   level;                         /* +0x2c: 0..2, the next full-focus level */
    int   armed;                         /* +0x4: focus shot requested at swing begin -> type 9 */
    int   soft;                          /* +0x6: forces type 3 */
    float overflow;                      /* +0x40: charge added while already full (msg 0x1021) */
    int   overflow_hold;                 /* +0x44 */
    int   full_posted;                   /* +0x4c: msg 0x1020 "FocusMeterFilled" sent */
    int   auto_request;                  /* +0x45: activation request for this frame (cleared every
                                            tick): a human's button 0x39, or the CPU's AI with the
                                            meter above 0.9999 (FUN_80096ff0 / FUN_800a149c) */
} FocusMeter;

/* full focus, per player and level (gdGameData +0xac + p*0x24 + level*0xc, +0xf4 remaining s,
 * +0xfc start time) */
typedef struct {
    int   active[2][3];
    float remaining[2];
    float started[2];
} FullFocus;

int   full_focus_any(const FullFocus* f, int p);                 /* FUN_80119d0c */
void  full_focus_off(FullFocus* f, int p);                       /* FUN_80119ea8 (msg 0x1023) */

/* FUN_800abbf4 (msgs 0x840 match intro, 0x841 game start, 0x80c, 0x5003): gauge 0, level 0,
 * flags clear, full focus off for both players */
void  focus_reset(FocusMeter* m, FullFocus* f);

/* FUN_800ac3bc: ghost += clamp(|amount|), into overflow when value is already full */
void  focus_fill(FocusMeter* m, float amount);
/* FUN_800ac4ac: value -= clamp(|amount|) */
void  focus_drain(FocusMeter* m, float amount);
/* FUN_800ac68c / FUN_800ac724: start the fill / spend animation (force restarts it) */
void  focus_mark(FocusMeter* m, int spend, int force);
/* FUN_800ac4f4: one animation step (30 units) */
void  focus_animate(FocusMeter* m);

/* the meter half of one rally swing__ResolveShotTick frame */
typedef struct {
    int   charge_allowed;   /* FUN_8009ddfc: a spin input latched, a normal swing active and the
                               HitZone not a lunge / JIT (2, 3, 8, 10, 11, 16); 1 in control mode >= 2 */
    int   flat;             /* hit +0x12e */
    int   point_over;       /* player +0x4bd0 (set by the point messages, cleared at contact) */
    float intensity;        /* player +0x4bbc, 0..1 (plyrIntensityMgr) */
    float momentum;         /* gdPlyrAttr +0x70 Momentum */
    float charge_delta;     /* gdPlyrLimits +0x84 MeterChargeDelta (20) */
    float dt_game, dt_real; /* 0x804f23a4 / 0x804f23f4 */
    int   serve_into_net;   /* ball +0x164 */
} FocusSwingInput;
void  focus_swing_frame(FocusMeter* m, const FullFocus* f, int p, const FocusSwingInput* in);

/* the meter's charge for this swing, as the PostHitSpeed call passes it on to the elasticity:
 * clamp((ghost - value) / 1000, 0, 1), 1 with full-focus level 2 active */
float focus_shot_charge(const FocusMeter* m, const FullFocus* f, int p);

/* FUN_8009cdf4, at launch: a CPU stretched shot (HitZone 2, 3, 8, 10, 11, 16) without full focus
 * pays meter (180 / 170 / 160 / 200 / 220 for own - opponent level 0 / -1 / -2 / +1 / +2, or all
 * of it) unless the meter is full; such a shot, or a soft shot (type 3) on an empty level-0 meter,
 * is "stretched" (player +0x4c3a: no spin charge, KickPitchOffset 31 on a fast ball, short cell).
 * Returns the stretched flag. */
int   focus_launch_stretch(FocusMeter* m, const FullFocus* f, int p, int cpu, int hit_zone, int soft,
                           int opp_level);
/* plrSwingLogic__LaunchSwing: focus request cleared, target = ghost */
void  focus_launch(FocusMeter* m, int point_over);

/* pongPlayer::Process meter events */
void  focus_on_bounce(FocusMeter* m);                            /* msg 1 / 2, the striker's shot landed */
void  focus_on_fault(FocusMeter* m);                             /* msg 0x824: -180 for the server */
void  focus_on_point(FocusMeter* m, const FullFocus* f, int p, int lost);   /* 0x82d / 0x82e */

/* FUN_800abcc0, every frame (gdGameData +0x14 < 1, control mode != 2): animation, activation of
 * full focus (auto_activate = profile +0x51 "auto activate full focus"), its drain over 5 / 10 / 15 s
 * (dt_real; a quarter of it while both players are at level 2) and the level-up when it ends.
 * rally_live = FUN_80097c9c (player state 1 / 2 / 6 / 7 and the point not over), paused =
 * FUN_800b566c. Returns 1 on the frame full focus switched on. */
int   focus_frame(FocusMeter* m, FullFocus* f, int p, int auto_activate, int rally_live, int paused,
                  float dt_real, float now);

/* gdGameData__ComputeShotPaceBonus 0x80119fc8: 0.25 with both players at full-focus level 2,
 * 0.1 with any level of this player active, else 0.1 / 0.15 / 0.2 by the meter level */
float focus_pace_bonus(const FullFocus* f, int p, int meter_level);

#endif /* RTT_SWING_H */
