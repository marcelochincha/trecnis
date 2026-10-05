#pragma once

#include <math/sr_math.hpp>

// =============================================================================
// Table-tennis ball physics + rally referee. Pure kinematics and rules: no mesh,
// no SceneRuntime, no SDL. game.cpp owns the visual mesh and the World publish;
// this owns where the ball is, where it is heading, and when a point is won.
//
// The referee enforces the one-bounce-per-side rule: each side must let the ball
// bounce once on its own half before returning it. A second consecutive bounce
// on the same half (counters going to 2) is a failure and hands the point to
// the opponent. Death by floor or net is attributed by where the ball was due:
// before the receiver's half -> the striker failed; after -> the receiver failed.
// =============================================================================

namespace tt {

// Mirrors the static scenery authored in game_init(), so the physics and the
// vision cannot drift apart. Table at kPlateTopY, net across the middle at
// x = 0, floor at kFloorY (<= 0).
constexpr float kHalfLenX  = 1.4f;    // along the long axis (x), from the net
constexpr float kHalfWidZ  = 0.8f;    // along the short axis (z)
constexpr float kPlateTopY = 1.2f;    // playing surface
constexpr float kNetHeight = 0.1525f; // real ping-pong net height
constexpr float kNetTopY   = kPlateTopY + kNetHeight;
constexpr float kFloorY    = 0.0f;    // below this the ball is off the table
constexpr float kBallRadius = 0.02f;
constexpr float kGravity    = 9.8f;

enum class Side : int { Player = 0, AI = 1 };

inline Side other(Side s) { return s == Side::Player ? Side::AI : Side::Player; }
inline int  side_index(Side s) { return s == Side::Player ? 0 : 1; }

// What step() produced this frame. Points carry the full outcome; the caller
// only has to score them and issue the next serve.
enum class Event : int {
    None = 0,
    Bounce,        // legal bounce on the table (see ball.last_bounce / counters)
    PointPlayer,   // a point was decided: the player scores
    PointAI,       // a point was decided: the AI scores
};

enum class Phase : int { Serve = 0, Rally = 1 };

struct Ball {
    vec3 pos  = vec3(0.0f, kPlateTopY + 0.1f, 0.0f);
    vec3 vel  = vec3(0.0f, 0.0f, 0.0f);
    float radius = kBallRadius;

    Side  server    = Side::Player;
    Side  for_side  = Side::Player;  // whose obligation it is to return the ball
    int   bounces[2] = {0, 0};       // consecutive table bounces per side
    bool  serve_own = false;         // serve already bounced on the server's half
    Phase phase     = Phase::Serve;
    bool  dead      = true;          // rally over; caller should serve() again
    Side  last_bounce = Side::Player;
};

// New rally: deterministic serve from `server`'s half. Bounces once on the
// server's side, clears the net and lands on the receiver's half.
void serve(Ball& b, Side server);

// One fixed-dt step. Returns the outcome event; leaves the ball standing if
// the rally is over (dead == true) so the caller can serve().
Event step(Ball& b, float dt);

// A paddle struck the ball towards the opponent. Resets the striker's bounce
// counter, so the next bounce on their half is again their "one bounce".
void struck(Ball& b, Side striker, vec3 v);

} // namespace tt