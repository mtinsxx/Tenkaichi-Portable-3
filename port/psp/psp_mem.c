/*
 * Memory of the PSP build (port/src/plat_mem.c is the PC's).
 *
 * The PC build gives the game memory at the PS2's own addresses. A PSP program cannot choose where its memory is,
 * and it does not have to: the game takes its heap with one malloc and keeps ordinary 32-bit pointers, which is
 * what the PSP has. So here
 *   - the game heap is one block from the system (28.3 MB, the size of the PS2's region), wherever it lands;
 *   - the hardware registers are two arrays (the game's register macros go through PORT_HW, port_compat.h);
 *   - the data tables of the game's two programs are filled in from the user's own files at start
 *     (Psp_LoadGameData), through the table of addresses build.py generates (psp_dat.c).
 *
 * The heap is that size because the game's allocator walks the PS2's whole region; it is taken from the PSP's
 * user memory, so the program needs a console with 64 MB (PSP-2000, 3000, Go, E1000) - on a PSP-1000 the block
 * cannot be had and the program says so at start (heap=... in bt3.txt asks for a smaller one, which the game
 * runs out of sooner or later).
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_dat.h"

#ifndef PSP_HEAP_MB
#define PSP_HEAP_MB 29 /* 0x2000000 - 0x3BE000 rounded up */
#endif
#define HEAP_SIZE ((unsigned)(PSP_HEAP_MB) * 0x100000u)
#define HEAP_SKIP 0x730u /* the PS2's first address is 0x3BE730: keep its position within a page */

unsigned char gPortHw[0x10000] __attribute__((aligned(16)));   /* stands for 0x10000000..0x1000FFFF */
unsigned char gPortGsReg[0x2000] __attribute__((aligned(16))); /* stands for 0x12000000..0x12001FFF */
volatile uint32_t gPortDataStripped = 1;                       /* the tables come from the disc, always */
uint32_t gPspHeapBase, gPspHeapUsed;

static uint32_t sHeapNext, sHeapEnd;
extern void Psp_Fatal(const char *fmt, ...);
extern void Psp_Log(const char *fmt, ...);

void Psp_MapMemory(void) {
    void *base;

    base = malloc(HEAP_SIZE);
    if (base == NULL) {
        Psp_Fatal("no memory for the game heap (%u MB).\nThe game needs a PSP with 64 MB of memory\n(2000, 3000, Go or E1000).",
                  (unsigned)(HEAP_SIZE >> 20));
    }
    memset(base, 0, HEAP_SIZE); /* the PS2's heap starts cleared */
    gPspHeapBase = (uint32_t)(uintptr_t)base;
    sHeapNext = gPspHeapBase + HEAP_SKIP;
    sHeapEnd = gPspHeapBase + HEAP_SIZE;
}

/* The game's malloc (port_compat.h renames it): a bump allocator over the heap block, 16-byte aligned. */
void *Port_Malloc(uint32_t size) {
    uint32_t p = (sHeapNext + 15) & ~15u;

    if (size > sHeapEnd - p) {
        Psp_Fatal("malloc(0x%X) does not fit in the game heap", size);
    }
    sHeapNext = p + size;
    gPspHeapUsed = sHeapNext - gPspHeapBase;
    return (void *)(uintptr_t)p;
}

/* Memory the port hands to the game by address (file handles, the "second processor's" memory): zeroed. */
void *Port_LowAlloc(size_t size) {
    void *p = calloc(1, size);

    if (p == NULL) {
        Psp_Fatal("out of memory (%u bytes)", (unsigned)size);
    }
    return p;
}

void Port_LowFree(void *addr) {
    free(addr);
}

/* `len` bytes of `fd` at `off`. The PSP has no room to hold the game's files whole, so everything is read
   where it is needed, a piece at a time (sceIoRead takes at most a signed int's worth in one call). */
static int read_at(SceUID fd, uint32_t off, void *dst, uint32_t len) {
    uint8_t *p = (uint8_t *)dst;

    if (sceIoLseek(fd, (SceOff)off, PSP_SEEK_SET) < 0) {
        return 0;
    }
    while (len > 0) {
        uint32_t chunk = len > 0x10000u ? 0x10000u : len;
        int n = sceIoRead(fd, p, (SceSize)chunk);

        if (n <= 0) {
            return 0;
        }
        p += n;
        len -= (uint32_t)n;
    }
    return 1;
}

