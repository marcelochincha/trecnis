#pragma once
#include <array>
#include <vector>

#include <render/render_scene.hpp>
#include <core/sr_framebuffer.hpp>
#include <render/raytrace/cpu_tracer.hpp>

// A render backend produces a full frame from a RenderScene. Every mode is a
// backend behind this one interface: the raster fallback plus the three ray
// tracers (CPU SAH BVH, Embree, OpenCL). They are all compiled in and chosen at
// runtime, so there is a single dispatch point instead of a raster/raytrace
// branch and scattered enum / #ifdef checks.
struct IRenderBackend {
    virtual ~IRenderBackend() = default;
    virtual const char* name() const = 0;
    virtual bool available() const = 0;
    virtual void render(RenderScene& scene, framebuffer& fb) = 0;

    // Upload static scenery / skybox / area lights (GPU backend only). Default
    // no-op so CPU-side backends ignore it.
    virtual void upload_static(const bvh::BVH& /*static_bvh*/,
                               const std::array<texture, 6>& /*skybox*/,
                               const std::vector<bvh::Tri>& /*emissive*/) {}

    // The static scene was rebuilt (scene switch); invalidate cached trees.
    virtual void on_scene_changed() {}
};

// The single render entry point. Owns the shared CPU worker pool and every
// concrete backend (raster + ray tracers), and holds the runtime selection. The
// game app builds a RenderScene each frame and calls render(); nothing backend-
// or mode-specific leaks out.
class Renderer {
public:
    void init(int num_workers, int max_bounces, float ambient, float shadow_eps);
    void shutdown();

    // Push the static scene to backends that cache it (GPU). Call once the
    // initial scene geometry exists.
    void upload_static(const bvh::BVH& static_bvh,
                       const std::array<texture, 6>& skybox,
                       const std::vector<bvh::Tri>& emissive);

    // Re-push the scene after a scene switch: invalidate cached trees (Embree)
    // and re-upload static geometry / lights (OpenCL).
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

private:
    CpuTracer                    cpu_;
    std::vector<IRenderBackend*> backends_;   // owned
    int                          cur_ = 0;
};
