#pragma once








#include <vector>
#include <math/sr_math.hpp>
#include <render/raytrace/bvh.hpp>

struct Table {
    float height       = 0.76f;
    float half_width   = 0.7625f;
    float half_len     = 1.37f;
    float net_height   = 0.1525f;
    float net_thickness = 0.006f;





    void append_tris(std::vector<bvh::Tri>& out) const;









    bool resolve(float prev_y, vec3& pos, vec3& vel, float radius,
                 float restitution, float rest_speed) const;














    bool resolve_net(vec3& pos, vec3& vel, float radius, float restitution) const;
};
