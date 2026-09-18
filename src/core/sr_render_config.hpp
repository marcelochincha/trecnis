#pragma once

#include <cstdint>
#include <math/sr_math.hpp>
#include <core/sr_texture.hpp>


struct renderConfig
{
    uint32_t baseColor = 0xFFFFFFFF;
    texture* tex = nullptr;
    float lightInfluence = 1.0f;
    bool ignoreDepth = false;
    bool ignoreLight = false;
    bool backfaceCull = true;
};

