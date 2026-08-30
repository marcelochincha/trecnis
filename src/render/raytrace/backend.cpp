#include <render/raytrace/backend.hpp>
#include <render/raytrace/bvh_accel.hpp>
#include <render/raytrace/sr_raytrace.hpp>   // SUN_DIR, camera math via render_scene
#include <render/raytrace/sr_ocl.hpp>
#include <cmath>

#ifdef WITH_EMBREE
#include <render/raytrace/embree_accel.hpp>
#endif

// ---- CPU SAH BVH backend ----------------------------------------------------

namespace {

class CpuBvhBackend : public IRenderBackend {
public:
    explicit CpuBvhBackend(CpuTracer& tracer) : tracer_(tracer) {}
    const char* name() const override { return "CPU SOFTWARE"; }
    bool available() const override { return true; }

    void render(RenderScene& s, framebuffer& fb) override {
        accel_.static_bvh  = s.static_bvh;
        accel_.dynamic_bvh = s.dynamic_bvh;
        accel_.brute_tris  = s.brute_tris;
        accel_.use_bvh     = s.use_bvh;
        s.accel = &accel_;
        tracer_.render(s, fb);
    }

private:
    CpuTracer& tracer_;
    BvhAccel   accel_;
};

#ifdef WITH_EMBREE
class EmbreeBackend : public IRenderBackend {
public:
    explicit EmbreeBackend(CpuTracer& tracer) : tracer_(tracer) {}
    const char* name() const override { return "EMBREE"; }
    bool available() const override { return true; }

    void render(RenderScene& s, framebuffer& fb) override {
        accel_.sync(*s.static_bvh, s.static_strategy, *s.brute_tris, s.dynamic_strategy);
        s.accel = &accel_;
        tracer_.render(s, fb);
    }

    void on_scene_changed() override { accel_.mark_static_dirty(); }

private:
    CpuTracer&  tracer_;
    EmbreeAccel accel_;
};
#endif

// ---- OpenCL GPU backend -----------------------------------------------------

class OpenClBackend : public IRenderBackend {
public:
    OpenClBackend(int max_bounces, float ambient, float shadow_eps) {
        ocl::init(max_bounces, ambient, shadow_eps);
    }
    const char* name() const override { return "OCL GPU"; }
    bool available() const override { return ocl::available(); }

    void upload_static(const bvh::BVH& static_bvh,
                       const std::array<texture, 6>& skybox,
                       const std::vector<bvh::Tri>& emissive) override {
        if (!ocl::available()) return;
        std::vector<float> nb, tf; std::vector<int> nl;
        if (!static_bvh.empty()) {
            static_bvh.flatten(nb, nl, tf);
            ocl::set_room(nb.data(), nl.data(), tf.data(),
                          (int)static_bvh.node_count(), (int)static_bvh.triangle_count());
        } else {
            ocl::set_room(nullptr, nullptr, nullptr, 0, 0);
        }
        std::vector<uint32_t> px; int off[6], w[6], h[6]; int cur = 0;
        for (int i = 0; i < 6; ++i) {
            const texture& t = skybox[i];
            w[i] = t.width; h[i] = t.height; off[i] = cur;
            int n = t.width * t.height;
            for (int k = 0; k < n; ++k) px.push_back(t.data ? t.data[k] : 0u);
            cur += n;
        }
        ocl::set_sky(px.data(), (int)px.size(), off, w, h);
        upload_emissive(emissive);
    }

    void render(RenderScene& s, framebuffer& fb) override {
        std::vector<float> nb, tf; std::vector<int> nl;
        s.dynamic_bvh->flatten(nb, nl, tf);
        ocl::set_dynamic(nb.data(), nl.data(), tf.data(),
                         (int)s.dynamic_bvh->node_count(), (int)s.dynamic_bvh->triangle_count());

        mat4 R = s.cam->rotation();
        vec4 cx = R * vec4(1, 0, 0, 0), cy = R * vec4(0, 1, 0, 0), cz = R * vec4(0, 0, 1, 0);
        float tb = tanf(to_radians(s.cam->_fov) * 0.5f), ta = tb / s.cam->_aspectRatio;
        ocl::render(s.cam->_position.x, s.cam->_position.y, s.cam->_position.z,
                    cx.x, cx.y, cx.z, cy.x, cy.y, cy.z, cz.x, cz.y, cz.z,
                    tb, ta, SUN_DIR.x, SUN_DIR.y, SUN_DIR.z,
                    s.spp, s.skybox_enabled ? 1 : 0, s.reflections ? 1 : 0,
                    fb.width, fb.height, fb.colorBuffer);
    }

private:
    // Pack area-light triangles into the flat 32-float/tri layout the kernel
    // expects for next-event estimation (only v0/v1/v2, face normal, emission).
    static void upload_emissive(const std::vector<bvh::Tri>& tris) {
        if (!ocl::available()) return;
        std::vector<float> tf(tris.size() * 32, 0.0f);
        for (std::size_t i = 0; i < tris.size(); ++i) {
            const bvh::Tri& t = tris[i];
            float* p = &tf[i * 32];
            p[0]=t.v0.x;      p[1]=t.v0.y;      p[2]=t.v0.z;
            p[3]=t.v1.x;      p[4]=t.v1.y;      p[5]=t.v1.z;
            p[6]=t.v2.x;      p[7]=t.v2.y;      p[8]=t.v2.z;
            p[9]=t.normal.x;  p[10]=t.normal.y; p[11]=t.normal.z;
            p[29]=t.emission.x; p[30]=t.emission.y; p[31]=t.emission.z;
        }
        ocl::set_emissive(tf.data(), (int)tris.size());
    }
};

} // namespace

// ---- RaytraceRenderer -------------------------------------------------------

void RaytraceRenderer::init(int num_workers, int max_bounces, float ambient, float shadow_eps) {
    cpu_.start(num_workers);
    backends_.push_back(new CpuBvhBackend(cpu_));
#ifdef WITH_EMBREE
    backends_.push_back(new EmbreeBackend(cpu_));
#endif
    backends_.push_back(new OpenClBackend(max_bounces, ambient, shadow_eps));
    cur_ = 0;
}

void RaytraceRenderer::shutdown() {
    for (IRenderBackend* b : backends_) delete b;
    backends_.clear();
    cpu_.stop();
}

void RaytraceRenderer::upload_static(const bvh::BVH& static_bvh,
                                     const std::array<texture, 6>& skybox,
                                     const std::vector<bvh::Tri>& emissive) {
    for (IRenderBackend* b : backends_) b->upload_static(static_bvh, skybox, emissive);
}

void RaytraceRenderer::reload_scene(const bvh::BVH& static_bvh,
                                    const std::array<texture, 6>& skybox,
                                    const std::vector<bvh::Tri>& emissive) {
    for (IRenderBackend* b : backends_) {
        b->on_scene_changed();
        b->upload_static(static_bvh, skybox, emissive);
    }
}

void RaytraceRenderer::render(RenderScene& scene, framebuffer& fb) {
    backends_[cur_]->render(scene, fb);
}

void RaytraceRenderer::cycle(int dir) {
    int n = (int)backends_.size();
    for (int step = 0; step < n; ++step) {
        cur_ = (cur_ + (dir < 0 ? n - 1 : 1)) % n;
        if (backends_[cur_]->available()) return;
    }
    cur_ = 0;  // CPU is always available
}
