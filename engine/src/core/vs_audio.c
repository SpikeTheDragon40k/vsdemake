/* Sound mixer (notes/behaviour/audio.md). See vs_audio.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/vs_audio.h"
#include "core/vs_compat.h"
#include "core/vs_platform.h"
#include "core/vs_vorbis.h"

#define MAX_SOUNDS 48
#define MAX_VOICES 32
#define MAX_THROTTLE 16
#define STREAM_SECONDS 20.0f     /* longer sounds are decoded while playing */
#define STREAM_CHUNK 2048        /* frames decoded at a time for streams */

typedef struct {
    char key[32];
    const uint8_t *ogg; int ogg_size;
    int channels, rate;
    int16_t *pcm; int frames;    /* decoded sounds */
    int streamed;
    float throttle_until[MAX_THROTTLE];
} snd_t;

typedef struct {
    int active, id, loop;
    double pos, step;            /* position in source frames, frames per output frame */
    float vol;
    /* streamed sources: a window of decoded frames [win_start, win_start + win_len) */
    stb_vorbis *vorbis;
    int16_t *win; int win_len, win_cap; long win_start; int stream_done;
} voice_t;

static snd_t g_snd[MAX_SOUNDS];
static int g_snd_count;
static voice_t g_voice[MAX_VOICES];
static voice_t g_music;
static int g_music_id = -1;
static float g_music_base, g_music_cur, g_music_target, g_music_fade_ms, g_music_fade_left;
static float g_sound_volume = 0.5f, g_music_volume = 0.5f;
static float g_clock_ms;
static float g_mixbuf[2 * 4096];
static int g_last_peak, g_max_peak;

/* ---- loading ---- */

int vs_audio_init(const vs_pack *pack) {
    vs_audio_shutdown();
    for (uint32_t i = 0; i < pack->count && g_snd_count < MAX_SOUNDS; i++) {
        if (!vs_pack_entry_is(pack, (int)i, "SND")) continue;
        uint32_t size;
        const uint8_t *b = vs_pack_blob(pack, (int)i, &size);
        if (!b || size < 16 || vs_be32(b) != 1) continue;
        uint32_t n = vs_be32(b + 4);
        if (16 + n > size) continue;
        snd_t *s = &g_snd[g_snd_count];
        memset(s, 0, sizeof *s);
        snprintf(s->key, sizeof s->key, "%s", vs_pack_entry_name(pack, (int)i));
        s->ogg = b + 16; s->ogg_size = (int)n;
        int err = 0;
        stb_vorbis *v = stb_vorbis_open_memory(s->ogg, s->ogg_size, &err, NULL);
        if (!v) { vs_plat_log("audio: cannot open %s (stb_vorbis error %d)", s->key, err); continue; }
        stb_vorbis_info info = stb_vorbis_get_info(v);
        s->channels = info.channels > 2 ? 2 : info.channels;
        s->rate = (int)info.sample_rate;
        float seconds = stb_vorbis_stream_length_in_seconds(v);
        s->streamed = seconds > STREAM_SECONDS;
        if (!s->streamed) {
            int total = (int)stb_vorbis_stream_length_in_samples(v);
            s->pcm = (int16_t *)malloc((size_t)(total + 1) * (size_t)s->channels * sizeof(int16_t));
            if (s->pcm) {
                s->frames = stb_vorbis_get_samples_short_interleaved(v, s->channels, s->pcm, total * s->channels);
                memset(s->pcm + (size_t)s->frames * s->channels, 0, (size_t)s->channels * sizeof(int16_t));
            }
        }
        stb_vorbis_close(v);
        if (!s->streamed && !s->pcm) continue;
        g_snd_count++;
    }
    vs_plat_log("audio: %d sounds", g_snd_count);
    return g_snd_count;
}

static void voice_release(voice_t *v) {
    if (v->vorbis) stb_vorbis_close(v->vorbis);
    free(v->win);
    memset(v, 0, sizeof *v);
}

