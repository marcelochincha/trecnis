#include "rtt_audio.h"

void audio_init(AudioSystem* a, AudioPlayFn fn, void* user) {
    memset(a, 0, sizeof(*a));
    for (int i = 0; i < AUD_COOLDOWN_SLOTS; i++) a->cooldown[i].sound = -1;
    for (int i = 0; i < CAT_COUNT; i++) a->category_volume[i] = 1.0f;
    a->duck = 1.0f;
    a->cooldown_seconds = 0.1f;                       /* placeholder until decoded [OPEN] */
    a->play = fn;
    a->play_user = user;
}

int audio_add_cue(AudioSystem* a, const char* name, int bank, int first, int variants, AudioCategory cat) {
    if (a->cue_count >= AUD_MAX_CUES) return -1;
    AudioCue* c = &a->cues[a->cue_count];
    strncpy(c->name, name, RTT_NAME_LEN - 1);
    c->bank = bank; c->first_sound = first; c->variant_count = variants; c->category = cat;
    return a->cue_count++;
}

int audio_find(const AudioSystem* a, const char* name) {
    for (int i = 0; i < a->cue_count; i++)
        if (strcmp(a->cues[i].name, name) == 0) return i;
    return -1;
}

int audio_play_cue(AudioSystem* a, int cue, float pan, Rng* rng) {
    if (cue < 0 || cue >= a->cue_count) return -1;
    const AudioCue* c = &a->cues[cue];
    if (c->variant_count <= 0) return -1;
    int sound = c->first_sound + rng_int(rng, c->variant_count);   /* random() % count, all slots [RTT] */
    for (int i = 0; i < AUD_COOLDOWN_SLOTS; i++)
        if (a->cooldown[i].sound == sound && a->now < a->cooldown[i].until) {
            a->cooldown[i].until = a->now + a->cooldown_seconds;    /* refresh, skip playback [RTT] */
            return -1;
        }
    a->cooldown[a->cooldown_next].sound = sound;
    a->cooldown[a->cooldown_next].until = a->now + a->cooldown_seconds;
    a->cooldown_next = (a->cooldown_next + 1) % AUD_COOLDOWN_SLOTS;
    float vol = a->category_volume[c->category] * (c->category == CAT_MUSIC ? 1.0f : a->duck);
    if (a->play) a->play(a->play_user, sound, vol, pan);
    return sound;
}

int audio_play_named(AudioSystem* a, const char* name, float pan, Rng* rng) {
    return audio_play_cue(a, audio_find(a, name), pan, rng);
}