/* The table's runs in the order their parts of the files lie in, so that the memory stick is read forwards.
   (The check below hashes them in the table's own order; the two are independent.) */
static const PortDatRun *sSortRuns;
static uint32_t *sOrder;

static int run_cmp(const void *a, const void *b) {
    const PortDatRun *x = &sSortRuns[*(const uint32_t *)a], *y = &sSortRuns[*(const uint32_t *)b];

    if (x->source != y->source) {
        return x->source < y->source ? -1 : 1;
    }
    return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}

/* Copies the values of the game's data tables in from <disc>/SLUS_216.78 (its loaded sections as one image from
   0x100000) and <disc>/BIN/DBZP.BIN, and checks them against the checksum of port/data: the same step as
   Port_LoadGameData of the PC build, with the destinations as addresses the PSP's loader has already placed. */
#define ROM_BASE 0x100000u
#define ROM_END 0x2FF180u

/* Only reached if the normal game ELF path does not open. List the PSP's
   *actual* view of the disc directory; this detects a wrong PPSSPP
   Memory Stick mapping, missing directory, renamed files or case mismatch. */
static void log_disc_contents(const char *disc) {
    SceUID dh = sceIoDopen(disc);
    unsigned i;
    if (dh < 0) {
        Psp_Log("psp: disc diagnostic: cannot list '%s' (sceIoDopen=0x%08X)\n",
                disc, (unsigned)dh);
        return;
    }
    Psp_Log("psp: disc diagnostic: entries visible in '%s':\n", disc);
    for (i = 0; i < 24; ++i) {
        SceIoDirent ent;
        int n;
        memset(&ent, 0, sizeof(ent));
        n = sceIoDread(dh, &ent);
        if (n <= 0) {
            Psp_Log("psp: disc diagnostic: end of listing (result=%d)\n", n);
            break;
        }
        Psp_Log("psp: disc diagnostic:  %s (%llu bytes)\n",
                ent.d_name, (unsigned long long)ent.d_stat.st_size);
    }
    sceIoDclose(dh);
}