void vs_audio_shutdown(void) {
    for (int i = 0; i < MAX_VOICES; i++) voice_release(&g_voice[i]);
    voice_release(&g_music);
    for (int i = 0; i < g_snd_count; i++) free(g_snd[i].pcm);
    memset(g_snd, 0, sizeof g_snd);
    g_snd_count = 0;
    g_music_id = -1;
}

int vs_audio_find(const char *key) {
    for (int i = 0; i < g_snd_count; i++) if (strcmp(g_snd[i].key, key) == 0) return i;
    return -1;
}

/* ---- voices ---- */

static int voice_start(voice_t *v, int id, float vol, float speed, int loop) {
    snd_t *s = &g_snd[id];
    voice_release(v);
    v->id = id; v->vol = vol; v->loop = loop;
    v->step = (double)s->rate / VS_AUDIO_RATE * speed;
    if (s->streamed) {
        int err = 0;
        v->vorbis = stb_vorbis_open_memory(s->ogg, s->ogg_size, &err, NULL);
        v->win_cap = STREAM_CHUNK * 2;
        v->win = (int16_t *)malloc((size_t)v->win_cap * (size_t)s->channels * sizeof(int16_t));
        if (!v->vorbis || !v->win) { voice_release(v); return -1; }
    }
    v->active = 1;
    return 0;
}

/* Streams: makes frames [i, i + 1] available in the window. 0 at the end. */
static int stream_ensure(voice_t *v, long i) {
    snd_t *s = &g_snd[v->id];
    int ch = s->channels;
    while (i + 1 >= v->win_start + v->win_len) {
        if (v->stream_done) return i < v->win_start + v->win_len;
        /* keep the last frame for interpolation, drop the rest */
        if (v->win_len > 1) {
            int keep = 1;
            memmove(v->win, v->win + (size_t)(v->win_len - keep) * ch, (size_t)keep * ch * sizeof(int16_t));
            v->win_start += v->win_len - keep;
            v->win_len = keep;
        }
        int got = stb_vorbis_get_samples_short_interleaved(v->vorbis, ch, v->win + (size_t)v->win_len * ch, (v->win_cap - v->win_len) * ch);
        if (got <= 0) {
            if (v->loop) { stb_vorbis_seek_start(v->vorbis); continue; }
            v->stream_done = 1;
            return i < v->win_start + v->win_len;
        }
        v->win_len += got;
    }
    return 1;
}

/* Adds `frames` of voice v at volume `vol` into the float mix. 0 when finished. */
static int voice_mix(voice_t *v, float *mix, int frames, float vol) {
    snd_t *s = &g_snd[v->id];
    int ch = s->channels;
    for (int f = 0; f < frames; f++) {
        long i = (long)v->pos;
        float frac = (float)(v->pos - (double)i);
        const int16_t *a, *b;
        if (s->streamed) {
            if (!stream_ensure(v, i)) return 0;
            a = v->win + (size_t)(i - v->win_start) * ch;
            b = (i + 1 < v->win_start + v->win_len) ? a + ch : a;
        } else {
            if (i >= s->frames) {
                if (!v->loop || s->frames == 0) return 0;
                v->pos -= s->frames; i -= s->frames;
            }
            a = s->pcm + (size_t)i * ch;
            b = a + ch;                 /* one zero frame is kept after the end */
        }
        float l = a[0] + (b[0] - a[0]) * frac;
        float r = ch == 2 ? a[1] + (b[1] - a[1]) * frac : l;
        mix[2 * f] += l * vol;
        mix[2 * f + 1] += r * vol;
        v->pos += v->step;
    }
    return 1;
}

/* ---- the original's sound manager ---- */

