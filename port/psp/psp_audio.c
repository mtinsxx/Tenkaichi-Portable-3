/*
 * PSP: sound output. The PC port's sound code (snd_adx.c: music and voices from ADX streams; snd_se.c: the game's
 * sound effects) asks SDL3 for "audio streams" that SDL pulls data from through a callback and mixes. This file is
 * that part of SDL on the PSP's audio output: one thread that, every 1024 samples at 44.1 kHz, asks each stream
 * for what it needs, brings it to 44.1 kHz (straight-line interpolation), applies the stream's gain, adds the
 * streams up and hands the block to the audio hardware. Interface: sdlshim/SDL3/SDL.h.
 * One lock for everything (the streams are few and a block takes well under a millisecond to mix); it can be taken
 * again by the thread that holds it, as the callers do.
 */
#include <pspaudio.h>
#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <SDL3/SDL.h>

#define OUT_RATE 44100
#define GRAIN 1024
#define STREAMS 24
#define FIFO 8192 /* frames (stereo) a stream can hold */

struct SDL_AudioStream {
    int used, bound, freq, channels;
    float gain;
    SDL_AudioStreamCallback cb;
    void *user;
    int16_t fifo[FIFO * 2];
    int head, count; /* frames */
    uint32_t frac;   /* position between fifo[head] and the next frame, 16.16 */
};

static struct SDL_AudioStream sStreams[STREAMS];
static SceUID sThread = -1;
static pthread_mutex_t sMutex;
static int sMutexReady;
static volatile int sOwner = -1, sDepth;
static int sChannel = -1;
unsigned gPspAudioBlocks, gPspAudioStarved;
/* Input beyond the bounded stereo FIFO was silently discarded while
   SDL_PutAudioStreamData reported success. Count discarded frames for
   audio-glitch triage; never allocate an unbounded queue on PSP. */
unsigned gPspAudioOverflowFrames;
extern void Psp_Log(const char *fmt, ...);

/* Audio plays on its own hardware-paced 44.1 kHz thread, even when the
   emulated PS2 game cannot finish a frame on time. Expose that playback
   clock so the movie can follow what the user actually hears, rather than
   making an increasingly delayed picture for every game VBlank.
   The counter advances in 1024-sample units: ~23 ms resolution. */
unsigned long long Psp_AudioPlaybackUs(void) {
    unsigned blocks;
    if (sThread < 0 || sChannel < 0) return 0;
    blocks = __sync_fetch_and_add(&gPspAudioBlocks, 0);
    return ((unsigned long long)blocks * GRAIN * 1000000ull) / OUT_RATE;
}

static void lock(void) {
    int me = sceKernelGetThreadId();
    if (sOwner == me) {
        sDepth++;
        return;
    }
    pthread_mutex_lock(&sMutex); /* recursive: a stream's callback takes it again */
    sOwner = me;
    sDepth = 1;
}

static void unlock(void) {
    if (--sDepth == 0) {
        sOwner = -1;
        pthread_mutex_unlock(&sMutex);
    }
}

static int audio_thread(SceSize args, void *argp) {
    static int16_t out[GRAIN * 2] __attribute__((aligned(64)));
    static int32_t mix[GRAIN * 2];
    int i, k;

    (void)args; (void)argp;
    for (;;) {
        memset(mix, 0, sizeof(mix));
        lock();
        for (k = 0; k < STREAMS; k++) {
            struct SDL_AudioStream *s = &sStreams[k];
            uint32_t step;
            int need, g;

            if (!s->used || !s->bound || s->freq <= 0) {
                continue;
            }
            step = (uint32_t)(((uint64_t)s->freq << 16) / OUT_RATE);
            need = (int)(((uint64_t)step * GRAIN + s->frac) >> 16) + 2;
            if (s->count < need && s->cb != NULL) {
                s->cb(s->user, s, (need - s->count) * 2 * s->channels, need * 2 * s->channels);
            }
            if (s->count < 2) {
                continue;
            }
            g = (int)(s->gain * 256.0f);
            for (i = 0; i < GRAIN; i++) {
                const int16_t *a, *b;
                int f = (int)(s->frac >> 8) & 0xFF;
                if (s->count < 2) {
                    gPspAudioStarved++;
                    break;
                }
                a = &s->fifo[(s->head & (FIFO - 1)) * 2];
                b = &s->fifo[((s->head + 1) & (FIFO - 1)) * 2];
                mix[i * 2] += ((a[0] * (256 - f) + b[0] * f) >> 8) * g >> 8;
                mix[i * 2 + 1] += ((a[1] * (256 - f) + b[1] * f) >> 8) * g >> 8;
                s->frac += step;
                s->head += (int)(s->frac >> 16);
                s->count -= (int)(s->frac >> 16);
                s->frac &= 0xFFFF;
            }
            if (s->count < 0) {
                s->count = 0;
            }
        }
        unlock();
        for (i = 0; i < GRAIN * 2; i++) {
            int32_t v = mix[i];
            out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
        gPspAudioBlocks++;
        /* waits for the block to have been taken: this is the thread's clock */
        sceAudioOutputPannedBlocking(sChannel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, out);
    }
    return 0;
}

bool SDL_InitSubSystem(uint32_t flags) { (void)flags; return true; }
const char *SDL_GetCurrentAudioDriver(void) { return "PSP audio output, 44.1 kHz"; }
const char *SDL_GetError(void) { return "PSP audio"; }
bool SDL_ResumeAudioDevice(SDL_AudioDeviceID dev) { (void)dev; return true; }

SDL_AudioDeviceID SDL_OpenAudioDevice(SDL_AudioDeviceID id, const SDL_AudioSpec *spec) {
    (void)id; (void)spec;
    if (sThread >= 0) {
        return 1;
    }
    {
        pthread_mutexattr_t at;
        pthread_mutexattr_init(&at);
        pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
        sMutexReady = pthread_mutex_init(&sMutex, &at) == 0;
    }
    sChannel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, GRAIN, PSP_AUDIO_FORMAT_STEREO);
    if (!sMutexReady || sChannel < 0) {
        Psp_Log("psp: sound: no audio channel (mutex %d, channel %08x)\n", (int)sMutexReady, (unsigned)sChannel);
        return 0;
    }
    sThread = sceKernelCreateThread("bt3_audio", audio_thread, 0x18, 0x8000, PSP_THREAD_ATTR_USER, 0);
    /* above the game's thread: a late block is heard */
    if (sThread < 0 || sceKernelStartThread(sThread, 0, NULL) < 0) {
        Psp_Log("psp: sound: no audio thread (%08x)\n", (unsigned)sThread);
        sThread = -1;
        return 0;
    }
    Psp_Log("psp: sound: audio channel open, 44.1 kHz stereo\n");
    return 1;
}

