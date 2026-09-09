#include <game/racket.hpp>

// Emit an oriented box (centre `c`, orthonormal axes `R`, half-extents `half`)
// as 12 shaded triangles. Same corner indexing / winding as geom::add_box, with
// the axis-aligned face normals replaced by the rotated axes.
static void add_oriented_box(std::vector<bvh::Tri>& out, const vec3& c,
                             const vec3 R[3], const vec3& half,
                             const vec3& albedo, float roughness) {
    auto P = [&](int sx, int sy, int sz) {
        return c + R[0] * (sx * half.x) + R[1] * (sy * half.y) + R[2] * (sz * half.z);
    };
    vec3 v[8] = {
        P(-1,-1,-1), P(+1,-1,-1), P(+1,-1,+1), P(-1,-1,+1),
        P(-1,+1,-1), P(+1,+1,-1), P(+1,+1,+1), P(-1,+1,+1),
    };
    auto face = [&](int a, int b, int cc, int d, const vec3& n) {
        out.push_back({ v[a], v[b], v[cc], n, albedo, roughness });
        out.push_back({ v[a], v[cc], v[d], n, albedo, roughness });
    };
    face(4, 5, 6, 7,  R[1]);       // +Y
    face(3, 2, 1, 0, -R[1]);       // -Y
    face(0, 3, 7, 4, -R[0]);       // -X
    face(1, 5, 6, 2,  R[0]);       // +X
    face(0, 1, 5, 4, -R[2]);       // -Z
    face(3, 7, 6, 2,  R[2]);       // +Z (hitting face)
}

void Racket::configure(const vec3& pos, const vec3& euler, const vec3& size,
                       float restitution) {
    pos_   = pos;
    euler_ = euler;
    size_  = size;

    mat4 rot = rotationMatrix(euler_.x, euler_.y, euler_.z);   // ZYX
    vec3 R[3] = {
        vec3(rot * vec3(1.0f, 0.0f, 0.0f)),
        vec3(rot * vec3(0.0f, 1.0f, 0.0f)),
        vec3(rot * vec3(0.0f, 0.0f, 1.0f)),
    };

    obb_.center      = pos_;
    obb_.axis[0]     = R[0];
    obb_.axis[1]     = R[1];
    obb_.axis[2]     = R[2];
    obb_.half        = size_ * 0.5f;
    obb_.restitution = restitution;
}

void Racket::append_tris(std::vector<bvh::Tri>& out) const {
    add_oriented_box(out, obb_.center, obb_.axis, obb_.half, albedo_, 0.35f);
}
