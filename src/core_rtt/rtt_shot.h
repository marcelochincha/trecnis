/*
 * rtt_shot.h -- one swing from its start to the launch, wiring the ported swing and ball pieces in
 * RTT's per-frame order (pongPlayer__BeginSwing -> swing__ReadSpinInputAndTick /
 * swing__ResolveShotTick + aimGrid__TickTableTarget every frame -> plrSwingLogic__LaunchSwing ->
 * ball::OnRacketHit). Docs: map/gameplay.md (2026-10-11 swing sections), map/ball-physics.md.
 *
 * Everything here takes already-loaded tuning (characters.xml, swingcfg.xml, shottarget,
 * table cfg); nothing parses XML. What a human device or the AI must supply per frame is in
 * ShotFrameInput; the AI side of it (which inputs a CPU presses) is the AI topic and stays a
 * [CHOICE] in rtt_match.cpp.
 */
#ifndef RTT_SHOT_H
#define RTT_SHOT_H

#include "rtt_ball.h"
#include "rtt_swing.h"

enum { HIT_ZONE_COUNT = 19 };   /* powerZoneData order: Push, Forehand, FH Lunge, FH Lunge Far, FH
                                   Smash, FH Scoop, FH OTT, FH SOT, FH JIT, Backhand, BH ..., SoftPush,
                                   SoftMid (4 / 12 = the Slam zones) [DATA] */

/* per character [DATA characters.xml gdPlyrLimits / gdPlyrAttr / powerZoneAttr, shottarget] */
typedef struct {
    PlayerAttribs  attribs;                 /* MaxSpinRPS, charge time / rate */
    SwingAttrSet   attr[3];                 /* gdPlyrAttr normal / ITZ / MaxITZ (+0x10 / +0x2c / +0x48) */
    PowerZone      zone[HIT_ZONE_COUNT];
    float          serve_spin_attr[4];      /* ServeTopSpin / BackSpin / LeftSpin / RightSpin (+0x88..+0x94),
                                               in latched-input order top, back, side a, side b */
    float          momentum;                /* gdPlyrAttr +0x70 Momentum (Focus meter fill) */
    ShotTargetCoef target_coef;             /* VertSpinCoef / SideSpinCoef 0.022 */
} ShotCharacter;

/* what a device or the AI feeds a swing each frame */
typedef struct {
    float spin_in[4];        /* side a, side b, top, back held this frame, times the input factor
                                (HumanSpinChargeFactor 1.0 / AiSpinChargeFactor 0.5) */
    int   aim_dir;           /* 0 none, 1..8 (FUN_8009b2b0 buckets) */
    float aim_mag;           /* 1 for a human (FUN_8009b5dc), the AI's value for a CPU */
    float gesture;           /* human swing gesture magnitude (0 = none) */
    int   soft_button, power_button;
    float ai_force;          /* CPU soft-shot HitForce (player->ai +0x4c8) */
    float swing_strength;    /* player +0x1934 at contact: tier (3.4 / 4.1); 0 on a serve = whiff */
    float serve_meter;       /* *(player +0x17ec) + 0x14 */
    float serve_timer;       /* player +0x4c2c */
    int   focus_held;        /* any of the 8 ITZ_* spin inputs held at swing begin (FUN_800ac254):
                                arms the focus shot, SwingType 9 */
} ShotFrameInput;

/* the world around one swing */
typedef struct {
    const BallParams*     bp;
    const TableGeom*      table;
    const TableTargetCfg* tcfg;
    const SwingConfig*    cfg;
    float  now, dt;
    float* wobble;           /* DAT_806db368, one per world */
    int    camera_flip;      /* DAT_806dacd8 + 5: the active camera shot's flip byte (shot data
                                +0x28 -> +0x14, copied by FUN_80018d80 on every shot change);
                                live: 1 while the human plays the +z half (capture 10) */
    int    dest_flip;        /* camera destination +0x30 (FUN_80120c04 == 0): HitSpinInput.flip_side;
                                live: 1 while the human plays the -z half, for both hitters */
    int    human;            /* gdGameData +0x38[p] == 1 */
    int    control_mode;     /* player +0x1918 (>= 2: charging always allowed, FUN_8009ddfc) */
    Vec3   ball_pos;         /* BallPos: the predicted sample the swing meets */
    float  player_z;         /* for the kick's depth push */
    /* the incoming shot (rally) */
    int    have_prediction;
    Vec3   in_spin, in_vel;  /* prediction spin / velocity at the contact sample */
    float  in_speed, in_pace;/* hitter's PostHitSpeed / pace accumulator */
    const ShotCharacter* in_char;   /* hitter's attributes (spin attribute, MaxSpinRPS) */
    float  far_extra;        /* gdGameData +0x84 + +0x88 + table half length (SolveHitAim cap) */
    float  net_height;       /* DAT_806db760 (0.147) */
    float  net_margin;       /* table cfg net +0x80 (0.05) */
    /* Focus meter */
    FocusMeter* focus;       /* the hitter's meter (player +0x1858), may be 0 */
    FullFocus*  full;        /* gdGameData full-focus levels */
    int    player;           /* 0 / 1 (player +0x1d0) */
    int    opp_level;        /* opponent's meter level (FUN_8009cdf4) */
    float  intensity;        /* player +0x4bbc, 0..1 */
    int    point_over;       /* player +0x4bd0 */
    float  dt_real;          /* 0x804f23f4 */
    int    serve_into_net;   /* ball +0x164 */
} ShotContext;

