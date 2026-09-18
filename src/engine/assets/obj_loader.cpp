#include <engine/assets/obj_loader.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

const texture* obj_cached_texture(const std::string& path) {
    static std::map<std::string, texture*> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    texture* t = new texture();
    if (!load_png_texture(path, *t)) { delete t; cache[path] = nullptr; return nullptr; }
    cache[path] = t;
    return t;
}

bool load_obj_mesh(const std::string& path, mesh& out, float scale) {
    std::ifstream f(path);
    if (!f) return false;

    std::string dir(path);
    auto sl = dir.find_last_of("/\\");
    dir = (sl != std::string::npos) ? dir.substr(0, sl + 1) : "";

    std::vector<vec3> verts;
    std::vector<vec2> uvs;
    std::map<std::pair<int,int>, uint32_t> dedup;
    out.vertices.clear();
    out.faces.clear();
    out.src_vertex.clear();
    out.tex = nullptr;


    auto load_mtl = [&](const std::string& fname) {
        std::ifstream mf(dir + fname);
        if (!mf) return;
        std::string line;
        while (std::getline(mf, line)) {
            std::istringstream ss(line);
            std::string key; ss >> key;
            if (key == "map_Kd") {
                std::string fn; std::getline(ss, fn);
                size_t a = fn.find_first_not_of(" \t");
                if (a != std::string::npos && !out.tex)
                    out.tex = obj_cached_texture(dir + fn.substr(a));
            }
        }
    };

    auto emit = [&](int vi, int ti) -> uint32_t {
        auto key = std::make_pair(vi, ti);
        auto it = dedup.find(key);
        if (it != dedup.end()) return it->second;
        uint32_t idx = (uint32_t)out.vertices.size();
        vertex ve;
        ve.p = (vi >= 0 && vi < (int)verts.size()) ? verts[vi] * scale : vec3(0, 0, 0);
        ve.t = (ti >= 0 && ti < (int)uvs.size())    ? uvs[ti]           : vec2(0, 0);
        out.vertices.push_back(ve);
        out.src_vertex.push_back((uint32_t)vi);
        dedup[key] = idx;
        return idx;
    };

    std::string line;
    while (std::getline(f, line)) {
        const char* s = line.c_str();
        while (*s == ' ' || *s == '\t') ++s;
        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            char* e; const char* p = s + 2;
            float x = std::strtof(p, &e); p = e;
            float y = std::strtof(p, &e); p = e;
            float z = std::strtof(p, &e);
            verts.push_back(vec3(x, y, z));
        } else if (s[0] == 'v' && s[1] == 't' && (s[2] == ' ' || s[2] == '\t')) {
            char* e; const char* p = s + 3;
            float u = std::strtof(p, &e); p = e;
            float v = std::strtof(p, &e);
            uvs.push_back(vec2(u, v));
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            const int nv = (int)verts.size(), nt = (int)uvs.size();
            std::vector<uint32_t> poly;
            const char* p = s + 2;
            while (*p) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p) break;
                char* e;
                long vi = std::strtol(p, &e, 10); p = e;
                long ti = 0; bool hasT = false;
                if (*p == '/') {
                    ++p;
                    if (*p != '/') { ti = std::strtol(p, &e, 10); p = e; hasT = true; }
                    if (*p == '/') { ++p; std::strtol(p, &e, 10); p = e; }
                }
                if (vi == 0) continue;
                int vIdx = (vi > 0) ? (int)vi - 1 : nv + (int)vi;
                int tIdx = (hasT && ti != 0) ? ((ti > 0) ? (int)ti - 1 : nt + (int)ti) : -1;
                poly.push_back(emit(vIdx, tIdx));
            }
            for (std::size_t i = 1; i + 1 < poly.size(); ++i)
                out.faces.push_back({ poly[0], poly[i], poly[i + 1] });
        } else if (s[0] == 'm') {
            std::istringstream ss(s);
            std::string tag, fn; ss >> tag >> fn;
            if (tag == "mtllib") load_mtl(fn);
        }
    }
    out._modelMatrixDirty = true;
    return true;
}

