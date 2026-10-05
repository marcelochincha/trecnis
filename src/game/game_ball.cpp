#include <game/game_ball.hpp>

#include <algorithm>
#include <cmath>

namespace tt {

namespace {

constexpr float kBounceRestitution = 0.82f;   // energy kept on a table bounce
constexpr float kNetZHalf = kHalfWidZ;        // net spans the table width

// Deterministic serve parameters. The release point sits over the server's
// half; the velocity puts a first bounce on the server's half, then over the
// net and down onto the receiver's half.
constexpr float kServeReleaseY = 1.60f;
constexpr float kServeDx       = 0.95f;   // release toward the net from the server
constexpr float kServeVx       = 2.1f;    // horizontal toward the receiver
constexpr float kServeVy       = 0.40f;   // slight upward drift to clear the net
constexpr float kServeZ        = 0.30f;   // a little width so it's not boring

Side half_of(float x) { return x < 0.0f ? Side::Player : Side::AI; }

} // namespace

void serve(Ball& b, Side server)
{
    const float dir = server == Side::Player ? 1.0f : -1.0f;
    b.pos = vec3(-dir * kServeDx, kServeReleaseY, 0.0f);
    b.vel = vec3(dir * kServeVx, kServeVy, kServeZ);
    b.server   = server;
    b.for_side = other(server);
    b.bounces[0] = 0;
    b.bounces[1] = 0;
    b.serve_own = false;
    b.phase     = Phase::Serve;
    b.dead       = false;
    b.last_bounce = server;
}

Event step(Ball& b, float dt)
{
    if (b.dead) return Event::None;

    const vec3 prev = b.pos;
    b.vel.y -= kGravity * dt;
    b.pos = prev + b.vel * dt;

    // ---- Net ----------------------------------------------------------------
    // Swept plane test: the ball's x-crossing of x=0, with the height there.
    if ((prev.x < 0.0f && b.pos.x > 0.0f) || (prev.x > 0.0f && b.pos.x < 0.0f)) {
        const float t = (0.0f - prev.x) / (b.pos.x - prev.x);
        const float yc = prev.y + (b.pos.y - prev.y) * t;
        const float zc = prev.z + (b.pos.z - prev.z) * t;
        const bool in_z = std::fabs(zc) <= kNetZHalf + b.radius;
        if (in_z && yc + b.radius > kPlateTopY && yc - b.radius < kNetTopY) {
            b.dead = true;
            return b.for_side == Side::Player ? Event::PointPlayer : Event::PointAI;
        }
    }

    // ---- Floor: the rally is dead, the shooter or the receiver failed --------
    if (b.pos.y - b.radius < kFloorY) {
        b.dead = true;
        if (b.phase == Phase::Serve) {
            // The serve never reached the receiver's half -> the server failed.
            return b.server == Side::Player ? Event::PointAI : Event::PointPlayer;
        }
        const int fi = side_index(b.for_side);
        return (b.bounces[fi] >= 1)
            ? (b.for_side == Side::Player ? Event::PointAI : Event::PointPlayer)
            : (b.for_side == Side::Player ? Event::PointPlayer : Event::PointAI);
    }

    // ---- Table bounce --------------------------------------------------------
    const bool on_table_xy = std::fabs(b.pos.x) <= kHalfLenX && std::fabs(b.pos.z) <= kHalfWidZ;
    const bool crossed_top = prev.y - b.radius > kPlateTopY && b.pos.y - b.radius <= kPlateTopY;
    if (crossed_top && on_table_xy) {
        b.pos.y = kPlateTopY + b.radius;
        b.vel.y = -b.vel.y * kBounceRestitution;
        const Side s = half_of(b.pos.x);
        b.last_bounce = s;

        if (b.phase == Phase::Serve) {
            if (s == b.server) {
                // The serve bounced on its own half: legal so far. A second
                // bounce before crossing the net would be a failed serve.
                if (b.serve_own) {
                    b.dead = true;
                    return b.server == Side::Player ? Event::PointAI : Event::PointPlayer;
                }
                b.serve_own = true;
                b.bounces[side_index(s)] = 1;
            } else {
                // Reached the receiver's half: the rally proper has started.
                b.phase = Phase::Rally;
                b.for_side = s;
                b.bounces[0] = 0;
                b.bounces[1] = 0;
                b.bounces[side_index(s)] = 1;
            }
            return Event::Bounce;
        }

        // Rally bounce.
        if (s == b.for_side) {
            b.bounces[side_index(s)]++;
            if (b.bounces[side_index(s)] >= 2) {
                // Took a second bounce without a return -> the receiver failed.
                b.dead = true;
                return s == Side::Player ? Event::PointAI : Event::PointPlayer;
            }
        } else {
            // Bounced on the striker's own half: the shot never cleared.
            b.dead = true;
            return b.for_side == Side::Player ? Event::PointPlayer : Event::PointAI;
        }
        return Event::Bounce;
    }

    return Event::None;
}

void struck(Ball& b, Side striker, vec3 v)
{
    b.vel = v;
    b.for_side = other(striker);
    b.phase = Phase::Rally;
    b.bounces[0] = 0;
    b.bounces[1] = 0;
}

} // namespace tt