/*
 * rtt_rules.h -- referee, scoring and serve rotation (gmLogicSinglesMatch + match score object).
 * Doc: findings/map/match-rules.md (all rules there are [RTT], decompiled 2026-10-09, static).
 */
#ifndef RTT_RULES_H
#define RTT_RULES_H

#include "rtt_core.h"
#include "rtt_events.h"

typedef enum { POINTS_5 = 0, POINTS_7, POINTS_11, POINTS_21 } PointsPerGame;   /* [RTT] */
typedef enum { GAMES_1_OF_1 = 0, GAMES_2_OF_3, GAMES_3_OF_5 } GamesPerMatch;  /* [RTT] */

typedef struct {
    PointsPerGame points_enum;
    GamesPerMatch games_enum;
    int   serves_enum;        /* 0 -> 2 serves per turn, 1 -> 5 [RTT table 0x806def00] */
    int   points_to_win;      /* {5,7,11,21}[points_enum]           settings+0x13c */
    int   games_to_win;       /* {1,2,3}[games_enum]                settings+0x140 */
    int   serves_per_turn;    /* {2,5}[serves_enum]                 settings+0x144 */
    int   win_margin;         /* always 2                           settings+0x148 */
    Side  first_server;       /* settings+0x40: same for every game of a match [RTT] */
    int   timed;              /* timed-match mode, settings+0xc */
    float timed_seconds;      /* settings+0x10 */
} MatchSettings;

/* rally states that own a referee handler [RTT names; numeric ids not needed] */
typedef enum {
    RS_PRE_SERVE,             /* PrePreServe/PreServe: striker = server */
    RS_SERVE_HIT,             /* serve struck, no bounce yet */
    RS_SERVE_BOUNCE,          /* bounced once on the server's side */
    RS_HIT_NET_SERVE,         /* serve touched the net */
    RS_AWAIT_SERVE_RETURN,    /* bounced on receiver's side, waiting for the return */
    RS_RETURN_HIT,            /* rally shot in flight */
    RS_AWAIT_RETURN,          /* rally shot bounced on the right side, waiting for the return */
    RS_LET,                   /* serve replayed, no point */
    RS_POINT_SCORED,
    RS_MATCH_OVER
} RallyState;

typedef struct {
    int   points[2];          /* +4 / +8 */
    int   total_points[2];    /* +0xc / +0x10: sum of history */
    int   games[2];           /* +0x1c / +0x20 */
    int   history[2][8];      /* +0x24 / +0x44: points per finished game */
    Side  server;             /* +0x12c */
    Side  striker;            /* +0x128: whose shot is in flight / who must play next */
    Side  point_winner;       /* +0x130 (SIDE_NONE = none yet) */
    int   match_winner;       /* +0x68: 0 A, 1 B, 2 tie, -1 none */
    int   time_up;            /* +0x66 timed mode */
    int   net_touched;        /* rally net-touch flag (DAT_806dae3c) */
    int   rally_shots;        /* rally shot counter (music RALLY at 5) */
    int   control_mode[2];    /* settings+0x38: 1 human, 0 CPU; 2 allows early paddle contact */
    RallyState state;
} MatchScore;

void rules_make_settings(MatchSettings* s, PointsPerGame p, GamesPerMatch g, int serves_enum, Side first_server);

/* statePreMatch / statePreGame enter: reset points, server = first server [RTT] */
void rules_start_match(MatchScore* m, const MatchSettings* s, EventBus* bus);
void rules_start_game(MatchScore* m, const MatchSettings* s, EventBus* bus);
/* PrePreServe: striker = server, clear flags */
void rules_start_point(MatchScore* m, EventBus* bus);
/* the server's paddle struck the serve */
void rules_on_serve_struck(MatchScore* m);
/* referee: feed every msgBall event while a point is live. `predictor_says_out` = the current
 * ball prediction's outcome is known and decided out (IsOutcomeUnknown == 0 && FUN_80120f8c);
 * ServeBounce only accepts BelowTable/BeyondTableEnd as a fault when it is set. */
void rules_on_ball_event(MatchScore* m, const MatchSettings* s, const Event* e,
                         int predictor_says_out, EventBus* bus);
/* after the post-point delay: rotate server, next point or next game [RTT AdvanceToNextPoint] */
void rules_advance(MatchScore* m, const MatchSettings* s, EventBus* bus);

/* predicates [RTT] */
int rules_is_game_over(const MatchScore* m, const MatchSettings* s);
int rules_is_deuce(const MatchScore* m, const MatchSettings* s);
int rules_is_game_point(const MatchScore* m, const MatchSettings* s);
int rules_is_match_point(const MatchScore* m, const MatchSettings* s);
int rules_is_match_over(const MatchScore* m, const MatchSettings* s);
int rules_should_switch_server(const MatchScore* m, const MatchSettings* s);

#endif /* RTT_RULES_H */