SDL_AudioStream *SDL_CreateAudioStream(const SDL_AudioSpec *src, const SDL_AudioSpec *dst) {
    int k;

    (void)dst;
    if (!sMutexReady || src == NULL || src->channels < 1 || src->channels > 2) {
        return NULL;
    }
    lock();
    for (k = 0; k < STREAMS && sStreams[k].used; k++) {
    }
    if (k < STREAMS) {
        memset(&sStreams[k], 0, sizeof(sStreams[k]));
        sStreams[k].freq = src->freq;
        sStreams[k].channels = src->channels;
        sStreams[k].gain = 1.0f;
        sStreams[k].used = 1;
    }
    unlock();
    return k < STREAMS ? &sStreams[k] : NULL;
}

bool SDL_SetAudioStreamGetCallback(SDL_AudioStream *s, SDL_AudioStreamCallback cb, void *userdata) {
    if (s == NULL || !sMutexReady) return false;
    lock();
    s->user = userdata;
    s->cb = cb;
    unlock();
    return true;
}

bool SDL_BindAudioStream(SDL_AudioDeviceID dev, SDL_AudioStream *s) {
    (void)dev;
    if (s == NULL || !sMutexReady) return false;
    lock();
    s->bound = 1;
    unlock();
    return true;
}

/* (from a stream's callback, or under SDL_LockAudioStream) */
bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len) {
    const int16_t *p = buf;
    int frames, i;

    if (s == NULL || !sMutexReady || buf == NULL || len < 0 ||
        s->channels < 1 || s->channels > 2) return false;
    frames = len / (2 * s->channels);
    /* May be called inside a callback while the mixer owns the lock:
       lock() is recursive for precisely this case. */
    lock();
    for (i = 0; i < frames && s->count < FIFO; i++) {
        int16_t *o = &s->fifo[((s->head + s->count) & (FIFO - 1)) * 2];
        if (s->channels == 2) {
            o[0] = p[i * 2];
            o[1] = p[i * 2 + 1];
        } else {
            o[0] = o[1] = p[i];
        }
        s->count++;
    }
    if (i < frames) __sync_fetch_and_add(&gPspAudioOverflowFrames, (unsigned)(frames - i));
    unlock();
    return i == frames;
}

bool SDL_SetAudioStreamGain(SDL_AudioStream *s, float gain) {
    if (s == NULL || !sMutexReady) return false;
    lock();
    s->gain = gain;
    unlock();
    return true;
}
bool SDL_LockAudioStream(SDL_AudioStream *s) {
    if (s == NULL || !sMutexReady) return false;
    lock();
    return true;
}
bool SDL_UnlockAudioStream(SDL_AudioStream *s) {
    if (s == NULL || !sMutexReady) return false;
    unlock();
    return true;
}

bool SDL_ClearAudioStream(SDL_AudioStream *s) {
    if (s == NULL || !sMutexReady) return false;
    lock();
    s->count = 0;
    s->frac = 0;
    unlock();
    return true;
}

void SDL_DestroyAudioStream(SDL_AudioStream *s) {
    if (s == NULL || !sMutexReady) return;
    lock();
    s->used = 0;
    s->bound = 0;
    s->cb = NULL;
    unlock();
}
