/*
 * rtt_ball.h -- ball physics, trajectory prediction and ball events.
 * Docs: findings/reconstruction/ball-physics.md, findings/map/ball-physics.md (2026-10-09 updates).
 *
 * RTT predicts the whole flight at the moment of the hit (ProcessHit fills a 120-slot ring of
 * samples, one per physics step), then plays it back on a clock. Gameplay (AI, swing selection,
 * aim assist) reads the future from the same ring. Events (bounces, net, out) are posted as the
 * playback clock passes the sample that produced them.
 */
#ifndef RTT_BALL_H
#define RTT_BALL_H

#include "rtt_core.h"
#include "rtt_events.h"

/* Surface response, ResolveBounceImpulse 0x80046390 (re-read 2026-10-10):
 *   jn = -(1+e)*vn*n
 *   slide if (mu*vn)^2 < slide_thresh*|vt|^2 :  jt = -2*mu*|vn| * vt_hat
 *   else (grip)                             :  jt = -grip*(1+e) * vt
 *   spin += -(spin_coupling/r) * (n x (jn+jt))      (hollow sphere, I = 2/3 m r^2)
 *   pos  += separation * n                          (always, even when separating) */
typedef struct {
    float radius;            /* 0.0200 physics bound (live 2026-10-10; the render bound is 0.020947) [RTT] */
    float gravity_y;         /* -9.8 [RTT] */
    float gravity_scale;     /* rally: lerp(0.9, -9.8, -11.85)/-9.8 = 1.18829 (SetGravityFromPace 0x8011c280; live-confirmed 2026-10-10) [RTT] */
    float magnus;            /* 0.004 (both shot-type slots) [RTT] */
    float drag;              /* 0.01, 0.011 for shot type 10 only -- quadratic [RTT] */
    float drag_special;
    float table_restitution; /* e: material +0xa4 = 0.9 in a rally (live 2026-10-10); e == 0.95 would force mu 0.1 [RTT] */
    float table_friction;    /* mu = material 0.25 * table cfg = 0.21 (live 2026-10-10) [RTT] */
    float serve_gravity_scale; /* serve flight: 1.0 -- OnRacketHit skips ApplyHitGravityScale for IsServe and
                                * resets the scale to 1.0 after every prediction (FUN_80045e0c) [RTT] */
    float serve_restitution;   /* serve flight: e = 1.0 (ball_hit_elasticity; live 2026-10-10) [RTT] */
    float pace_factor;       /* K = GamePaceFactor 1.6 (scenegraph_wii.xml gmBallNode) [DATA] */
    float pace_normal_bias;  /* GamePaceNormalBias -0.8 [DATA] */
    float grip;              /* 0.40 (r2-0x77d0) [RTT] */
    float slide_thresh;      /* 0.16 (r2-0x77d8) [RTT] */
    float spin_coupling;     /* 1.50 (r2-0x77c8) [RTT] */
    float separation;        /* 0.002 m push along n after every contact (r2-0x77c4) [RTT] */
    float cast_skin;         /* 0.001 m added to the swept segment (r2-0x76a0) [RTT] */
    float spin_cap_sq;       /* 1e6 (rad/s)^2 [RTT] */
    float net_restitution;   /* table cfg +0x7c = NetBallElasticity 0.0 [RTT field, DATA value] */
    float net_friction;      /* table cfg +0x78 = NetBallFriction 10.0 [RTT field, DATA value] */
} BallParams;

typedef struct {
    float half_width;        /* 0.7612  [RTT] */
    float half_length;       /* 1.3720  [RTT] */
    float top_y;             /* 0.763 (live 2026-10-10: bounce plane = top + r = 0.783) [RTT] */
    float net_half_width;    /* 0.886   [DATA net.xml NetUpperLeft.x] */
    float net_bottom_y;      /* 0.755   [DATA] */
    float net_top_y;         /* 0.915   [DATA] */
    int   net_enabled;       /* demo: keep the collider even when the net mesh is hidden [CHOICE] */
} TableGeom;

typedef enum { SURF_NONE = 0, SURF_TABLE = 1, SURF_NET = 2 } Surface;

