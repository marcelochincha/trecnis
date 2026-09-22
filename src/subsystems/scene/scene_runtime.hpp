#pragma once

#include <array>
#include <vector>

#include <subsystems/scene/world.hpp>
#include <render/render_scene.hpp>
#include <render/raytrace/bvh.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_texture.hpp>

class Renderer;

// Render quality / debug knobs: the app's business (options menu + hotkeys),
// never gameplay's. Kept out of World on purpose — whether the tracer does two
// bounces or which BVH build strategy is in use is not a fact about the world.
struct RenderOpts {
    bool               use_bvh          = true;
    bvh::BuildStrategy dynamic_strategy = bvh::SAH;   // static one lives in SceneRuntime

    bool  sun_enabled = true;
    bool  reflections = true;
    int   max_bounces = 3;
    bool  gi_enabled  = true;
    int   gi_samples  = 3;
    float gi_strength = 0.25f;

    bool  emissive_enabled = false;   // emissive-material area lights
    bool  skybox_enabled   = true;
    vec3  ambient_fallback = vec3(0.00f, 1.0f, 0.00f);  // used when no cubemap
    float ambient_strength = 0.25f;                        // scales either source
    vec3  bg_color         = vec3(0.00f, 1.0f, 0.00f);
};

// =============================================================================
// SceneRuntime — the layer that knows what a BVH is, so nothing above it has to.
//
// Owns both acceleration structures, the folded triangle lists, the raster
// mirror geometry and the skybox, and performs the one per-frame translation
// World -> RenderScene. Game logic talks to it only to author static scenery
// (add_static_box / add_static / commit_static); the app hands it a World each
// frame and gets back the view the render backends consume.
//
// Two trees, as before: STATIC scenery is built once (rebuilt only when the
// scenery or the build strategy changes) and DYNAMIC geometry is refolded and
// rebuilt every frame, which is what lets skinned and moving meshes work.
// =============================================================================
class SceneRuntime {
public:
    ~SceneRuntime();

    // ---- host wiring (called once, by app/) --------------------------------
    // The backends that cache geometry (Embree / OpenCL) must be re-pushed when
    // the static scene changes. Wiring the renderer in here once means callers
    // of commit_static() do not have to know those backends exist.
    void attach(Renderer& r) { renderer_ = &r; }

    // Camera setup that does not change per frame; the pose comes from World.
    void init_camera(float aspect, float near_plane, float far_plane);

    void               set_static_strategy(bvh::BuildStrategy s) { static_strategy_ = s; }
    bvh::BuildStrategy static_strategy() const { return static_strategy_; }

    // ---- static scenery (authored by game logic) ---------------------------
    void clear_static();

    // A static mesh entity: a table, a wall, a loaded model.
    void add_static(const Entity& e);

    // Axis-aligned box from two corners. Faces carry explicit outward normals,
    // which is why scenery authored this way is kept as triangles rather than
    // turned into a mesh: the winding alone would give the tracer inward
    // normals for the horizontal faces.
    void add_static_box(const vec3& lo, const vec3& hi, const vec3& albedo,
                        float roughness, float metallic = 0.0f);

    // Horizontal quad facing down, optionally emissive (an area light).
    void add_static_quad(const vec3& center, float hx, float hz,
                         const vec3& albedo, float roughness,
                         const vec3& emission = vec3(0.0f, 0.0f, 0.0f),
                         float metallic = 0.0f);

    // Build the static tree from everything added since clear_static(), collect
    // the emissive triangles as area lights, and re-push to the attached
    // renderer. Also the way to apply a new build strategy: set it, commit
    // again — the authored scenery is kept, so nothing has to be re-created.
    void commit_static();

    // ---- per frame (called by the host) ------------------------------------
    // Call after gameplay has moved everything (and after skinning). Refolds
    // every dynamic entity into the dynamic BVH and fills the render view.
    RenderScene build_frame(const World& w, const RenderOpts& o,
                            int width, int height);

    // ---- introspection: HUD counters and the debug overlays -----------------
    const bvh::BVH&              static_bvh()  const { return static_bvh_; }
    const bvh::BVH&              dynamic_bvh() const { return dynamic_bvh_; }
    const std::vector<bvh::Tri>& dynamic_tris() const { return dyn_tris_; }
    const std::vector<bvh::Tri>& emissive_tris() const { return emissive_; }
    double                       static_build_ms() const { return static_build_ms_; }

    std::array<texture, 6>&       skybox()       { return skybox_; }

    // Re-project the cubemap into the ambient SH. Call after filling skybox();
    // it walks every texel of all six faces, so it is a load-time cost, not a
    // per-frame one.
    void commit_sky();

    const std::array<texture, 6>& skybox() const { return skybox_; }

    // The camera the backends render from, posed from World::camera each frame.
    // Borrowed by the gizmo and the debug overlays, which need real matrices.
    camera&       cam()       { return cam_; }
    const camera& cam() const { return cam_; }

private:
    Renderer* renderer_ = nullptr;
    camera    cam_;

    std::vector<Entity>     static_ents_;     // mesh-backed scenery, folded at commit
    std::vector<bvh::Tri>   authored_tris_;   // procedurally authored scenery
    std::vector<mesh*>      owned_mirrors_;   // raster mirrors we allocated
    std::vector<RasterItem> static_raster_;   // raster draw list for the statics
    bvh::BVH                static_bvh_;
    std::vector<bvh::Tri>   emissive_;
    bvh::BuildStrategy      static_strategy_ = bvh::SAH;
    double                  static_build_ms_ = 0.0;

    bvh::BVH              dynamic_bvh_;
    std::vector<bvh::Tri> dyn_tris_;

    std::vector<RasterItem> raster_items_;
    std::array<texture, 6>  skybox_;
    SkyIrradiance           sky_irr_;
};
