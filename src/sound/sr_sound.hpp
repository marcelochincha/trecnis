#pragma once

#include <SDL2/SDL_mixer.h>
#include <cstdio>
#include <vector>

/*
    SOUND: Ultra-simple audio system using SDL_mixer.
    - Music:   streamed background track (xm, ogg, mp3, mid)
    - Samples: one-shot sound effects (wav)
*/

struct sr_sound_state
{
    Mix_Music *music;
    std::vector<Mix_Chunk *> samples;

    sr_sound_state() : music(nullptr) {}
};

inline sr_sound_state g_sound;

inline bool sound_init(int freq = 22050, int channels = 1, int chunk_size = 1024)
{
    if (Mix_OpenAudio(freq, AUDIO_S16, channels, chunk_size) < 0)
    {
        printf("sound_init failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MOD | MIX_INIT_MP3);

    // SDL_mixer may silently open at a different rate than requested
    int actual_freq; Uint16 actual_format; int actual_channels;
    Mix_QuerySpec(&actual_freq, &actual_format, &actual_channels);
    printf("sound_init: requested=%d Hz  actual=%d Hz  format=0x%04X  ch=%d\n",
           freq, actual_freq, actual_format, actual_channels);

    return true;
}

inline void sound_play_music(const char *path, bool loop = true, int fade_in_ms = 0)
{
    if (g_sound.music) { Mix_FreeMusic(g_sound.music); g_sound.music = nullptr; }
    g_sound.music = Mix_LoadMUS(path);
    if (!g_sound.music) { printf("sound_play_music: failed to load '%s': %s\n", path, Mix_GetError()); return; }
    int loops = loop ? -1 : 1;
    if (fade_in_ms > 0) Mix_FadeInMusic(g_sound.music, loops, fade_in_ms);
    else Mix_PlayMusic(g_sound.music, loops);
}

inline void sound_stop_music(int fade_out_ms = 0)
{
    if (fade_out_ms > 0) Mix_FadeOutMusic(fade_out_ms);
    else Mix_HaltMusic();
}

// Software post-mix gain. SDL_mixer's native volume is capped at
// MIX_MAX_VOLUME (128), so to go LOUDER than 1.0 we amplify the final
// mixed buffer ourselves. Stored in a separate global so the callback can
// read it without capturing state.
inline float g_post_mix_gain = 1.0f;

inline void sr_postmix_amplify(void * /*udata*/, Uint8 *stream, int len)
{
    float g = g_post_mix_gain;
    if (g <= 1.0f) return;
    Sint16 *s = (Sint16 *)stream;
    int n = len / (int)sizeof(Sint16);
    for (int i = 0; i < n; ++i)
    {
        int v = (int)(s[i] * g);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        s[i] = (Sint16)v;
    }
}

inline void sound_set_music_volume(float v)
{
    // v in [0, 1] → native SDL_mixer volume (no distortion).
    // v > 1.0     → native pinned at max + software post-mix amplification
    //               (can clip; useful when the source file is too quiet).
    float native = (v > 1.0f) ? 1.0f : (v < 0.0f ? 0.0f : v);
    int vol = (int)(native * MIX_MAX_VOLUME);
    Mix_VolumeMusic(vol);

    g_post_mix_gain = (v > 1.0f) ? v : 1.0f;
    Mix_SetPostMix(g_post_mix_gain > 1.0f ? sr_postmix_amplify : nullptr, nullptr);
}

// Returns sample slot index, or -1 on failure
inline int sound_load_sample(const char *path)
{
    Mix_Chunk *chunk = Mix_LoadWAV(path);
    if (!chunk) { printf("sound_load_sample: failed to load '%s': %s\n", path, Mix_GetError()); return -1; }
    int slot = (int)g_sound.samples.size();
    g_sound.samples.push_back(chunk);
    return slot;
}

inline void sound_play_sample(int slot, float volume = 1.0f)
{
    if (slot < 0 || slot >= (int)g_sound.samples.size() || !g_sound.samples[slot]) return;
    int vol = (int)(volume * MIX_MAX_VOLUME);
    if (vol < 0) vol = 0;
    if (vol > MIX_MAX_VOLUME) vol = MIX_MAX_VOLUME;
    Mix_VolumeChunk(g_sound.samples[slot], vol);
    Mix_PlayChannel(-1, g_sound.samples[slot], 0);
}

inline void sound_shutdown()
{
    Mix_HaltMusic();
    if (g_sound.music) { Mix_FreeMusic(g_sound.music); g_sound.music = nullptr; }
    for (Mix_Chunk *&chunk : g_sound.samples)
    {
        if (chunk) { Mix_FreeChunk(chunk); chunk = nullptr; }
    }
    g_sound.samples.clear();
    Mix_CloseAudio();
    Mix_Quit();
}