void vs_audio_play(int id, float volume, float detune_cents, float rate, float delay_ms, int max_instances) {
    if (id < 0 || id >= g_snd_count || g_sound_volume <= 0) return;
    snd_t *s = &g_snd[id];
    if (delay_ms > 0 && max_instances > 0) {
        int busy = 0, slot = -1;
        for (int k = 0; k < MAX_THROTTLE; k++) {
            if (s->throttle_until[k] > g_clock_ms) busy++;
            else if (slot < 0) slot = k;
        }
        if (busy >= max_instances || slot < 0) return;
        s->throttle_until[slot] = g_clock_ms + delay_ms;
    }
    float vol = (volume >= 0 ? volume * 0.5f : 0.1f) * g_sound_volume;
    float speed = (rate > 0 ? rate : 1.0f) * powf(2.0f, detune_cents / 1200.0f);
    voice_t *v = NULL;
    for (int k = 0; k < MAX_VOICES && !v; k++) if (!g_voice[k].active) v = &g_voice[k];
    if (!v) {                                   /* steal the voice nearest its end */
        double best = -1;
        for (int k = 0; k < MAX_VOICES; k++) {
            voice_t *c = &g_voice[k];
            double done = g_snd[c->id].frames ? c->pos / g_snd[c->id].frames : 0;
            if (!g_snd[c->id].streamed && done > best) { best = done; v = c; }
        }
        if (!v) return;
    }
    voice_start(v, id, vol, speed, 0);
}

void vs_audio_stop(int id) {
    for (int k = 0; k < MAX_VOICES; k++) if (g_voice[k].active && g_voice[k].id == id) voice_release(&g_voice[k]);
}

void vs_audio_music_play(int id, float volume, int loop) {
    if (id < 0 || id >= g_snd_count) return;
    g_music_id = id;
    g_music_base = volume;
    g_music_cur = g_music_target = volume;
    g_music_fade_left = 0;
    voice_start(&g_music, id, 1.0f, 1.0f, loop);
}

void vs_audio_music_fade(float volume, float ms) {
    if (g_music_id < 0) return;
    g_music_base = volume;
    if (ms <= 0) { g_music_cur = g_music_target = volume; g_music_fade_left = 0; return; }
    g_music_target = volume; g_music_fade_ms = g_music_fade_left = ms;
}

void vs_audio_music_stop(void) { voice_release(&g_music); g_music_id = -1; }
int vs_audio_music_current(void) { return g_music_id; }

void vs_audio_set_volumes(float sound, float music) {
    g_sound_volume = sound < 0 ? 0 : sound > 1 ? 1 : sound;
    g_music_volume = music < 0 ? 0 : music > 1 ? 1 : music;
}

void vs_audio_advance(float ms) {
    g_clock_ms += ms;
    if (g_music_fade_left > 0) {
        float t = ms < g_music_fade_left ? ms / g_music_fade_left : 1.0f;
        g_music_cur += (g_music_target - g_music_cur) * t;
        g_music_fade_left -= ms;
        if (g_music_fade_left <= 0) g_music_cur = g_music_target;
    }
}

void vs_audio_mix(int16_t *out, int frames) {
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        memset(g_mixbuf, 0, sizeof(float) * 2 * (size_t)n);
        for (int k = 0; k < MAX_VOICES; k++)
            if (g_voice[k].active && !voice_mix(&g_voice[k], g_mixbuf, n, g_voice[k].vol)) voice_release(&g_voice[k]);
        if (g_music.active) {
            float mv = g_music_cur * g_music_volume;
            if (!voice_mix(&g_music, g_mixbuf, n, mv)) { voice_release(&g_music); g_music_id = -1; }
        }
        int peak = 0;
        for (int i = 0; i < 2 * n; i++) {
            float x = g_mixbuf[i];
            out[i] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)x);
            int a = out[i] < 0 ? -out[i] : out[i];
            if (a > peak) peak = a;
        }
        g_last_peak = peak;
        if (peak > g_max_peak) g_max_peak = peak;
        out += 2 * n;
        frames -= n;
    }
}

float vs_audio_decoded_seconds(void) {
    double frames = 0;
    for (int i = 0; i < g_snd_count; i++) if (g_snd[i].rate > 0) frames += (double)g_snd[i].frames / g_snd[i].rate;
    return (float)frames;
}
int vs_audio_last_peak(void) { return g_last_peak; }
int vs_audio_max_peak(void) { return g_max_peak; }
