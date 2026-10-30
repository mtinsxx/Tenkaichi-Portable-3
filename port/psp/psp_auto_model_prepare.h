#ifndef TP3_PSP_AUTO_MODEL_PREPARE_H
#define TP3_PSP_AUTO_MODEL_PREPARE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/* Safe, allocation-free scanner of the BT3 model's ORIGINAL raw mesh
 * format, before model texture rebasing:
 * ObjMdlFile.meshOfs +0x6c, ObjMdlMesh chain +0x60, node +0x0a.
 * VIF program0/1 vertex layout = 3 V4-32 per vertex at TOPS+5.
 *
 * Unsupported VIF commands never create synthetic geometry; the
 * original engine continues to interpret their streams unchanged.
 * No disc assets, .F3P files, user folders or flags are required. */
enum { TP3_AUTO_PART_LIMIT=512, TP3_AUTO_CHAIN_LIMIT=512*1024,
       TP3_AUTO_FIGHTER_MAX_VERTS=38 };
typedef int (*PspAutoModel_Store)(const uint8_t *vertices,
                                 unsigned count,unsigned node,void *user);
static inline uint32_t PspAutoModel_U32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline uint16_t PspAutoModel_U16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static inline float PspAutoModel_F32(const uint8_t *p) {
    uint32_t bits=PspAutoModel_U32(p);
    float f;
    memcpy(&f,&bits,sizeof(f));
    return f;
}
static inline int PspAutoModel_ValidVertexStrip(
                                  const uint8_t *v,unsigned count) {
    unsigned i,j;
    if(!v||count<3u||count>TP3_AUTO_FIGHTER_MAX_VERTS)return 0;
    for(i=0;i<count;i++) {
        const uint8_t *p=v+(size_t)i*48u;
        float w=PspAutoModel_F32(p+12u);
        if(!isfinite(w)||w<0.0f||w>1.0f)return 0;
        for(j=0;j<3;j++) {
            if(!isfinite(PspAutoModel_F32(p+j*4u)) ||
               !isfinite(PspAutoModel_F32(p+16u+j*4u)))
                return 0;
        }
        if(!isfinite(PspAutoModel_F32(p+32u)) ||
           !isfinite(PspAutoModel_F32(p+36u)))return 0;
    }
    return 1;
}
static inline unsigned PspAutoModel_Scan(const void *buffer,size_t size,
                         PspAutoModel_Store store,void *user) {
    const uint8_t *model=(const uint8_t *)buffer;
    size_t at;
    unsigned part,total=0u;
    if(!model || !store || size<0x70u)return 0u;
    at=PspAutoModel_U32(model+0x6cu);
    if(at<0x70u || at>size-0x64u)return 0u;
    for(part=0;part<TP3_AUTO_PART_LIMIT;part++) {
        uint32_t step;
        unsigned node,enabled,last;
        size_t chain,limit,pos;
        if(at>size-0x64u)return total;
        step=PspAutoModel_U32(model+at);
        last=PspAutoModel_U16(model+at+6u);
        enabled=PspAutoModel_U16(model+at+8u);
        node=PspAutoModel_U16(model+at+0xau);
        chain=at+0x60u;
        if(last) {
            limit=size;
            if(limit-chain>TP3_AUTO_CHAIN_LIMIT)
                limit=chain+TP3_AUTO_CHAIN_LIMIT;
        } else {
            if(step<0x64u || (size_t)step>size-at)return total;
            limit=at+step;
            if(limit-chain>TP3_AUTO_CHAIN_LIMIT)return total;
        }
        /* VIF does not provide lengths for every opcode here.
         * Interpret ONLY known unmasked V4-32 commands, skipping their
         * full raw payload. Halt the part on unknown 0x6x UNPACK
         * instead of treating its payload as new VIF commands. */
        if(enabled && node<=0xa8u) {
            for(pos=chain;pos+4u<=limit;) {
                uint32_t cmd=PspAutoModel_U32(model+pos);
                unsigned byte=(cmd>>24)&0x7fu;
                if(cmd==0x70000000u)break;
                if(byte>=0x60u && byte<=0x7fu) {
                    unsigned q=(cmd>>16)&0xffu;
                    size_t payload;
                    if(byte!=0x6cu)break; /* unknown VIF payload shape */
                    if(!q)q=256u;
                    payload=(size_t)q*16u;
                    if(payload>limit-pos-4u)break;
                    if((cmd&0x3ffu)==5u && (cmd&0x8000u) &&
                       !(cmd&0x10000000u) && q%3u==0u) {
                        unsigned count=q/3u;
                        const uint8_t *v=model+pos+4u;
                        if(PspAutoModel_ValidVertexStrip(v,count)) {
                            total+=(unsigned)(store(v,count,node,user)!=0);
                        }
                    }
                    pos+=4u+payload;
                } else {
                    pos+=4u;
                }
            }
        }
        if(last)return total;
        at+=step;
    }
    return total;
}
#endif
