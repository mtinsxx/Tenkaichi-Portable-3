#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "psp_native_fighter_packet.h"
#include "psp_fighter_pack_miss_cache.h"
#include "psp_auto_model_prepare.h"

extern void Psp_Log(const char *, ...);

/* Resource converter bridge using REAL original BT3 VU1 packets.
 * Exactly one current frame's packet is compared before reuse; bone
 * matrices, scene transform, light and GS materials are never copied
 * from older frames. The VU1 program's existing native PSP GU renderer
 * remains the renderer of these arrays.
 *
 * Entire path is disabled unless user installs explicit marker files.
 * No game-owned assets are added to the source repo or EBOOT. */
enum { FIGHTER_PACK_MEM_SLOTS=32,
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
static unsigned sAutoModels,sAutoInserted,sAutoHits;
static int sAutoReady;
static int sLoadEnabled=-1,sCaptureEnabled=-1;

/* Called once when BT3 binds an already-loaded original model, before
 * the game's MdlTex_RebaseChain edits its VIF material references.
 * Copies only proven literal original fighter vertices into bounded RAM.
 * The data never outlives its original file buffer by pointer; the
 * native draw still uses CURRENT game's bones and GS material state. */
static int auto_store(const uint8_t *v,unsigned count,unsigned node,void *ctx) {
    unsigned i;
    size_t bytes=PspFighterPacket_Length(count);
    uint32_t hash;
    FighterPackSlot *slot;
    (void)node;(void)ctx;
    if(!bytes)return 0;
    hash=PspFighterPacket_Fnv(v,bytes);
    for(i=0;i<FIGHTER_PACK_MEM_SLOTS;i++) {
        slot=&sSlots[i];
        if(slot->valid && slot->hash==hash && slot->count==count &&
           memcmp(slot->data,v,bytes)==0)return 1;
    }
    slot=&sSlots[sNextSlot++%FIGHTER_PACK_MEM_SLOTS];
    memcpy(slot->data,v,bytes);
    slot->hash=hash;slot->count=count;slot->valid=1u;
    ++sAutoInserted;
    sAutoReady=1;
    return 1;
}
void PspNativeFighterPacket_AutoPrepare(const void *model,unsigned size) {
    unsigned count;
    if(!model||size<0x70u || size>16u*1024u*1024u)return;
    count=PspAutoModel_Scan(model,size,auto_store,NULL);
    ++sAutoModels;
    if(sAutoModels<=6u)
        Psp_Log("psp: automatic original BT3 model native prewarm: model=%u accepted=%u cache=%u (no files or manual packets; live bones/TEX0)\\n",
                sAutoModels,count,sAutoInserted);
}
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
    if(!sAutoReady && !sLoadEnabled && !sCaptureEnabled)return live;
    hash=PspFighterPacket_Fnv(src,bytes);
    for(i=0;i<FIGHTER_PACK_MEM_SLOTS;i++) {
        slot=&sSlots[i];
        if(slot->valid && slot->hash==hash && slot->count==count &&
           memcmp(slot->data,src,bytes)==0) {
            ++sHit;
            if(sAutoReady) ++sAutoHits;
            return (const float *)slot->data;
        }
    }
    /* Never blacklist a pack that was loaded successfully. Unlike
       the previous permanent 96-source blacklist, this circular
       NEGATIVE cache permits new costumes/characters indefinitely. */
    if(!sLoadEnabled && !sCaptureEnabled)return live;
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
    if(sLoadEnabled<=0 && sCaptureEnabled<=0 && !sAutoReady)return;
    Psp_Log("psp: auto native BT3 model assets: models=%u prepared=%u candidate-hits=%u (RAM only, no user files)\\n",
            sAutoModels,sAutoInserted,sAutoHits);
    Psp_Log("psp: native BT3 fighter packet replay: memory-hits=%u loaded=%u captured=%u misses=%u rejected=%u negative-hits=%u negative-size=%u (exact original vertex input, live bones/GS textures)\n",
            sHit,sLoaded,sWrote,sMiss,sRejected,sNegativeHit,sNegative.length);
}
