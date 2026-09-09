#include <game/racket.hpp>
#include <algorithm>

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
    vel_   = vec3(0.0f, 0.0f, 0.0f);
    euler_ = euler;
    size_  = size;

    mat4 rot = rotationMatrix(euler_.x, euler_.y, euler_.z);   // ZYX
    obb_.axis[0]     = vec3(rot * vec3(1.0f, 0.0f, 0.0f));
    obb_.axis[1]     = vec3(rot * vec3(0.0f, 1.0f, 0.0f));
    obb_.axis[2]     = vec3(rot * vec3(0.0f, 0.0f, 1.0f));
    obb_.half        = size_ * 0.5f;
    obb_.center      = pos_;
    obb_.restitution = restitution;
}

void Racket::step(const vec3& dir, const AABB& limits, float dt, float speed) {
    vec3  d  = dir;
    float dl = magnitude(d);
    if (dl > 1e-4f) d = d / dl;                 // cap diagonal speed

    vec3 prev = pos_;
    pos_ = pos_ + d * (speed * dt);
    pos_.x = std::clamp(pos_.x, limits.min.x, limits.max.x);
    pos_.y = std::clamp(pos_.y, limits.min.y, limits.max.y);
    pos_.z = std::clamp(pos_.z, limits.min.z, limits.max.z);

    vel_ = (dt > 1e-6f) ? (pos_ - prev) * (1.0f / dt) : vec3(0.0f, 0.0f, 0.0f);
    obb_.center = pos_;
}

void Racket::append_local_tris(std::vector<bvh::Tri>& out) const {
    add_oriented_box(out, vec3(0.0f, 0.0f, 0.0f), obb_.axis, obb_.half, albedo_, 0.35f);
}
