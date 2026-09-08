#pragma once
#include <SDL2/SDL.h>
#include <render/render_scene.hpp>
#include <core/sr_framebuffer.hpp>

// A persistent pool of worker threads that trace a frame in horizontal stripes.
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
    void run_stripe(int id);
    void dispatch();                      // release the pool, block until the frame is done
    static int worker_entry(void* arg);

    // Trace one horizontal stripe of the frame straight into the framebuffer.
    void trace_stripe(int y0, int y1);

    static constexpr int MAX_WORKERS = 64;

    int  num_workers_ = 0;
    bool running_     = false;

    SDL_Thread* threads_[MAX_WORKERS] = {};
    SDL_sem*    start_[MAX_WORKERS]   = {};
    SDL_sem*    done_                 = nullptr;

    // Current frame job, published before workers are released.
    const RenderScene* job_scene_ = nullptr;
    framebuffer*       job_fb_    = nullptr;

    struct WorkerArg { CpuTracer* self; int id; };
    WorkerArg args_[MAX_WORKERS];
};
