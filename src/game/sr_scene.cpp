#include <game/sr_scene.hpp>
#include <render/raytrace/sr_raytrace.hpp>
#include <engine/anim/skinned_mesh.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

vec3 color_from_hash(const std::string& name) {
    uint32_t h = std::hash<std::string>{}(name);
    return vec3(((h >> 16) & 0xFF) / 255.f, ((h >> 8) & 0xFF) / 255.f, (h & 0xFF) / 255.f);
}

// Axis-aligned box from two corners.
static void add_box(std::vector<bvh::Tri>& out,
                    const vec3& lo, const vec3& hi,
                    const vec3& albedo, float roughness,
                    float metallic = 0.0f, float ior = 1.5f)
{
    vec3 v[8] = {
        {lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},
        {lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}
    };
    auto face = [&](int a, int b, int c, int d, const vec3& n) {
        out.push_back({ v[a],v[b],v[c], n, albedo, roughness, metallic, ior });
        out.push_back({ v[a],v[c],v[d], n, albedo, roughness, metallic, ior });
    };
    face(4,5,6,7,{ 0, 1, 0}); face(3,2,1,0,{ 0,-1, 0});
    face(0,3,7,4,{-1, 0, 0}); face(1,5,6,2,{ 1, 0, 0});
    face(0,1,5,4,{ 0, 0,-1}); face(3,7,6,2,{ 0, 0, 1});
}

// A horizontal emissive quad (area light) at height center.y, facing down.
static void add_emissive_quad(std::vector<bvh::Tri>& out, const vec3& center,
                              float hx, float hz, const vec3& emission)
{
    vec3 a(center.x-hx, center.y, center.z-hz), b(center.x+hx, center.y, center.z-hz);
    vec3 c(center.x+hx, center.y, center.z+hz), d(center.x-hx, center.y, center.z+hz);
    vec3 n(0.0f, -1.0f, 0.0f);
    bvh::Tri t0{ a, b, c, n, vec3(0,0,0), 1.0f }; t0.emission = emission;
    bvh::Tri t1{ a, c, d, n, vec3(0,0,0), 1.0f }; t1.emission = emission;
    out.push_back(t0); out.push_back(t1);
}

// Place the free camera at a fixed starting pose looking at `target`.
static void set_start_view(Game* e, const vec3& pos, const vec3& target) {
    vec3 d = normalize(target - pos);
    float pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    float yaw   = std::atan2(d.x, -d.z);
    e->position = pos;
    e->pitch    = pitch;
    e->yaw      = yaw;
    e->cam.setPosition(pos);
    e->cam.setRotation(vec3(pitch, yaw, 0.0f));
}

// Mirror the static triangles into a raster mesh (used by the raster fallback).
static void rebuild_field_mesh(Game* e, const std::vector<bvh::Tri>& tris) {
    delete e->field_mesh;
    e->field_mesh = new mesh;
    e->field_mesh->vertices.reserve(tris.size() * 3);
    e->field_mesh->faces.reserve(tris.size());
    for (const auto& t : tris) {
        uint32_t b = (uint32_t)e->field_mesh->vertices.size();
        e->field_mesh->vertices.push_back({ t.v0, {0.0f, 0.0f} });
        e->field_mesh->vertices.push_back({ t.v1, {0.0f, 0.0f} });
        e->field_mesh->vertices.push_back({ t.v2, {0.0f, 0.0f} });
        e->field_mesh->faces.push_back({ b, b+1, b+2 });
    }
    e->field_mesh->_modelMatrixDirty = true;
}

// Fold every registered mesh (per-frame deformed by skinning) into the flat
// triangle list and rebuild the DYNAMIC BVH. Called every frame.
void build_scene_tris(Game* e) {
    e->rt_tris.clear();
    for (auto& [name, mp] : e->meshes) {
        const mesh& m = *mp;
        mat4 model = m.modelMatrix();
        vec3 albedo = color_from_hash(name);
        float rough = 1.0f - (float)e->reflectivity_map[name];
        for (const triangle& tri : m.faces) {
            const vertex& a = m.vertices[tri.v0];
            const vertex& b = m.vertices[tri.v1];
            const vertex& c = m.vertices[tri.v2];
            vec3 v0 = vec3(model * a.p);
            vec3 v1 = vec3(model * b.p);
            vec3 v2 = vec3(model * c.p);
            vec3 n  = normalize(cross(v1-v0, v2-v0));
            bvh::Tri t{ v0, v1, v2, n, albedo, rough };
            if (m.tex) { t.tex = m.tex; t.uv0 = a.t; t.uv1 = b.t; t.uv2 = c.t; }
            e->rt_tris.push_back(t);
        }
    }
    e->dynamic_bvh.build(e->rt_tris, e->dynamic_build_strategy);
}

// The single scene: a floor, an overhead area light, and a procedurally
// generated skinned character that animates into the dynamic BVH each frame.
static void build_default(Game* e) {
    e->meshes.erase("character");
    delete e->skin_mesh;
    e->skin_mesh = nullptr;

    std::vector<bvh::Tri> tris;
    add_box(tris, vec3(-8.f, -0.02f, -8.f), vec3(8.f, 0.f, 8.f),
            vec3(0.55f, 0.55f, 0.58f), 0.85f);
    add_emissive_quad(tris, vec3(0.0f, 5.0f, 0.0f), 2.0f, 2.0f, vec3(6.f, 6.f, 6.f));

    // Procedural skinned character standing on the floor. Its per-frame deformed
    // triangles feed the dynamic BVH (skinning + dynamic BVH coexist).
    e->skin = SkinnedMesh{};
    mesh* cm = new mesh;
    build_procedural_character(*cm, e->skin);
    cm->setPosition(vec3(0.0f, 0.0f, 0.0f));
    if (e->skin.valid()) e->skin.apply(*cm, 0.0f);
    e->skin_mesh = cm;
    e->meshes["character"] = cm;
    e->anim_time = 0.0f;

    set_start_view(e, vec3(0.0f, 1.6f, 6.0f), vec3(0.0f, 1.2f, 0.0f));
    rebuild_field_mesh(e, tris);
    e->static_bvh.build(std::move(tris), e->build_strategy);
    std::cout << "Scene: " << e->static_bvh.triangle_count()
              << " static tris, procedural character ("
              << e->skin.bones << " bones, " << e->skin.frames << " frames)\n";
}

void rebuild_field(Game* e) {
    uint64_t t0 = SDL_GetPerformanceCounter();

    build_default(e);
    e->skybox_enabled  = true;
    e->static_build_ms = (SDL_GetPerformanceCounter()-t0)*1000.0/SDL_GetPerformanceFrequency();

    e->emissive_tris.clear();
    for (int i = 0; i < (int)e->static_bvh.triangle_count(); ++i) {
        const bvh::Tri& t = e->static_bvh.tri(i);
        if (t.emission.x + t.emission.y + t.emission.z > 0.0f)
            e->emissive_tris.push_back(t);
    }

    // Invalidate cached backend trees and re-push static geometry / lights. A
    // no-op during initial setup (renderer not yet initialized).
    e->renderer.reload_scene(e->static_bvh, e->skybox_faces, e->emissive_tris);
}
