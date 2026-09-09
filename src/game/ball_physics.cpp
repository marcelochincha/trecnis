#include <game/ball_physics.hpp>

#include <algorithm>
#include <cmath>

namespace ballphys {

// =============================================================================
// §5  The trajectory store
// =============================================================================

void PredictQueue::clear() { head_ = tail_ = count_ = 0; }

bool PredictQueue::enqueue(const Sample& s) {
    if (count_ >= kCapacity) return false;     // full: dropped, as in Enqueue
    buf_[tail_] = s;
    tail_ = (tail_ + 1) % kCapacity;
    ++count_;
    return true;
}

const Sample& PredictQueue::at(int i) const { return buf_[(head_ + i) % kCapacity]; }
Sample&       PredictQueue::at(int i)       { return buf_[(head_ + i) % kCapacity]; }

float PredictQueue::min_t() const { return count_ ? at(0).t : 0.0f; }
float PredictQueue::max_t() const { return count_ ? at(count_ - 1).t : 0.0f; }

bool PredictQueue::find_neighbor_indices(float t, int& lo, int& hi, float& frac) const {
    if (count_ < 2) return false;                  // empty / single sample
    if (t < min_t() || t > max_t()) return false;  // before MinT / after MaxT

    // The samples are sorted by t, so bisect.
    int a = 0, b = count_ - 1;
    while (b - a > 1) {
        int m = (a + b) / 2;
        if (at(m).t <= t) a = m; else b = m;
    }
    lo = a; hi = b;
    float span = at(b).t - at(a).t;
    frac = (span > 1e-9f) ? (t - at(a).t) / span : 0.0f;
    return true;
}

bool PredictQueue::sample_at(float t, BallState& out) const {
    int lo, hi; float f;
    if (!find_neighbor_indices(t, lo, hi, f)) return false;
    const Sample& a = at(lo);
    const Sample& b = at(hi);
    out.pos  = lerp(a.pos,  b.pos,  f);
    out.vel  = lerp(a.vel,  b.vel,  f);
    out.spin = lerp(a.spin, b.spin, f);
    return true;
}

void PredictQueue::pop_expired(float now) {
    // PopExpiredSamples drops everything the clock has passed. We keep the last
    // sample at or before `now`: it is the lower bracket sample_at(now) needs.
    while (count_ >= 2 && at(1).t <= now) {
        head_ = (head_ + 1) % kCapacity;
        --count_;
    }
}

// =============================================================================
// §1  The physics step
// =============================================================================

vec3 compute_acceleration(const BallState& b) {
    vec3 a(0.0f, 0.0f, 0.0f);

    const float v2 = dot(b.vel, b.vel);
    const float s2 = dot(b.spin, b.spin);

    // Magnus: spin x vel is perpendicular to both, so topspin dips the ball,
    // backspin floats it and sidespin curves it.
    if (s2 > 1e-8f && v2 > 1e-6f) {
        // The spin gate: when the spin axis sits too shallow relative to the
        // velocity the term is zeroed, which keeps grazing angles from
        // producing nonsensical curve.
        const vec3  saxis = b.spin / std::sqrt(s2);
        const vec3  vaxis = b.vel  / std::sqrt(v2);
        const float sin_angle = magnitude(cross(saxis, vaxis));
        static const float kGateSin = std::sin(kSpinGateDeg * 0.0174533f);
        if (sin_angle > kGateSin)
            a = a + cross(b.spin, b.vel) * kMagnusK;
    }

    // Drag is linear in velocity here, not the classic v^2 model.
    if (v2 > 1e-6f) a = a - b.vel * kDragK;

    a.y -= kGravity;
    return a;
}

// Is (x, z) over the playing surface?
static bool over_table(const vec3& p, const Table& tb) {
    return std::fabs(p.x) <= tb.half_width && std::fabs(p.z) <= tb.half_len;
}

void integrate_step(BallState& b, float dt, const Table& tb, StepEvents& ev) {
    const vec3 a = compute_acceleration(b);

    // Semi-implicit Euler: velocity first, then position.
    b.vel = b.vel + a * dt;
    const vec3 prev = b.pos;
    b.pos = b.pos + b.vel * dt;

    if (hits_net(prev, b.pos, tb)) {
        ev.net_hit = true;
        return;                        // dead ball, nothing else to resolve
    }

    // A table-plane crossing is the ball descending past the surface. Inside the
    // footprint that is a bounce; outside it, the ball has landed off the table.
    const float surf = tb.height + kBallRadius;
    if (prev.y > surf && b.pos.y <= surf && b.vel.y < 0.0f) {
        ev.crossed = true;
        if (over_table(b.pos, tb)) {
            b.pos.y = surf;            // sit the ball on the surface
            resolve_bounce_impulse(b, tb);
            ev.bounced = true;
        } else {
            ev.landed_out = true;
        }
    }
}

// =============================================================================
// §2  Bounce off the table
// =============================================================================

void resolve_bounce_impulse(BallState& b, const Table& tb) {
    (void)tb;
    const vec3 n(0.0f, 1.0f, 0.0f);              // surface normal
    const vec3 r_contact = -n * kBallRadius;      // centre -> contact point

    // Contact-point velocity: linear plus spin about the contact radius. This
    // is the term that makes topspin kick forward and backspin check.
    const vec3  v_contact = b.vel + cross(b.spin, r_contact);
    const float vn = dot(v_contact, n);
    if (vn >= 0.0f) return;                       // not approaching

    const vec3  vt     = v_contact - n * vn;      // tangential part
    const float vt_len = magnitude(vt);

    // Normal impulse (per unit mass): bounce height.
    const float jn = -(1.0f + kRestitution) * vn;
    b.vel = b.vel + n * jn;

    // Tangential impulse: stick vs slide.
    vec3 jt(0.0f, 0.0f, 0.0f);
    if (vt_len > 1e-6f) {
        const vec3 tdir = vt / vt_len;
        if (vt_len < kStickThreshold * jn) {
            jt = -tdir * vt_len;                        // sticking: kill the slip
        } else {
            jt = -tdir * (kFrictionKinetic * jn);       // kinetic friction
        }
    }
    b.vel = b.vel + jt;

    // The tangential impulse also torques the ball. NOTE: §2 writes this as
    // (tangential_impulse x r_contact); that ordering is the negative of the
    // angular impulse r x J and would amplify spin on every bounce instead of
    // bleeding it into forward speed. The document states its pseudocode is a
    // reconstruction, so we use the physically correct order here.
    b.spin = b.spin + cross(r_contact, jt) * kSpinCoupling;
    b.spin = b.spin * (1.0f - kSpinDecay);
}

// =============================================================================
// §3  Net collision
// =============================================================================

bool hits_net(const vec3& from, const vec3& to, const Table& tb) {
    // §3 models the net as a capsule and tests the swept ball against it. A
    // capsule wide enough to span the table is also thick in Z (its radius),
    // which would make the net a 15 cm deep cylinder, so we keep the sweep test
    // but against the net's actual slab: the z = 0 plane, bounded in X and Y,
    // inflated by the ball radius.
    if ((from.z > 0.0f) == (to.z > 0.0f)) return false;   // no crossing of z = 0

    const float dz = to.z - from.z;
    if (std::fabs(dz) < 1e-9f) return false;
    const float f = (0.0f - from.z) / dz;                 // where it crosses
    const vec3  at = from + (to - from) * f;

    const float top = tb.height + tb.net_height + kBallRadius;
    const float bottom = tb.height - kBallRadius;
    return at.y <= top && at.y >= bottom &&
           std::fabs(at.x) <= tb.half_width + kBallRadius;
}

// =============================================================================
// §4  The prediction loop
// =============================================================================

void process_hit(const BallState& start, const Table& tb, Prediction& out,
                 float striker_side_sign) {
    out.queue.clear();
    out.outcome      = 0;
    out.outcome_time = 0.0f;
    out.bounces      = 0;
    out.crossings    = 0;

    BallState b = start;

    // Validate the post-hit spin magnitude (cap ~1000 rad/s, |spin|^2 <= 1e6).
    const float s2 = dot(b.spin, b.spin);
    if (s2 > kSpinCap * kSpinCap) b.spin = b.spin * (kSpinCap / std::sqrt(s2));

    float t = 0.0f;
    Sample seed;
    seed.t = 0.0f; seed.pos = b.pos; seed.vel = b.vel; seed.spin = b.spin;
    out.queue.enqueue(seed);

    StepEvents pending;      // events since the last emitted sample
    int sub = 0;
    bool stop = false;

    while (!stop && out.queue.count() < PredictQueue::kCapacity) {
        StepEvents ev;
        integrate_step(b, kSubDt, tb, ev);
        t += kSubDt;

        if (ev.bounced)    { ++out.bounces;   pending.bounced = true; }
        if (ev.crossed)    { ++out.crossings; pending.crossed = true; }
        if (ev.landed_out) { pending.landed_out = true; }
        if (ev.net_hit)    { pending.net_hit = true; stop = true; }

        if (out.bounces >= 2 || out.crossings >= 6) stop = true;
        if (b.pos.y < 0.0f)                         stop = true;  // reached the floor

        // Emit a sample every kSampleEvery sub-steps (and always on the last).
        if (++sub >= kSampleEvery || stop) {
            sub = 0;
            Sample s;
            s.t = t; s.pos = b.pos; s.vel = b.vel; s.spin = b.spin;
            s.bounce     = pending.bounced;
            s.blocked    = pending.net_hit;
            s.landed_out = pending.landed_out;
            out.queue.enqueue(s);
            pending = StepEvents{};
        }
    }

    // The receiver stands on the other half, so that is where the hittable
    // window lives.
    mark_hittable(out, tb, -striker_side_sign);
    classify_outcome(out, tb, striker_side_sign);
}

// =============================================================================
// §6  Reachability and the hittable window
// =============================================================================

bool is_sample_reachable(const Sample& s, const Table& tb) {
    // Table half-extents scaled by 0.5, plus the reach margin and tolerance,
    // offset by the table height. §6 tests X *or* Z, not both.
    const float hx = tb.half_width * kReachExtentScale + kReachMargin;
    const float hz = tb.half_len   * kReachExtentScale + kReachMargin;
    if (s.pos.y < tb.height - kReachTolerance) return false;
    return std::fabs(s.pos.x) <= hx + kReachTolerance
        || std::fabs(s.pos.z) <= hz + kReachTolerance;
}

void mark_hittable(Prediction& p, const Table& tb, float receiver_side_sign) {
    // MarkHittableFlag sets the flag along a range based on height: the ball is
    // playable while it is on this player's side, above the surface and below
    // roughly head height, and not already dead.
    bool dead = false;
    for (int i = 0; i < p.queue.count(); ++i) {
        Sample& s = p.queue.at(i);
        if (s.blocked) dead = true;
        const bool on_side = (s.pos.z * receiver_side_sign) > 0.0f;
        const bool in_band = s.pos.y > tb.height - 0.30f &&
                             s.pos.y < tb.height + 1.20f;
        s.hittable = !dead && on_side && in_band && is_sample_reachable(s, tb);
    }
}

int find_first_hittable(const Prediction& p) {
    for (int i = 0; i < p.queue.count(); ++i)
        if (p.queue.at(i).hittable) return i;
    return -1;
}

int find_first_non_hittable(const Prediction& p, int from) {
    for (int i = std::max(0, from); i < p.queue.count(); ++i)
        if (!p.queue.at(i).hittable) return i;
    return -1;
}

int find_nearest_hittable(const Prediction& p, const vec3& ref) {
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < p.queue.count(); ++i) {
        const Sample& s = p.queue.at(i);
        if (!s.hittable) continue;
        const vec3 d = s.pos - ref;
        const float d2 = dot(d, d);
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    return best;
}

// =============================================================================
// §7  Outcome classification
// =============================================================================

void classify_outcome(Prediction& p, const Table& tb, float striker_side_sign) {
    (void)tb;
    p.outcome = 0;
    p.outcome_time = p.queue.max_t();

    // Walk the samples and take the FIRST deciding event. A bounce only decides
    // the point when it lands on the receiver's half: a ball that bounces on the
    // striker's own half (a serve, or a mishit) is still live, and whatever it
    // does next — clear the net, hit it, land off the table — is what counts.
    for (int i = 0; i < p.queue.count(); ++i) {
        const Sample& s = p.queue.at(i);
        if (s.blocked) {
            p.outcome = kOutcomeKnown | kOutcomeNet;
            p.outcome_time = s.t;
            return;
        }
        if (s.landed_out) {
            p.outcome = kOutcomeKnown | kOutcomeOut;
            p.outcome_time = s.t;
            return;
        }
        if (s.bounce && (s.pos.z * striker_side_sign) < 0.0f) {
            p.outcome = kOutcomeKnown | kOutcomeIn;
            p.outcome_time = s.t;
            return;
        }
    }

    // Nothing decided inside the queue: extrapolate an outcome, as
    // ClassifyOutcomeCore does before it gives up and warns.
    p.outcome = kOutcomeKnown | kOutcomeOut | kOutcomeExtrapolated;
}

const char* outcome_name(uint32_t outcome) {
    if (!(outcome & kOutcomeKnown)) return "unknown";
    if (outcome & kOutcomeNet) return "NET";
    if (outcome & kOutcomeIn)  return "IN";
    if (outcome & kOutcomeOut) return (outcome & kOutcomeExtrapolated) ? "OUT (extrapolated)" : "OUT";
    return "unknown";
}

// =============================================================================
// §8  Aim assist ("fudge")
// =============================================================================

FudgeParams fudge_for(Stroke s) {
    switch (s) {
        case Stroke::Sot:      return { 0.45f, 0.60f };
        case Stroke::Scoop:    return { 0.40f, 0.45f };
        case Stroke::Slam:     return { 0.30f, 0.40f };
        case Stroke::LungeFar: return { 0.30f, 0.30f };
        case Stroke::Ott:      return { 0.25f, 0.60f };
        case Stroke::Normal:
        default:               return { 0.20f, 0.10f };
    }
}

void apply_autoaim(Prediction& p, const vec3& target, const FudgeParams& fp, float dt) {
    const int n = p.queue.count();
    if (n < 2) return;

    const int idx = find_nearest_hittable(p, target);
    if (idx <= 0) return;

    vec3 delta = target - p.queue.at(idx).pos;
    const float dist = magnitude(delta);
    if (dist < 1e-6f) return;

    // MinFudgeDist is a small always-on assist; MaxFudgeDist caps the reach and
    // DistPerSecond limits how fast the correction is applied, so it never
    // reads as a snap.
    float allowed = std::max(kMinFudgeDist, fp.dist_per_second * dt);
    allowed = std::min(allowed, fp.max_dist);
    if (dist > allowed) delta = delta * (allowed / dist);

    // --- ApplyPositionFudge: distribute the offset along the path, ramping in
    // toward the aimed-at sample and holding after it.
    const float old_len = magnitude(p.queue.at(idx).pos - p.queue.at(0).pos);
    for (int i = 1; i < n; ++i) {
        const float w = (i < idx) ? float(i) / float(idx) : 1.0f;
        p.queue.at(i).pos = p.queue.at(i).pos + delta * w;
    }

    // --- Re-derive the velocities so the bent path stays physically continuous.
    for (int i = 0; i < n - 1; ++i) {
        const float span = p.queue.at(i + 1).t - p.queue.at(i).t;
        if (span > 1e-9f)
            p.queue.at(i).vel = (p.queue.at(i + 1).pos - p.queue.at(i).pos) / span;
    }
    if (n >= 2) p.queue.at(n - 1).vel = p.queue.at(n - 2).vel;

    // --- ApplyTimestampFudge: a bent path is a different length, so the ball
    // would otherwise arrive early or late. Scale the timeline to match.
    const float new_len = magnitude(p.queue.at(idx).pos - p.queue.at(0).pos);
    if (old_len > 1e-4f) {
        const float scale = std::clamp(new_len / old_len, 0.9f, 1.1f);
        const float t0 = p.queue.at(0).t;
        for (int i = 1; i < n; ++i)
            p.queue.at(i).t = t0 + (p.queue.at(i).t - t0) * scale;
    }
}

}  // namespace ballphys
