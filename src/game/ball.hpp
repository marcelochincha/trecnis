#pragma once



























#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>
#include <game/table.hpp>
#include <game/wall.hpp>




struct Obb {
    vec3  center      = vec3(0.0f, 0.0f, 0.0f);
    vec3  axis[3]     = { vec3(1,0,0), vec3(0,1,0), vec3(0,0,1) };
    vec3  half        = vec3(0.5f, 0.5f, 0.5f);
    vec3  vel         = vec3(0.0f, 0.0f, 0.0f);
    float restitution = 0.85f;






    bool resolve(vec3& c, vec3& v, float r) const;
};

struct Ball {
    vec3  pos    = vec3(0.0f, 2.0f, 0.0f);
    vec3  vel    = vec3(0.0f, 0.0f, 0.0f);
    vec3  spin   = vec3(0.0f, 0.0f, 0.0f);
    float radius = 0.5f;


    float gravity     = 9.81f;
    float restitution = 0.75f;
    float rest_speed  = 0.50f;
    float drag        = 0.10f;
    float magnus      = 0.12f;
    float spin_decay  = 0.10f;

    long  racket_hits      = 0;
    float hit_speed_in     = 0.0f;
    float hit_speed_out    = 0.0f;
    float hit_racket_speed = 0.0f;
    long  net_hits         = 0;
    long  swing_hits       = 0;




    int update(float dt, const AABB& bounds, const Obb* racket = nullptr,
               const Table* table = nullptr, const Wall* wall = nullptr);



    void reset(const vec3& p, const vec3& v, const vec3& w) {
        pos = p; vel = v; spin = w;
        accum_ = 0.0f;
        racket_hits = 0;
        hit_speed_in = hit_speed_out = hit_racket_speed = 0.0f;
        net_hits = 0;
        swing_hits = 0;
    }








    void apply_hit(const vec3& new_vel) { vel = new_vel; ++swing_hits; }

    float speed()     const { return magnitude(vel); }
    float spin_rate() const { return magnitude(spin); }

private:
    float accum_ = 0.0f;
    int   step_fixed(float h, const AABB& b, const Obb* racket,
                      const Table* table, const Wall* wall);
};
