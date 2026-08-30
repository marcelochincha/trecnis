#include <engine/anim/skinned_mesh.hpp>
#include <fstream>
#include <sstream>

bool SkinnedMesh::load(const std::string& weights_path, const std::string& anim_path,
                       const mesh& m) {
    if (m.src_vertex.size() != m.vertices.size()) return false; // not an OBJ
    bind = m.vertices;

    // --- BINDING (weights): bones, verts, V ints -----------------------------
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

    // --- ANIMATION (anim): fps, bones, frames, F*B matrices ------------------
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
    if (bones <= 0 || frames <= 0 || bones != bw) return false; // bones != binding
    skin.resize((std::size_t)frames * bones);
    for (auto& M : skin)
        for (int i = 0; i < 16; ++i) af >> M.m[i];
    if (!af) return false;

    // The binding must cover every OBJ 'v'.
    uint32_t maxSrc = 0;
    for (uint32_t s : m.src_vertex) if (s > maxSrc) maxSrc = s;
    if ((int)maxSrc >= V) return false; // OBJ 'v' count != binding verts

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

    // Per-bone interpolated matrix (bones is small; reused per vertex).
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
    const int   bones      = 7;      // joints in the chain
    const float seg        = 0.34f;  // segment length (height)
    const int   ringsPerSeg = 4;
    const int   slices     = 14;     // vertices per ring
    const int   rings      = bones * ringsPerSeg;
    const float rBase      = 0.30f, rTip = 0.10f;
    const float pi         = 3.14159265358979f;

    out.vertices.clear();
    out.faces.clear();
    out.src_vertex.clear();
    skin.vertexBone.clear();

    // --- rest geometry (straight column along +Y) ----------------------------
    for (int r = 0; r <= rings; ++r) {
        float ft = (float)r / rings;      // 0..1 up the height
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
    // top cap (one central vertex bound to the last bone)
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
    for (int s = 0; s < slices; ++s) // cap
        out.faces.push_back({ ring(rings, s), capIdx, ring(rings, s + 1) });

    // --- bake the animation (travelling wave that bends the chain) ------------
    const int   frames = 60;   // 2.5 s loop at 24 fps
    skin.bind    = out.vertices;
    skin.bones   = bones;
    skin.frames  = frames;
    skin.fps     = 24.0f;
    skin.matched = (int)out.vertices.size();
    skin.skin.assign((std::size_t)frames * bones, mat4(1.0f));

    const vec3 zAxis(0.0f, 0.0f, 1.0f);
    for (int f = 0; f < frames; ++f) {
        float ph = (float)f / frames * 2.0f * pi;
        mat4 A(1.0f);                 // accumulated animated transform
        vec3 prevP(0.0f, 0.0f, 0.0f); // P_{b-1}
        for (int b = 0; b < bones; ++b) {
            vec3 P(0.0f, b * seg, 0.0f);
            // per-joint angle: wave travelling toward the tip, base stiller
            float amp   = 0.22f + 0.03f * b;
            float theta = amp * std::sin(ph - b * 0.9f);
            A = A * translationMatrix(P - prevP) * rotationMatrix(theta, zAxis);
            // S_b = A_b * inverse(bind_b) = A_b * translate(-P_b)
            skin.skin[(std::size_t)f * bones + b] = A * translationMatrix(vec3(0,0,0) - P);
            prevP = P;
        }
    }
    out._modelMatrixDirty = true;
}
