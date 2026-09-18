#include <game/racket.hpp>
#include <engine/assets/obj_loader.hpp>
#include <algorithm>
#include <limits>




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
    face(4, 5, 6, 7,  R[1]);
    face(3, 2, 1, 0, -R[1]);
    face(0, 3, 7, 4, -R[0]);
    face(1, 5, 6, 2,  R[0]);
    face(0, 1, 5, 4, -R[2]);
    face(3, 7, 6, 2,  R[2]);
}

void Racket::configure(const vec3& pos, const vec3& euler, const vec3& size,
                       float restitution) {
    pos_   = pos;
    vel_   = vec3(0.0f, 0.0f, 0.0f);
    euler_ = euler;
    size_  = size;

    mat4 rot = rotationMatrix(euler_.x, euler_.y, euler_.z);
    obb_.axis[0]     = vec3(rot * vec3(1.0f, 0.0f, 0.0f));
    obb_.axis[1]     = vec3(rot * vec3(0.0f, 1.0f, 0.0f));
    obb_.axis[2]     = vec3(rot * vec3(0.0f, 0.0f, 1.0f));
    obb_.half        = size_ * 0.5f;
    obb_.center      = pos_;
    obb_.vel         = vec3(0.0f, 0.0f, 0.0f);
    obb_.restitution = restitution;
}

void Racket::step(const vec3& dir, const AABB& limits, float dt, float speed) {
    vec3  d  = dir;
    float dl = magnitude(d);
    if (dl > 1e-4f) d = d / dl;

    vec3 prev = pos_;
    pos_ = pos_ + d * (speed * dt);
    pos_.x = std::clamp(pos_.x, limits.min.x, limits.max.x);
    pos_.y = std::clamp(pos_.y, limits.min.y, limits.max.y);
    pos_.z = std::clamp(pos_.z, limits.min.z, limits.max.z);

    vel_ = (dt > 1e-6f) ? (pos_ - prev) * (1.0f / dt) : vec3(0.0f, 0.0f, 0.0f);
    obb_.center = pos_;
    obb_.vel    = vel_;
}

void Racket::recenter(const vec3& p) {
    pos_        = p;
    vel_        = vec3(0.0f, 0.0f, 0.0f);
    obb_.center = p;
    obb_.vel    = vec3(0.0f, 0.0f, 0.0f);
}

void Racket::append_local_tris(std::vector<bvh::Tri>& out) const {
    add_oriented_box(out, vec3(0.0f, 0.0f, 0.0f), obb_.axis, obb_.half, albedo_, 0.35f);
}

void Racket::append_local_tris_from_obj(const std::string& path, std::vector<bvh::Tri>& out) const {
    std::vector<bvh::Tri> raw;
    load_obj_tris(path.c_str(), vec3(0.0f, 0.0f, 0.0f), 1.0f, albedo_, 0.35f, raw);
    if (raw.empty()) { append_local_tris(out); return; }





    vec3 mn( std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max());
    vec3 mx(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    for (const bvh::Tri& t : raw) {
        const vec3 verts[3] = { t.v0, t.v1, t.v2 };
        for (const vec3& v : verts) {
            mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
            mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
        }
    }
    const vec3  center        = (mn + mx) * 0.5f;
    const vec3  extent        = mx - mn;
    const float model_extent  = std::max(extent.x, std::max(extent.y, extent.z));
    const float target_extent = std::max(size_.x,  std::max(size_.y,  size_.z));
    const float fit           = (model_extent > 1e-6f) ? target_extent / model_extent : 1.0f;






    auto place = [&](const vec3& v) {
        const vec3 local = (v - center) * fit;
        return obb_.axis[0] * local.x + obb_.axis[1] * local.y + obb_.axis[2] * local.z;
    };
    auto orient = [&](const vec3& n) {
        return obb_.axis[0] * n.x + obb_.axis[1] * n.y + obb_.axis[2] * n.z;
    };

    out.reserve(out.size() + raw.size());
    for (bvh::Tri t : raw) {
        t.v0 = place(t.v0); t.v1 = place(t.v1); t.v2 = place(t.v2);
        t.normal = normalize(orient(t.normal));
        if (t.smooth) {
            t.n0 = normalize(orient(t.n0));
            t.n1 = normalize(orient(t.n1));
            t.n2 = normalize(orient(t.n2));
        }
        out.push_back(t);
    }
}
