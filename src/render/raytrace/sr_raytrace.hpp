#pragma once
#include <render/render_scene.hpp>
#include <cstdint>

// Global lighting constants shared by the CPU and GPU shading paths.
extern const vec3  SUN_DIR;
extern const float AMBIENT;
extern const float SHADOW_EPS;

struct ray {
    vec3 origin;
    vec3 direction;
    ray(const vec3& o, const vec3& d) : origin(o), direction(d) {}
};

uint32_t pack(vec3 c);
vec3 get_ray_direction(const camera& cam, int px, int py, int w, int h);

// Shade a single primary/secondary ray against the scene. Uses scene.accel for
// visibility, so it is identical for the CPU-BVH and Embree backends.
vec3 trace_ray(const ray& r, const RenderScene& scene, int depth, uint32_t& seed,
               bool skip_emission = false);
vec3 trace_ray(const ray& r, const RenderScene& scene);  // seed-free overload
