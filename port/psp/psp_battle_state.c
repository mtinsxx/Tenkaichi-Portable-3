/*
 * PSP battle state bridge: compile WITHOUT PSP SDK headers.
 *
 * PSPSDK defines types such as s32 differently from the game's types.h.
 * The game header belongs in a translation unit without those SDK headers,
 * so psp_main.c can use PSP SDK types safely.
 *
 * Port_compat fixes the PS2 64-bit s64/u64 representations for the PSP.
 */
#include "port_compat.h"
#include "battle/battle.h"
#include "psp_controls.h"
#include <stdint.h>

/* Same game sequence state declaration previously used by psp_main.c. */
extern struct { int state; } *gBtlSeq;

int Psp_InBattle(void) {
    return gBtlSeq != NULL && gBtlSeq->state >= 0 && gBtlSeq->state <= 6;
}

/* The game stays in sequence state 3 in pause/skill-list menus.
 * Respect its actual battle flags before switching to portable controls,
 * so D-pad UP/DOWN cannot accidentally fly while navigating a pause menu.
 */
int Psp_InFight(void) {
    const BattleWork *work;
    const uint64_t inactive = (uint64_t)(BATTLE_FLAG_PAUSE | BATTLE_FLAG_PAUSE_MENU |
                                        BATTLE_FLAG_DEMO | BATTLE_FLAG_READY |
                                        BATTLE_FLAG_LOADING);
    if (gBtlSeq == NULL || gBtlSeq->state != 3) return 0;
    work = Battle_GetWork();
    return work != NULL &&
           PspControls_ActiveFight(gBtlSeq->state, work->running, work->flags, inactive);
}
