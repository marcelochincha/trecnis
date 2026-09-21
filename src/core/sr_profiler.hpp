#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

// Per-frame micro profiler for the HUD.
//
// Sections are a fixed enum rather than strings, so a scope costs two
// performance-counter reads and an array add — no hashing, no allocation, safe
// to leave compiled in. Timings are smoothed with an exponential moving average
// because raw per-frame numbers jitter too much to read off the screen.
//
// Only the main thread instruments: the tracer's worker threads are covered as
// a whole by PROF_TRACE, which measures the dispatch-and-join, i.e. the wall
// time the frame actually waits for the pool.

enum ProfSection {
    PROF_SKIN = 0,   // skinned character deformation
    PROF_BVH_DYN,    // dynamic BVH rebuild (per frame)
    PROF_CLEAR,      // framebuffer clear
    PROF_TRACE,      // active render backend (raster / CPU BVH / Embree / OpenCL)
    PROF_GIZMO,      // origin gizmo overlay
    PROF_DEBUG,      // BVH wireframe + normals overlays
    PROF_HUD,        // HUD/menu text rasterization
    PROF_UPLOAD,     // SDL_UpdateTexture (framebuffer -> GPU texture)
    PROF_PRESENT,    // SDL_RenderClear/Copy/Present
    PROF_COUNT
};

class Profiler {
public:
    void begin_frame();                  // zero this frame's accumulators
    void add(ProfSection s, double ms);  // accumulate (a section may be hit twice)
    void end_frame();                    // fold the frame into the moving average

    double ms(ProfSection s) const { return avg_[s]; }
    double total_ms() const;
    static const char* name(ProfSection s);

private:
    static constexpr double kSmoothing = 0.05;  // EMA weight of the newest frame

    double cur_[PROF_COUNT] = {};
    double avg_[PROF_COUNT] = {};
    bool   primed_ = false;
};

extern Profiler g_prof;

// RAII scope timer. Declare with PROF_SCOPE(PROF_TRACE) at the top of a block.
struct ProfScope {
    explicit ProfScope(ProfSection s)
        : s_(s), t0_(SDL_GetPerformanceCounter()) {}
    ~ProfScope() {
        uint64_t dt = SDL_GetPerformanceCounter() - t0_;
        g_prof.add(s_, (double)dt * 1000.0 / (double)SDL_GetPerformanceFrequency());
    }
    ProfScope(const ProfScope&) = delete;
    ProfScope& operator=(const ProfScope&) = delete;

private:
    ProfSection s_;
    uint64_t    t0_;
};

// The enum name doubles as the variable suffix, so two scopes in one block
// would collide — which is what we want, they'd be measuring the same thing.
#define PROF_SCOPE(sec) ProfScope prof_scope_##sec(sec)