void Psp_LoadGameData(const char *disc) {
    char path[300];
    uint8_t *src = NULL;             /* the program's loaded sections, as they lie at ROM_BASE in memory */
    uint8_t hdr[0x34];
    uint64_t hash = 0xCBF29CE484222325ull;
    SceOff srcSize[2];   /* [0] the program's window (its sections), [1] the file being read */
    SceUID fd;
    SceOff elfSize;
    uint32_t shoff, sh[6], i, k;
    uint16_t shentsize, shnum;

    srcSize[0] = ROM_END - ROM_BASE;
    snprintf(path, sizeof(path), "%s/SLUS_216.78", disc);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        int firstError = fd;
        unsigned attempt;
        static const char *const alternates[] = {
            "SLUS_216.78;1", "slus_216.78", "slus_216.78;1"
        };
        Psp_Log("psp: game ELF: cannot open '%s' (sceIoOpen=0x%08X)\n",
                path, (unsigned)firstError);
        log_disc_contents(disc);
        /* Some raw ISO extraction tools retain ;1 or lowercase names.
           Only try filename variants; never relax the ELF/content checks. */
        for (attempt = 0; attempt < sizeof(alternates) / sizeof(alternates[0]); ++attempt) {
            snprintf(path, sizeof(path), "%s/%s", disc, alternates[attempt]);
            fd = sceIoOpen(path, PSP_O_RDONLY, 0);
            if (fd >= 0) {
                Psp_Log("psp: game ELF: using alternate filename '%s'\n", path);
                break;
            }
            Psp_Log("psp: game ELF: alternate '%s' failed (0x%08X)\n",
                    path, (unsigned)fd);
        }
        if (fd < 0) {
            Psp_Fatal("cannot OPEN the game's ELF (error 0x%08X):\n  %s/SLUS_216.78\nEnable Memory Stick; check BT3/disc/.\nNo bt3.txt is required. See log.txt.",
                      (unsigned)firstError, disc);
        }
    }
    elfSize = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (elfSize < (SceOff)sizeof(hdr)) {
        sceIoClose(fd);
        Psp_Fatal("the game's ELF is too short or unreadable:\n  %s\nsize / seek result = %d\nSee log.txt.", path, (int)elfSize);
    }
    if (!read_at(fd, 0, hdr, sizeof(hdr))) {
        Psp_Log("psp: game ELF: cannot read first %u bytes of '%s'\n",
                (unsigned)sizeof(hdr), path);
        sceIoClose(fd);
        Psp_Fatal("cannot READ the game's ELF header:\n  %s\nSee log.txt.", path);
    }
    if (memcmp(hdr, "\177ELF", 4) != 0) {
        Psp_Log("psp: game ELF: invalid magic %02X %02X %02X %02X in '%s'\n",
                hdr[0], hdr[1], hdr[2], hdr[3], path);
        sceIoClose(fd);
        Psp_Fatal("game file opens, but is NOT an ELF:\n  %s\nSee log.txt.", path);
    }
    Psp_Log("psp: game ELF opened and validated: %s (%d bytes)\n",
            path, (int)elfSize);
    {
        memcpy(&shoff, hdr + 0x20, 4);
        memcpy(&shentsize, hdr + 0x2E, 2);
        memcpy(&shnum, hdr + 0x30, 2);
        src = calloc(1, (size_t)srcSize[0]);
        if (src == NULL) {
            Psp_Fatal("out of memory");
        }
        for (i = 0; i < shnum; i++) {
            if (!read_at(fd, (uint32_t)(shoff + (uint32_t)i * shentsize), sh, sizeof(sh))) {
                break; /* the table is shorter than it says: what has been read is what there is */
            }
            /* name, type, flags, addr, offset, size */
            if ((sh[2] & 2) && sh[1] != 8 && sh[5] != 0 && sh[3] >= ROM_BASE && sh[3] + sh[5] <= ROM_END &&
                (SceOff)sh[4] + sh[5] <= elfSize &&
                !read_at(fd, sh[4], src + (sh[3] - ROM_BASE), sh[5])) {
                Psp_Fatal("%s\nis not the game's program (a part of it cannot be read)", path);
            }
        }
        sceIoClose(fd);
    }
    snprintf(path, sizeof(path), "%s/BIN/DBZP.BIN", disc);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        Psp_Fatal("the game's menu program was not found:\n  %s", path);
    }
    srcSize[1] = sceIoLseek(fd, 0, PSP_SEEK_END);

    for (i = 0; i < gPortDatCount; i++) {
        const PortDatRun *r = &gPortDatRuns[i];

        if (r->source > 1 || (SceOff)r->offset + r->length > srcSize[r->source]) {
            Psp_Fatal("the list of the game's data does not fit the files in\n  %s", disc);
        }
    }
    sOrder = malloc((size_t)(gPortDatCount ? gPortDatCount : 1) * sizeof(uint32_t));
    if (sOrder == NULL) {
        Psp_Fatal("out of memory");
    }
    for (i = 0; i < gPortDatCount; i++) {
        sOrder[i] = i;
    }
    sSortRuns = gPortDatRuns;
    qsort(sOrder, (size_t)gPortDatCount, sizeof(uint32_t), run_cmp);
    for (i = 0; i < gPortDatCount; i++) {   /* the runs, read in the order they lie in the files */
        const PortDatRun *r = &gPortDatRuns[sOrder[i]];

        if (r->source == 0) {
            memcpy(r->dst, src + r->offset, r->length);
        } else if (!read_at(fd, r->offset, r->dst, r->length)) {
            Psp_Fatal("%s\ncannot be read (a part of the game's data is missing)", path);
        }
    }
    free(sOrder);
    sceIoClose(fd);
    free(src);
    for (i = 0; i < gPortDatCount; i++) {   /* and hashed in the table's own order, as port/data is */
        const PortDatRun *r = &gPortDatRuns[i];
        const uint8_t *from = (const uint8_t *)r->dst;

        for (k = 0; k < r->length; k++) {
            hash = (hash ^ from[k]) * 0x100000001B3ull;
        }
    }
    if (hash != gPortDatChecksum) {
        Psp_Fatal("SLUS_216.78 / BIN/DBZP.BIN in\n  %s\nare not the unmodified USA release (SLUS-21678).", disc);
    }
}

/* The program's end address on the PS2: a table of the PS2's C library points at it; nothing reads it here
   (src/sys/heap.c takes the number itself in this build). */
unsigned char D_3BE71C[16];
