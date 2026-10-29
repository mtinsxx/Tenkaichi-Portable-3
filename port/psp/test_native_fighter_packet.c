#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_native_fighter_packet.h"

int main(void) {
    uint8_t original[7*TP3_FIGHTER_PACKET_VERTEX_BYTES];
    uint8_t stored[sizeof(original)];
    PspFighterPacketHeader h;
    size_t i;
    for(i=0;i<sizeof(original);i++)
        original[i]=(uint8_t)((i*17u+31u)&255u);
    assert(sizeof(PspFighterPacketHeader)==TP3_FIGHTER_PACKET_HEADER_BYTES);
    assert(!PspFighterPacket_Length(2u));
    assert(PspFighterPacket_Length(3u)==3u*48u);
    assert(PspFighterPacket_Length(38u)==38u*48u);
    assert(!PspFighterPacket_Length(39u));
    PspFighterPacket_CreateHeader(&h,original,7u);
    memcpy(stored,original,sizeof(stored));
    assert(PspFighterPacket_Match(&h,original,sizeof(original)));
    assert(PspFighterPacket_Verify(&h,stored,original,sizeof(original)));
    assert(!PspFighterPacket_Verify(&h,stored,original,sizeof(original)-1));
    original[25]^=0x03u;
    assert(!PspFighterPacket_Verify(&h,stored,original,sizeof(original)));
    original[25]^=0x03u;
    stored[25]^=0x01u;
    assert(!PspFighterPacket_Verify(&h,stored,original,sizeof(original)));
    stored[25]^=0x01u;
    assert(PspFighterPacket_Verify(&h,stored,original,sizeof(original)));
    h.stride=40u;
    assert(!PspFighterPacket_Match(&h,original,sizeof(original)));
    h.stride=48u;
    h.magic[0]='X';
    assert(!PspFighterPacket_Match(&h,original,sizeof(original)));
    puts("BT3 native fighter VU packet capture/replay exact-byte parity passed");
    return 0;
}
