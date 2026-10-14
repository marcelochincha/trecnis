/*
 * rtt_events.h -- the one message bus every system talks through.
 *
 * RTT has no single game loop; subsystems are ticked in a fixed order and communicate through
 * msgQueue__Post / msgQueue__Send (findings/runtime-architecture.md). The gameplay core only POSTS;
 * presentation (animation, camera, audio, HUD) only SUBSCRIBES. Where RTT's numeric message id is
 * known it is kept as the enum value, so logs can be compared against the binary.
 */
#ifndef RTT_EVENTS_H
#define RTT_EVENTS_H

#include "rtt_core.h"

typedef enum {
    /* ---- msgBall, 1-based, name table 0x8043b830 [RTT] (findings/map/match-rules.md §2) ---- */
    EV_BALL_BOUNCE_TABLE_A      = 1,
    EV_BALL_BOUNCE_TABLE_B      = 2,
    EV_BALL_BOUNCE_PADDLE_A     = 3,
    EV_BALL_BOUNCE_PADDLE_B     = 4,
    EV_BALL_MISS_PADDLE_A       = 5,
    EV_BALL_MISS_PADDLE_B       = 6,
    EV_BALL_COLLIDE_NET         = 7,
    EV_BALL_COLLIDE_PLAYER_SOFT = 8,
    EV_BALL_COLLIDE_PLAYER_HARD = 9,
    EV_BALL_COLLIDE_UNKNOWN     = 10,
    EV_BALL_AT_APEX             = 11,
    EV_BALL_BELOW_TABLE         = 12,
    EV_BALL_BEYOND_TABLE_END    = 13,
    EV_BALL_PAST_PLAYER         = 14,
    EV_BALL_FELL_THROUGH_FLOOR  = 44,
    EV_POINT_TIMEOUT            = 0x3849,   /* non-ball msg that ends a point as a miss [OPEN name] */

    /* ---- match / score [RTT] (match-rules.md §3) ---- */
    EV_FAULT                    = 0x824,    /* serve fault; i[0] = 1 after the first correct bounce
                                               or a net serve back on the server's side */
    EV_SERVE_BOUNCED_OK         = 0x825,    /* serve landed on the receiver's side */
    EV_OUT_A                    = 0x827,    /* rally shot out, per striker side */
    EV_OUT_B                    = 0x828,
    EV_POINT_A                  = 0x82d,
    EV_POINT_B                  = 0x82e,
    EV_SCORE_RESET              = 0x82f,
    EV_GAME_SCORE               = 0x830,    /* payload: games A, games B */
    EV_MID_GAME                 = 0x831,
    EV_DEUCE                    = 0x832,
    EV_GAME_POINT_A             = 0x833,
    EV_GAME_POINT_B             = 0x834,
    EV_GAME_WON_A               = 0x835,
    EV_GAME_WON_B               = 0x836,
    EV_POINT_START              = 0x83b,    /* posted on entering the per-point pre-serve state
                                               (FUN_80034f40), before every serve [RTT] */
    EV_GAME_START               = 0x841,    /* statePreGame__Enter, once per game before
                                               ResetForNewGame; resets the Focus meter [RTT] */
    EV_RALLY_SHOT_COUNT         = 0x846,    /* payload: count; RALLY music at count 5 [DATA] */
    EV_MATCH_POINT_A            = 0x854,
    EV_MATCH_POINT_B            = 0x855,
    EV_MATCH_WON_A              = 0x856,
    EV_MATCH_WON_B              = 0x857,
    EV_MATCH_TIED               = 0x858,

    /* ---- swing / animation feedback [RTT] (animation-runtime.md, gameplay.md) ---- */
    EV_SERVE_ARMED              = 0x1002,
    EV_SWING_ANIM_DONE          = 0x1016,
    EV_SWING_EARLY              = 0x180e,
    EV_SWING_LATE               = 0x1811,
    EV_SWING_ON_TIME            = 0x1812,
    EV_PADDLE_CONTACT           = 0x1900,   /* [CHOICE] id; RTT routes contact via msgs 3/4 */

    EV_COUNT_SENTINEL
} EventId;

typedef struct {
    EventId id;
    int     side;        /* SIDE_A / SIDE_B / SIDE_NONE: who it concerns */
    int     i[2];        /* small integer payload (e.g. games A/B, rally count) */
    float   f[4];        /* small float payload (e.g. impact speed^2, time) */
} Event;

typedef void (*EventFn)(void* user, const Event* e);

typedef struct {
    Event   queue[RTT_MAX_EVENTS];
    int     head, count;
    struct { EventId id; EventFn fn; void* user; } subs[RTT_MAX_SUBSCRIBERS];
    int     sub_count;
} EventBus;

void ev_init(EventBus* bus);
/* id == EV_COUNT_SENTINEL subscribes to every event */
void ev_subscribe(EventBus* bus, EventId id, EventFn fn, void* user);
void ev_post(EventBus* bus, EventId id, int side);
void ev_send(EventBus* bus, const Event* e);
/* delivers queued events in post order; events posted during dispatch are delivered too */
void ev_dispatch(EventBus* bus);

#endif /* RTT_EVENTS_H */
