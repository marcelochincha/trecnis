#include <game/ball.hpp>

int Ball::update(float dt, const AABB& b) {
    // Semi-implicit-ready form; with zero acceleration this is just p += v*dt.
    pos = pos + vel * dt;

    int hits = 0;

    // Reflect against each axis-aligned face. Clamp the centre back to the
    // contact point so a large dt cannot tunnel the ball through a wall.
    if (pos.x - radius < b.min.x)      { pos.x = b.min.x + radius; vel.x = -vel.x; ++hits; }
    else if (pos.x + radius > b.max.x) { pos.x = b.max.x - radius; vel.x = -vel.x; ++hits; }

    if (pos.y - radius < b.min.y)      { pos.y = b.min.y + radius; vel.y = -vel.y; ++hits; }
    else if (pos.y + radius > b.max.y) { pos.y = b.max.y - radius; vel.y = -vel.y; ++hits; }

    if (pos.z - radius < b.min.z)      { pos.z = b.min.z + radius; vel.z = -vel.z; ++hits; }
    else if (pos.z + radius > b.max.z) { pos.z = b.max.z - radius; vel.z = -vel.z; ++hits; }

    return hits;
}
