#include <game/ball.hpp>
#include <algorithm>
#include <cmath>

static constexpr float kFixedStep  = 1.0f / 240.0f;
static constexpr float kMaxCatchUp = 0.25f;





bool Obb::resolve(vec3& c, vec3& v, float r) const {

    vec3  d  = c - center;
    float lx = dot(d, axis[0]);
    float ly = dot(d, axis[1]);
    float lz = dot(d, axis[2]);


    float qx = std::clamp(lx, -half.x, half.x);
    float qy = std::clamp(ly, -half.y, half.y);
    float qz = std::clamp(lz, -half.z, half.z);
    vec3  closest = center + axis[0] * qx + axis[1] * qy + axis[2] * qz;

    vec3  delta = c - closest;
    float dist2 = dot(delta, delta);
    if (dist2 >= r * r) return false;

    vec3  n;
    float dist = std::sqrt(dist2);
    if (dist > 1e-6f) {
        n = delta / dist;
    } else {

        float px = half.x - std::fabs(lx);
        float py = half.y - std::fabs(ly);
        float pz = half.z - std::fabs(lz);
        if      (px <= py && px <= pz) n = axis[0] * (lx < 0.0f ? -1.0f : 1.0f);
        else if (py <= pz)             n = axis[1] * (ly < 0.0f ? -1.0f : 1.0f);
        else                           n = axis[2] * (lz < 0.0f ? -1.0f : 1.0f);
        dist = 0.0f;
    }


    c = c + n * (r - dist);










    float vn_rel = dot(v - vel, n);
    if (vn_rel < 0.0f) v = v - n * ((1.0f + restitution) * vn_rel);
    return true;
}





int Ball::update(float dt, const AABB& b, const Obb* racket, const Table* table, const Wall* wall) {
    if (dt < 0.0f) dt = 0.0f;
    accum_ += std::min(dt, kMaxCatchUp);

    int contacts = 0;
    while (accum_ >= kFixedStep) {
        contacts += step_fixed(kFixedStep, b, racket, table, wall);
        accum_   -= kFixedStep;
    }
    return contacts;
}

int Ball::step_fixed(float h, const AABB& b, const Obb* racket, const Table* table, const Wall* wall) {


    vec3 a = vec3(0.0f, -gravity, 0.0f) + magnus * cross(spin, vel);
    vel = vel + a * h;




    float sp = magnitude(vel);
    if (sp > 1e-6f) vel = vel / (1.0f + drag * sp * h);


    if (spin_decay > 0.0f) spin = spin / (1.0f + spin_decay * h);

    const float prev_y = pos.y;
    pos = pos + vel * h;


    int c = 0;


    if (pos.x - radius < b.min.x)      { pos.x = b.min.x + radius; vel.x = -vel.x * restitution; ++c; }
    else if (pos.x + radius > b.max.x) { pos.x = b.max.x - radius; vel.x = -vel.x * restitution; ++c; }


    if (pos.z - radius < b.min.z)      { pos.z = b.min.z + radius; vel.z = -vel.z * restitution; ++c; }
    else if (pos.z + radius > b.max.z) { pos.z = b.max.z - radius; vel.z = -vel.z * restitution; ++c; }


    if (pos.y + radius > b.max.y)      { pos.y = b.max.y - radius; vel.y = -vel.y * restitution; ++c; }



    if (table && table->resolve(prev_y, pos, vel, radius, restitution, rest_speed)) ++c;




    if (table && table->resolve_net(pos, vel, radius, restitution)) { ++c; ++net_hits; }






    if (wall && wall->resolve(pos, vel, radius, restitution)) ++c;



    if (pos.y - radius < b.min.y) {
        pos.y = b.min.y + radius;
        float v_in = -vel.y;
        if (v_in > rest_speed) { vel.y = v_in * restitution; ++c; }
        else                     vel.y = 0.0f;
    }




    if (racket) {
        float sp_before = magnitude(vel);
        if (racket->resolve(pos, vel, radius)) {
            ++c;
            ++racket_hits;
            hit_speed_in     = sp_before;
            hit_speed_out    = magnitude(vel);
            hit_racket_speed = magnitude(racket->vel);
        }
    }

    return c;
}