typedef struct {
    float   t;               /* playback key [RTT +0x00]: ball_step advances it by physics dt (dt*toi on contact);
                              * ball_predict rescales it by key_step/dt (game pace) [RTT] */
    Vec3    pos, vel, spin;  /* spin = angular velocity, rad/s [RTT +0x0c/+0x1c/+0x2c] */
    uint8_t bounced;         /* table contact this step [RTT +0x08] */
    uint8_t net;             /* net contact this step [RTT +0x09] */
    uint8_t hittable;        /* a paddle may strike here, set by classification [RTT +0x0a] */
    Vec3    contact;         /* contact point of bounce/net (valid if bounced||net) */
} BallSample;

/* gdBallPredictData outcome word **(data+0x2420), time at data+0x2428 (getters 0x80120f78..fbc,
 * setters 0x80120fd4..1054) [RTT]:
 *   KNOWN          1    clear = unknown (gdBallPredictData__IsOutcomeUnknown)
 *   FAULT          2    the striker's shot fails (out / net / wrong half); the receiver's solver
 *                       then walks its abort sets (let it go)
 *   BOUNCES_AGAIN  4    after the hittable window the ball bounces on the table again (HitZone mask 4)
 *   NET            8    the failing flight touched the net
 *   NEAR_TABLE  0x10    the ball drops below the table top close to it (HitZone mask 0x10)
 * A legal landing is NOT decided by ClassifyOutcomeCore: ExtrapolateUnknownOutcome (0x8004caf4)
 * decides it and sets BOUNCES_AGAIN / NEAR_TABLE. */
enum { OUTCOME_KNOWN = 1, OUTCOME_FAULT = 2, OUTCOME_BOUNCES_AGAIN = 4, OUTCOME_NET = 8, OUTCOME_NEAR_TABLE = 0x10 };

typedef struct {
    BallSample s[RTT_BALL_PATH_SLOTS];
    int   count;             /* filled samples, s[0] = hit */
    int   cursor;            /* next sample whose events have not been posted */
    float clock;             /* playback time since the hit */
    unsigned outcome;        /* OUTCOME_* bits */
    float outcome_t;
    int   landing;           /* index of the legal far-half bounce, -1 if none */
    int   shot_type;         /* selects drag/magnus slot (10 special) [RTT] */
    float key_step;          /* playback seconds per physics step = s/60 from the game pace (mgr+0x28) [RTT] */
    Side  striker;
    int   is_serve;          /* classification expects an own-half bounce first (hit+0xb6 & 0x1000 = IsServe, gdHitState__AddWidgets) [RTT] */
    int   ended;             /* end-of-path event already posted */
    float gravity_scale, restitution;   /* the flight's own ball state, for the ring extension */
    int   terminated;        /* data +0x2418: the flight ended (ProcessHit stop rule, or an end
                                marker); a path cut only by the 120 slots is extended every frame */
    int   fudged;            /* aim assist already applied to this path */
} BallPath;

void ball_default_params(BallParams* p);
void table_default_geom(TableGeom* g);

/* a = Magnus(spin x vel)*km - kd*|v|*v + g*scale  (ComputeAcceleration 0x8004670c) [RTT] */
Vec3 ball_accel(const BallParams* p, Vec3 vel, Vec3 spin, int shot_type);

/* table/net impulse (ResolveBounceImpulse 0x80046390, formula above) [RTT] */
void ball_bounce(const BallParams* p, float restitution, float friction, Vec3 n,
                 Vec3* pos, Vec3* vel, Vec3* spin);

/* one fixed step: integrate, cast the segment (plus skin) against table top + net, on contact
 * rewind pos/vel to the impact and bounce there; the sample then sits AT the impact and its time
 * advances by dt*toi, not dt (IntegrateStep 0x8004b2ec) [RTT] */
void ball_step(const BallParams* p, const TableGeom* g, int shot_type,
               const BallSample* prev, BallSample* out, float dt);

/* playback seconds per physics step for a shot's game pace in [0,1]
 * (SetGamePace 0x8011c4b4 + SetTimeStepFromGamePace 0x80124dd8):
 *   x = clamp(pace + NormalBias, -1, 1);  s = x > 0 ? lerp(x, 1, 1/K) : 1 + |x|*(K-1);  -> s/60
 * K = 1.6: pace 0 -> 1.48 (ball shown 1.48x slower than its physics), 0.1 -> 1.42, 0.8 -> 1.0. [RTT] */
