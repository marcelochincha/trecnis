#include "rtt_rules.h"

static const int k_points[4] = { 5, 7, 11, 21 };     /* 0x8042ff48 [RTT] */
static const int k_games[3]  = { 1, 2, 3 };          /* 0x8042ff58 [RTT] */
static const int k_serves[2] = { 2, 5 };             /* 0x806def00 [RTT] */
static const int k_midgame[4] = { 3, 4, 7, 16 };     /* MidGame thresholds, 0x8002eac8 [RTT] */

void rules_make_settings(MatchSettings* s, PointsPerGame p, GamesPerMatch g, int serves_enum, Side first_server) {
    memset(s, 0, sizeof(*s));
    s->points_enum = p; s->games_enum = g; s->serves_enum = serves_enum;
    s->points_to_win = k_points[p];
    s->games_to_win = k_games[g];
    s->serves_per_turn = k_serves[serves_enum ? 1 : 0];
    s->win_margin = 2;
    s->first_server = first_server;
}

/* ---------------------------------------------------------------- predicates */
int rules_is_game_over(const MatchScore* m, const MatchSettings* s) {
    int a = m->points[0], b = m->points[1];
    if (a >= s->points_to_win && a >= b + s->win_margin) return 1;
    if (b >= s->points_to_win && b >= a + s->win_margin) return 1;
    if (a >= 99 || b >= 99) return 1;                                  /* safety cap [RTT] */
    return s->timed ? m->time_up : 0;
}

int rules_is_deuce(const MatchScore* m, const MatchSettings* s) {
    return m->points[0] == m->points[1] && s->win_margin > 1 && m->points[0] >= s->points_to_win - 1;
}

int rules_is_game_point(const MatchScore* m, const MatchSettings* s) {
    int t = s->points_to_win - 1;
    for (int i = 0; i < 2; i++)
        if (m->points[i] >= t && m->points[i] >= m->points[1 - i] + s->win_margin - 1) return 1;
    return 0;
}

int rules_is_match_point(const MatchScore* m, const MatchSettings* s) {
    if (s->timed || !rules_is_game_point(m, s)) return 0;
    for (int i = 0; i < 2; i++)
        if (m->points[i] >= m->points[1 - i] && m->games[i] >= s->games_to_win - 1) return 1;
    return 0;
}

int rules_is_match_over(const MatchScore* m, const MatchSettings* s) {
    if (s->timed) return rules_is_game_over(m, s) && (m->time_up || m->games[0] + m->games[1] >= 7);
    return m->games[0] >= s->games_to_win || m->games[1] >= s->games_to_win;
}

int rules_should_switch_server(const MatchScore* m, const MatchSettings* s) {
    int sum = m->points[0] + m->points[1];
    if (sum % s->serves_per_turn == 0) return 1;
    int t = s->points_to_win - 1;
    return m->points[0] >= t && m->points[1] >= t;                      /* every point at deuce [RTT] */
}

/* ---------------------------------------------------------------- flow */
void rules_start_game(MatchScore* m, const MatchSettings* s, EventBus* bus) {
    ev_post(bus, EV_GAME_START, SIDE_NONE);       /* statePreGame__Enter, before ResetForNewGame */
    m->points[0] = m->points[1] = 0;
    m->server = s->first_server;                  /* NOT alternated between games [RTT] */
    m->striker = m->server;
    m->point_winner = SIDE_NONE;
    m->state = RS_PRE_SERVE;
    ev_post(bus, EV_SCORE_RESET, SIDE_NONE);
    Event e; memset(&e, 0, sizeof(e));
    e.id = EV_GAME_SCORE; e.side = SIDE_NONE; e.i[0] = m->games[0]; e.i[1] = m->games[1];
    ev_send(bus, &e);
}

void rules_start_match(MatchScore* m, const MatchSettings* s, EventBus* bus) {
    memset(m, 0, sizeof(*m));
    m->match_winner = -1;
    m->control_mode[0] = 1;
    rules_start_game(m, s, bus);
}

void rules_start_point(MatchScore* m, EventBus* bus) {
    m->striker = m->server;                       /* SetStrikerToServer, PrePreServe [RTT] */
    m->point_winner = SIDE_NONE;
    m->net_touched = 0;
    m->rally_shots = 0;
    m->state = RS_PRE_SERVE;
    ev_post(bus, EV_POINT_START, SIDE_NONE);
}

