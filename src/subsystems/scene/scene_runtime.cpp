#include <subsystems/scene/scene_runtime.hpp>

#include <render/renderer.hpp>
#include <render/raytrace/sr_raytrace.hpp>   // pack()
#include <core/sr_profiler.hpp>

#include <SDL2/SDL.h>
#include <cstdio>

// Fold a world-space mesh into flat ray-trace triangles with an explicit
// material (or its texture, if any). Face normals are derived from the winding.
static void fold_mesh(std::vector<bvh::Tri>& out, const mesh& m,
                      const vec3& albedo, float roughness, float metallic,
                      const vec3& emission) {
    mat4 model = m.modelMatrix();
    for (const triangle& tri : m.faces) {
        const vertex& a = m.vertices[tri.v0];
        const vertex& b = m.vertices[tri.v1];
        const vertex& c = m.vertices[tri.v2];
        vec3 v0 = vec3(model * a.p), v1 = vec3(model * b.p), v2 = vec3(model * c.p);
        vec3 n  = normalize(cross(v1 - v0, v2 - v0));
        bvh::Tri t{ v0, v1, v2, n, albedo, roughness, metallic };
        t.emission = emission;
        if (m.tex) { t.tex = m.tex; t.uv0 = a.t; t.uv1 = b.t; t.uv2 = c.t; }
        out.push_back(t);
    }
}

// Mirror a triangle list into a raster mesh, so procedurally authored scenery
// (built as triangles, not as a model) still draws in the raster backend. The
// raster path is flat-shaded from RasterItem::color, so the mirror does not
// need normals — which is just as well, since the winding disagrees with the
// authored face normals for the horizontal faces of a box.
static mesh* make_raster_mesh(const std::vector<bvh::Tri>& tris) {
    mesh* m = new mesh;
    m->vertices.reserve(tris.size() * 3);
    m->faces.reserve(tris.size());
    for (const auto& t : tris) {
        uint32_t b = (uint32_t)m->vertices.size();
        m->vertices.push_back({ t.v0, {0.0f, 0.0f} });
        m->vertices.push_back({ t.v1, {0.0f, 0.0f} });
        m->vertices.push_back({ t.v2, {0.0f, 0.0f} });
        m->faces.push_back({ b, b + 1, b + 2 });
    }
    m->_modelMatrixDirty = true;
    return m;
}

SceneRuntime::~SceneRuntime() {
    for (mesh* m : owned_mirrors_) delete m;
}

void SceneRuntime::init_camera(float aspect, float near_plane, float far_plane) {
    cam_ = camera(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 0.0f),
                  90.0f, aspect, near_plane, far_plane);
}

void SceneRuntime::clear_static() {
    for (mesh* m : owned_mirrors_) delete m;
    owned_mirrors_.clear();
    static_ents_.clear();
    authored_tris_.clear();
    static_raster_.clear();
}

void SceneRuntime::add_static(const Entity& e) {
    if (!e.geo) return;
    static_ents_.push_back(e);
    static_raster_.push_back({ e.geo, pack(e.albedo), e.shadow });
}

// Triangles authored here go into the tree verbatim — they carry per-face
// normals, per-triangle materials and emission that a mesh cannot express. The
// raster backend gets a mirror mesh instead, owned here and freed with the
// static scene.
static void push_authored(std::vector<bvh::Tri>& authored,
                          std::vector<mesh*>& mirrors,
                          std::vector<RasterItem>& raster,
                          const std::vector<bvh::Tri>& tris) {
    if (tris.empty()) return;
    authored.insert(authored.end(), tris.begin(), tris.end());
    mesh* mirror = make_raster_mesh(tris);
    mirrors.push_back(mirror);
    raster.push_back({ mirror, pack(tris[0].albedo), false });
}

void SceneRuntime::add_static_box(const vec3& lo, const vec3& hi,
                                  const vec3& albedo, float roughness, float metallic) {
    vec3 v[8] = {
        {lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},
        {lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}
    };
    std::vector<bvh::Tri> tris;
    tris.reserve(12);
    auto face = [&](int a, int b, int c, int d, const vec3& n) {
        tris.push_back({ v[a],v[b],v[c], n, albedo, roughness, metallic });
        tris.push_back({ v[a],v[c],v[d], n, albedo, roughness, metallic });
    };
    face(4,5,6,7,{ 0, 1, 0}); face(3,2,1,0,{ 0,-1, 0});
    face(0,3,7,4,{-1, 0, 0}); face(1,5,6,2,{ 1, 0, 0});
    face(0,1,5,4,{ 0, 0,-1}); face(3,7,6,2,{ 0, 0, 1});

    push_authored(authored_tris_, owned_mirrors_, static_raster_, tris);
}

