/* Sound mixer (notes/behaviour/audio.md). Platform independent: the game
 * calls vs_audio_mix() to produce 44.1 kHz stereo 16-bit frames and hands
 * them to the platform (vs_plat_audio_push). No threads. */
#ifndef VS_AUDIO_H
#define VS_AUDIO_H

#include <stdint.h>

#include "core/vs_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VS_AUDIO_RATE 44100

/* Loads every SND entry of the pack: short sounds are decoded now, long
 * ones (music) are decoded while playing. Returns the number of sounds. */
int vs_audio_init(const vs_pack *pack);
void vs_audio_shutdown(void);

/* Sound id for a key ("Gem", "BGM_Forest", ...), or -1. */
int vs_audio_find(const char *key);

/* The original's PlaySound: volume < 0 means "none given" (0.1 is used);
 * otherwise volume × 0.5. Then × the sound volume option. detune in cents,
 * rate multiplies the speed. delay_ms/max_instances throttle the key
 * (both > 0 to apply). */
void vs_audio_play(int id, float volume, float detune_cents, float rate, float delay_ms, int max_instances);
void vs_audio_stop(int id);
/* Moves the sound the latest vs_audio_play started to `ms` from its start
 * (the original's `seek`; decoded sounds only). */
void vs_audio_seek_last(float ms);

/* Music: one track at a time. volume is the base (0.3 in the game), played
 * at base × music volume option. */
void vs_audio_music_play(int id, float volume, int loop);
void vs_audio_music_fade(float volume, float ms);
void vs_audio_music_stop(void);
int vs_audio_music_current(void);

/* Options, 0..1 (the original's defaults are 0.5). */
void vs_audio_set_volumes(float sound, float music);

/* Advances the audio clock (throttles, fades) by ms. */
void vs_audio_advance(float ms);

/* Diagnostics: seconds of sound decoded at start-up, peak sample of the
 * most recent mix (0..32767). */
float vs_audio_decoded_seconds(void);
int vs_audio_last_peak(void);
int vs_audio_max_peak(void);   /* loudest sample since start-up */
/* Diagnostics: a 1-second 440 Hz tone mixed in directly (no decoder). */
void vs_audio_test_tone(void);
/* Diagnostics: one line of mixer state (volumes, voices, plays, sound `key`). */
void vs_audio_debug(const char *key, char *buf, int size);

/* Mixes `frames` stereo frames (interleaved L, R) into out. */
void vs_audio_mix(int16_t *out, int frames);

#ifdef __cplusplus
}
#endif

#endif
