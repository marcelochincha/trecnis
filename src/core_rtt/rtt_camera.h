/*
 * rtt_camera.h -- camera director: shot catalogue, clips of camera instances, event -> destination
 * cuts, full ADSR blend. Docs: map/camera.md (confirmed runtime: gdCamera_ConditionEventDispatch ->
 * gdCamera_EvaluateConditionCut -> per-dest predicate -> SwitchToClipShot; CMtoCSAdapter ADSR blend),
 * reconstruction/camera.md. Data: tune/camera/shots.xml (gdCamData) + clips.xml (gdCameraDef) [DATA].
 *
 * Only 6 shots matter for gameplay (map/camera.md "Only ~6 cameras are actually gameplay"):
 * CAM_SWING x3 (Near/Medium/Far, behind the player) + CAM_SIDE x3 (ThreeQuarter/Side/TopDown).
 * Everything else (815 CAM_ANIMATED, 328 CAM_REPLAY, intros/emotes) is cinematic polish, out of
 * scope for the first playable [RTT scope choice, see reconstruction/camera.md].
 *
 * The camera only listens to the event bus; it never writes back to gameplay [RTT].
 */
#ifndef RTT_CAMERA_H
#define RTT_CAMERA_H

#include "rtt_core.h"
#include "rtt_events.h"

enum { CAM_MAX_SHOTS = 16, CAM_MAX_DESTS = 16, CAM_MAX_CLIPS = 4, CAM_MAX_INSTS = 8 };

/* CamType, 19 values confirmed from binary strings (map/camera.md §2); only the gameplay subset
 * is modeled here -- CAM_ANIMATED* / CAM_REPLAY / CAM_LOOKATLERP are [OPEN], not needed for v1 */
typedef enum { CAMT_SWING, CAMT_SIDE } CamType;

/* the 3 tiers per family, same enum for both CAM_SWING and CAM_SIDE (distinguished by CamType) */
typedef enum { CAM_TIER_NEAR = 0, CAM_TIER_MEDIUM = 1, CAM_TIER_FAR = 2 } CamTier;

/* one named camera shot (shots.xml gdCamData row) [RTT: struct identity / fields] [OPEN: exact
 * numeric offsets -- pongcamera.cmt ships the common SwingCam/SideCam tunables mostly zeroed,
 * overridden per-shot by a TuningFileName .cm diff that was not individually decoded; the values
 * below are placeholder behavior, not recovered RTT numbers] */
typedef struct {
    char    name[RTT_NAME_LEN];
    CamType type;
    CamTier tier;
    float   back_dist;    /* SwingCam: distance behind the player along -facing [OPEN] */
    float   height;       /* eye height above the floor [OPEN] */
    float   side_dist;     /* SideCam: distance off to the broadcast side [OPEN] */
    float   fov;           /* SW_NearFOV / SW_FarBackFOV family, or SideCam FOV [OPEN] */
    int     top_down;      /* SideCam TopDown tier: look straight down [OPEN] */
    int     fixed;         /* SideCam from its .cm: eye / target are the file's world points */
    Vec3    look_from, look_at;   /* SC_LookFrom / SC_LookAt (tune/camera/sidecam/ .cm files) [DATA] */
} CamShot;

/* a conditional cut inside a clip (clips.xml gdCameraDest) [RTT] */
typedef struct {
    EventId condition;      /* matches an Event.id from the bus [RTT: Condition= string, CHOICE: our EventId mapping] */
    int     target_shot;    /* index into CameraDirector.shots */
    float   time_bias;      /* ConditionTimeBias [DATA] */
} CamDest;

/* ADSR blend envelope (clips.xml gdEnvelope) [RTT]. The port uses Attack only: the new shot's weight
 * ramps 0 -> 1 over Attack, then the cut locks to the new shot (camViewCS phase 4). How Decay /
 * Sustain / Release shape the blend is not traced [OPEN]; reading Release as "fade the new shot
 * back out" swung the eye back to the old shot and then jumped (7 m in one frame). */
typedef struct { float attack, decay, sustain, release; } CamEnvelope;

/* a clip = one rally/shot-moment state machine (clips.xml gdCameraDef): a small set of camera
 * instances plus the dests that cut between them [RTT] */
