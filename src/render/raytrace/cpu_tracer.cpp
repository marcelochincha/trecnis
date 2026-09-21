#include <render/raytrace/cpu_tracer.hpp>
#include <render/raytrace/sr_raytrace.hpp>
#include <algorithm>
#include <cmath>

void CpuTracer::start(int num_workers) {
    num_workers_ = std::clamp(num_workers, 1, MAX_WORKERS);
    running_     = true;
    done_        = SDL_CreateSemaphore(0);
    for (int i = 0; i < num_workers_; ++i) {
        start_[i] = SDL_CreateSemaphore(0);
        args_[i]  = WorkerArg{ this, i };
        threads_[i] = SDL_CreateThread(worker_entry, "RenderWorker", &args_[i]);
    }
}

void CpuTracer::stop() {
    running_ = false;
    for (int i = 0; i < num_workers_; ++i) SDL_SemPost(start_[i]);
    for (int i = 0; i < num_workers_; ++i) SDL_WaitThread(threads_[i], nullptr);
    for (int i = 0; i < num_workers_; ++i) SDL_DestroySemaphore(start_[i]);
    if (done_) SDL_DestroySemaphore(done_);
    done_ = nullptr;
    num_workers_ = 0;
}

void CpuTracer::dispatch() {
    next_row_.store(0, std::memory_order_relaxed);   // publish before releasing
    for (int i = 0; i < num_workers_; ++i) SDL_SemPost(start_[i]);
    for (int i = 0; i < num_workers_; ++i) SDL_SemWait(done_);
}

void CpuTracer::render(const RenderScene& scene, framebuffer& fb) {
    job_scene_ = &scene;
    job_fb_    = &fb;
    dispatch();   // trace every stripe straight into the framebuffer
}

int CpuTracer::worker_entry(void* arg) {
    auto* a = static_cast<WorkerArg*>(arg);
    while (true) {
        SDL_SemWait(a->self->start_[a->id]);
        if (!a->self->running_) break;
        a->self->run_worker();
        SDL_SemPost(a->self->done_);
    }
    return 0;
}

// Pull bands of scanlines off the shared counter until the frame is consumed.
// Cost per row varies hugely across the image -- background rows miss into a
// flat colour, rows covering the character trace a reflection ray per pixel --
// so a static split makes every worker wait on the slowest one. Grabbing work
// on demand keeps all of them busy right to the end of the frame.
void CpuTracer::run_worker() {
    const int h = job_fb_->height;
    for (;;) {
        int y0 = next_row_.fetch_add(ROWS_PER_CHUNK, std::memory_order_relaxed);
        if (y0 >= h) return;
        trace_rows(y0, std::min(y0 + ROWS_PER_CHUNK, h));
    }
}

void CpuTracer::trace_rows(int y0, int y1) {
    const RenderScene& s = *job_scene_;
    framebuffer&       fb = *job_fb_;
    int w = fb.width, h = fb.height;

    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            ray r(s.cam->_position, get_ray_direction(*s.cam, x, y, w, h));
            fb.colorBuffer[idx] = pack(trace_ray(r, s, 0)) | 0xFF000000;
        }
}
