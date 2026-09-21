#define SDL_MAIN_HANDLED

#include <iostream>
#include <array>
#include <vector>
#include <cstring>

#include <SDL2/SDL.h>

#include <math/sr_math.hpp>
#include <render/raster/sr_raster.hpp>
#include <core/sr_camera.hpp>
#include <core/sr_texture.hpp>
#include <core/sr_text.hpp>
#include <sound/sr_sound.hpp>
#include <app/app.hpp>
#include <app/app_state.hpp>
#include <game/game.hpp>
#include <core/sr_profiler.hpp>
#include <core/sr_dump.hpp>
#include <sr_config.hpp>

SDL_Window   *window;
SDL_Renderer *renderer;
SDL_Texture  *sdl_fb_texture;

void init_sdl()
{
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(
        "sr_lec",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        global_config.window_width, global_config.window_height,
        SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(renderer, global_config.window_width, global_config.window_height);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    sdl_fb_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        global_config.window_width, global_config.window_height);

}


// ---- Offline benchmark ------------------------------------------------------
// Renders straight to the framebuffer with no present, no HUD and no frame cap,
// so the number reported is the renderer's real cost rather than the 60 fps
// limiter. Each row toggles one lighting feature off, which isolates how much
// that feature costs without needing a profiler.
static void run_bench(App* g, SDL_Texture* tex, int frames)
{
    const bool sun0 = g->opts.sun_enabled, refl0 = g->opts.reflections;
    const bool gi0  = g->opts.gi_enabled,  pl0   = game_point_light(g->game);
    const bool hud0 = g->show_hud;
    g->show_hud = false;   // exclude text rendering from the measurement

    struct Cfg { const char* name; bool sun, refl, gi, pl; };
    const Cfg cfgs[] = {
        { "baseline (as configured)", sun0,  refl0, gi0,   pl0   },
        { "no sun",                   false, refl0, gi0,   pl0   },
        { "no reflections",           sun0,  false, gi0,   pl0   },
        { "no point light",           sun0,  refl0, gi0,   false },
        { "primary rays only",        false, false, false, false },
    };

    const double freq = (double)SDL_GetPerformanceFrequency();
    const float  dt   = 1.0f / 60.0f;

    printf("\n=== BENCH: %d frames/config  %dx%d  %d workers ===\n",
           frames, g->fb.width, g->fb.height, g->num_workers);
    printf("%-26s %10s %10s %10s\n", "config", "update ms", "render ms", "fps");

    for (const Cfg& c : cfgs) {
        g->opts.sun_enabled = c.sun; g->opts.reflections = c.refl;
        g->opts.gi_enabled  = c.gi;  game_set_point_light(g->game, c.pl);

        for (int i = 0; i < 5; ++i) {          // warm caches / branch predictors
            app_update(g, dt);
            app_render(g, tex, dt);
        }

        double upd = 0.0, ren = 0.0;
        for (int i = 0; i < frames; ++i) {
            uint64_t a = SDL_GetPerformanceCounter();
            app_update(g, dt);
            uint64_t b = SDL_GetPerformanceCounter();
            app_render(g, tex, dt);
            uint64_t d = SDL_GetPerformanceCounter();
            upd += (double)(b - a) * 1000.0 / freq;
            ren += (double)(d - b) * 1000.0 / freq;
        }
        upd /= frames; ren /= frames;
        printf("%-26s %10.2f %10.2f %10.1f\n", c.name, upd, ren, 1000.0 / (upd + ren));
        fflush(stdout);
    }

    g->opts.sun_enabled = sun0; g->opts.reflections = refl0;
    g->opts.gi_enabled  = gi0;  game_set_point_light(g->game, pl0);

    // ---- Full-frame breakdown ------------------------------------------------
    // The rows above stop at the renderer. The frame the user actually waits for
    // also draws the HUD and blits/presents through SDL, so measure those two
    // separately: HUD cost is (render with HUD) - (render without), and present
    // is the RenderClear/RenderCopy/RenderPresent block on its own.
    const double f2 = (double)SDL_GetPerformanceFrequency();
    double r_nohud = 0.0, r_hud = 0.0, pres = 0.0;

    g->show_hud = false;
    for (int i = 0; i < frames; ++i) {
        app_update(g, dt);
        uint64_t a = SDL_GetPerformanceCounter();
        app_render(g, tex, dt);
        r_nohud += (double)(SDL_GetPerformanceCounter() - a) * 1000.0 / f2;
    }

    g->show_hud = true; g->hud_simple = false;
    for (int i = 0; i < frames; ++i) {
        app_update(g, dt);
        uint64_t a = SDL_GetPerformanceCounter();
        app_render(g, tex, dt);
        uint64_t b = SDL_GetPerformanceCounter();
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, NULL, NULL);
        SDL_RenderPresent(renderer);
        uint64_t c2 = SDL_GetPerformanceCounter();
        r_hud += (double)(b - a) * 1000.0 / f2;
        pres  += (double)(c2 - b) * 1000.0 / f2;
    }
    r_nohud /= frames; r_hud /= frames; pres /= frames;

    printf("\n--- full-frame breakdown (baseline settings) ---\n");
    printf("  render (no HUD)   %8.2f ms\n", r_nohud);
    printf("  HUD text          %8.2f ms\n", r_hud - r_nohud);
    printf("  SDL present       %8.2f ms\n", pres);
    printf("  -> total          %8.2f ms  = %.1f fps\n",
           r_hud + pres, 1000.0 / (r_hud + pres));
    fflush(stdout);


    // ---- Worst-case sweep -----------------------------------------------------
    // The rows above use the start view, where the character covers few pixels
    // and most rays miss into the flat background. Cost scales with how much of
    // the screen the (mirror-finish) character actually covers, so walk the
    // camera in; also time the brute-force path the [B] key switches to.
    {
        const vec3 eye0 = g->world.camera.pos;
        const vec3 rot0 = g->world.camera.euler;
        const bool bvh0 = g->opts.use_bvh;
        const float dists[] = { 6.0f, 3.0f, 1.5f, 0.8f };

        printf("\n--- render cost vs camera distance to character ---\n");
        for (float d : dists) {
            g->world.camera.pos   = vec3(0.0f, 1.2f, d);
            g->world.camera.euler = vec3(0.0f, 0.0f, 0.0f);
            for (int i = 0; i < 3; ++i) app_render(g, tex, dt);
            double ms = 0.0;
            for (int i = 0; i < frames; ++i) {
                uint64_t a = SDL_GetPerformanceCounter();
                app_render(g, tex, dt);
                ms += (double)(SDL_GetPerformanceCounter() - a) * 1000.0 / f2;
            }
            ms /= frames;
            printf("  dist %4.1f        %8.2f ms = %6.1f fps\n", d, ms, 1000.0 / ms);
        }

        g->world.camera.pos = vec3(0.0f, 1.2f, 3.0f);
        g->opts.use_bvh = false;
        for (int i = 0; i < 2; ++i) app_render(g, tex, dt);
        double bf = 0.0;
        for (int i = 0; i < frames; ++i) {
            uint64_t a = SDL_GetPerformanceCounter();
            app_render(g, tex, dt);
            bf += (double)(SDL_GetPerformanceCounter() - a) * 1000.0 / f2;
        }
        bf /= frames;
        printf("  BVH OFF (key B)  %8.2f ms = %6.1f fps\n", bf, 1000.0 / bf);
        fflush(stdout);

        g->opts.use_bvh = bvh0;
        g->world.camera.pos   = eye0;
        g->world.camera.euler = rot0;
    }

    // ---- Real main-loop simulation -------------------------------------------
    // Everything above measures work. This reproduces the shipping loop verbatim,
    // frame cap included, because a limiter built on SDL_Delay is subject to the
    // OS timer granularity (~15.6 ms on Windows unless timeBeginPeriod is set):
    // asking for a 10 ms sleep can hand back 16, which silently costs a frame.
    {
        const float target_ms = 1000.0f / global_config.target_fps;
        uint64_t last, cur = SDL_GetTicks64();
        double worst_sleep_err = 0.0, total = 0.0;
        for (int i = 0; i < frames; ++i) {
            last = cur;
            g_prof.begin_frame();
            app_update(g, dt);
            app_render(g, tex, dt);
            { PROF_SCOPE(PROF_PRESENT);
              SDL_RenderClear(renderer);
              SDL_RenderCopy(renderer, tex, NULL, NULL);
              SDL_RenderPresent(renderer); }
            g_prof.end_frame();
            cur = SDL_GetTicks64();
            if (cur - last < target_ms) {
                uint32_t want = (uint32_t)(target_ms - (cur - last));
                uint64_t s0 = SDL_GetPerformanceCounter();
                SDL_Delay(want);
                double got = (double)(SDL_GetPerformanceCounter() - s0) * 1000.0 / f2;
                if (got - want > worst_sleep_err) worst_sleep_err = got - want;
                cur = SDL_GetTicks64();
            }
            total += (double)(cur - last);
        }
        printf("\n--- real loop (with the %.0f fps cap) ---\n", global_config.target_fps);
        printf("  achieved        %8.2f ms/frame = %.1f fps\n",
               total / frames, 1000.0 / (total / frames));
        printf("  worst SDL_Delay overshoot %6.2f ms\n", worst_sleep_err);

        // Same numbers the HUD's profiler block shows, dumped to the console so
        // the instrumentation can be checked without reading pixels.
        printf("\n--- HUD profiler readout ---\n");
        for (int i = 0; i < PROF_COUNT; ++i) {
            double v = g_prof.ms((ProfSection)i);
            if (v < 0.005) continue;
            printf("%-6s %5.2f\n", Profiler::name((ProfSection)i), v);
        }
        printf("%-6s %5.2f\n", "TOTAL", g_prof.total_ms());
        fflush(stdout);
    }

    g->show_hud = hud0;
}

