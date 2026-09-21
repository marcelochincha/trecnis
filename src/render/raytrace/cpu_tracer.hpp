#pragma once
#include <SDL2/SDL.h>
#include <atomic>
#include <render/render_scene.hpp>
#include <core/sr_framebuffer.hpp>

// A persistent pool of worker threads that trace a frame in horizontal bands.
// Backend-agnostic: it shades through scene.accel, so the same pool serves both
// the CPU-BVH and Embree backends. Threads are spawned once and parked on a
// semaphore between frames.
class CpuTracer {
public:
    void start(int num_workers);   // spawn the pool
    void stop();                   // signal + join all workers

    // Trace `scene` into `fb` on the pool, blocking until the frame is complete.
    void render(const RenderScene& scene, framebuffer& fb);

    int workers() const { return num_workers_; }

private:
    void run_worker();
    void dispatch();                      // release the pool, block until the frame is done
    static int worker_entry(void* arg);

    // Trace a run of scanlines straight into the framebuffer.
    void trace_rows(int y0, int y1);

    static constexpr int MAX_WORKERS = 64;

    // Rows handed out per atomic grab. Small enough that a worker that draws an
    // expensive band (the character, whose mirror finish spawns an extra ray per
    // pixel) doesn't hold up the frame, large enough that the counter isn't hot.
    // A row is width*4 bytes, so chunks never share a cache line.
    static constexpr int ROWS_PER_CHUNK = 4;

    int  num_workers_ = 0;
    bool running_     = false;

    SDL_Thread* threads_[MAX_WORKERS] = {};
    SDL_sem*    start_[MAX_WORKERS]   = {};
    SDL_sem*    done_                 = nullptr;

    // Current frame job, published before workers are released.
    const RenderScene* job_scene_ = nullptr;
    framebuffer*       job_fb_    = nullptr;

    // Next unclaimed scanline. Workers pull bands from here instead of owning a
    // fixed stripe: with a fixed split the frame costs as much as the busiest
    // stripe, and a centred subject leaves the top and bottom workers idle.
    std::atomic<int>   next_row_{0};

    struct WorkerArg { CpuTracer* self; int id; };
    WorkerArg args_[MAX_WORKERS];
};
