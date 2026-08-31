#pragma once
#include <math/sr_math.hpp>
#include <string>
#include <vector>

// A camera keyframe: world position + Euler angles in DEGREES (pitch, yaw, roll).
struct CameraKey { vec3 pos; vec3 euler_deg; };

// Load a camera path file. Lines are "fps <f>", "music <name>", '#' comments,
// or 6 floats: px py pz pitch yaw roll. Returns false if no keys were read.
bool load_camera_anim(const std::string& path, std::vector<CameraKey>& keys,
                      float& fps, std::string& music);

// Euler (deg) in the engine's gameplay convention so a camera at `pos` looks at
// `target`. Forward maps as (sin yaw, sin pitch, -cos yaw).
vec3 euler_deg_looking_at(const vec3& pos, const vec3& target);

// Sample the path at time `t` (seconds). The first keyframe eases in from
// `start`; after the last key the pose is held. Writes position + Euler(deg).
void sample_camera_anim(const std::vector<CameraKey>& keys, const CameraKey& start,
                        float t, float fps, vec3& out_pos, vec3& out_euler_deg);