typedef struct {
    int   active, serving, human, side, done_type;
    int   swing_type, hit_zone;
    SwingTypeFlags     flags;
    SwingClassifyState cls;
    SwingChargeState   ch;
    KickState          kick;
    AimTarget          aim;
    Vec3  table_err;         /* TableTgtError hit +0xc4 (CPU only) */
    Vec3  table_tgt;         /* TableTgtPos +0xfc */
    Vec3  world_tgt;         /* WorldTgtPos +0x1c */
    Vec3  kick_vec;          /* TableTgtKick +0xd4 */
    int   kick_pitch;        /* KickPitchOffset +0x2c */
    float post_hit_speed;    /* +0x70 */
    float latched[4];        /* player +0x4c24..+0x4c27: top, back, side a, side b */
    int   stretched;         /* player +0x4c3a (FUN_8009cdf4 at launch) */
    float focus_charge;      /* the meter charge passed to the elasticity (focus_shot_charge) */
    /* HitZone selection and swing start (driven from the prediction by rtt_match.cpp) */
    int   started;           /* plrSwingLogic__StartSwing done */
    float t_start, t_contact;
    ZoneTarget zone;         /* the chosen pcrSwingSet target: set, blend, sample time, time key */
    unsigned char cat_latch[ACT_CAT_COUNT];   /* DAT_804ce840[p], cleared at swing begin */
} ShotState;

/* the launch a contact produces, for ball_predict */
typedef struct {
    Vec3  pos, vel, spin;
    int   swing_type, is_serve, whiff;
    float game_pace;         /* [0, 1] before GamePaceNormalBias */
    float restitution;       /* ball_hit_elasticity */
    float paddle_speed;      /* |PaddleVel|, 0 = unknown */
    float elev;
    ShotTier tier;
} ShotLaunch;

/* swing begin (pongPlayer__BeginSwing / aim reset): per-swing state cleared, the grid cell back to
 * (1, 1), base SwingType from the Focus meter (itzMeter__GetBaseSwingType: 9 when a human held an
 * ITZ input, else 5; the CPU variant FUN_8009c6fc clears the request first), or swingcfg
 * Player1SwingType 2 for a human serve [DATA].
 * table_err_mag: CPU aiMaxError * (1 - Accuracy) (FUN_800aaab8; AI topic), 0 for a human. */
void shot_begin(ShotState* s, int side, int serving, int human, float table_err_mag, Rng* rng,
                float* wobble, FocusMeter* focus, int focus_held);

/* every point: the grid side / cells (aimGrid__ResetForPoint) */
void shot_reset_point(ShotState* s, int side, float* wobble);

/* one swing frame; pace = the hitter's hit +0x10c accumulator (kept across swings, reset per point) */
void shot_tick(ShotState* s, const ShotCharacter* c, const ShotContext* x, const ShotFrameInput* in,
               float* pace);

/* contact: LaunchSwing's extra tick, the hard-swing applier, the launch spin and the solve */
void shot_contact(ShotState* s, const ShotCharacter* c, const ShotContext* x, const ShotFrameInput* in,
                  float* pace, ShotLaunch* out);

/* shipped data as literals (no parsing): characters.xml gdPlyrLimits + the first gdCharData
 * (Sweden) gdPlyrAttr / powerZoneAttr, shottarget/char_sweden.txt, swingcfg.xml SwingDataCfg, and
 * the table cfg target fractions read live (capture 7) [DATA] */
void shot_character_sweden(ShotCharacter* c);
void swing_config_default(SwingConfig* c);
void table_target_cfg_default(TableTargetCfg* t);

#endif /* RTT_SHOT_H */
