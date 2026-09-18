#include <game/wall.hpp>
#include <engine/geom/shapes.hpp>

void Wall::append_tris(std::vector<bvh::Tri>& out) const {
    const vec3 wall_col(0.58f, 0.58f, 0.62f);
    geom::add_box(out, vec3(-half_width, 0.0f,   inner_z),
                       vec3( half_width, height,  inner_z + thickness), wall_col, 0.80f);
}

bool Wall::resolve(vec3& pos, vec3& vel, float radius, float restitution) const {
    if (pos.x < -half_width || pos.x > half_width) return false;
    if (pos.y < 0.0f || pos.y > height)             return false;
    if (pos.z + radius < inner_z)                   return false;
    if (vel.z <= 0.0f)                               return false;

    pos.z = inner_z - radius;
    vel.z = -vel.z * restitution;
    return true;
}
