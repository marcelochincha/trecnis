#pragma once
// Ball physics for the table-tennis game.
//
// A direct implementation of the subsystem described in ball-physics-explained.md
// (the reverse-engineered ball code of Rockstar Games Presents Table Tennis, Wii
// build). The "§n" references in the comments point at that document.
//
// The shape of the model, and the reason it is not a per-frame simulation: the
// entire future trajectory is integrated in ONE burst at the moment of the hit
// (process_hit), stored as time-stamped samples, and every consumer afterwards
// — renderer, AI, camera, scoring — just interpolates that store by time. This
// buys deterministic gameplay (the outcome is known the instant the ball is
// struck), a cheap per-frame cost, and a clean place to bend the path toward
// where the player aimed without fighting the integrator (§0).

#include <math/sr_math.hpp>

#include <cstdint>

namespace ballphys {

// ---- §10 constants cheat-sheet ---------------------------------------------
// Exact values read out of main.dol. Do not "tune" these — they are measured.
constexpr float kRestitution      = 0.95f;    // table bounce, normal impulse
constexpr float kFrictionKinetic  = 0.4f;     // table bounce, sliding branch
constexpr float kStickThreshold   = 0.16f;    // bounce branch select
constexpr float kSpinCoupling     = 1.5f;     // bounce angular update
constexpr float kSpinDecay        = 0.002f;   // spin bleed per bounce
constexpr float kSpinCap          = 1000.0f;  // rad/s, validated in process_hit
constexpr float kReachExtentScale = 0.5f;     // table half-extents scale (§6)
constexpr float kReachMargin      = 0.05f;    // paddle reach margin (§6)
constexpr float kReachTolerance   = 0.02f;    // reach tolerance (§6)
constexpr float kMinFudgeDist     = 0.1f;     // always-on aim assist (§8)

// ---- Coefficients the original supplies through functions -------------------
// Gravity comes from FUN_80045dfc and the Magnus constant from FUN_8011b4d0, so
// the document has no flat value for either (§10). These are ours, and they are
// the knobs to turn for feel: kMagnusK is "how much spin bends flight" (§1).
constexpr float kGravity     = 9.81f;
constexpr float kDragK       = 0.30f;    // linear drag, 1/s (NOT the v^2 model)
constexpr float kMagnusK     = 0.0025f;  // scales spin x vel into acceleration
constexpr float kSpinGateDeg = 5.0f;     // below this spin/vel angle, no Magnus
constexpr float kBallRadius  = 0.028f;  // slightly larger than a real 40mm ball (0.02), for visibility

// Integration is sub-stepped for accuracy but only every kSampleEvery-th step is
// stored, so the 120-sample queue spans ~2 s at a 60 Hz sample rate — the same
// sampling the Wii build used.
constexpr float kSubDt       = 1.0f / 240.0f;
constexpr int   kSampleEvery = 4;

// ---- Geometry ---------------------------------------------------------------
// +Y is up, X and Z are the table plane (§ header). X is the lateral axis and Z
// the long one, so the net lies on the z = 0 plane and the two half-courts are
// z > 0 (near) and z < 0 (far).
struct Table {
    float height     = 0.76f;    // playing surface, above the floor
    float half_width = 0.7625f;  // X half-extent (1.525 m wide)
    float half_len   = 1.37f;    // Z half-extent (2.74 m long)
    float net_height = 0.1525f;  // above the surface
};

struct BallState {
    vec3 pos;
    vec3 vel;
    vec3 spin;   // rad/s, right-handed about each axis
};

// One entry of the trajectory store. Mirrors the 0x4c-byte sample of §5: time,
// state, and the three flag bytes at +0x08/09/0a.
struct Sample {
    float t       = 0.0f;
    vec3  pos;
    vec3  vel;
    vec3  spin;
    bool  bounce     = false;  // +0x09: a table bounce resolved on this step
    bool  hittable   = false;  // +0x0a: inside the hittable window
    bool  blocked    = false;  // +0x08: dead ball (hit the net)
    bool  landed_out = false;  // crossed the table plane off the footprint
};

// §7: the outcome is not an integer enum, it is bit-flags plus an event time.
enum OutcomeBits : uint32_t {
    kOutcomeKnown        = 1u << 0,
    kOutcomeIn           = 1u << 1,
    kOutcomeOut          = 1u << 2,
    kOutcomeNet          = 1u << 3,
    kOutcomeExtrapolated = 1u << 4,
};

// §5: the trajectory store. A fixed ring buffer of 120 samples sorted by time,
// appended by the prediction loop, read back by interpolation, and drained from
// the head as the play clock advances.
class PredictQueue {
public:
    static constexpr int kCapacity = 120;

