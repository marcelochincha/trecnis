#include <app/app_debug.hpp>
#include <app/app_state.hpp>

#include <cstring>
#include <cstdio>

#ifndef WITH_IMGUI

// ---- No-op build -----------------------------------------------------------
// Everything below is guarded, so the rest of app/ calls these unconditionally.
namespace app_debug {
void init(App*, SDL_Window*, SDL_Renderer*) {}
void handle_event(App*, const SDL_Event&) {}
void frame(App*, SDL_Renderer*) {}
void shutdown() {}
bool capture_mouse(App*)    { return false; }
bool capture_keyboard(App*) { return false; }
}  // namespace app_debug

#else

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <game/game.hpp>
#include <core/sr_profiler.hpp>
#include <render/raytrace/sr_ocl.hpp>

namespace {

const char* strategy_name_ui(bvh::BuildStrategy s) {
    return s == bvh::SAH ? "SAH" : s == bvh::Median ? "Median" : "Morton";
}

// The same backend label the old framebuffer HUD built: worker count for the
// CPU tracer, device name for OpenCL, "(unavailable)" otherwise.
void backend_label(const App* e, char* out, size_t n) {
    const char* bname = e->renderer.current_name();
    if (!e->renderer.current_available()) {
        snprintf(out, n, "%s (unavailable)", bname);
    } else if (std::strcmp(bname, "CPU SOFTWARE") == 0) {
        snprintf(out, n, "%s (%d workers)", bname, e->renderer.workers());
    } else if (ocl::available() && std::strcmp(bname, "OCL GPU") == 0) {
        snprintf(out, n, "%s: %s", bname, ocl::device_name());
    } else {
        snprintf(out, n, "%s", bname);
    }
}

// ---- HUD overlay -----------------------------------------------------------
// The old framebuffer text HUD, now a borderless translucent ImGui window.
// Visible with show_hud even while the debugger is closed; [H] toggles the
// compact form. NoInputs keeps it from stealing the mouse.
void draw_hud(App* e, float dt) {
    char backend[192];
    backend_label(e, backend, sizeof(backend));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_AlwaysAutoResize
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##hud", nullptr, flags);

    if (e->hud_simple) {
        ImGui::Text("FPS: %.0f  |  %s  |  BVH: %s  |  %s  |  [H] expand",
                    1.0f / dt, backend, e->opts.use_bvh ? "On" : "Off",
                    game_status(e->game));
    } else {
        ImGui::Text("=== SR-LEC ===");
        ImGui::Text("Frametime: %.2fms   FPS: %.2f", dt * 1000.0f, 1.0f / dt);
        ImGui::Separator();
        for (int i = 0; i < PROF_COUNT; ++i) {
            double v = g_prof.ms((ProfSection)i);
            if (v < 0.005) continue;
            ImGui::Text("%-6s %5.2f", Profiler::name((ProfSection)i), v);
        }
        ImGui::Text("%-6s %5.2f", "TOTAL", g_prof.total_ms());
        ImGui::Separator();
        const bvh::BVH& sb = e->scene.static_bvh();
        const bvh::BVH& db = e->scene.dynamic_bvh();
        ImGui::Text("Static BVH: %zu tris  %zu nodes  build %.2fms",
                    sb.triangle_count(), sb.node_count(), e->scene.static_build_ms());
        ImGui::Text("Dynamic BVH: %zu tris  %zu nodes",
                    db.triangle_count(), db.node_count());
        ImGui::Text("Accel: %s   Static: %s   Dyn: %s",
                    e->opts.use_bvh ? "BVH (fast)" : "BRUTE FORCE (slow)",
                    strategy_name_ui(e->scene.static_strategy()),
                    strategy_name_ui(e->opts.dynamic_strategy));
        ImGui::Text("Backend: %s", backend);
        ImGui::Text("Game: %s", game_status(e->game));
        ImGui::Text("[TAB/G] backend  [B] BVH  [V] vis  [N] normals  [L] sun");
        ImGui::Text("[M] debugger  [H] compact");
    }
    ImGui::End();
}

// ---- Render panel ----------------------------------------------------------
void draw_render_panel(App* e) {
    ImGui::Begin("Render");

    if (ImGui::BeginCombo("Backend", e->renderer.current_name())) {
        for (int i = 0; i < e->renderer.count(); ++i) {
            const bool avail = e->renderer.available(i);
            const bool sel   = i == e->renderer.index();
            if (!avail) ImGui::BeginDisabled();
            if (ImGui::Selectable(e->renderer.name(i), sel) && avail)
                e->renderer.select(i);
            if (sel) ImGui::SetItemDefaultFocus();
            if (!avail) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }

    ImGui::Checkbox("BVH (off = brute force)", &e->opts.use_bvh);
    ImGui::Checkbox("Show BVH", &e->show_bvh);
    if (e->show_bvh) ImGui::SliderInt("BVH depth", &e->bvh_debug_depth, 0, 40);
    ImGui::Checkbox("Normals", &e->show_normals);

    ImGui::Separator();
    ImGui::Checkbox("Reflections", &e->opts.reflections);
    ImGui::Checkbox("Sun", &e->opts.sun_enabled);
    ImGui::Checkbox("GI", &e->opts.gi_enabled);
    ImGui::SliderInt("Max bounces", &e->opts.max_bounces, 1, 3);
    ImGui::Checkbox("Skybox", &e->opts.skybox_enabled);
    ImGui::Checkbox("Emissive lights", &e->opts.emissive_enabled);

    ImGui::Separator();
    static const char* strat[] = { "SAH", "Median", "Morton" };
    int ss = (int)e->scene.static_strategy();
    if (ImGui::Combo("Static build", &ss, strat, 3)) {
        // Rebuild only the tree: the authored scenery is kept, so nothing has to
        // be re-created (same path the old menu used).
        e->scene.set_static_strategy((bvh::BuildStrategy)ss);
        e->scene.commit_static();
    }
    int ds = (int)e->opts.dynamic_strategy;
    if (ImGui::Combo("Dynamic build", &ds, strat, 3))
        e->opts.dynamic_strategy = (bvh::BuildStrategy)ds;

    ImGui::End();
}

// ---- Profiler panel --------------------------------------------------------
void draw_profiler_panel(App* e) {
    ImGui::Begin("Profiler");
    if (ImGui::BeginTable("prof", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("section");
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < PROF_COUNT; ++i) {
            double v = g_prof.ms((ProfSection)i);
            if (v < 0.005) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Profiler::name((ProfSection)i));
            ImGui::TableNextColumn(); ImGui::Text("%.2f", v);
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("TOTAL");
        ImGui::TableNextColumn(); ImGui::Text("%.2f", g_prof.total_ms());
        ImGui::EndTable();
    }
    ImGui::PlotLines("Frametime (ms)", e->frametime_ms, App::kFrametimeSamples,
                     e->frametime_head, nullptr, 0.0f, 40.0f, ImVec2(0, 80));
    ImGui::End();
}

// ---- Scene panel -----------------------------------------------------------
void draw_scene_panel(App* e) {
    ImGui::Begin("Scene");
    const CameraPose& c = e->world.camera;
    ImGui::Text("Camera pos   %.2f  %.2f  %.2f", c.pos.x, c.pos.y, c.pos.z);
    ImGui::Text("Camera euler %.2f  %.2f  %.2f", c.euler.x, c.euler.y, c.euler.z);
    ImGui::Text("Camera fov   %.1f", c.fov);
    ImGui::Separator();
    const bvh::BVH& sb = e->scene.static_bvh();
    const bvh::BVH& db = e->scene.dynamic_bvh();
    ImGui::Text("Static BVH : %zu tris  %zu nodes  build %.2fms",
                sb.triangle_count(), sb.node_count(), e->scene.static_build_ms());
    ImGui::Text("Dynamic BVH: %zu tris  %zu nodes",
                db.triangle_count(), db.node_count());
    ImGui::Separator();
    if (ImGui::Button("Dump frame")) e->dump_next = true;
    ImGui::End();
}

// ---- Game panel ------------------------------------------------------------
// Walks DebugVars, grouping by the group string. game/ filled the registry; it
// just holds pointers, so this is the only place that knows how to render one.
void draw_game_panel(App* e) {
    ImGui::Begin("Game");
    const char* group = nullptr;
    for (const DebugVar& v : e->debug_vars.vars) {
        if (!group || std::strcmp(group, v.group) != 0) {
            group = v.group;
            ImGui::SeparatorText(group);
        }
        switch (v.kind) {
            case DebugVar::Float:
                ImGui::SliderFloat(v.name, (float*)v.ptr, v.min, v.max);
                break;
            case DebugVar::Int:
                ImGui::SliderInt(v.name, (int*)v.ptr, (int)v.min, (int)v.max);
                break;
            case DebugVar::Bool:
                ImGui::Checkbox(v.name, (bool*)v.ptr);
                break;
        }
    }
    ImGui::End();
}

}  // namespace

namespace app_debug {

void init(App* e, SDL_Window* window, SDL_Renderer* renderer) {
    (void)e;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // do not litter the repo with imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
}

void handle_event(App* e, const SDL_Event& event) {
    (void)e;
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void frame(App* e, SDL_Renderer* renderer) {
    PROF_SCOPE(PROF_IMGUI);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (e->show_hud)
        draw_hud(e, e->frame_dt);

    if (e->show_debugger) {
        draw_render_panel(e);
        draw_profiler_panel(e);
        draw_scene_panel(e);
        draw_game_panel(e);
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void shutdown() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

bool capture_mouse(App* e) {
    if (e->show_debugger) return true;
    if (!ImGui::GetCurrentContext()) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool capture_keyboard(App* e) {
    if (e->show_debugger) return true;
    if (!ImGui::GetCurrentContext()) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

}  // namespace app_debug

#endif  // WITH_IMGUI
