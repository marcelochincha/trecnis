#include <render/raytrace/bvh.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace bvh {




static inline AABB aabb_empty() {
    return AABB{ vec3( 1e30f,  1e30f,  1e30f),
                vec3(-1e30f, -1e30f, -1e30f) };
}

static inline void aabb_grow(AABB& b, const vec3& p) {
    b.min = minimum(b.min, p);
    b.max = maximum(b.max, p);
}

static inline AABB aabb_union(const AABB& a, const AABB& b) {
    return AABB{ minimum(a.min, b.min), maximum(a.max, b.max) };
}



static inline float aabb_area(const AABB& b) {
    vec3 d = b.max - b.min;
    if (d.x < 0.0f) return 0.0f;
    return d.x * d.y + d.y * d.z + d.z * d.x;
}


static inline AABB tri_bounds(const Tri& t) {
    AABB b = aabb_empty();
    aabb_grow(b, t.v0);
    aabb_grow(b, t.v1);
    aabb_grow(b, t.v2);
    return b;
}






static inline bool aabb_hit(const AABB& b, const vec3& o, const vec3& inv,
                            float t_max, float& t_near) {
    float tx1 = (b.min.x - o.x) * inv.x, tx2 = (b.max.x - o.x) * inv.x;
    float tmin = std::fmin(tx1, tx2),    tmax = std::fmax(tx1, tx2);
    float ty1 = (b.min.y - o.y) * inv.y, ty2 = (b.max.y - o.y) * inv.y;
    tmin = std::fmax(tmin, std::fmin(ty1, ty2));
    tmax = std::fmin(tmax, std::fmax(ty1, ty2));
    float tz1 = (b.min.z - o.z) * inv.z, tz2 = (b.max.z - o.z) * inv.z;
    tmin = std::fmax(tmin, std::fmin(tz1, tz2));
    tmax = std::fmin(tmax, std::fmax(tz1, tz2));
    tmin = std::fmax(tmin, 0.0f);
    tmax = std::fmin(tmax, t_max);
    t_near = tmin;
    return tmax >= tmin;
}


static inline bool tri_hit(const TriISect& tr, const vec3& o, const vec3& d,
                           float& t) {
    const float EPS = 1e-8f;
    vec3 h  = cross(d, tr.e2);
    float a = dot(tr.e1, h);
    if (std::fabs(a) < EPS) return false;
    float f = 1.0f / a;
    vec3 s  = o - tr.v0;
    float u = f * dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    vec3 q  = cross(s, tr.e1);
    float v = f * dot(d, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * dot(tr.e2, q);
    return t > EPS;
}





static inline uint32_t expand_bits_10(uint32_t v) {
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v <<  8)) & 0x0300F00Fu;
    v = (v | (v <<  4)) & 0x030C30C3u;
    v = (v | (v <<  2)) & 0x09249249u;
    return v;
}
static inline uint32_t morton3d(float x, float y, float z) {
    auto q = [](float f) {
        return expand_bits_10((uint32_t)(std::clamp(f, 0.0f, 1.0f) * 1023.0f));
    };
    return (q(x) << 2) | (q(y) << 1) | q(z);
}

