#include <game/table.hpp>
#include <engine/geom/shapes.hpp>

void Table::append_tris(std::vector<bvh::Tri>& out) const {
    const vec3 top_albedo(0.06f, 0.20f, 0.35f);
    geom::add_box(out, vec3(-half_width, height - 0.02f, -half_len),
                       vec3( half_width, height,          half_len), top_albedo, 0.35f);

    // White edge lines, laid just proud of the surface.
    const vec3 line(0.90f, 0.90f, 0.92f);
    const float lw = 0.02f, eps = 0.001f;
    geom::add_box(out, vec3(-half_width,      height, -half_len),
                       vec3(-half_width + lw, height + eps, half_len), line, 0.35f);
    geom::add_box(out, vec3( half_width - lw, height, -half_len),
                       vec3( half_width,      height + eps, half_len), line, 0.35f);

    // Net.
    geom::add_box(out, vec3(-half_width, height, -0.006f),
                       vec3( half_width, height + net_height, 0.006f),
                       vec3(0.85f, 0.85f, 0.88f), 0.8f);

    // Four legs, so the table is not floating.
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            float cx = sx * (half_width - 0.10f);
            float cz = sz * (half_len   - 0.15f);
            geom::add_box(out, vec3(cx - 0.03f, 0.0f, cz - 0.03f),
                               vec3(cx + 0.03f, height - 0.02f, cz + 0.03f),
                               vec3(0.15f, 0.15f, 0.17f), 0.5f);
        }
    }
}

bool Table::resolve(float prev_y, vec3& pos, vec3& vel, float radius,
                     float restitution, float rest_speed) const {
    // A resting ball's prev_y sits exactly at the surface (height + radius),
    // so allow a small tolerance below it — otherwise gravity's one-substep
    // nudge would read as "was already below" and the ball would sink through.
    constexpr float kSurfaceEps = 0.01f;
    if (pos.x < -half_width || pos.x > half_width) return false;
    if (pos.z < -half_len   || pos.z > half_len)   return false;
    if (prev_y - radius < height - kSurfaceEps) return false;   // was already below the surface
    if (pos.y - radius >= height) return false;
    if (vel.y >= 0.0f) return false;   // only stop a downward approach

    pos.y = height + radius;
    float v_in = -vel.y;               // incoming downward speed (>= 0)
    if (v_in > rest_speed) { vel.y = v_in * restitution; return true; }
    vel.y = 0.0f;
    return false;
}
