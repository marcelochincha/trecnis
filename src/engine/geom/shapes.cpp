#include <engine/geom/shapes.hpp>
#include <cmath>

namespace geom {

void add_box(std::vector<bvh::Tri>& out,
             const vec3& lo, const vec3& hi,
             const vec3& albedo, float roughness,
             float metallic, float ior)
{
    vec3 v[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    auto face = [&](int a, int b, int c, int d, const vec3& n) {
        out.push_back({ v[a], v[b], v[c], n, albedo, roughness, metallic, ior });
        out.push_back({ v[a], v[c], v[d], n, albedo, roughness, metallic, ior });
    };
    face(4, 5, 6, 7, { 0,  1,  0}); face(3, 2, 1, 0, { 0, -1,  0});
    face(0, 3, 7, 4, {-1,  0,  0}); face(1, 5, 6, 2, { 1,  0,  0});
    face(0, 1, 5, 4, { 0,  0, -1}); face(3, 7, 6, 2, { 0,  0,  1});
}

void add_box_c(std::vector<bvh::Tri>& out, const vec3& center, const vec3& half,
               const vec3& albedo, float roughness, float metallic, float ior)
{
    add_box(out, center - half, center + half, albedo, roughness, metallic, ior);
}

void add_sphere(std::vector<bvh::Tri>& out,
                const vec3& center, float radius,
                const vec3& albedo, float roughness, float metallic, float ior,
                int slices, int stacks, bool smooth)
{
    for (int st = 0; st < stacks; ++st) {
        float ph0 = (float)st       / stacks * 3.14159265f;
        float ph1 = (float)(st + 1) / stacks * 3.14159265f;
        for (int sl = 0; sl < slices; ++sl) {
            float th0 = (float)sl       / slices * 6.2831853f;
            float th1 = (float)(sl + 1) / slices * 6.2831853f;
            auto p = [&](float ph, float th) {
                return vec3(std::sin(ph) * std::cos(th), std::cos(ph), std::sin(ph) * std::sin(th));
            };
            vec3 v0 = center + p(ph0, th0) * radius, v1 = center + p(ph1, th0) * radius;
            vec3 v2 = center + p(ph1, th1) * radius, v3 = center + p(ph0, th1) * radius;
            vec3 n0n = normalize(p(ph0, th0)), n1n = normalize(p(ph1, th0));
            vec3 n2n = normalize(p(ph1, th1)), n3n = normalize(p(ph0, th1));
            vec3 fn = normalize(cross(v1 - v0, v2 - v0));
            out.push_back({ v0, v1, v2, fn, albedo, roughness, metallic, ior, smooth, n0n, n1n, n2n });
            out.push_back({ v0, v2, v3, fn, albedo, roughness, metallic, ior, smooth, n0n, n2n, n3n });
        }
    }
}

}
