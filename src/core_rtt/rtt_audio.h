/*
 * rtt_audio.h -- cue dispatch shared by every bank-based sound (xactSound::Prepare 0x801ea320).
 * Docs: map/audio-cue-dispatch.md, ASSET-TRANSLATION.md §3, map/audio-mixing.md.
 *
 * name -> variant pool (from .td + .cue, exported per variant) -> uniform random pick over ALL
 * variants -> per-exact-sound cooldown (5-entry table, FUN_801ea844) -> backend play.
 */
#ifndef RTT_AUDIO_H
#define RTT_AUDIO_H

#include "rtt_core.h"

enum { AUD_MAX_CUES = 8192, AUD_MAX_VARIANTS = 64, AUD_COOLDOWN_SLOTS = 5 };

typedef enum { CAT_MUSIC, CAT_SFX, CAT_SPEECH, CAT_ANNOUNCER, CAT_CROWD, CAT_COUNT } AudioCategory; /* 5 sliders [RTT] */

typedef struct {
    char  name[RTT_NAME_LEN];         /* e.g. "Announcer_Serve_FRENCH" [DATA .td] */
    int   bank;                       /* bank index */
    int   first_sound;                /* index into the loaded sound table */
    int   variant_count;              /* all variants are valid [RTT] */
    AudioCategory category;
} AudioCue;

/* backend: returns a handle, plays sound `sound` at volume [0,1] and pan [-1,1] [CHOICE] */
typedef int (*AudioPlayFn)(void* user, int sound, float volume, float pan);

typedef struct {
    AudioCue cues[AUD_MAX_CUES];
    int      cue_count;
    struct { int sound; float until; } cooldown[AUD_COOLDOWN_SLOTS];
    int      cooldown_next;
    float    cooldown_seconds;        /* tuned value not decoded [OPEN] */
    float    category_volume[CAT_COUNT];
    float    duck;                    /* global ducking multiplier, audio-mixing.md [RTT] */
    AudioPlayFn play;
    void*    play_user;
    float    now;
} AudioSystem;

void audio_init(AudioSystem* a, AudioPlayFn fn, void* user);
int  audio_add_cue(AudioSystem* a, const char* name, int bank, int first_sound, int variants, AudioCategory cat);
/* RTT callers build the name at runtime (sprintf, e.g. "Announcer_Serve_%s") and resolve it
 * by name; MUSIC passes an integer directly. Linear lookup here; hash it in TRECNIS [CHOICE]. */
int  audio_find(const AudioSystem* a, const char* name);
/* returns the played sound index, or -1 if suppressed by the cooldown / unknown */
int  audio_play_cue(AudioSystem* a, int cue, float pan, Rng* rng);
int  audio_play_named(AudioSystem* a, const char* name, float pan, Rng* rng);

#endif /* RTT_AUDIO_H */
