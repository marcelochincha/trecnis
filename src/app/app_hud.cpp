#include <app/app_hud.hpp>
#include <app/app.hpp>
#include <render/raster/sr_raster.hpp>   // draw_gizmo_line
#include <render/raytrace/sr_raytrace.hpp>

// Debug overlays drawn straight into the framebuffer. They sit below the dump
// capture point, so they are only ever seen interactively, never in a dump.

static void draw_aabb_wire(framebuffer& fb, const camera& cam, const AABB& b, uint32_t color) {
    vec3 c[8] = {
        {b.min.x,b.min.y,b.min.z},{b.max.x,b.min.y,b.min.z},
        {b.max.x,b.min.y,b.max.z},{b.min.x,b.min.y,b.max.z},
        {b.min.x,b.max.y,b.min.z},{b.max.x,b.max.y,b.min.z},
        {b.max.x,b.max.y,b.max.z},{b.min.x,b.max.y,b.max.z},
    };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    for (auto& ed : edges) draw_gizmo_line(fb, cam, c[ed[0]], c[ed[1]], color);
}

void draw_bvh_debug(App* e) {
    static std::vector<bvh::BVH::DebugNode> nodes;
    static const uint32_t palette[] = {
        0xFFFF4040,0xFFFFA040,0xFFFFFF40,0xFF40FF40,
        0xFF40FFFF,0xFF4080FF,0xFFC040FF,0xFFFF40C0,
    };
    // Only draw the top levels of the hierarchy. A production scene BVH has
    // 100k+ nodes; drawing all of them is ~2M lines/frame (~50ms) AND an
    // unreadable box soup that made toggling the wireframe feel like a freeze.
    // Capping the depth keeps it to a few thousand boxes: cheap and legible.
    const int max_depth = e->bvh_debug_depth;
    auto draw_bvh = [&](const bvh::BVH& b) {
        b.debug_nodes(nodes);
        for (const auto& n : nodes) {
            if (n.depth > max_depth) continue;
            uint32_t col = palette[n.depth % (sizeof(palette)/sizeof(palette[0]))];
            draw_aabb_wire(e->fb, e->scene.cam(), n.bounds, col);
        }
    };
    draw_bvh(e->scene.static_bvh());
    draw_bvh(e->scene.dynamic_bvh());
}

void draw_normals_debug(App* e) {
    const vec3  cam_pos   = e->scene.cam()._position;
    const float scale     = 0.3f;
    const float threshold = 0.5f;

    auto draw_vn = [&](const vec3& pos, const vec3& n) {
        if (dot(n, normalize(pos - cam_pos)) >= threshold) return;
        draw_gizmo_line(e->fb, e->scene.cam(), pos, pos + n * scale, 0xFF00FF88);
    };
    auto draw_tri_vn = [&](const bvh::Tri& t) {
        if (t.smooth) { draw_vn(t.v0,t.n0); draw_vn(t.v1,t.n1); draw_vn(t.v2,t.n2); }
        else          { draw_vn(t.v0,t.normal); draw_vn(t.v1,t.normal); draw_vn(t.v2,t.normal); }
    };
    for (const auto& t : e->scene.dynamic_tris()) draw_tri_vn(t);
    for (std::size_t i = 0; i < e->scene.static_bvh().triangle_count(); ++i)
        draw_tri_vn(e->scene.static_bvh().tri((int)i));
}
