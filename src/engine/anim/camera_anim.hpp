#pragma once
#include <math/sr_math.hpp>
#include <string>
#include <vector>


struct CameraKey { vec3 pos; vec3 euler_deg; };



bool load_camera_anim(const std::string& path, std::vector<CameraKey>& keys,
                      float& fps, std::string& music);



vec3 euler_deg_looking_at(const vec3& pos, const vec3& target);



void sample_camera_anim(const std::vector<CameraKey>& keys, const CameraKey& start,
                        float t, float fps, vec3& out_pos, vec3& out_euler_deg);
