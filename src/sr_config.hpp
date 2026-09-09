#pragma once

#include <cstring>
#include <cstdlib>
#include <iostream>

#ifndef W_WIDTH
#define W_WIDTH 480
#endif
#ifndef W_HEIGHT
#define W_HEIGHT 360
#endif
// The window is this many times larger than the framebuffer. Every backend here
// renders on the CPU, so pixels are the cost driver: scaling the window up costs
// nothing, while raising --width/--height quadruples the work.
#ifndef W_SCALE
#define W_SCALE 2
#endif
#define TARGET_FPS 60.0f
#define AUDIO_RATE 8192 * 2
#define DEBUG false

struct config
{
    int window_width;    // framebuffer size: what actually gets rendered
    int window_height;
    int window_scale;    // window is window_width * window_scale pixels wide
    float target_fps;
    int audio_rate;
    bool debug_mode;
    int  num_workers; // CPU render threads; -1 = all hardware threads

    config()
        : window_width(W_WIDTH),
          window_height(W_HEIGHT),
          window_scale(W_SCALE),
          target_fps(TARGET_FPS),
          audio_rate(AUDIO_RATE),
          debug_mode(DEBUG),
          num_workers(-1)
    {
    }
};

inline void print_help()
{
    printf("Usage:\n");
    printf("  --width <pixels>        Render width (default: %d)\n", W_WIDTH);
    printf("  --height <pixels>       Render height (default: %d)\n", W_HEIGHT);
    printf("  --scale <n>             Window magnification, 1-8 (default: %d).\n", W_SCALE);
    printf("                          Enlarges the window without rendering more\n");
    printf("                          pixels, so it is free; --width/--height are\n");
    printf("                          not.\n");
    printf("  --fps <value>           Target FPS (default: 60.0)\n");
    printf("  --audio-rate <hz>       Audio sample rate (default: %d)\n", AUDIO_RATE);
    printf("  --threads <n>           CPU render threads (-1 = all cores)\n");
    printf("  --debug                 Enable debug mode\n");
    printf("  --help                  Show this help\n");
    printf("\nExample: bvh_raytracer.exe --scale 3            (same speed, 3x bigger)\n");
    printf("         bvh_raytracer.exe --width 960 --height 720  (sharper, slower)\n");
}

inline config parse_args(int argc, char* argv[])
{
    config cfg;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            cfg.window_width = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            cfg.window_height = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
        {
            int s = atoi(argv[++i]);
            cfg.window_scale = (s < 1) ? 1 : (s > 8 ? 8 : s);
        }
        else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
        {
            cfg.target_fps = (float)atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--audio-rate") == 0 && i + 1 < argc)
        {
            cfg.audio_rate = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
        {
            cfg.num_workers = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--debug") == 0)
        {
            cfg.debug_mode = true;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            print_help();
            exit(0);
        }
    }

    return cfg;
}

inline void print_config(const config& cfg)
{
    printf("Configuration loaded:\n");
    printf("  Render: %dx%d\n", cfg.window_width, cfg.window_height);
    printf("  Window: %dx%d (scale %dx)\n", cfg.window_width * cfg.window_scale,
           cfg.window_height * cfg.window_scale, cfg.window_scale);
    printf("  Target FPS: %.1f\n", cfg.target_fps);
    printf("  Audio Rate: %d Hz\n", cfg.audio_rate);
    printf("  Debug Mode: %s\n", cfg.debug_mode ? "ON" : "OFF");
}

inline config global_config;
