#pragma once
#ifdef WITH_EMBREE
#include <render/raytrace/accel.hpp>
#include <render/raytrace/embree_bvh.hpp>
#include <vector>





class EmbreeAccel : public ISceneAccel {
public:


    void sync(const bvh::BVH& static_bvh, bvh::BuildStrategy sstrat,
              const std::vector<bvh::Tri>& dyn_tris, bvh::BuildStrategy dstrat) {
        if (static_dirty_) {
            static_tris_.clear();
            for (std::size_t i = 0; i < static_bvh.triangle_count(); ++i)
                static_tris_.push_back(static_bvh.tri((int)i));
            static_scene_.build(static_tris_, sstrat);
            static_dirty_ = false;
        }
        dynamic_tris_ = dyn_tris;
        dynamic_scene_.build(dynamic_tris_, dstrat);
    }

    void mark_static_dirty() { static_dirty_ = true; }

    bool intersect(const vec3& origin, const vec3& dir, SceneHit& out) const override {
        float ts = 1e30f, td = 1e30f;
        unsigned ps = 0, pd = 0;
        bool hs = static_scene_.intersect(origin, dir, ts, ps);
        bool hd = dynamic_scene_.intersect(origin, dir, td, pd);
        if (!hs && !hd) return false;
        if (hd && (!hs || td < ts) && pd < dynamic_tris_.size())
            { out.t = td; out.tri = &dynamic_tris_[pd]; }
        else if (hs && ps < static_tris_.size())
            { out.t = ts; out.tri = &static_tris_[ps]; }
        else return false;
        return true;
    }

    bool occluded(const vec3& origin, const vec3& dir, float max_t) const override {
        return static_scene_.occluded(origin, dir, max_t) ||
               dynamic_scene_.occluded(origin, dir, max_t);
    }

private:
    embree_ref::Scene     static_scene_, dynamic_scene_;
    std::vector<bvh::Tri> static_tris_,  dynamic_tris_;
    bool                  static_dirty_ = true;
};
#endif
