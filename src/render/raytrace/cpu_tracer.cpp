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
    for (int i = 0; i < num_workers_; ++i) SDL_SemPost(start_[i]);
    for (int i = 0; i < num_workers_; ++i) SDL_SemWait(done_);
}

void CpuTracer::render(const RenderScene& scene, framebuffer& fb) {
    job_scene_ = &scene;
    job_fb_    = &fb;
    dispatch();
}

int CpuTracer::worker_entry(void* arg) {
    auto* a = static_cast<WorkerArg*>(arg);
    while (true) {
        SDL_SemWait(a->self->start_[a->id]);
        if (!a->self->running_) break;
        a->self->run_stripe(a->id);
        SDL_SemPost(a->self->done_);
    }
    return 0;
}

void CpuTracer::run_stripe(int id) {
    int h       = job_fb_->height;
    int stripe  = h / num_workers_;
    int y0      = stripe * id;
    int y1      = (id == num_workers_ - 1) ? h : stripe * (id + 1);
    trace_stripe(y0, y1);
}

void CpuTracer::trace_stripe(int y0, int y1) {
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