void load_obj_tris(const char* path, const vec3& base, float scale,
                   const vec3& albedo_def, float roughness_def,
                   std::vector<bvh::Tri>& out,
                   float metallic_def, float ior_def)
{
    struct ObjMat {
        vec3 albedo; float roughness, metallic, ior; vec3 emission;
        std::string map_kd;
    };
    const ObjMat default_mat = { albedo_def, roughness_def, metallic_def, ior_def, vec3(0,0,0), "" };

    std::string dir(path);
    auto sl = dir.find_last_of("/\\");
    dir = (sl != std::string::npos) ? dir.substr(0, sl + 1) : "";

    std::map<std::string, ObjMat> mats;
    auto load_mtl = [&](const std::string& fname) {
        std::ifstream mf(dir + fname);
        if (!mf) return;
        std::string cur, line;
        ObjMat m = default_mat;
        vec3 ks(0, 0, 0);
        bool has_pr = false;

        auto flush = [&]() {
            if (cur.empty()) return;
            float ks_lum = (ks.x + ks.y + ks.z) / 3.0f;
            float kd_lum = (m.albedo.x + m.albedo.y + m.albedo.z) / 3.0f;
            if (!has_pr && ks_lum > 0.3f && kd_lum < 0.2f) {
                m.metallic = std::clamp(ks_lum, 0.0f, 1.0f);
                m.albedo   = ks;
            }
            mats[cur] = m;
        };

        while (std::getline(mf, line)) {
            std::istringstream ss(line);
            std::string key; ss >> key;
            if      (key == "newmtl") { flush(); ss >> cur; m = default_mat; ks = vec3(0,0,0); has_pr = false; }
            else if (key == "Kd")     { ss >> m.albedo.x >> m.albedo.y >> m.albedo.z; }
            else if (key == "Ks")     { ss >> ks.x >> ks.y >> ks.z; }
            else if (key == "Ke")     { ss >> m.emission.x >> m.emission.y >> m.emission.z; }
            else if (key == "Ns")     { float n; ss >> n; if (!has_pr) m.roughness = 1.0f - std::clamp(n / 900.0f, 0.0f, 1.0f); }
            else if (key == "Pr")     { ss >> m.roughness; has_pr = true; }
            else if (key == "Pm")     { ss >> m.metallic; }
            else if (key == "Ni")     { ss >> m.ior; }
            else if (key == "map_Kd") { std::string fn; std::getline(ss, fn); size_t a = fn.find_first_not_of(" \t"); if (a != std::string::npos) m.map_kd = fn.substr(a); }
        }
        flush();
    };

    std::ifstream f(path);
    if (!f) { std::cout << "load_obj_tris: could not open " << path << "\n"; return; }

    std::vector<vec3> verts;
    std::vector<vec2> uvs;

    struct FaceVert { int vi, ti; };
    struct Face { std::vector<FaceVert> corners; std::string mat; };
    std::vector<Face> faces;
    std::string cur_mat, line;


    while (std::getline(f, line)) {
        const char* s = line.c_str();
        while (*s == ' ' || *s == '\t') ++s;

        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            char* e; const char* p = s + 2;
            float x = std::strtof(p, &e); p = e;
            float y = std::strtof(p, &e); p = e;
            float z = std::strtof(p, &e);
            verts.push_back(vec3(x, y, z));
        }
        else if (s[0] == 'v' && s[1] == 't' && (s[2] == ' ' || s[2] == '\t')) {
            char* e; const char* p = s + 3;
            float u = std::strtof(p, &e); p = e;
            float v = std::strtof(p, &e);
            uvs.push_back(vec2(u, v));
        }
        else if (s[0] == 'v' && s[1] == 'n') {

        }
        else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            Face fc; fc.mat = cur_mat;
            const int nv = (int)verts.size(), nt = (int)uvs.size();
            const char* p = s + 2;
            while (*p) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p) break;
                char* e;
                long vi = std::strtol(p, &e, 10); p = e;
                long ti = 0; bool hasT = false;
                if (*p == '/') {
                    ++p;
                    if (*p != '/') { ti = std::strtol(p, &e, 10); p = e; hasT = true; }
                    if (*p == '/') { ++p; std::strtol(p, &e, 10); p = e; }
                }
                if (vi == 0) continue;
                int vIdx = (vi > 0) ? (int)vi - 1 : nv + (int)vi;
                int tIdx = (hasT && ti != 0) ? ((ti > 0) ? (int)ti - 1 : nt + (int)ti) : -1;
                fc.corners.push_back({vIdx, tIdx});
            }
            if (fc.corners.size() >= 3) faces.push_back(std::move(fc));
        }
        else if (s[0] == 'u' || s[0] == 'm') {
            std::istringstream ss(s);
            std::string tag; ss >> tag;
            if      (tag == "usemtl") { cur_mat.clear(); ss >> cur_mat; }
            else if (tag == "mtllib") { std::string fn; ss >> fn; load_mtl(fn); }
        }
    }


    auto valid_v = [&](int i) { return i >= 0 && i < (int)verts.size(); };
    auto valid_t = [&](int i) { return i >= 0 && i < (int)uvs.size(); };
    std::vector<vec3> vn(verts.size(), vec3(0, 0, 0));
    for (auto& fc : faces)
        for (size_t i = 1; i + 1 < fc.corners.size(); ++i) {
            int a = fc.corners[0].vi, b = fc.corners[i].vi, c = fc.corners[i+1].vi;
            if (!valid_v(a) || !valid_v(b) || !valid_v(c)) continue;
            vec3 n = cross(verts[b] - verts[a], verts[c] - verts[a]);
            vn[a] = vn[a] + n; vn[b] = vn[b] + n; vn[c] = vn[c] + n;
        }
    for (auto& n : vn) n = normalize(n);

    std::map<std::string, const texture*> mat_tex;

    for (auto& fc : faces) {
        auto it = mats.find(fc.mat);
        const ObjMat& mat = (it != mats.end()) ? it->second : default_mat;

        const texture* tex = nullptr;
        if (!mat.map_kd.empty()) {
            auto tt = mat_tex.find(mat.map_kd);
            if (tt == mat_tex.end()) {
                tex = obj_cached_texture(dir + mat.map_kd);
                mat_tex[mat.map_kd] = tex;
            } else {
                tex = tt->second;
            }
        }

        for (size_t i = 1; i + 1 < fc.corners.size(); ++i) {
            int a = fc.corners[0].vi, b = fc.corners[i].vi, c = fc.corners[i+1].vi;
            if (!valid_v(a) || !valid_v(b) || !valid_v(c)) continue;
            vec3 v0 = base + verts[a] * scale, v1 = base + verts[b] * scale, v2 = base + verts[c] * scale;
            vec3 fn = normalize(cross(v1 - v0, v2 - v0));

            int ta = fc.corners[0].ti, tb = fc.corners[i].ti, tc = fc.corners[i+1].ti;
            vec2 t0 = valid_t(ta) ? uvs[ta] : vec2(0,0);
            vec2 t1 = valid_t(tb) ? uvs[tb] : vec2(0,0);
            vec2 t2 = valid_t(tc) ? uvs[tc] : vec2(0,0);

            bvh::Tri tri { v0, v1, v2, fn, mat.albedo, mat.roughness, mat.metallic, mat.ior,
                           false, vn[a], vn[b], vn[c] };
            tri.emission = mat.emission;
            tri.uv0 = t0; tri.uv1 = t1; tri.uv2 = t2;
            tri.tex = tex;
            out.push_back(tri);
        }
    }
    std::cout << "load_obj_tris: " << path << " -> " << verts.size() << " verts, "
              << faces.size() << " faces, " << mats.size() << " materials\n";
}
