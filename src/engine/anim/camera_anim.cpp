#include <engine/anim/camera_anim.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

bool load_camera_anim(const std::string& path, std::vector<CameraKey>& keys,
                      float& fps, std::string& music) {
    std::ifstream in(path);
    if (!in) return false;
    keys.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tok; ss >> tok;
        if (tok == "fps")   { ss >> fps; continue; }
        if (tok == "music") { std::getline(ss >> std::ws, music); continue; }
        // Otherwise the first token was px; parse the remaining 5 floats.
        CameraKey k{};
        k.pos.x = std::stof(tok);
        ss >> k.pos.y >> k.pos.z >> k.euler_deg.x >> k.euler_deg.y >> k.euler_deg.z;
        keys.push_back(k);
    }
    return !keys.empty();
}

vec3 euler_deg_looking_at(const vec3& pos, const vec3& target) {
    vec3 d = normalize(target - pos);
    float pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    float yaw   = std::atan2(d.x, -d.z);
    return vec3(to_degrees(pitch), to_degrees(yaw), 0.0f);
}

void sample_camera_anim(const std::vector<CameraKey>& keys, const CameraKey& start,
                        float t, float fps, vec3& out_pos, vec3& out_euler_deg) {
    if (keys.empty()) { out_pos = start.pos; out_euler_deg = start.euler_deg; return; }

    float u = t * fps; // in keyframe units
    int   n = (int)keys.size();
    CameraKey a, b; float local;
    if (u < 1.0f) { a = start; b = keys[0]; local = u; }
    else {
        float uu = u - 1.0f;
        int seg = (int)std::floor(uu);
        if (seg >= n - 1) {           // past the end: hold the last keyframe
            out_pos = keys[n - 1].pos;
            out_euler_deg = keys[n - 1].euler_deg;
            return;
        }
        local = uu - std::floor(uu);
        a = keys[seg]; b = keys[seg + 1];
    }
    float s = local * local * (3.0f - 2.0f * local); // smoothstep ease
    out_pos = lerp(a.pos, b.pos, s);
    auto alerp = [&](float x, float y) {              // wrap-aware angle lerp (deg)
        float d = y - x;
        while (d >  180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        return x + d * s;
    };
    out_euler_deg = vec3(alerp(a.euler_deg.x, b.euler_deg.x),
                         alerp(a.euler_deg.y, b.euler_deg.y),
                         alerp(a.euler_deg.z, b.euler_deg.z));
}
