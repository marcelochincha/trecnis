#include <render/raytrace/cpu_tracer.hpp>
#include <render/raytrace/sr_raytrace.hpp>
#include <algorithm>

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

void CpuTracer::render(const RenderScene& scene, framebuffer& fb) {
    job_scene_ = &scene;
    job_fb_    = &fb;
    for (int i = 0; i < num_workers_; ++i) SDL_SemPost(start_[i]);
    for (int i = 0; i < num_workers_; ++i) SDL_SemWait(done_);
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
    const RenderScene& s = *job_scene_;
    framebuffer&       fb = *job_fb_;

    int stripe  = fb.height / num_workers_;
    int y_start = stripe * id;
    int y_end   = (id == num_workers_ - 1) ? fb.height : stripe * (id + 1);

    int spp = s.spp;
    for (int y = y_start; y < y_end; ++y)
        for (int x = 0; x < fb.width; ++x) {
            vec3 sum(0.0f, 0.0f, 0.0f);
            for (int si = 0; si < spp; ++si) {
                ray r(s.cam->_position, get_ray_direction(*s.cam, x, y, fb.width, fb.height));
                uint32_t seed = ((uint32_t)(y * fb.width + x) * 2654435761u)
                              ^ ((uint32_t)si * 805459861u);
                sum = sum + trace_ray(r, s, 0, seed);
            }
            vec3 col = sum * (1.0f / spp);
            fb.colorBuffer[y * fb.width + x] = pack(col) | 0xFF000000;
        }
}