float ball_time_step(const BallParams* p, float game_pace);

/* table restitution of the next flight, set at swing resolution (FUN_80047114 -> hit.Elasticity
 * +0x7c, read back by IntegrateStep as e) [RTT, decompiled 2026-10-10; 0.9 / 1.0 live-confirmed]:
 *   hit+0x2c > 5 (meaning not traced)  -> 0.95  (IntegrateStep then forces mu = 0.1)
 *   swing_type 3                       -> lerp(clamp(x + mode/3, 0, 1), 0.875, 0.775)
 *   swing_type 6 or 11                 -> 0.85
 *   serve                              -> 1.0
 *   any other rally shot               -> 0.9
 * swing_type = hit+0x8c (5 normal, 3 short/fast gesture, 6/11 = 5 or 9/10 on hit zone 4/12,
 * 7 specialty, 8 ITZ); x = the 0..1 swing value FUN_8009d044 passes, mode = swing mode 0..2. */
float ball_hit_elasticity(int swing_type, int is_serve, int hit_2c, float x, int mode);

/* launch ease-in of the playback keys (FUN_8004c49c, run by ProcessHit when DAT_806d44b0 = 1;
 * live-validated 2026-10-10 on 21 captured shots, 2e-6 s): only when |launch vel| <= paddle_speed
 * (hit state PaddleVel +0x60). With i the RTT ring index (0 pre-hit, 1 post-hit, 2.. integrated):
 *   f = clamp((i-2)/15, 0, 1),  w = sin(pi/2 * f)
 *   t_i = t_{i-1} + lerp(w, |pos_i - pos_{i-1}| / paddle_speed, original key step of i)
 * so the ball leaves the racket at racket speed and settles to the game-pace step over 15 samples.
 * ring_index_of_s0 = RTT ring index of path->s[0] (ball_predict: 1). [RTT] */
void ball_ease_launch_keys(BallPath* path, float paddle_speed, int ring_index_of_s0);

/* predict the full flight after a hit, then classify it and mark the hittable window
 * (ProcessHit 0x8004ad98 + ClassifyOutcomeCore 0x8004c848). Stops after 3 consecutive bounces
 * on one half, or 7 "tail" samples (on the floor, or after a 2nd same-half bounce), or 120. [RTT] */
void ball_predict(BallPath* path, const BallParams* p, const TableGeom* g,
                  Vec3 pos, Vec3 vel, Vec3 spin, int shot_type, Side striker, int is_serve,
                  float game_pace, float paddle_speed);

/* classify an already-built path (its samples, bounce / net flags): hittable window + outcome
 * (ClassifyOutcomeCore + ExtrapolateUnknownOutcome). ball_predict calls it. A path rewritten by the
 * aim assist is NOT classified again: it keeps the original outcome word and time (live, 87/87
 * redone predictions of dumps/ball_capture.txt), as ball_fudge_to_contact does [RTT] */
void ball_classify(BallPath* path, const BallParams* p, const TableGeom* g);

/* the live ring, every frame after playback (FUN_8004ac10, from ApplySampleToLivePhysicsInstance)
 * [RTT]: pongLerpQueue__PopExpiredSamples drops the head while count > 1 and the next sample's time
 * has passed; then, unless the flight ended (terminated) or the path is an aim-assist rewrite (it
 * ends in its hold key), IntegrateStep appends as many samples as were dropped. The outcome word is
 * not touched: only ProcessHit classifies. Indices (cursor, landing) follow the shift. */
void ball_advance_ring(BallPath* path, const BallParams* p, const TableGeom* g);

/* interpolated position at playback time t (FindNeighborIndices + lerp) [RTT] */
Vec3 ball_pos_at(const BallPath* path, float t);

