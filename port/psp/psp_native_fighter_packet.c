#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "psp_native_fighter_packet.h"
#include "psp_fighter_pack_miss_cache.h"

extern void Psp_Log(const char *, ...);

/* Resource converter bridge using REAL original BT3 VU1 packets.
 * Exactly one current frame's packet is compared before reuse; bone
 * matrices, scene transform, light and GS materials are never copied
 * from older frames. The VU1 program's existing native PSP GU renderer
 * remains the renderer of these arrays.
 *
 * Entire path is disabled unless user installs explicit marker files.
 * No game-owned assets are added to the source repo or EBOOT. */
enum { FIGHTER_PACK_MEM_SLOTS=16,
       FIGHTER_PACK_MAX_WRITES=24 };
typedef struct FighterPackSlot {
    uint32_t hash,count;
    uint8_t data[TP3_FIGHTER_PACKET_MAX_VERTS*
                 TP3_FIGHTER_PACKET_VERTEX_BYTES];
    unsigned valid;
} FighterPackSlot;
static FighterPackSlot sSlots[FIGHTER_PACK_MEM_SLOTS];
static PspFighterNegativeCache sNegative;
static unsigned sNextSlot,sWrote,sLoaded,sHit,sRejected,sMiss,sNegativeHit;
static int sLoadEnabled=-1,sCaptureEnabled=-1;

static int marker(const char *path) {
    FILE *f=fopen(path,"rb");
    if(!f)return 0;
    fclose(f);return 1;
}
const float *PspNativeFighterPacket_Resolve(const float *live,
                                             unsigned count) {
    char path[180];
    uint32_t hash;
    size_t bytes;
    unsigned i;
    const uint8_t *src=(const uint8_t *)live;
    PspFighterPacketHeader head;
    FILE *f;
    FighterPackSlot *slot;
    if(!live || !(bytes=PspFighterPacket_Length(count)))return live;
    if(sLoadEnabled<0) {
        sLoadEnabled=marker("ms0:/PSP/GAME/BT3/native/enable-fighter-packets.txt");
        sCaptureEnabled=marker("ms0:/PSP/GAME/BT3/native/enable-fighter-capture.txt");
        if(sLoadEnabled||sCaptureEnabled)
            Psp_Log("psp: fighter VU source packet integration enabled: replay=%d capture=%d (original live bones/TEX0 retained)\n",
                    sLoadEnabled,sCaptureEnabled);
    }
    if(!sLoadEnabled && !sCaptureEnabled)return live;
    hash=PspFighterPacket_Fnv(src,bytes);
    for(i=0;i<FIGHTER_PACK_MEM_SLOTS;i++) {
        slot=&sSlots[i];
        if(slot->valid && slot->hash==hash && slot->count==count &&
           memcmp(slot->data,src,bytes)==0) {
            ++sHit;return (const float *)slot->data;
        }
    }
    /* Never blacklist a pack that was loaded successfully. Unlike
       the previous permanent 96-source blacklist, this circular
       NEGATIVE cache permits new costumes/characters indefinitely. */
    if(PspFighterNegative_Has(&sNegative,hash,count)) {
        ++sNegativeHit;
        return live;
    }
    snprintf(path,sizeof(path),
             "ms0:/PSP/GAME/BT3/native/fighter_%08x_%u.f3p",
             hash,count);
    if(sLoadEnabled) {
        f=fopen(path,"rb");
        if(f) {
            slot=&sSlots[sNextSlot%FIGHTER_PACK_MEM_SLOTS];
            if(fread(&head,1,sizeof(head),f)==sizeof(head) &&
               PspFighterPacket_Match(&head,src,bytes) &&
               fread(slot->data,1,bytes,f)==bytes &&
               fgetc(f)==EOF &&
               PspFighterPacket_Verify(&head,slot->data,src,bytes)) {
                slot->hash=hash;slot->count=count;slot->valid=1u;
                sNextSlot++;
                ++sLoaded;
                fclose(f);
                return (const float *)slot->data;
            }
            ++sRejected;
            fclose(f);
            PspFighterNegative_Add(&sNegative,hash,count);
            Psp_Log("psp: fighter packet rejected or stale: source=%08x count=%u\n",
                    hash,count);
            return live;
        }
    }
    ++sMiss;
    PspFighterNegative_Add(&sNegative,hash,count);
    if(!sCaptureEnabled || sWrote>=FIGHTER_PACK_MAX_WRITES)
        return live;
    /* Avoid overwrites; the user can inspect/manually curate captures.
     * First capture costs I/O only when explicitly enabled. */
    f=fopen(path,"rb");
    if(f) {fclose(f);return live;}
    f=fopen(path,"wb");
    if(!f)return live;
    PspFighterPacket_CreateHeader(&head,src,count);
    if(fwrite(&head,1,sizeof(head),f)==sizeof(head) &&
       fwrite(src,1,bytes,f)==bytes && fflush(f)==0) {
        ++sWrote;
        Psp_Log("psp: captured actual BT3 fighter VU packet %08x vertices=%u (live bone matrices and materials remain authoritative)\n",
                hash,count);
    } else {
        ++sRejected;
    }
    fclose(f);
    return live;
}
void PspNativeFighterPacket_Stats(void) {
    if(sLoadEnabled<=0 && sCaptureEnabled<=0)return;
    Psp_Log("psp: native BT3 fighter packet replay: memory-hits=%u loaded=%u captured=%u misses=%u rejected=%u negative-hits=%u negative-size=%u (exact original vertex input, live bones/GS textures)\n",
            sHit,sLoaded,sWrote,sMiss,sRejected,sNegativeHit,sNegative.length);
}
