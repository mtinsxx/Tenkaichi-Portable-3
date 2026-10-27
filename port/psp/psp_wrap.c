/*
 * PSP: the link's wrappers that call straight through.
 *
 * port/psp/build.py links with --wrap=x for a handful of entry points, so that the port can put itself around
 * them: every call of x then goes to __wrap_x, and x itself is reached as __real_x. On the Vita three of those
 * are taken over by its renderer (port/vita/vita_vgl.c: the VIF1 chain is handed to the render thread and
 * sceMpegGetPicture decodes with the Vita's hardware). The PSP runs the portable implementations as they are,
 * so here they simply call through -- without this file the link would make each of them a stub that stops
 * the program, and nothing would ever be drawn.
 */
#include <stdint.h>

extern void __real_GsVu1_Call(int addr);
extern void __real_Port_GsVif1Chain(uint32_t tadr, int tte);
extern int __real_sceMpegGetPicture(void *mp, void *rgb, int blocks);

void __wrap_GsVu1_Call(int addr) { __real_GsVu1_Call(addr); }
void __wrap_Port_GsVif1Chain(uint32_t tadr, int tte) { __real_Port_GsVif1Chain(tadr, tte); }
int __wrap_sceMpegGetPicture(void *mp, void *rgb, int blocks) { return __real_sceMpegGetPicture(mp, rgb, blocks); }