    void clear();
    bool enqueue(const Sample& s);          // false when full (dropped)
    int  count() const { return count_; }
    bool empty() const { return count_ == 0; }

    const Sample& at(int i) const;          // i in [0, count)
    Sample&       at(int i);

    float min_t() const;
    float max_t() const;

    // Find the two samples bracketing t. Returns false on an empty queue, fewer
    // than two samples, or a t outside [min_t, max_t] — the edge cases §5 calls
    // out explicitly.
    bool find_neighbor_indices(float t, int& lo, int& hi, float& frac) const;

    // "Where is the ball at time t?" — the only read the renderer/AI ever does.
    bool sample_at(float t, BallState& out) const;

    // Retire samples the play clock has passed. Always keeps the one sample at
    // or before `now`, otherwise sample_at(now) would lose its lower bracket.
    void pop_expired(float now);

private:
    Sample buf_[kCapacity];
    int head_ = 0, tail_ = 0, count_ = 0;
};

// What process_hit produces: the path, plus the decided outcome.
struct Prediction {
    PredictQueue queue;
    uint32_t     outcome      = 0;      // OutcomeBits
    float        outcome_time = 0.0f;   // when the deciding event happens
    int          bounces      = 0;
    int          crossings    = 0;
};

// §8: how far the aim assist may bend a path, per stroke (from ballfudge.xml).
struct FudgeParams {
    float max_dist;         // MaxFudgeDist: cap on the correction
    float dist_per_second;  // DistPerSecond: rate limit, so it is not a snap
};

enum class Stroke { Sot, Scoop, Slam, LungeFar, Ott, Normal };

FudgeParams fudge_for(Stroke s);

// ---- §1 the physics step ----------------------------------------------------
vec3 compute_acceleration(const BallState& b);

// Events raised by one sub-step, accumulated into the next emitted sample.
struct StepEvents {
    bool bounced   = false;  // bounced on the table (inside the footprint)
    bool crossed   = false;  // crossed the table plane, in or out
    bool landed_out= false;  // crossed the plane outside the footprint
    bool net_hit   = false;
};

void integrate_step(BallState& b, float dt, const Table& tb, StepEvents& ev);

// ---- §2 bounce --------------------------------------------------------------
void resolve_bounce_impulse(BallState& b, const Table& tb);

// ---- §3 net -----------------------------------------------------------------
bool hits_net(const vec3& from, const vec3& to, const Table& tb);

// ---- §4 the prediction loop -------------------------------------------------
// Integrates from `start` until 2 bounces / 6 table-plane crossings / 120
// samples, fills `out`, marks the hittable window and classifies the outcome.
// `striker_side_sign` is the sign of Z of the half the ball was struck from: the
// receiver — and so the hittable window, and the half a good shot must land in —
// is the other one.
void process_hit(const BallState& start, const Table& tb, Prediction& out,
                 float striker_side_sign = 1.0f);

// ---- §6 reachability --------------------------------------------------------
bool is_sample_reachable(const Sample& s, const Table& tb);
void mark_hittable(Prediction& p, const Table& tb, float receiver_side_sign);
int  find_first_hittable(const Prediction& p);
int  find_first_non_hittable(const Prediction& p, int from);
int  find_nearest_hittable(const Prediction& p, const vec3& ref);

// ---- §7 outcome -------------------------------------------------------------
void classify_outcome(Prediction& p, const Table& tb, float striker_side_sign);
const char* outcome_name(uint32_t outcome);

// ---- §8 aim assist ----------------------------------------------------------
// Bends the already-computed path toward `target` without touching the physics:
// offsets the sample positions, re-derives the velocities so the bent path stays
// continuous, then adjusts the arrival timing. `dt` is the frame time, used by
// the DistPerSecond rate limit.
void apply_autoaim(Prediction& p, const vec3& target, const FudgeParams& fp, float dt);

}  // namespace ballphys
