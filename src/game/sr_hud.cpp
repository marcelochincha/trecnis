#include <game/sr_hud.hpp>
#include <game/sr_game.hpp>
#include <render/raytrace/sr_raytrace.hpp>
#include <algorithm>
#include <cstdio>

double tick_ms(uint64_t& since) {
    uint64_t now = SDL_GetPerformanceCounter();
    double ms = (now - since) * 1000.0 / SDL_GetPerformanceFrequency();
    since = now;
    return ms;
}

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

void draw_bvh_debug(Game* e) {
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
            draw_aabb_wire(e->fb, e->cam, n.bounds, col);
        }
    };
    draw_bvh(e->static_bvh);
    draw_bvh(e->dynamic_bvh);
}

void draw_normals_debug(Game* e) {
    const vec3  cam_pos   = e->cam._position;
    const float scale     = 0.3f;
    const float threshold = 0.5f;

    auto draw_vn = [&](const vec3& pos, const vec3& n) {
        if (dot(n, normalize(pos - cam_pos)) >= threshold) return;
        draw_gizmo_line(e->fb, e->cam, pos, pos + n * scale, 0xFF00FF88);
    };
    auto draw_tri_vn = [&](const bvh::Tri& t) {
        if (t.smooth) { draw_vn(t.v0,t.n0); draw_vn(t.v1,t.n1); draw_vn(t.v2,t.n2); }
        else          { draw_vn(t.v0,t.normal); draw_vn(t.v1,t.normal); draw_vn(t.v2,t.normal); }
    };
    for (const auto& t : e->rt_tris) draw_tri_vn(t);
    for (std::size_t i = 0; i < e->static_bvh.triangle_count(); ++i)
        draw_tri_vn(e->static_bvh.tri((int)i));
}

const int MENU_ITEMS = 7;

static const char* menu_label(int i) {
    static const char* L[MENU_ITEMS] = {
        "Backend","Acceleration","Show BVH","Reflections",
        "Ray bounces","Static build","Dyn build"
    };
    return L[i];
}

static const char* menu_value(const Game* e, int i) {
    switch (i) {
        case 0: return e->renderer.current_name();
        case 1: return e->use_bvh       ? "BVH"        : "Brute force";
        case 2: return e->show_bvh      ? "On"         : "Off";
        case 3: return e->reflections   ? "On"         : "Off";
        case 4: { static char b[8]; snprintf(b,sizeof(b),"%d",e->max_bounces); return b; }
        case 5: return e->build_strategy==bvh::SAH    ? "SAH"
                     : e->build_strategy==bvh::Median ? "Median" : "Morton";
        default: return e->dynamic_build_strategy==bvh::SAH    ? "SAH"
                      : e->dynamic_build_strategy==bvh::Median ? "Median" : "Morton";
    }
}

void menu_apply(Game* e, int dir) {
    switch (e->menu_cursor) {
        case 0: e->renderer.cycle(dir); break;
        case 1: e->use_bvh       = !e->use_bvh;       break;
        case 2: e->show_bvh      = !e->show_bvh;      break;
        case 3: e->reflections   = !e->reflections;   break;
        case 4: e->max_bounces = 1+((e->max_bounces-1+(dir<0?2:1))%3); break;
        case 5: {
            int s = (int)e->build_strategy;
            s = (s+(dir<0?2:1))%3;
            e->build_strategy = (bvh::BuildStrategy)s;
            game_rebuild_static(e);
            break;
        }
        case 6: {
            int s = (int)e->dynamic_build_strategy;
            s = (s+(dir<0?2:1))%3;
            e->dynamic_build_strategy = (bvh::BuildStrategy)s;
            break;
        }
    }
}

static void fill_rect_alpha(framebuffer& fb, int x0, int y0, int x1, int y1, uint32_t col) {
    float a = (float)((col>>24)&0xFF)/255.0f;
    int cr=(col>>16)&0xFF, cg=(col>>8)&0xFF, cb=col&0xFF;
    for (int y = std::max(0,y0); y < std::min(fb.height,y1); ++y)
        for (int x = std::max(0,x0); x < std::min(fb.width,x1); ++x) {
            uint32_t d = fb.colorBuffer[y*fb.width+x];
            int dr=(d>>16)&0xFF, dg=(d>>8)&0xFF, db=d&0xFF;
            int r=(int)(cr*a+dr*(1.0f-a)), g=(int)(cg*a+dg*(1.0f-a)), b=(int)(cb*a+db*(1.0f-a));
            fb.colorBuffer[y*fb.width+x] = 0xFF000000u|(r<<16)|(g<<8)|b;
        }
}

void draw_menu(Game* e) {
    const int lh=18, pad=14, w=312;
    const int h = pad+24+pad+lh*MENU_ITEMS+pad+18;
    const int x = e->fb.width-w-12, y0=18;
    fill_rect_alpha(e->fb, x, y0, x+w, y0+h, 0xE80B0E18);
    fill_rect_alpha(e->fb, x, y0, x+w, y0+24, 0xFF1B6FB5);
    draw_text(e->fb, x+10, y0+7, "OPTIONS   [M] close", 0xFFFFFFFF);
    for (int i = 0; i < MENU_ITEMS; ++i) {
        int ry = y0+24+pad+i*lh;
        if (i == e->menu_cursor)
            fill_rect_alpha(e->fb, x+6, ry-2, x+w-6, ry+lh-3, 0x603A66B0);
        char line[96];
        snprintf(line, sizeof(line), "%-13s %s", menu_label(i), menu_value(e,i));
        draw_text(e->fb, x+14, ry, line, i==e->menu_cursor ? 0xFFFFE27A : 0xFFE8E8E8);
    }
    draw_text(e->fb, x+10, y0+h-16, "^/v select   </>  change", 0xFFA8B0BC);
}
