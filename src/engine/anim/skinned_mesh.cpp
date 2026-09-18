#include <engine/anim/skinned_mesh.hpp>
#include <fstream>
#include <sstream>

bool SkinnedMesh::load(const std::string& weights_path, const std::string& anim_path,
                       const mesh& m) {
    if (m.src_vertex.size() != m.vertices.size()) return false;
    bind = m.vertices;


    std::ifstream wf(weights_path);
    if (!wf) return false;
    int bw = 0, V = 0;
    std::string line, tok;
    while (std::getline(wf, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        ss >> tok;
        if      (tok == "bones") ss >> bw;
        else if (tok == "verts") { ss >> V; break; }
    }
    if (bw <= 0 || V <= 0) return false;
    std::vector<int> bone_of(V);
    for (int i = 0; i < V; ++i) wf >> bone_of[i];
    if (!wf) return false;


    std::ifstream af(anim_path);
    if (!af) return false;
    while (std::getline(af, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        ss >> tok;
        if      (tok == "fps")    ss >> fps;
        else if (tok == "bones")  ss >> bones;
        else if (tok == "frames") { ss >> frames; break; }
    }
    if (bones <= 0 || frames <= 0 || bones != bw) return false;
    skin.resize((std::size_t)frames * bones);
    for (auto& M : skin)
        for (int i = 0; i < 16; ++i) af >> M.m[i];
    if (!af) return false;


    uint32_t maxSrc = 0;
    for (uint32_t s : m.src_vertex) if (s > maxSrc) maxSrc = s;
    if ((int)maxSrc >= V) return false;

    vertexBone.resize(bind.size());
    matched = 0;
    for (std::size_t v = 0; v < bind.size(); ++v) {
        vertexBone[v] = bone_of[m.src_vertex[v]];
        ++matched;
    }
    return true;
}

void SkinnedMesh::apply(mesh& m, float t) const {
    if (!valid()) return;
    float ff = t * fps;
    if (ff < 0.0f) ff = 0.0f;
    float maxf = (float)(frames - 1);
    if (ff > maxf) ff = maxf;
    int   f0 = (int)ff;
    int   f1 = (f0 + 1 < frames) ? f0 + 1 : f0;
    float a  = ff - (float)f0;


    std::vector<mat4> Mb((std::size_t)bones);
    const mat4* A = &skin[(std::size_t)f0 * bones];
    const mat4* B = &skin[(std::size_t)f1 * bones];
    for (int b = 0; b < bones; ++b)
        for (int i = 0; i < 16; ++i)
            Mb[b].m[i] = A[b].m[i] * (1.0f - a) + B[b].m[i] * a;

    for (std::size_t v = 0; v < bind.size(); ++v) {
        vec4 p = Mb[vertexBone[v]] * bind[v].p;
        m.vertices[v].p = vec3(p.x, p.y, p.z);
    }
    m._modelMatrixDirty = true;
}

void build_procedural_character(mesh& out, SkinnedMesh& skin) {
    const int   bones      = 7;
    const float seg        = 0.34f;
    const int   ringsPerSeg = 4;
    const int   slices     = 14;
    const int   rings      = bones * ringsPerSeg;
    const float rBase      = 0.30f, rTip = 0.10f;
    const float pi         = 3.14159265358979f;

    out.vertices.clear();
    out.faces.clear();
    out.src_vertex.clear();
    skin.vertexBone.clear();


    for (int r = 0; r <= rings; ++r) {
        float ft = (float)r / rings;
        float h  = ft * (bones * seg);
        float rad = rBase + (rTip - rBase) * ft;
        int bone = (int)std::lround(h / seg);
        if (bone < 0) bone = 0; if (bone >= bones) bone = bones - 1;
        for (int sdiv = 0; sdiv < slices; ++sdiv) {
            float a = (float)sdiv / slices * 2.0f * pi;
            vertex ve;
            ve.p = vec3(std::cos(a) * rad, h, std::sin(a) * rad);
            ve.t = vec2((float)sdiv / slices, ft);
            out.vertices.push_back(ve);
            out.src_vertex.push_back((uint32_t)out.vertices.size() - 1);
            skin.vertexBone.push_back(bone);
        }
    }

    uint32_t capIdx = (uint32_t)out.vertices.size();
    {
        vertex ve; ve.p = vec3(0.0f, bones * seg, 0.0f); ve.t = vec2(0.5f, 1.0f);
        out.vertices.push_back(ve);
        out.src_vertex.push_back(capIdx);
        skin.vertexBone.push_back(bones - 1);
    }

    auto ring = [&](int r, int s) -> uint32_t {
        return (uint32_t)(r * slices + (s % slices));
    };
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            uint32_t a = ring(r, s),     b = ring(r, s + 1);
            uint32_t c = ring(r + 1, s), d = ring(r + 1, s + 1);
            out.faces.push_back({ a, c, b });
            out.faces.push_back({ b, c, d });
        }
    for (int s = 0; s < slices; ++s)
        out.faces.push_back({ ring(rings, s), capIdx, ring(rings, s + 1) });


    const int   frames = 60;
    skin.bind    = out.vertices;
    skin.bones   = bones;
    skin.frames  = frames;
    skin.fps     = 24.0f;
    skin.matched = (int)out.vertices.size();
    skin.skin.assign((std::size_t)frames * bones, mat4(1.0f));

    const vec3 zAxis(0.0f, 0.0f, 1.0f);
    for (int f = 0; f < frames; ++f) {
        float ph = (float)f / frames * 2.0f * pi;
        mat4 A(1.0f);
        vec3 prevP(0.0f, 0.0f, 0.0f);
        for (int b = 0; b < bones; ++b) {
            vec3 P(0.0f, b * seg, 0.0f);

            float amp   = 0.22f + 0.03f * b;
            float theta = amp * std::sin(ph - b * 0.9f);
            A = A * translationMatrix(P - prevP) * rotationMatrix(theta, zAxis);

            skin.skin[(std::size_t)f * bones + b] = A * translationMatrix(vec3(0,0,0) - P);
            prevP = P;
        }
    }
    out._modelMatrixDirty = true;
}