// ---- Backend parity dump ----------------------------------------------------
// Renders the SAME frame with every available backend and writes each one to
// `dir` as a BMP, alongside a hash of its pixels and a pixel-diff against the
// CPU reference. This is the check the shading-parity workflow asks for: after
// porting a shading change into the OpenCL kernel, the two either agree or they
// do not, and a number says which.
//
// The world is advanced once, up front, and then only app_render runs, so every
// backend draws an identical scene. The simulation is stepped with a fixed dt
// and no input, so two invocations of --dump produce the same frame.
static void run_dump(App* g, SDL_Texture* tex, const char* dir)
{
    const float dt = 1.0f / 60.0f;
    const int   SETTLE_FRAMES = 30;   // let the orbiting light / animation reach a pose

    g->dump_dir = dir;
    const bool hud0 = g->show_hud;
    g->show_hud = false;              // excluded from the image anyway; keeps the log clean

    for (int i = 0; i < SETTLE_FRAMES; ++i) app_update(g, dt);

    printf("\n=== DUMP: %dx%d after %d settle frames -> %s ===\n",
           g->fb.width, g->fb.height, SETTLE_FRAMES, dir);

    struct Row {
        const char*           name;
        uint64_t              hash;
        std::vector<uint32_t> pixels;
    };
    std::vector<Row> rows;

    const int start = g->renderer.index();
    for (int i = 0; i < g->renderer.count(); ++i) {
        const char* name = g->renderer.name(i);
        if (!g->renderer.available(i)) {
            printf("[dump] %-14s unavailable, skipped\n", name);
            continue;
        }
        g->renderer.select(i);
        // Twice: the first frame lets a backend settle any lazily-built state
        // (Embree commits its scene, OpenCL uploads the dynamic BVH). Only the
        // second one is captured, and it is captured by app_render itself, at
        // the same pre-overlay point [F12] uses.
        app_render(g, tex, dt);
        snprintf(g->dump_tag, sizeof(g->dump_tag), "%s", name);
        g->dump_next = true;
        app_render(g, tex, dt);
        rows.push_back({ name, g->dump_hash, g->dump_pixels });
    }
    g->renderer.select(start);

    // ---- Verdict -----------------------------------------------------------
    // Read the two backends differently, because only one of them is supposed to
    // agree at all times:
    //
    //   EMBREE  shares trace_ray with the CPU tracer and differs only in
    //           traversal, so anything past edge-rounding is a real problem.
    //   OCL GPU is a PORT TARGET, not a peer. Shading is prototyped on the CPU
    //           and carried over to the kernel once it has settled, so the GPU
    //           is normally behind and a difference here is the expected state.
    //           It only means something right after a deliberate port.
    //
    // RASTER is a different algorithm entirely: dumped for eyeballing, never
    // compared.
    const Row* ref = nullptr;
    for (const Row& r : rows)
        if (strcmp(r.name, "CPU SOFTWARE") == 0) { ref = &r; break; }

    if (!ref) {
        printf("\n[dump] no CPU SOFTWARE reference frame -> nothing to compare\n");
    } else {
        printf("\n--- parity vs %s ---\n", ref->name);
        const long total = (long)g->fb.width * g->fb.height;
        for (const Row& r : rows) {
            if (&r == ref || strcmp(r.name, "RASTER") == 0) continue;
            const dump::DiffStats d = dump::diff(ref->pixels, r.pixels, 0);
            const bool port_target = strcmp(r.name, "OCL GPU") == 0;
            const char* verdict;
            if      (d.pixels == 0)     verdict = "IDENTICAL";
            else if (d.max_delta <= 2)  verdict = "ok (rounding)";
            else if (port_target)       verdict = "behind CPU (port pending)";
            else                        verdict = "MISMATCH";
            printf("  %-14s %7d / %ld px differ (%.2f%%)  max channel delta %3d  %s\n",
                   r.name, d.pixels, total, 100.0 * d.pixels / (double)total,
                   d.max_delta, verdict);
        }
    }
    fflush(stdout);
    g->show_hud = hud0;
}

