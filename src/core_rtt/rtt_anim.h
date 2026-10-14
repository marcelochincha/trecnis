/*
 * rtt_anim.h -- skeleton, clips, pose blending, FK, skinning palette, look-at.
 * Doc: findings/reconstruction/animation-runtime.md, "Portable runtime block for TRECNIS".
 *
 * Clips are assumed already decoded offline (tools/decode_clips.py) into dense per-frame
 * rotation/translation arrays at 30 fps; the runtime never sees RTT's quantized channels.
 */
#ifndef RTT_ANIM_H
#define RTT_ANIM_H

#include "rtt_core.h"

typedef struct {
    int   count;                              /* 63 for players [RTT] */
    int   parent[RTT_MAX_BONES];              /* parent before child, root = -1 [RTT] */
    char  name[RTT_MAX_BONES][32];            /* generic names: head, jaw, wrist_l ... [RTT] */
    Quat  rest_rot[RTT_MAX_BONES];            /* identity for players: rest is a translation chain [RTT] */
    Vec3  rest_trans[RTT_MAX_BONES];
    Mat43 inv_bind[RTT_MAX_BONES];            /* inverse of rest world matrices [STD] */
} Skeleton;

typedef struct {
    char   name[RTT_NAME_LEN];
    int    frames;                            /* sampled at 30 fps [RTT] */
    float  duration;                          /* (frames-1)/30 */
    /* dense storage owned by the loader: frames * bones entries each; NULL = channel absent
     * (absent translation -> rest translation; absent rotation -> rest rotation) */
    const Quat* rot;
    const Vec3* trans;
    uint8_t has_rot[RTT_MAX_BONES];
    uint8_t has_trans[RTT_MAX_BONES];
    /* mover (root motion) track, applied to the entity, never to a bone [RTT] */
    const Vec3* mover_trans;
    const Quat* mover_rot;
    /* swing timing marks, in frames [DATA swingsolver_*.xml] */
    float  anticipation_frame, contact_frame, blend_out_frame;
} Clip;

typedef struct {
    Quat rot[RTT_MAX_BONES];
    Vec3 trans[RTT_MAX_BONES];
    Vec3 mover_trans;                         /* delta for this frame */
    Quat mover_rot;
} Pose;

/* per-bone blend weight in [0,1]; NULL mask = all bones 1.0 (LAB_80214c04 bone mask) [RTT] */
typedef struct { float w[RTT_MAX_BONES]; } BoneMask;

void pose_rest(const Skeleton* sk, Pose* out);
/* sample at time t (seconds), linear between frames, nlerp for rotations [STD] */
void clip_sample(const Skeleton* sk, const Clip* c, float t, Pose* out);

/* dst = blend(dst, src, w): w<=0 keep, w>=1 copy, else nlerp (hemisphere fix on dst) / lerp
 * (crAnimDofQuaternion__BlendFrom 0x80212a5c, crAnimDofVector3__BlendFrom 0x80213598) [RTT] */
void pose_blend(const Skeleton* sk, Pose* dst, const Pose* src, float w, const BoneMask* mask);

/* N weighted clips sampled at a shared normalized phase:
 * pose = first non-zero; then pose = blend(pose, clip_i, w_i / (w_0+..+w_i))
 * (pcrSwingBlend__GetSequentialWeight 0x800d49fc, SampleBlendedFrame 0x800d4eec) [RTT] */
void blend_clips(const Skeleton* sk, const Clip* const* clips, const float* weights, int n,
                 float phase, Pose* out);

/* world[0] = local[0] * entity; world[i] = local[i] * world[parent]  (0x801b5674) [RTT] */
void skeleton_fk(const Skeleton* sk, const Pose* p, const Mat43* entity, Mat43* world);

/* skin palette: invBind * world  -> v' = sum w_k * (v * palette[b_k])  (LBS) [STD] */
void skin_palette(const Skeleton* sk, const Mat43* world, Mat43* palette);

/* ---------------------------------------------------------------- look-at (pose space, NOT IK)
 * OPTIONAL. Exists in RTT but is OFF in a match: live check 2026-10-09 (map/animation-runtime.md,
 * "Session update 2026-10-09" s.4) never hit either ApplyToPose in a full match + replay, so in RTT
 * the head/eyes only follow the body clips. Not part of the required per-frame tick; call
 * lookat_apply only if TRECNIS wants ball tracking as its own improvement [CHOICE].
 * RAGE IK is dead too (IK body never built); there is no IK here and TRECNIS needs none. */
typedef struct {
    float scale_u, scale_v, off_u, off_v;     /* per stance (standing / ready) [RTT, values OPEN] */
} LookAtStance;

typedef struct {
    const Clip* head_standing;                /* <nat>_head.anim, 1 frame per pose [DATA] */
    const Clip* head_ready;                   /* <nat>_head_ready.anim */
    const Clip* eyes;                         /* <nat>_eyes.anim */
    LookAtStance standing, ready;
    float smooth_rate;                        /* [OPEN] value */
    float smooth_exp;                         /* exponent DAT_806de998 [OPEN] value */
    float weight;                             /* look-at weight * fade */
    float reference_stoop_pitch;              /* characters.xml ReferenceStoopPitch [DATA] */
    Vec3  neck_translate_adj;                 /* (0, NeckTranslateYAdj, NeckTranslateZAdj) [DATA] */
    int   head_bone, eye_bone_l, eye_bone_r;
    int   stoop_bones[3];                     /* 3 neck/spine bones that share the correction [RTT] */
    BoneMask head_mask, eye_mask;
    float u, v;                               /* smoothed state */
    int   ready_stance;
} LookAt;

/* target in world space -> (u,v) in [-1,1]^2, +-45 deg = +-1 (ComputeLookUV 0x800c5f0c) [RTT] */
void lookat_target_uv(const LookAt* la, const Mat43* head_ref_world, Vec3 target, float* u, float* v);
/* smooth, blend 3 of the 5/9 directional poses, apply with mask, stoop correction, eyes.
 * Where RTT would run it: after the body pose and FK, then re-run FK (pcrCharacter_UpdateAnimTick
 * order) [RTT] -- but gated off in a match, see above. Optional. */
void lookat_apply(LookAt* la, const Skeleton* sk, Pose* pose, const Mat43* entity,
                  Mat43* world, Vec3 target, float dt);

#endif /* RTT_ANIM_H */
