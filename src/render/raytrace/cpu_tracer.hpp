#pragma once
#include <SDL2/SDL.h>
#include <render/render_scene.hpp>
#include <core/sr_framebuffer.hpp>





class CpuTracer {
public:
    void start(int num_workers);
    void stop();


    void render(const RenderScene& scene, framebuffer& fb);

    int workers() const { return num_workers_; }

private:
    void run_stripe(int id);
    void dispatch();
    static int worker_entry(void* arg);


    void trace_stripe(int y0, int y1);

    static constexpr int MAX_WORKERS = 64;

    int  num_workers_ = 0;
    bool running_     = false;

    SDL_Thread* threads_[MAX_WORKERS] = {};
    SDL_sem*    start_[MAX_WORKERS]   = {};
    SDL_sem*    done_                 = nullptr;


    const RenderScene* job_scene_ = nullptr;
    framebuffer*       job_fb_    = nullptr;

    struct WorkerArg { CpuTracer* self; int id; };
    WorkerArg args_[MAX_WORKERS];
};
