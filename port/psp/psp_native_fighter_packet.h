#ifndef TP3_PSP_NATIVE_FIGHTER_PACKET_H
#define TP3_PSP_NATIVE_FIGHTER_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* VU1 programs 0 and 1 consume 3 float4 per original fighter vertex:
 * position.xyz + two-bone blend weight; normal float4; UV float4.
 * This exact game-owned packet is already an intermediate native
 * asset. The live VU constants (both bones, pivots, projection, light)
 * and TEX0/PRIM are ALWAYS taken from the current frame. Never freeze
 * animation or material registers in a saved packet.
 *
 * A source identity hit alone is never enough: the byte-for-byte
 * contents must match the current game's real VU1 packet. */
enum { TP3_FIGHTER_PACKET_MAX_VERTS=38,
       TP3_FIGHTER_PACKET_VERTEX_BYTES=48,
       TP3_FIGHTER_PACKET_HEADER_BYTES=24 };
typedef struct PspFighterPacketHeader {
    unsigned char magic[4];  /* "F3P1" */
    uint32_t version,count,stride,sourceFnv,payloadFnv;
} PspFighterPacketHeader;
static inline uint32_t PspFighterPacket_Fnv(const uint8_t *p,size_t size){
    uint32_t h=2166136261u;
    size_t i;
    for(i=0;i<size;i++)h=(h^p[i])*16777619u;
    return h;
}
static inline size_t PspFighterPacket_Length(uint32_t count){
    return count>=3u && count<=TP3_FIGHTER_PACKET_MAX_VERTS ?
           (size_t)count*TP3_FIGHTER_PACKET_VERTEX_BYTES:0u;
}
static inline int PspFighterPacket_Match(const PspFighterPacketHeader *h,
                              const uint8_t *source,size_t bytes){
    static const uint8_t magic[4]={'F','3','P','1'};
    if(!h||!source || memcmp(h->magic,magic,4)!=0 ||
       h->version!=1u || h->stride!=TP3_FIGHTER_PACKET_VERTEX_BYTES ||
       PspFighterPacket_Length(h->count)!=bytes)
        return 0;
    return PspFighterPacket_Fnv(source,bytes)==h->sourceFnv;
}
static inline int PspFighterPacket_Verify(const PspFighterPacketHeader *h,
                              const uint8_t *pack,const uint8_t *live,size_t n){
    return PspFighterPacket_Match(h,live,n) && pack &&
           h->payloadFnv==PspFighterPacket_Fnv(pack,n) &&
           memcmp(pack,live,n)==0;
}
static inline void PspFighterPacket_CreateHeader(PspFighterPacketHeader *h,
                                            const uint8_t *live,uint32_t count){
    const uint8_t magic[4]={'F','3','P','1'};
    size_t n=PspFighterPacket_Length(count);
    memcpy(h->magic,magic,4);
    h->version=1u; h->count=count;
    h->stride=TP3_FIGHTER_PACKET_VERTEX_BYTES;
    h->sourceFnv=PspFighterPacket_Fnv(live,n);
    h->payloadFnv=h->sourceFnv;
}
#endif
