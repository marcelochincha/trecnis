/*
 * rtt_match.h -- the unified world and the per-frame order.
 * Docs: runtime-architecture.md (tick order), core-gameplay-pipeline.md (data flow),
 *       TRECNIS-STEP2-PLAN.md (architecture + first demo).
 *
 * Gameplay core (input, AI, swing, ball, rules) is renderer-free and deterministic at a fixed
 * 1/60 s step. Presentation (animation pose, camera, audio) only consumes events and state.
 */
#ifndef RTT_MATCH_H
#define RTT_MATCH_H

#include "rtt_ai.h"
#include "rtt_anim.h"
#include "rtt_audio.h"
#include "rtt_ball.h"
#include "rtt_camera.h"
#include "rtt_rules.h"
#include "rtt_shot.h"
#include "rtt_swing.h"

/* one frame of player intent, from a human device or the AI */
typedef struct {
    int            swing_pressed;   /* commit to the coming ball (or start the serve) [CHOICE device] */
    ShotFrameInput shot;            /* what the swing reads every frame (rtt_shot.h) */
    int            focus_activate;  /* human: the "activate full focus" button (pad input 0x39,
                                       FUN_80096ff0) pressed this frame */
    float          move_stick[2];   /* human: the movement stick DAT_804ce830[p] (x, y), which the
                                       swing-set ActivationCategory reads (FUN_800ed260..) [CHOICE device] */
} PlayerInput;

typedef struct {
    Side          side;
    int           is_ai;
    PlayerAttribs attribs;    /* characters.xml [DATA] */
    const ShotCharacter* chr; /* characters.xml / shottarget [DATA] */
    ShotState     shot;       /* the swing in progress */
    float         pace;       /* hit +0x10c game-pace accumulator, reset every point */
    float         serve_toss; /* seconds the serve has been tossed [CHOICE timing] */
    FocusMeter    focus;      /* player +0x1858 Focus meter */
    float         intensity;  /* player +0x4bbc, 0..1: plyrIntensityMgr (not ported here) */
    int           point_over; /* player +0x4bd0: set by the point messages, cleared at contact */
    int           control_mode;   /* player +0x1918 */
    AiContext     ai;
    const SwingSpace* space;  /* swingspace.xml for this character [DATA] */
    const CharacterSwingData* swings;   /* swingsolver_*.xml / swingspace.xml / ballfudge.xml [DATA] */
    Vec3          root;       /* character root on the floor */
    char          announcer_name[16];   /* e.g. "CHINESE" for cue names [DATA .td] */
} Player;

typedef struct {
    EventBus       bus;
    BallParams     ball_params;
    TableGeom      table;
    BallPath       path;
    MatchSettings  settings;
    MatchScore     score;
    SwingConfig    swing_cfg;
    TableTargetCfg tcfg;      /* table cfg +0x18..+0x38 [DATA live] */
    ShotCharacter  default_chr;
    CharacterSwingData default_swings;   /* Sweden, generated literals */
    float          wobble;    /* DAT_806db368 */
    RallyState     last_state;
    FullFocus      full;      /* gdGameData full-focus levels */
    int            auto_full_focus[RTT_MAX_PLAYERS];   /* profile +0x51 "auto activate full focus" */
    int            last_hitter;   /* DAT_806daf24: whose shot is in flight (player index) */
    AiData         ai_data;
    Player         players[RTT_MAX_PLAYERS];
    AudioSystem*   audio;     /* optional (presentation) */
    CameraDirector* camera;   /* optional (presentation) */
    Rng            rng;
    float          time;
    float          post_point_timer;
} World;

void world_init(World* w, const MatchSettings* s);
/* one fixed 1/60 s step */
void world_tick(World* w, const PlayerInput in[RTT_MAX_PLAYERS]);

#endif /* RTT_MATCH_H */
