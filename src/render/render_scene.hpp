#pragma once
#include <array>
#include <vector>

#include <render/raytrace/bvh.hpp>
#include <render/raytrace/accel.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_texture.hpp>
#include <core/sr_geometry.hpp>





struct PointLight {
    vec3  pos       = vec3(0.0f, 3.0f, 0.0f);
    vec3  color     = vec3(1.0f, 1.0f, 1.0f);
    float intensity = 10.0f;
};




struct RasterItem {
    const mesh* geo    = nullptr;
    uint32_t    color  = 0xFFFFFFFF;
    bool        shadow = false;
};





struct RenderScene {
    const camera* cam = nullptr;
    int width  = 0;
    int height = 0;





    const bvh::BVH*              static_bvh  = nullptr;
    const bvh::BVH*              dynamic_bvh = nullptr;
    const std::vector<bvh::Tri>* brute_tris = nullptr;
    bool use_bvh = true;



    bvh::BuildStrategy static_strategy  = bvh::SAH;
    bvh::BuildStrategy dynamic_strategy  = bvh::Morton;



    const ISceneAccel* accel = nullptr;



    const std::vector<RasterItem>* raster_items = nullptr;


    const std::vector<bvh::Tri>* emissive = nullptr;
    const std::vector<PointLight>* point_lights = nullptr;
    const std::array<texture, 6>* skybox  = nullptr;
    bool skybox_enabled = true;




    vec3 bg_color = vec3(0.0f, 0.0f, 0.0f);


    bool sun_enabled = true;
    bool reflections = true;
    int  max_bounces = 1;



    bool  gi_enabled  = true;
    int   gi_samples  = 4;
    float gi_strength = 1.0f;
};