typedef struct {
    char        name[RTT_NAME_LEN];   /* TwoPlayerPZ, OnePlayerNZ, ... (gdShotDataMgr__SelectRallyClip) */
    CamEnvelope env;
    int         insts[CAM_MAX_INSTS]; /* shot indices available in this clip */
    int         inst_count;
    int         default_inst;         /* ShotDef instance -- what rally_start cuts to [RTT] */
    CamDest     dests[CAM_MAX_DESTS];
    int         dest_count;
} CamClip;

typedef struct {
    CamShot  shots[CAM_MAX_SHOTS];
    int      shot_count;
    CamClip  clips[CAM_MAX_CLIPS];
    int      clip_count;

    int      active_clip;
    int      cur_shot, prev_shot;      /* -1 = none */
    float    blend_t;                  /* seconds since the cut started [RTT: adapter +0xc4/+0xc8] */
    CamEnvelope blend_env;              /* envelope copied in at cut time, so later clip changes don't retroactively alter an in-flight blend */

    Vec3     eye, target;               /* output transform */
    float    fov;
    /* where a cut blends FROM: the view on screen when the cut fired (CMtoCSAdapter__SwitchEvaluators
     * takes the installed evaluator -- the current, possibly mid-blend view -- as the outgoing one).
     * Blending from the old shot's pure pose instead made a cut that interrupts another cut jump
     * back ~4 m. Frozen at cut time [CHOICE: RTT keeps stepping the outgoing evaluator]. */
    int      has_from;
    Vec3     from_eye, from_target;
    float    from_fov;
} CameraDirector;

/* fills the 6-shot / clip set with the recovered shots.xml/clips.xml structure (TwoPlayerPZ /
 * TwoPlayerNZ / OnePlayerPZ / OnePlayerNZ, each with CAM_SWING Near/Medium/Far behind the
 * receiving side's own half and CAM_SIDE's 3 broadcast tiers) [RTT structure, CHOICE numeric tuning] */
void camera_init(CameraDirector* c);

/* rally start (gdShotDataMgr__SelectRallyClip): pick "OnePlayer"/"TwoPlayer" + PZ/NZ by player
 * count and receiving side, cut to that clip's default (ShotDef) instance with no blend [RTT] */
void camera_rally_start(CameraDirector* c, int player_count, Side receiving_side);

/* subscribe this to the World's EventBus with ev_subscribe(bus, EV_COUNT_SENTINEL, camera_on_event, camera)
 * (gdCamera_ConditionEventDispatch is itself an event-bus listener) [RTT] */
void camera_on_event(void* user, const Event* e);

/* per-frame director tick (gdCamera__UpdatePerFrame): advance the ADSR blend and evaluate the
 * current/previous shot against the live ball position, write the output transform [RTT] */
void camera_update(CameraDirector* c, Vec3 ball_pos, Vec3 player_a_root, Vec3 player_b_root, float dt);

/* ------------------------------------------------------------------------------------------------
 * Follow cam: one continuous behind-the-player view that pans left / right with the player and the
 * ball, no cuts. A stand-in for RTT's SwingCam until the camera graph (tune/camera/pongcam.ccm, 70
 * nodes, evaluated over the PongcamParams blackboard) is ported [CHOICE placement and gains; DATA:
 * FOV 0.586 from swingcam/medium.cm, approach rate 3 from the .cm *ApproachRate fields].
 * ------------------------------------------------------------------------------------------------ */
typedef struct {
    Side  side;              /* whose back the camera sits behind (SIDE_A = +z) */
    float back, height;      /* eye: this far behind / above the player's rest line [CHOICE] */
    float look_z, look_y;    /* target: on the table's centre line, this far toward the far end [CHOICE] */
    float eye_follow;        /* share of the player's x the eye follows [CHOICE] */
    float look_follow;       /* share of the ball's x the target follows [CHOICE] */
    float rate;              /* exponential approach rate, 1/s (SC_PosApproachRate 3.0) [DATA] */
    float fov;               /* swingcam/medium.cm FOV 0.586 [DATA] */
    int   started;
    Vec3  eye, target;       /* output */
} FollowCam;

void follow_cam_init(FollowCam* c, Side side);
/* per frame: player = the root of the player the camera sits behind */
void follow_cam_update(FollowCam* c, Vec3 ball_pos, Vec3 player_root, float dt);

#endif /* RTT_CAMERA_H */