void BVH::build(std::vector<Tri> tris, BuildStrategy strategy) {
    tris_     = std::move(tris);
    strategy_ = strategy;
    nodes_.clear();
    if (tris_.empty()) return;

    centroids_.resize(tris_.size());
    for (std::size_t i = 0; i < tris_.size(); ++i)
        centroids_[i] = (tris_[i].v0 + tris_[i].v1 + tris_[i].v2) / 3.0f;




    if (strategy_ == Morton) {
        AABB cb = aabb_empty();
        for (const auto& c : centroids_) aabb_grow(cb, c);
        vec3 ext = cb.max - cb.min;
        std::vector<std::pair<uint32_t, int>> order(tris_.size());
        for (std::size_t i = 0; i < tris_.size(); ++i) {
            float nx = ext.x > 0 ? (centroids_[i].x - cb.min.x) / ext.x : 0.0f;
            float ny = ext.y > 0 ? (centroids_[i].y - cb.min.y) / ext.y : 0.0f;
            float nz = ext.z > 0 ? (centroids_[i].z - cb.min.z) / ext.z : 0.0f;
            order[i] = { morton3d(nx, ny, nz), (int)i };
        }
        std::sort(order.begin(), order.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<Tri>  nt(tris_.size());
        std::vector<vec3> nc(centroids_.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            nt[i] = tris_[order[i].second];
            nc[i] = centroids_[order[i].second];
        }
        tris_       = std::move(nt);
        centroids_  = std::move(nc);
    }

    nodes_.reserve(tris_.size() * 2);
    build_node(0, (int)tris_.size(), 0);
    centroids_.clear();
    centroids_.shrink_to_fit();



    tri_isect_.resize(tris_.size());
    for (std::size_t i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        tri_isect_[i] = TriISect{ t.v0, t.v1 - t.v0, t.v2 - t.v0 };
    }
}




int BVH::partition(int start, int count, int axis,
                   float cmin, float scale, int split_bin) {
    int i = start;
    int j = start + count - 1;
    while (i <= j) {
        int bin = (int)((centroids_[i].v[axis] - cmin) * scale);
        if (bin > split_bin) {
            std::swap(tris_[i], tris_[j]);
            std::swap(centroids_[i], centroids_[j]);
            --j;
        } else {
            ++i;
        }
    }
    return i;
}




int BVH::partition_median(int start, int count, int axis) {
    int mid = start + count / 2;
    int lo = start, hi = start + count - 1;
    while (lo < hi) {
        float pivot = centroids_[(lo + hi) / 2].v[axis];
        int i = lo, j = hi;
        while (i <= j) {
            while (centroids_[i].v[axis] < pivot) ++i;
            while (centroids_[j].v[axis] > pivot) --j;
            if (i <= j) {
                std::swap(tris_[i], tris_[j]);
                std::swap(centroids_[i], centroids_[j]);
                ++i; --j;
            }
        }
        if      (mid <= j) hi = j;
        else if (mid >= i) lo = i;
        else               break;
    }
    return mid;
}

int BVH::build_node(int start, int count, int depth) {
    int idx = (int)nodes_.size();
    nodes_.push_back(Node{});


    AABB bounds = aabb_empty();
    for (int i = start; i < start + count; ++i)
        bounds = aabb_union(bounds, tri_bounds(tris_[i]));



    if (count <= 2 || depth >= MAX_DEPTH) {
        nodes_[idx].bounds = bounds;
        nodes_[idx].left = -1;
        nodes_[idx].start = start;
        nodes_[idx].count = count;
        return idx;
    }


    AABB cb = aabb_empty();
    for (int i = start; i < start + count; ++i)
        aabb_grow(cb, centroids_[i]);

    vec3 ext = cb.max - cb.min;
    int axis = (ext.x > ext.y && ext.x > ext.z) ? 0 : (ext.y > ext.z ? 1 : 2);
    float cmin = cb.min.v[axis];
    float cext = ext.v[axis];

    int mid;
    if (strategy_ == Morton) {


        mid = start + count / 2;
    } else if (strategy_ == Median) {

        mid = partition_median(start, count, axis);
    } else {

        if (cext < 1e-8f) {
            nodes_[idx].bounds = bounds;
            nodes_[idx].left = -1;
            nodes_[idx].start = start;
            nodes_[idx].count = count;
            return idx;
        }

        constexpr int NB = 12;
        AABB bin_bounds[NB];
        int  bin_count[NB] = {0};
        for (int i = 0; i < NB; ++i) bin_bounds[i] = aabb_empty();

        float scale = NB / cext;
        for (int i = start; i < start + count; ++i) {
            int b = (int)((centroids_[i].v[axis] - cmin) * scale);
            if (b < 0) b = 0; if (b >= NB) b = NB - 1;
            bin_bounds[b] = aabb_union(bin_bounds[b], tri_bounds(tris_[i]));
            bin_count[b]++;
        }



        float left_area[NB - 1], right_area[NB - 1];
        int   left_cnt[NB - 1],  right_cnt[NB - 1];

        AABB acc = aabb_empty(); int cnt = 0;
        for (int i = 0; i < NB - 1; ++i) {
            acc = aabb_union(acc, bin_bounds[i]);
            cnt += bin_count[i];
            left_area[i] = aabb_area(acc);
            left_cnt[i]  = cnt;
        }
        acc = aabb_empty(); cnt = 0;
        for (int i = NB - 1; i > 0; --i) {
            acc = aabb_union(acc, bin_bounds[i]);
            cnt += bin_count[i];
            right_area[i - 1] = aabb_area(acc);
            right_cnt[i - 1]  = cnt;
        }


        float best_cost = 1e30f;
        int   best_split = -1;
        for (int i = 0; i < NB - 1; ++i) {
            float cost = left_area[i] * left_cnt[i] + right_area[i] * right_cnt[i];
            if (cost < best_cost) { best_cost = cost; best_split = i; }
        }



        float parent_area = aabb_area(bounds);
        float split_cost  = 1.0f + best_cost / (parent_area > 0.0f ? parent_area : 1.0f);
        float leaf_cost   = (float)count;
        if (best_split < 0 || split_cost >= leaf_cost) {





            if (count > MAX_LEAF) {
                mid = partition_median(start, count, axis);
                if (mid == start || mid == start + count) mid = start + count / 2;
                goto do_split;
            }
            nodes_[idx].bounds = bounds;
            nodes_[idx].left = -1;
            nodes_[idx].start = start;
            nodes_[idx].count = count;
            return idx;
        }


        mid = partition(start, count, axis, cmin, scale, best_split);
        if (mid == start || mid == start + count)
            mid = start + count / 2;
    }

do_split:
    int l = build_node(start, mid - start, depth + 1);
    int r = build_node(mid, start + count - mid, depth + 1);


    nodes_[idx].bounds = bounds;
    nodes_[idx].left  = l;
    nodes_[idx].right = r;
    nodes_[idx].count = 0;
    return idx;
}



bool BVH::intersect(const vec3& origin, const vec3& dir, Hit& out) const {
    if (nodes_.empty()) return false;

    vec3 inv(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);
    float best = out.t;
    int   best_tri = -1;






    int   stack[64];
    float stack_t[64];
    int   sp  = 0;
    int   cur = 0;

    for (;;) {
        const Node& n = nodes_[cur];

        if (n.left < 0) {
            for (int i = n.start; i < n.start + n.count; ++i) {
                float t;
                if (tri_hit(tri_isect_[i], origin, dir, t) && t < best) {
                    best = t;
                    best_tri = i;
                }
            }
        } else {
            int   c0 = n.left, c1 = n.right;
            float t0, t1;
            bool  h0 = aabb_hit(nodes_[c0].bounds, origin, inv, best, t0);
            bool  h1 = aabb_hit(nodes_[c1].bounds, origin, inv, best, t1);

            if (h0 && h1) {
                if (t0 > t1) { std::swap(c0, c1); std::swap(t0, t1); }
                if (sp < 64) { stack[sp] = c1; stack_t[sp] = t1; ++sp; }
                cur = c0;
                continue;
            }
            if (h0) { cur = c0; continue; }
            if (h1) { cur = c1; continue; }
        }



        do {
            if (sp == 0) goto done;
            cur = stack[--sp];
        } while (stack_t[sp] >= best);
    }

done:
    if (best_tri < 0) return false;
    out.t = best;
    out.tri = best_tri;
    return true;
}

bool BVH::occluded(const vec3& origin, const vec3& dir, float max_t) const {
    if (nodes_.empty()) return false;

    vec3 inv(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        const Node& n = nodes_[stack[--sp]];
        float t_near;
        if (!aabb_hit(n.bounds, origin, inv, max_t, t_near)) continue;

        if (n.left < 0) {
            for (int i = n.start; i < n.start + n.count; ++i) {
                float t;
                if (tri_hit(tri_isect_[i], origin, dir, t) && t < max_t)
                    return true;
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
    return false;
}



void BVH::flatten(std::vector<float>& nb, std::vector<int>& nl,
                  std::vector<float>& tf) const {
    nb.resize(nodes_.size() * 8);
    nl.resize(nodes_.size() * 4);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        nb[i*8+0] = n.bounds.min.x; nb[i*8+1] = n.bounds.min.y; nb[i*8+2] = n.bounds.min.z; nb[i*8+3] = 0.0f;
        nb[i*8+4] = n.bounds.max.x; nb[i*8+5] = n.bounds.max.y; nb[i*8+6] = n.bounds.max.z; nb[i*8+7] = 0.0f;
        nl[i*4+0] = n.left; nl[i*4+1] = n.right; nl[i*4+2] = n.start; nl[i*4+3] = n.count;
    }





    tf.resize(tris_.size() * 32);
    for (std::size_t i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        float* p = &tf[i*32];
        p[0]=t.v0.x;     p[1]=t.v0.y;     p[2]=t.v0.z;
        p[3]=t.v1.x;     p[4]=t.v1.y;     p[5]=t.v1.z;
        p[6]=t.v2.x;     p[7]=t.v2.y;     p[8]=t.v2.z;
        p[9]=t.normal.x; p[10]=t.normal.y; p[11]=t.normal.z;
        p[12]=t.albedo.x;p[13]=t.albedo.y; p[14]=t.albedo.z;
        p[15]=t.roughness;
        p[16]=t.metallic;
        p[17]=t.ior;
        p[18]=t.smooth ? 1.0f : 0.0f;
        p[19]=0.0f;
        p[20]=t.n0.x; p[21]=t.n0.y; p[22]=t.n0.z;
        p[23]=t.n1.x; p[24]=t.n1.y; p[25]=t.n1.z;
        p[26]=t.n2.x; p[27]=t.n2.y; p[28]=t.n2.z;
        p[29]=t.emission.x; p[30]=t.emission.y; p[31]=t.emission.z;
    }
}



void BVH::debug_nodes(std::vector<DebugNode>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    out.reserve(nodes_.size());


    struct Item { int node; int depth; };
    Item stack[128];
    int sp = 0;
    stack[sp++] = { 0, 0 };

    while (sp > 0) {
        Item it = stack[--sp];
        const Node& n = nodes_[it.node];
        bool leaf = (n.left < 0);
        out.push_back({ n.bounds, it.depth, leaf });
        if (!leaf) {
            stack[sp++] = { n.left,  it.depth + 1 };
            stack[sp++] = { n.right, it.depth + 1 };
        }
    }
}

}
