#pragma once

#include <math/sr_math.hpp>
#include <core/sr_geometry.hpp>
#include <vector>
#include <cstddef>

struct texture;






namespace bvh {


struct Tri {
    vec3  v0, v1, v2;
    vec3  normal;
    vec3  albedo;
    float roughness = 0.5f;
    float metallic  = 0.0f;
    float ior       = 1.5f;




    bool  smooth    = false;
    vec3  n0, n1, n2;
    vec3  emission  = vec3(0.0f, 0.0f, 0.0f);

    vec2           uv0{}, uv1{}, uv2{};
    const texture* tex = nullptr;
};





struct TriISect { vec3 v0, e1, e2; };


struct Hit {
    float t   = 1e30f;
    int   tri = -1;
};




enum BuildStrategy { SAH = 0, Median = 1, Morton = 2 };

class BVH {
public:



    void build(std::vector<Tri> tris, BuildStrategy strategy = SAH);

    BuildStrategy strategy() const { return strategy_; }



    bool intersect(const vec3& origin, const vec3& dir, Hit& out) const;



    bool occluded(const vec3& origin, const vec3& dir, float max_t) const;

    const Tri&  tri(int i)          const { return tris_[i]; }
    std::size_t triangle_count()    const { return tris_.size(); }
    std::size_t node_count()        const { return nodes_.size(); }
    bool        empty()             const { return nodes_.empty(); }







    void flatten(std::vector<float>& node_bounds,
                 std::vector<int>&   node_links,
                 std::vector<float>& tris) const;




    struct DebugNode { AABB bounds; int depth; bool leaf; };
    void debug_nodes(std::vector<DebugNode>& out) const;

private:
    struct Node {
        AABB bounds;
        int  left  = -1;
        int  right = -1;
        int  start = 0;
        int  count = 0;
    };

    std::vector<Tri>      tris_;
    std::vector<TriISect> tri_isect_;
    std::vector<Node>     nodes_;
    std::vector<vec3>     centroids_;








    static constexpr int MAX_DEPTH = 60;






    static constexpr int MAX_LEAF = 8;

    int  build_node(int start, int count, int depth);
    int  partition(int start, int count, int axis,
                   float cmin, float scale, int split_bin);



    int  partition_median(int start, int count, int axis);

    BuildStrategy strategy_ = SAH;
};

}