void SceneRuntime::add_static_quad(const vec3& center, float hx, float hz,
                                   const vec3& albedo, float roughness,
                                   const vec3& emission, float metallic) {
    vec3 a(center.x-hx, center.y, center.z-hz), b(center.x+hx, center.y, center.z-hz);
    vec3 c(center.x+hx, center.y, center.z+hz), d(center.x-hx, center.y, center.z+hz);
    vec3 n(0.0f, -1.0f, 0.0f);

    std::vector<bvh::Tri> tris;
    tris.reserve(2);
    bvh::Tri t0{ a, b, c, n, albedo, roughness, metallic }; t0.emission = emission;
    bvh::Tri t1{ a, c, d, n, albedo, roughness, metallic }; t1.emission = emission;
    tris.push_back(t0); tris.push_back(t1);

    push_authored(authored_tris_, owned_mirrors_, static_raster_, tris);
}

void SceneRuntime::commit_sky() {
    if (project_sky_irradiance(skybox_, sky_irr_)) {
        struct { const char* n; vec3 d; } probes[] = {
            { "up   +Y", vec3(0,1,0) }, { "down -Y", vec3(0,-1,0) },
            { "side +X", vec3(1,0,0) }, { "side +Z", vec3(0,0,1) },
        };
        for (auto& p : probes) {
            vec3 e = sky_irr_.eval(p.d);
            std::printf("Sky irradiance %s -> %.3f %.3f %.3f\n", p.n, e.x, e.y, e.z);
        }
    } else {
        std::printf("Sky irradiance: no cubemap, using the flat fallback\n");
    }
}

void SceneRuntime::commit_static() {
    uint64_t t0 = SDL_GetPerformanceCounter();

    std::vector<bvh::Tri> tris = authored_tris_;
    for (const Entity& e : static_ents_)
        fold_mesh(tris, *e.geo, e.albedo, e.rough, e.metallic, e.emission);

    static_bvh_.build(std::move(tris), static_strategy_);
    static_build_ms_ = (SDL_GetPerformanceCounter() - t0) * 1000.0
                     / SDL_GetPerformanceFrequency();

    emissive_.clear();
    for (int i = 0; i < (int)static_bvh_.triangle_count(); ++i) {
        const bvh::Tri& t = static_bvh_.tri(i);
        if (t.emission.x + t.emission.y + t.emission.z > 0.0f)
            emissive_.push_back(t);
    }

    // Invalidate cached backend trees and re-push static geometry / lights.
    // Skipped until the host has attached a renderer (initial scene setup runs
    // before Renderer::init).
    if (renderer_) renderer_->reload_scene(static_bvh_, skybox_, emissive_);
}

RenderScene SceneRuntime::build_frame(const World& w, const RenderOpts& o,
                                      int width, int height) {
    cam_.setPosition(w.camera.pos);
    cam_.setRotation(w.camera.euler);
    cam_.setFov(w.camera.fov);

    // Refold every dynamic entity and rebuild the dynamic tree. This happens
    // every frame regardless of backend, so the raster draw and the ray tracers
    // all see the same deformed geometry — no per-mode special cases.
    {
        PROF_SCOPE(PROF_BVH_DYN);
        dyn_tris_.clear();
        for (const Entity& e : w.dynamic)
            if (e.geo) fold_mesh(dyn_tris_, *e.geo, e.albedo, e.rough, e.metallic, e.emission);
        dynamic_bvh_.build(dyn_tris_, o.dynamic_strategy);
    }

    // Raster draw list: static scenery first, then the movers.
    raster_items_.clear();
    raster_items_.reserve(static_raster_.size() + w.dynamic.size());
    raster_items_.insert(raster_items_.end(), static_raster_.begin(), static_raster_.end());
    for (const Entity& e : w.dynamic)
        if (e.geo) raster_items_.push_back({ e.geo, pack(e.albedo), e.shadow });

    RenderScene s;
    s.cam    = &cam_;
    s.width  = width;
    s.height = height;

    s.static_bvh       = &static_bvh_;
    s.dynamic_bvh      = &dynamic_bvh_;
    s.brute_tris       = &dyn_tris_;
    s.use_bvh          = o.use_bvh;
    s.static_strategy  = static_strategy_;
    s.dynamic_strategy = o.dynamic_strategy;

    s.raster_items = &raster_items_;

    s.emissive       = o.emissive_enabled ? &emissive_ : nullptr;
    s.point_lights   = w.lights.empty() ? nullptr : &w.lights;
    s.skybox         = &skybox_;
    s.skybox_enabled = o.skybox_enabled;
    s.sky_irr          = &sky_irr_;
    s.ambient_fallback = o.ambient_fallback;
    s.ambient_strength = o.ambient_strength;
    s.bg_color       = o.bg_color;

    s.sun_enabled = o.sun_enabled;
    s.reflections = o.reflections;
    s.max_bounces = o.max_bounces;
    s.gi_enabled  = o.gi_enabled;
    s.gi_samples  = o.gi_samples;
    s.gi_strength = o.gi_strength;
    return s;
}
