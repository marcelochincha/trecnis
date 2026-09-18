#pragma once
#include <array>
#include <vector>

#include <render/render_scene.hpp>
#include <core/sr_framebuffer.hpp>
#include <render/raytrace/cpu_tracer.hpp>






struct IRenderBackend {
    virtual ~IRenderBackend() = default;
    virtual const char* name() const = 0;
    virtual bool available() const = 0;
    virtual void render(RenderScene& scene, framebuffer& fb) = 0;



    virtual void upload_static(const bvh::BVH& ,
                               const std::array<texture, 6>& ,
                               const std::vector<bvh::Tri>& ) {}


    virtual void on_scene_changed() {}
};





class Renderer {
public:
    void init(int num_workers, int max_bounces, float ambient, float shadow_eps);
    void shutdown();



    void upload_static(const bvh::BVH& static_bvh,
                       const std::array<texture, 6>& skybox,
                       const std::vector<bvh::Tri>& emissive);



    void reload_scene(const bvh::BVH& static_bvh,
                      const std::array<texture, 6>& skybox,
                      const std::vector<bvh::Tri>& emissive);

    void render(RenderScene& scene, framebuffer& fb);

    int         workers() const { return cpu_.workers(); }
    int         count() const   { return (int)backends_.size(); }
    int         index() const   { return cur_; }
    const char* name(int i) const      { return backends_[i]->name(); }
    bool        available(int i) const { return backends_[i]->available(); }
    const char* current_name() const   { return backends_[cur_]->name(); }
    bool        current_available() const { return backends_[cur_]->available(); }
    void        cycle(int dir);



    bool        select(int i) {
        if (i >= 0 && i < (int)backends_.size() && backends_[i]->available()) { cur_ = i; return true; }
        return false;
    }

private:
    CpuTracer                    cpu_;
    std::vector<IRenderBackend*> backends_;
    int                          cur_ = 0;
};
