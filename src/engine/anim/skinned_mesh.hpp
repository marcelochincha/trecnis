#pragma once

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
























struct SkinnedMesh {
    std::vector<vertex> bind;
    std::vector<int>    vertexBone;
    std::vector<mat4>   skin;
    int   bones   = 0;
    int   frames  = 0;
    float fps     = 24.0f;
    int   matched = 0;

    bool valid() const { return frames > 0 && bones > 0 && !bind.empty(); }





    bool load(const std::string& weights_path, const std::string& anim_path,
              const mesh& m);






    void apply(mesh& m, float t) const;
};




void build_procedural_character(mesh& out, SkinnedMesh& skin);