void rules_on_serve_struck(MatchScore* m) {
    if (m->state == RS_PRE_SERVE || m->state == RS_LET) m->state = RS_SERVE_HIT;
}

/* ProcessPointScored 0x8002ebb8 [RTT] */
static void process_point_scored(MatchScore* m, const MatchSettings* s, EventBus* bus) {
    int w = m->point_winner == SIDE_A ? 0 : 1;
    m->points[w]++;
    ev_post(bus, w == 0 ? EV_POINT_A : EV_POINT_B, (Side)w);
    m->total_points[w]++;
    if (!rules_is_game_over(m, s)) {
        if (m->points[w] == k_midgame[s->points_enum] && m->points[1 - w] < k_midgame[s->points_enum])
            ev_post(bus, EV_MID_GAME, (Side)w);
        if (rules_is_deuce(m, s)) ev_post(bus, EV_DEUCE, SIDE_NONE);
        int lead = m->points[0] >= m->points[1] ? 0 : 1;
        if (rules_is_match_point(m, s))      ev_post(bus, lead == 0 ? EV_MATCH_POINT_A : EV_MATCH_POINT_B, (Side)lead);
        else if (rules_is_game_point(m, s))  ev_post(bus, lead == 0 ? EV_GAME_POINT_A : EV_GAME_POINT_B, (Side)lead);
        return;
    }
    /* game over: AwardGame 0x8002ac98 */
    int gw = m->points[0] > m->points[1] ? 0 : 1;
    int slot = m->games[0] + m->games[1];
    if (slot < 8) { m->history[0][slot] = m->points[0]; m->history[1][slot] = m->points[1]; }
    m->games[gw]++;
    Event e; memset(&e, 0, sizeof(e));
    e.id = EV_GAME_SCORE; e.side = (Side)gw; e.i[0] = m->games[0]; e.i[1] = m->games[1];
    ev_send(bus, &e);
    if (rules_is_match_over(m, s)) {
        m->match_winner = (m->games[0] == m->games[1]) ? 2 : (m->games[0] > m->games[1] ? 0 : 1);
        ev_post(bus, m->match_winner == 0 ? EV_MATCH_WON_A : m->match_winner == 1 ? EV_MATCH_WON_B : EV_MATCH_TIED, SIDE_NONE);
        m->state = RS_MATCH_OVER;
    } else {
        ev_post(bus, gw == 0 ? EV_GAME_WON_A : EV_GAME_WON_B, (Side)gw);
    }
}

static void award(MatchScore* m, const MatchSettings* s, Side winner, EventBus* bus) {
    m->point_winner = winner;                     /* AwardPoint 0x8002f4f0 [RTT] */
    m->state = RS_POINT_SCORED;
    process_point_scored(m, s, bus);
}

/* a serve fault: msg 0x824 (HUD FAULT text; its arg 1 also costs the server 180 Focus meter in
 * pongPlayer::Process), then the point [RTT stateServeHit / ServeBounce / HitNetServe] */
static void fault(MatchScore* m, const MatchSettings* s, Side winner, int arg, EventBus* bus) {
    Event f; memset(&f, 0, sizeof(f));
    f.id = EV_FAULT; f.side = m->striker; f.i[0] = arg;
    ev_send(bus, &f);
    award(m, s, winner, bus);
}

static Side half_of(EventId id) { return id == EV_BALL_BOUNCE_TABLE_A ? SIDE_A : SIDE_B; }
static int is_table(EventId id) { return id == EV_BALL_BOUNCE_TABLE_A || id == EV_BALL_BOUNCE_TABLE_B; }
static int is_paddle(EventId id) { return id == EV_BALL_BOUNCE_PADDLE_A || id == EV_BALL_BOUNCE_PADDLE_B; }
static Side paddle_of(EventId id) { return id == EV_BALL_BOUNCE_PADDLE_A ? SIDE_A : SIDE_B; }
static int is_body(EventId id) { return id == EV_BALL_COLLIDE_PLAYER_SOFT || id == EV_BALL_COLLIDE_PLAYER_HARD; }
static int is_off(EventId id) { return id == EV_BALL_BELOW_TABLE || id == EV_BALL_BEYOND_TABLE_END; }
static int is_dead(EventId id) { return id == EV_BALL_COLLIDE_UNKNOWN || id == EV_BALL_FELL_THROUGH_FLOOR; }