int main(int argc, char *argv[])
{
    global_config = parse_args(argc, argv);
    print_config(global_config);

    init_sdl();
    sound_init(global_config.audio_rate);
    sound_set_music_volume(0.5f);
    App *app = app_create(global_config.window_width, global_config.window_height);
    app_init(app);

    if (global_config.dump_dir) {
        run_dump(app, sdl_fb_texture, global_config.dump_dir);
        app_shutdown(app);
        SDL_DestroyTexture(sdl_fb_texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    if (global_config.bench_frames > 0) {
        run_bench(app, sdl_fb_texture, global_config.bench_frames);
        app_shutdown(app);
        SDL_DestroyTexture(sdl_fb_texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    bool running = true;
    const float DT = 1.0f / 60.0f;
    float deltaTimeSeconds = DT;
    float target_delta_ms = 1000.0f / global_config.target_fps;
    uint64_t lastTime, currentTime = SDL_GetTicks64();
    int frameCount = 0;

    while (running)
    {
        lastTime = currentTime;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            app_handle_events(app, event, running);
        }

        if (global_config.debug_mode)
        {
            while (true)
            {
                SDL_Event debugEvent;
                if (SDL_PollEvent(&debugEvent))
                {
                    app_handle_events(app, debugEvent, running);
                    if (debugEvent.type == SDL_KEYDOWN && debugEvent.key.keysym.scancode == SDL_SCANCODE_F2)
                    {
                        deltaTimeSeconds = DT;
                        break;
                    }
                    if (debugEvent.type == SDL_QUIT)
                    {
                        running = false;
                        break;
                    }
                }
            }
        }

        if (frameCount < 5)
        {
            printf("Frame %d: dt=%.4f\n", frameCount, deltaTimeSeconds);
            frameCount++;
        }

        g_prof.begin_frame();
        app_update(app, deltaTimeSeconds);
        app_render(app, sdl_fb_texture, deltaTimeSeconds);

        { PROF_SCOPE(PROF_PRESENT);
          SDL_RenderClear(renderer);
          SDL_RenderCopy(renderer, sdl_fb_texture, NULL, NULL);
          SDL_RenderPresent(renderer); }
        g_prof.end_frame();

        currentTime = SDL_GetTicks64();
        if (currentTime - lastTime < target_delta_ms)
        {
            SDL_Delay((uint32_t)(target_delta_ms - (currentTime - lastTime)));
            currentTime = SDL_GetTicks64();
        }
        deltaTimeSeconds = (currentTime - lastTime) / 1000.0f;
    }

    app_shutdown(app);
    SDL_DestroyTexture(sdl_fb_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
