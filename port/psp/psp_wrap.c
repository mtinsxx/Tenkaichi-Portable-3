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

/* File-I/O crash gate. PPSSPP reported invalid write at 0x00000000
 * inside newlib _fread_r in the framebuffer-recovery build. Guard
 * application calls here so we can distinguish null FILE* from null
 * destination, and record both values plus a caller address before
 * a memory fault would erase the diagnostic log. Do not claim that
 * the framebuffer experiment was safe without a fresh battle test. */
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include "psp_io_guard.h"
extern void Psp_Log(const char *fmt, ...);
extern void Psp_LogFlush(void);
extern void Psp_Fatal(const char *fmt, ...);
extern size_t __real_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern FILE *__real_fopen(const char *path, const char *mode);

size_t __wrap_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (PspIo_BadFread(ptr,size,nmemb,stream)) {
        void *from=__builtin_return_address(0);
        Psp_Log("psp: IO-CRASH-GUARD invalid fread dest=%p stream=%p size=%u count=%u caller=%p\n",
                ptr,(void *)stream,(unsigned)size,(unsigned)nmemb,from);
        Psp_LogFlush();
        Psp_Fatal("invalid file read: stream=%p data=%p\ncaller=%p\nCheck log.txt", (void *)stream,ptr,from);
        return 0;
    }
    return __real_fread(ptr,size,nmemb,stream);
}

FILE *__wrap_fopen(const char *path, const char *mode) {
    static unsigned failures=0;
    FILE *result=__real_fopen(path,mode);
    if(!result && failures++<16u) {
        const int err=errno;
        Psp_Log("psp: IO fopen failed: errno=%d file=%s mode=%s\n",
                err,path?path:"(null)",mode?mode:"(null)");
    }
    return result;
}