void rules_on_ball_event(MatchScore* m, const MatchSettings* s, const Event* e,
                         int predictor_says_out, EventBus* bus) {
    EventId id = e->id;
    Side st = m->striker, other = rtt_other(m->striker);
    switch (m->state) {
    case RS_SERVE_HIT:                            /* stateServeHit 0x80030304 */
        if (is_table(id) && half_of(id) == st)          m->state = RS_SERVE_BOUNCE;
        else if (is_table(id) || id == EV_BALL_COLLIDE_NET || is_dead(id) || is_off(id))
                                                        fault(m, s, other, 0, bus);
        /* receiver paddle before the bounce: ignored unless control mode 2 */
        break;
    case RS_SERVE_BOUNCE:                         /* stateServeBounce 0x800304bc */
        if (is_table(id) && half_of(id) == other) {
            ev_post(bus, EV_SERVE_BOUNCED_OK, st);
            m->striker = other;                   /* receiver must play now */
            m->state = RS_AWAIT_SERVE_RETURN;
        } else if ((is_table(id) && half_of(id) == st) || is_dead(id)) {
            fault(m, s, other, 1, bus);
        } else if (is_off(id)) {
            /* else RTT logs "Encountered fault condition, but..." and ignores it [RTT] */
            if (predictor_says_out) fault(m, s, other, 1, bus);
        } else if (id == EV_BALL_COLLIDE_NET) {
            m->state = RS_HIT_NET_SERVE;
        }
        break;
    case RS_HIT_NET_SERVE:                        /* stateHitNetServe 0x80030708 */
        if (is_table(id) && half_of(id) == other)  m->state = RS_LET;           /* replay serve */
        else if (is_table(id) && half_of(id) == st) fault(m, s, other, 1, bus);
        else if (is_body(id) || is_dead(id) || is_off(id)) fault(m, s, other, 0, bus);
        break;
    case RS_AWAIT_SERVE_RETURN:                   /* 0x80030a14 */
    case RS_AWAIT_RETURN:                         /* 0x80030c18 -- striker here = expected hitter */
        if (is_paddle(id) && paddle_of(id) == st) {
            m->state = RS_RETURN_HIT;
            m->net_touched = 0;
            m->rally_shots++;
            Event c; memset(&c, 0, sizeof(c));
            c.id = EV_RALLY_SHOT_COUNT; c.side = st; c.i[0] = m->rally_shots;
            ev_send(bus, &c);
        } else if (is_table(id) || id == EV_BALL_PAST_PLAYER || is_body(id) || is_dead(id) ||
                   id == EV_POINT_TIMEOUT) {
            award(m, s, other, bus);              /* the expected hitter missed */
        }
        break;
    case RS_RETURN_HIT:                           /* stateReturnHit 0x80030d78 -- striker = who hit */
        if (is_table(id) && half_of(id) == other) { m->striker = other; m->state = RS_AWAIT_RETURN; }
        else if (is_table(id))                      award(m, s, other, bus);    /* own side */
        else if (id == EV_BALL_COLLIDE_NET)          m->net_touched = 1;         /* rally continues */
        else if (is_off(id)) {
            ev_post(bus, st == SIDE_A ? EV_OUT_A : EV_OUT_B, st);
            award(m, s, other, bus);
        } else if (is_body(id) || is_dead(id) || id == EV_POINT_TIMEOUT) {
            award(m, s, other, bus);              /* any body contact loses for the striker [RTT] */
        }
        break;
    default:
        break;
    }
}

void rules_advance(MatchScore* m, const MatchSettings* s, EventBus* bus) {
    if (m->state == RS_MATCH_OVER) return;
    if (m->state == RS_LET) { rules_start_point(m, bus); return; }
    int game_over = rules_is_game_over(m, s);
    if (game_over) { rules_start_game(m, s, bus); rules_start_point(m, bus); return; }
    if (m->point_winner != SIDE_NONE && rules_should_switch_server(m, s))
        m->server = rtt_other(m->server);         /* ToggleServer [RTT] */
    rules_start_point(m, bus);
}