/* advance playback by dt and post msgBall events (ApplySampleToLivePhysicsInstance 0x8004a55c):
 *   11 AtApex          vy crosses from > 0 to <= 0
 *   1/2 BounceTableA/B, 7 CollideNet   contact samples the clock passed
 *   13 BeyondTableEnd  |z| crosses half_length + r
 *   12 BelowTable      y crosses below top_y - r (anywhere, not only beside the table)
 *   10 + 44            y crosses below r (floor)
 *   14 PastPlayer      |z| crosses |player z| + 1 m on the ball's half (event side = that half)
 * player_abs_z[side] = |z| of the player standing on that half. [RTT] */
void ball_play(BallPath* path, const BallParams* p, const TableGeom* g,
               const float player_abs_z[2], float dt, EventBus* bus);

/* sample nearest to a point inside the first contiguous hittable run, from the playback cursor
 * on (FindNearestHittableSample 0x80123150) [RTT] */
int ball_nearest_hittable(const BallPath* path, Vec3 point);

/* aim assist: bend the predicted path so sample `idx` lands on the racket's contact point
 * (gdBallFudge__AutoAimTick 0x800d2684 -> ApplyPositionFudge 0x8004bcf4 + ApplyTimestampFudge
 * 0x8004c0a4; live-validated 2026-10-10 on 19/20 captured shots, 1e-6 m):
 *   run = samples after the last non-hittable sample before idx (the bounce) up to idx; the walk
 *   back stops at the live queue head (cursor - 1) if the bounce was already played
 *   pos_i += w_i * (target - pos_idx),  w_i = (t_i - t_start)/(t_idx - t_start), w_idx = 1
 *   vel_i  = (pos_i - pos_{i-1}) / (t_i - t_{i-1})       (playback keys, bent positions)
 *   keys of the run re-lerped so t_idx = contact_t; with snap, contact_t - now is rounded to
 *   whole 1/60 frames first (RTT snaps relative to the game clock `now`)
 *   path cut after idx, then a copy of idx 1 s later with all flags cleared (end marker)
 * No distance clamp in this path; the target comes from the swing chosen by the receiver
 * (ResolveSwingAimTarget -> BuildAimTarget: contact offset in player space). Returns 1 if applied. */
int ball_fudge_to_contact(BallPath* path, int idx, Vec3 target, float contact_t, float now, int snap);

/* launch velocity that lands the ball at `target` (y ignored) after flight time `t_flight`
 * with the given spin. RTT samples two precomputed arcs (SolveHitAim 0x80048468, arc table
 * layout in map/ball-physics.md); this shooting-method solver is a stand-in. [CHOICE] */
Vec3 ball_solve_launch(const BallParams* p, const TableGeom* g, Vec3 from, Vec3 target,
                       Vec3 spin, float t_flight, int shot_type);

/* inputs of RTT's shot solver, all from the hitter's hit state at contact [RTT field names,
 * gdHitState__AddWidgets] */
typedef struct {
    Vec3  ball_pos;          /* BallPos +0x50 */
    Vec3  target;            /* WorldTgtPos +0x1c; y is replaced by top + r */
    Vec3  spin;              /* from the swing's SpinAxis (FUN_8011bd78) */
    float speed;             /* PostHitSpeed +0x70 (FUN_800469c0) */
    float max_elev;          /* steepest arc, hit +0x124 (rad) */
    int   swing_type;        /* SwingType +0x8c: 6 / 11 skip the arc tables */
    int   kick_pitch;        /* KickPitchOffset +0x2c: |k| = arc row, sign = low (>= 0) or high table */
    float far_extra;         /* FUN_8011a69c: gdGameData +0x84 + +0x88 + table half length */
} HitAimInput;

/* ball::SolveHitAim 0x80048468: launch velocity of a rally shot. A direct solve (speed fixed,
 * Newton on azimuth/elevation) gives elevation el0; a low table (el0 -> max_elev) and a high table
 * (el0 -> ~0, speed capped at PostHitSpeed) each hold up to 32 rows solved with the elevation
 * fixed and the speed free; KickPitchOffset picks the row, and the low table walks down to the
 * first row under 62..32 deg (steeper allowed near the table). Writes the chosen elevation (hit
 * +0x128) to out_elev. [RTT] */
Vec3 ball_solve_hit_aim(const BallParams* p, const TableGeom* g, const HitAimInput* in, float* out_elev);

