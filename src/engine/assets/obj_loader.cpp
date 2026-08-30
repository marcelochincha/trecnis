#include <engine/assets/obj_loader.hpp>
#include <cstdlib>
#include <fstream>
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
    std::map<std::pair<int,int>, uint32_t> dedup; // (vi,ti) -> vertex index
    out.vertices.clear();
    out.faces.clear();
    out.src_vertex.clear();
    out.tex = nullptr;

    // Reads the first map_Kd from an .mtl and loads it into out.tex.
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
                    if (*p == '/') { ++p; std::strtol(p, &e, 10); p = e; } // skip vn
                }
                if (vi == 0) continue;
                int vIdx = (vi > 0) ? (int)vi - 1 : nv + (int)vi;
                int tIdx = (hasT && ti != 0) ? ((ti > 0) ? (int)ti - 1 : nt + (int)ti) : -1;
                poly.push_back(emit(vIdx, tIdx));
            }
            for (std::size_t i = 1; i + 1 < poly.size(); ++i)
                out.faces.push_back({ poly[0], poly[i], poly[i + 1] });
        } else if (s[0] == 'm') { // mtllib
            std::istringstream ss(s);
            std::string tag, fn; ss >> tag >> fn;
            if (tag == "mtllib") load_mtl(fn);
        }
    }
    out._modelMatrixDirty = true;
    return true;
}
