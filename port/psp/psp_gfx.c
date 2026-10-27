/*
 * PSP frame pacing and controller interface.
 * Input is normalized and mapped by psp_controls.h.
 * Portable fight controls are default in Tenkaichi Portable 3.
 * Menus, pauses and cutscenes retain the original mappings.
 */
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "psp_controls.h"

/* The vertical blank's grid: one every 1 / 59.94 s, as the console's. */
static unsigned long long sNext;
static unsigned long long sClockWaitUs; /* amount actually slept by game's VBlank clock */
#define VB_US 16683ull

void Psp_VBlankWait(void) {
    unsigned long long t = (unsigned long long)sceKernelGetSystemTimeWide();
    if (sNext > t && sNext - t < 100000ull) {
        sceKernelDelayThread((unsigned)(sNext - t));
        sClockWaitUs += (unsigned long long)sceKernelGetSystemTimeWide() - t;
        sNext += VB_US;
    } else {
        sNext = t + VB_US; /* late (or the first one): start a new grid from now */
    }
}

/*
 * Physical PSP-to-PS2 pad conversion is in psp_controls.h, where it can
 * be unit-tested without the PSP SDK. Legacy is default: opt into the
 * PSP-native fight layout is embedded as the default.
 * controls=legacy in bt3.txt explicitly restores the former mapping.
 */
int gPspControlsProfile = PSP_PROFILE_DEFAULT;
static PspControlsState sPspControls;
extern int Psp_InFight(void);
extern void Psp_Log(const char *fmt, ...);
/* Count real input samples to confirm the live-fight profile is reached. */
static unsigned sInputPortableLive, sInputPortableMenu, sInputLegacy;
static unsigned sPortableActivatedOnce;

int Port_PadRead(int socket, unsigned char *data) {
    static const struct { unsigned psp, normalized; } inputs[] = {
        {PSP_CTRL_START, PI_START}, {PSP_CTRL_SELECT, PI_SELECT},
        {PSP_CTRL_UP, PI_UP}, {PSP_CTRL_RIGHT, PI_RIGHT},
        {PSP_CTRL_DOWN, PI_DOWN}, {PSP_CTRL_LEFT, PI_LEFT},
        {PSP_CTRL_LTRIGGER, PI_L}, {PSP_CTRL_RTRIGGER, PI_R},
        {PSP_CTRL_TRIANGLE, PI_TRIANGLE}, {PSP_CTRL_CIRCLE, PI_CIRCLE},
        {PSP_CTRL_CROSS, PI_CROSS}, {PSP_CTRL_SQUARE, PI_SQUARE}
    };
    SceCtrlData pad;
    unsigned normalized = 0, i;

    if (socket != 0) return 0;
    memset(&pad, 0, sizeof(pad));
    pad.Lx = pad.Ly = 0x80;
    sceCtrlPeekBufferPositive(&pad, 1);
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        if (pad.Buttons & inputs[i].psp) normalized |= inputs[i].normalized;
    }
    {
        int in_fight = Psp_InFight();
        if (gPspControlsProfile == PSP_PROFILE_PORTABLE) {
            if (in_fight) {
                ++sInputPortableLive;
                if (!sPortableActivatedOnce) {
                    Psp_Log("psp: INPUT portable fighting controls ACTIVE (first live fight sample)\n");
                    sPortableActivatedOnce = 1;
                }
            } else {
                ++sInputPortableMenu;
            }
        } else {
            ++sInputLegacy;
        }
        PspControls_Encode(&sPspControls, normalized, gPspControlsProfile, in_fight,
                           (uint8_t)pad.Lx, (uint8_t)pad.Ly, data);
    }
    return 18;
}

extern unsigned long long Psp_TimeUs(void);

/* ---- the report: every two seconds, how fast the game is running and where its time goes (bt3.txt report=1) ---- */
extern unsigned gPortVBlanks;
int gPspReport = 1;
extern void Psp_Log(const char *fmt, ...);
extern void Psp_LogFlush(void);
extern void Psp_GuStats(unsigned out[4]);
extern unsigned Psp_GuFramesPresented(void);
extern unsigned gPspAudioOverflowFrames;

void Psp_VBlank(unsigned vblanks) {
    static unsigned long long t0;
    static unsigned vb0, drawn0;
    unsigned long long t;
    unsigned vb;

    if (!gPspReport) {
        return;
    }
    if (t0 == 0) {
        t0 = Psp_TimeUs();
        vb0 = vblanks;
        drawn0 = Psp_GuFramesPresented();
        return;
    }
    t = Psp_TimeUs();
    if (t - t0 < 2000000ull) {
        return;
    }
    vb = vblanks - vb0;
    Psp_Log("psp: INPUT profile=%s samples: portable-fight=%u original-menu=%u legacy-selected=%u\n",
            gPspControlsProfile == PSP_PROFILE_PORTABLE ? "portable" : "legacy",
            sInputPortableLive, sInputPortableMenu, sInputLegacy);
    sInputPortableLive = sInputPortableMenu = sInputLegacy = 0;
    {
        unsigned completed = Psp_GuFramesPresented();
        unsigned presented = completed - drawn0;
        unsigned fps10 = (unsigned)((unsigned long long)presented * 10000000ull / (t - t0));
        Psp_Log("psp: frame cadence: %u completed GS frames / %u simulated VBlanks in %u.%02u s = %u.%u real GS FPS\n",
                presented, vb, (unsigned)((t - t0) / 1000000),
                (unsigned)((t - t0) / 10000 % 100), fps10 / 10, fps10 % 10);
        drawn0 = completed;
        Psp_Log("psp: game-clock VBlank sleep=%u ms in sample (swap-VBlank wait is recorded separately by GU)\n",
                (unsigned)(sClockWaitUs / 1000ull));
        sClockWaitUs = 0;
    }
    {
        unsigned perSec10 = (unsigned)((unsigned long long)vb * 10000000ull / (t - t0));
        unsigned stat[4] = {0, 0, 0, 0};
        Psp_GuStats(stat);
        {
            unsigned dropped = __sync_lock_test_and_set(&gPspAudioOverflowFrames, 0);
            if (dropped != 0)
                Psp_Log("psp: audio FIFO saturated: %u input frames dropped in report interval\n", dropped);
        }
        Psp_Log("psp: %u blanks in %u.%02u s = %u.%u a second = %u%% of the console's speed"
                " (draws %u, batches %u, new textures %u, left out %u)\n",
                vb, (unsigned)((t - t0) / 1000000), (unsigned)((t - t0) / 10000 % 100), perSec10 / 10, perSec10 % 10,
                perSec10 * 1000u / 5994u, stat[0], stat[1], stat[2], stat[3]);
    }
    /* The previous ~40 log entries are one coherent interval; write
       them to the memory stick together, not with 40 blocking fflush
       calls scattered between menu and battle presentation. */
    Psp_LogFlush();
    t0 = t;
    vb0 = vblanks;
}

/* ---- the renderer's switches: gs_core.c and gs_vu1.c ask the environment in their innermost loops (developer
   switches of the PC build). Here every such question is answered at once. ---- */
char *Psp_GsGetenv(const char *name) {
    /* This also replaces getenv for the GS/VU1 front end; its input is
       not necessarily a constant with five characters. Never index
       name[4] before confirming the string is long enough. */
    if (name == NULL) return NULL;
    if (strcmp(name, "BT3_GS") == 0) {
        return "gpu"; /* the GPU backend draws; no software rasterizer on PSP */
    }
    if (strcmp(name, "BT3_VU_INTERP") == 0) {
        return "1"; /* keep interpreter ON until native VU backends exist */
    }
    return NULL;
}