/* ---------------------------------------------------------------- serve launch (ServeHitVel) */
/* ball__ComputeServeHitVel 0x80045d1c -> ball__SolveServeLaunch 0x80047b54 [RTT, decompiled
 * 2026-10-11]. A serve is not aimed by SolveHitAim: the solver builds tables of up to 32 serves at
 * fixed elevations, each solved with ComputePostHitVel's speed mode (start 5 m/s, Newton on
 * azimuth + speed) flying through the own-half bounce (SimulateShotLanding bounce mode), scores
 * them, and lets the serve meter pick one:
 *   table A  [-60, 60] deg toward WorldTgtPos ("good"; retried up to twice toward the corrected
 *            target while its best row is not valid)
 *   table N  [-20, 30] deg toward a point near the net: x clamped to +-W/4, depth from the net
 *            lerp(clamp(m/32, 0, 1), 0.025, 0.5), 0.475 for 27.5 <= m < 37 ("into the net")
 *   table L  A's elevations toward A's best target pushed 1.5 m further ("long")
 * m = |ServeMeterVal| (< 10 counts as 0). Normal position:
 *   70 <= m <= 100 -> best A;  37 <= m < 70 -> A row (m - 37)/33 of the good ones;
 *   otherwise      -> best N, and TableTgtPos / WorldTgtPos become N's target, SwingType 2.
 * Far from the table (|z| >= 2.25, |x| >= 1.75 * W/2, or serve time < 0):
 *   70..100 -> best A;  25 < m < 70 -> A row (m - 25)/45;  otherwise best L.
 * If no valid A row exists or A had to move its target more than 1 m, it takes the first row (A,
 * then L) that is a clean net fault: hits the net at 0.8 <= y < 0.89 with max height in
 * [0.99, 1.25). PostHitSpeed = clamp(|vel|, 0, 50) when A found a valid serve. */
typedef struct {
    Vec3  from;              /* BallPos (hit +0x50) */
    Vec3  target;            /* WorldTgtPos (hit +0x1c) */
    Vec3  spin;              /* ServeHitAngVel (launch spin of the SpinAxis, FUN_8011bd78) */
    float meter;             /* ServeMeterVal (hit +0x78) */
    float serve_t;           /* hit +0x118 (serve spin timer / 100) */
    int   swing_type;        /* drag / Magnus slot */
    float max_elev;          /* hit +0x124, 65 deg */
    float net_height;        /* DAT_806db760 above the table top (0.147 live) */
    float net_margin;        /* table cfg net +0x80 = 0.05 live (+0x84 = 0.2 when DAT_8047a9c8 and
                                FUN_800430b0): net-hit test height above net_height */
} ServeSolveInput;

typedef struct {
    Vec3  vel;               /* ServeHitVel (hit +0x90) */
    float elev;              /* obj +0x150 */
    int   good;              /* A had a valid row: obj +4 = 1, PostHitSpeed written */
    float post_hit_speed;
    int   net_target;        /* TableTgtPos replaced by the near-net point, SwingType 2 */
    Vec3  net_target_pos;
    int   pick;              /* 0 A, 1 N, 2 L (diagnostic) */
    int   row;               /* row in the chosen list (diagnostic) */
} ServeSolveResult;

/* the serve the solver never sees (pongPlayer__ReachAnticipation): when the swing strength
 * player +0x1934 is 0 or player +0x1920 != 5, ServeHitVel = ServeHitAngVel = (DAT_806db314 0,
 * 2, -0.75 * (camera flip ? 1 : -1)), Force 0, HitForce 0.2 * rand (live: one human serve,
 * (0, 2, -0.75)) */
Vec3 ball_serve_whiff_vel(int camera_flip);

/* p: rally params; the serve flies with gravity scale 1 and table restitution 1.0 (serve).
 * Live 2026-10-11 (capture 8, tests/compare_serve_capture.cpp): 11/11 solved serves exact to
 * 1e-5 m/s with net_margin 0.05 (0/11 with 0.2): meters 12.5 / 16.7 (near-net table), 63.3 (A
 * row 1), 76.7..100 (A row 0), human and CPU, both ends. */
void ball_solve_serve(const BallParams* p, const TableGeom* g, const ServeSolveInput* in,
                      ServeSolveResult* out);

#endif /* RTT_BALL_H */
