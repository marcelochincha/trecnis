#pragma once












#include <string>
#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>
#include <game/ball.hpp>

class Racket {
public:



    void configure(const vec3& pos, const vec3& euler, const vec3& size,
                   float restitution);




    void step(const vec3& dir, const AABB& limits, float dt, float speed);


    void recenter(const vec3& pos);



    void append_local_tris(std::vector<bvh::Tri>& out) const;















    void append_local_tris_from_obj(const std::string& path, std::vector<bvh::Tri>& out) const;

    const Obb&  collider()    const { return obb_; }
    vec3        position()    const { return pos_; }
    vec3        velocity()    const { return vel_; }
    vec3        face_normal() const { return obb_.axis[2]; }
    std::size_t tri_count()   const { return 12; }

private:
    vec3 pos_    = vec3(0.0f, 0.0f, 0.0f);
    vec3 vel_    = vec3(0.0f, 0.0f, 0.0f);
    vec3 euler_  = vec3(0.0f, 0.0f, 0.0f);
    vec3 size_   = vec3(1.0f, 1.0f, 0.2f);
    vec3 albedo_ = vec3(0.20f, 0.45f, 0.85f);
    Obb  obb_;
};
