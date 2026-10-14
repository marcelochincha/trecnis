/*
 * rtt_core.h -- shared types for the RTT -> TRECNIS reference reconstruction.
 *
 * Style (applies to every file in this folder):
 *   - C-style C++: POD structs, free functions, fixed-size arrays. No STL, no templates,
 *     no exceptions, no RTTI, no heap allocation inside the per-frame path.
 *   - Units: meters, seconds, radians. Y up, Z along the table (net at Z = 0),
 *     player A on +Z, player B on -Z (findings/world-context.md).
 *   - Evidence tags in comments:
 *       [RTT]    behavior/constant read from the binary (decompile, live capture)
 *       [DATA]   value comes from a shipped data file (XML/.sps/.td)
 *       [STD]    standard math RTT also uses; any equivalent is fine
 *       [CHOICE] TRECNIS design choice, not RTT
 *       [OPEN]   not fully recovered; behavior here is the documented best reading
 */
#ifndef RTT_CORE_H
#define RTT_CORE_H

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ limits */
enum {
    RTT_MAX_BONES        = 64,    /* player skeleton has 63 [RTT] */
    RTT_MAX_PLAYERS      = 2,
    RTT_BALL_PATH_SLOTS  = 120,   /* pongLerpQueue ring size [RTT] */
    RTT_MAX_EVENTS       = 256,
    RTT_MAX_SUBSCRIBERS  = 64,
    RTT_NAME_LEN         = 64
};

typedef enum { SIDE_A = 0, SIDE_B = 1, SIDE_NONE = -1 } Side;   /* A = +Z, B = -Z */
static inline Side rtt_other(Side s) { return s == SIDE_A ? SIDE_B : SIDE_A; }

/* ------------------------------------------------------------------ math */
typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y, z, w; } Quat;
/* Row-vector affine transform, same convention as RTT's crSkeleton:
 * p' = p * M ; rows 0..2 = basis, row 3 = translation. [RTT] */
typedef struct { Vec3 r[4]; } Mat43;

static inline Vec3  v3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
static inline Vec3  v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3  v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3  v3_scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3  v3_cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(Vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline Vec3  v3_norm(Vec3 a) { float l = v3_len(a); return l > 0.0f ? v3_scale(a, 1.0f / l) : a; }
static inline Vec3  v3_lerp(Vec3 a, Vec3 b, float t) { return v3_add(a, v3_scale(v3_sub(b, a), t)); }
static inline float rtt_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float rtt_lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline Quat q_identity(void) { Quat q = { 0, 0, 0, 1 }; return q; }
static inline float q_dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline Quat q_normalize(Quat q) {
    float l = sqrtf(q_dot(q, q));
    if (l > 0.0f) { q.x /= l; q.y /= l; q.z /= l; q.w /= l; }
    return q;
}
/* w rebuilt from xyz, clamped, always >= 0 (clip decode) [RTT] */
static inline Quat q_from_xyz(float x, float y, float z) {
    float w2 = 1.0f - x * x - y * y - z * z;
    Quat q = { x, y, z, sqrtf(w2 > 0.0f ? w2 : 0.0f) };
    return q;
}
/* rotation matrix rows for a unit quaternion (row-vector convention) [STD] */
static inline Mat43 m43_from_qt(Quat q, Vec3 t) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat43 m;
    m.r[0] = v3(1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy));
    m.r[1] = v3(2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx));
    m.r[2] = v3(2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy));
    m.r[3] = t;
    return m;
}
/* a * b  (apply a first, then b) -- RTT's world[i] = local[i] * world[parent] [RTT] */
static inline Mat43 m43_mul(const Mat43* a, const Mat43* b) {
    Mat43 o;
    for (int i = 0; i < 4; i++) {
        Vec3 r = a->r[i];
        Vec3 v = v3_add(v3_add(v3_scale(b->r[0], r.x), v3_scale(b->r[1], r.y)), v3_scale(b->r[2], r.z));
        o.r[i] = (i == 3) ? v3_add(v, b->r[3]) : v;
    }
    return o;
}
static inline Vec3 m43_point(const Mat43* m, Vec3 p) {
    return v3_add(v3_add(v3_add(v3_scale(m->r[0], p.x), v3_scale(m->r[1], p.y)), v3_scale(m->r[2], p.z)), m->r[3]);
}

/* ------------------------------------------------------------------ rng */
/* RTT uses a global float RNG (gd_rng_nextFloat). Any uniform [0,1) source matches. [STD] */
typedef struct { uint32_t s; } Rng;
static inline float rng_float(Rng* r) {
    r->s = r->s * 1664525u + 1013904223u;
    return (float)(r->s >> 8) * (1.0f / 16777216.0f);
}
static inline int rng_int(Rng* r, int n) { return n > 0 ? (int)(rng_float(r) * (float)n) % n : 0; }

/* ------------------------------------------------------------------ time */
/* One fixed step per rendered frame: dt = 1 / round(framerate) = 1/60 s on NTSC. [RTT]
 * (ballPredict__GetStepDt 0x8004bca0). Run gameplay at this fixed rate to reproduce RTT. */
#define RTT_FRAMERATE_HZ 60
#define RTT_DT (1.0f / (float)RTT_FRAMERATE_HZ)

#endif /* RTT_CORE_H */
