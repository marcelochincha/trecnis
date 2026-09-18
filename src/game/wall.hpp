#pragma once






#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>

struct Wall {
    float inner_z    = 0.0f;

    float thickness  = 0.10f;
    float half_width = 0.85f;
    float height     = 1.1f;



    void append_tris(std::vector<bvh::Tri>& out) const;


















    bool resolve(vec3& pos, vec3& vel, float radius, float restitution) const;
};
