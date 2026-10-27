/*
 * Forced into every game source of the PC build (-include port_compat.h), in front of the game's own headers.
 * The game sources under src/ and include/ are shared with the matching decompilation and are not edited for the
 * port unless there is no other way; differences between the PS2 compiler and a host compiler are bridged here.
 */
#ifndef PORT_COMPAT_H
#define PORT_COMPAT_H

#define PORT 1

/* PORT_CONSOLE: a port to a handheld (the PS Vita's or the PSP's). They share what follows from not being a PC:
   memory cannot be placed at the PS2's own addresses, the game's files are read as they are on the disc, and the
   screens and controls are the console's. The rest is one port's or the other's (PORT_VITA / PORT_PSP). */
#if defined(PORT_VITA) || defined(PORT_PSP)
#define PORT_CONSOLE 1
#endif

/* The game's types: on the PS2 `long` is 64 bits. Take over include/types.h. */
#define TYPES_H
typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef long long s64;
typedef unsigned long long u64;
typedef float f32;

/* Functions the matching build takes from assembly: the port supplies them elsewhere (see docs/port/). */
#define INCLUDE_ASM_H
#define INCLUDE_ASM(FOLDER, NAME)
#define INCLUDE_RODATA(FOLDER, NAME)
#define RODATA_ALIGN16()
#define LIT4_WORD(NAME, HEX)
#define ASM_STUB_BEGIN() extern int asm_stub_begin_
#define ASM_STUB_END() extern int asm_stub_end_

/* PS Vita: the PS2's hardware-register addresses (0x1000xxxx: timers, DMA, VIF, GIF; 0x1200xxxx: the GS) cannot be
   mapped there, so the register macros of the game go through PORT_HW() to two blocks of ordinary memory
   (port/vita/vita_mem.c). On PC the registers are plain memory at the PS2's own addresses (port/src/plat_mem.c). */
#ifdef PORT_CONSOLE
extern unsigned char gPortHw[], gPortGsReg[];
#define PORT_HW(a) ((a) >= 0x12000000u ? (unsigned)gPortGsReg + ((a) - 0x12000000u) : (unsigned)gPortHw + ((a) - 0x10000000u))
#endif

/* C library calls of the game that must not go to the host's versions (see port/src/plat_mem.c). */
#define malloc Port_Malloc

#endif
