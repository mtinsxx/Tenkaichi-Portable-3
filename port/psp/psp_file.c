/*
 * The game's file access on the PSP (port/src/plat_file.c is the PC's).
 *
 * The game reads everything through CRI's ADXF library: three AFS archives ("partitions" 0..2), files addressed by
 * (partition, index), read in 2048-byte sectors. The PC build unpacks the archives into 68,611 loose files first;
 * copying that many files to a memory stick takes hours, so here the archives are read as they are on the disc:
 * <disc>/DATA/PZS3US0.AFS, PZS3US1.AFS, PZS3US2.AFS, next to the other files of the disc's DATA folder.
 * <disc> is the folder gPspDisc (psp_main.c: ms0:/PSP/GAME/BT3/disc, or disc=... in bt3.txt).
 *
 * An AFS archive: "AFS\0", the number of files, then (offset, size) per file; the data follows, each file on a
 * 2048-byte boundary. Reads go through sceIoPread, so the position is not shared state. The sound streams read
 * their archive members the same way (port/src/gs/snd_adx.c). Not supported: the PC build's mods folder.
 */
#include <ctype.h>
#include <pthread.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 2048
enum { STAT_STOP = 1, STAT_READING = 2, STAT_READEND = 3, STAT_ERROR = 4 };

typedef struct Partition {
    SceUID fd; /* 0 = not loaded */
    uint32_t count;
    uint32_t *table; /* offset, size per file */
} Partition;

typedef struct PortFile {
    SceUID fd;
    int owned; /* a loose file: closed with the handle */
    uint32_t base, size;
    int32_t sizeSct, posSct, stat;
} PortFile;

static Partition sPart[8];
char gPortMoviePath[512]; /* host path of the movie file opened last */
extern char gPspDisc[];
extern void *Port_LowAlloc(size_t size);
extern void Port_LowFree(void *addr);
extern void Psp_Fatal(const char *fmt, ...);
extern unsigned long long Psp_TimeUs(void);
unsigned gPspFileOpens, gPspFileBytes;   /* for the status screen */
unsigned gPspFileReads, gPspFileReadsGame; /* for the log: reads, and those of the game's thread, and their time */
unsigned long long gPspFileUs, gPspFileUsGame;

/* The PSP has no sceIoPread: the PS2's I/O processor does, and the port's file layer was written against it
   (as the Vita's was). Every member open in an archive shares the one handle the archive was opened with, so
   what pread gives - a seek and a read no other thread can get between - takes a lock of its own here. */
static pthread_mutex_t sIoMutex = PTHREAD_MUTEX_INITIALIZER;

int sceIoPread(SceUID fd, void *data, SceSize size, SceOff offset) {
    SceSize done = 0;
    int error = 0;

    if (size > 0x7FFFFFFFu || (data == NULL && size != 0)) return -1;
    pthread_mutex_lock(&sIoMutex);
    if (sceIoLseek(fd, offset, PSP_SEEK_SET) < 0) {
        error = -1;
    } else {
        /* Reads can be short even before EOF (slow Memory Sticks).
           Preserve seek+all reads as ONE operation across threads. */
        while (done < size) {
            SceSize chunk = size - done;
            int got;
            if (chunk > 0x10000u) chunk = 0x10000u;
            got = sceIoRead(fd, (uint8_t *)data + done, chunk);
            if (got < 0) { error = got; break; }
            if (got == 0) break; /* true EOF */
            done += (SceSize)got;
        }
    }
    pthread_mutex_unlock(&sIoMutex);
    return error < 0 ? error : (int)done;
}

/* <disc>/DATA/<NAME>: upper case, stripped ;1. Reject truncation;
   snprintf returns the required length, which must never index out. */
static int data_path(const char *fname, char *out, size_t size) {
    const char *base = fname, *p;
    int written;
    size_t n;

    if (fname == NULL || out == NULL || size == 0) return 0;
    out[0] = '\0';
    for (p = fname; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;
    }
    written = snprintf(out, size, "%s/DATA/", gPspDisc);
    if (written < 0 || (size_t)written >= size) {
        out[0] = '\0';
        return 0;
    }
    n = (size_t)written;
    for (p = base; *p != '\0' && *p != ';'; p++) {
        if (n + 1 >= size) {
            out[0] = '\0';
            return 0;
        }
        out[n++] = (char)toupper((unsigned char)*p);
    }
    out[n] = '\0';
    return n != (size_t)written;
}

/* The PS2's Disc_Identify() (src/sys/file.c) only checks whether one
   of the boot executables exists; it does not inspect sceCdlFILE fields.
   Implement precisely that callsite instead of an unconditional fake
   success. The full PS2 CD file-info contract is NOT implemented here. */
int sceCdSearchFile(void *file, char *name) {
    const char *p, *base;
    char path[320];
    int written;
    size_t n, i;
    SceUID fd;

    (void)file; /* Disc_Identify() only checks the return value */
    if (name == NULL) return 0;
    base = name;
    for (p = name; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;
    }
    n = 0;
    while (base[n] && base[n] != ';') {
        if (++n >= 80) return 0;
    }
    if (!n) return 0;
    written = snprintf(path, sizeof(path), "%s/", gPspDisc);
    if (written < 0 || (size_t)written + n + 1 > sizeof(path)) return 0;
    for (i = 0; i < n; ++i)
        path[written + i] = (char)toupper((unsigned char)base[i]);
    path[written + n] = '\0';
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd);
    return 1;
}

int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size) {
    SceUID fd;

    if (out == NULL || size <= 0) return 0;
    if (fname == NULL) { /* a member of an archive: a name for Psp_LoadWhole below, not a path */
        Partition *pt = &sPart[ptid & 7];
        if (pt->fd <= 0 || flid < 0 || (uint32_t)flid >= pt->count) {
            return 0;
        }
        return snprintf(out, (size_t)size, "afs:%d:%d", ptid & 7, flid) < size;
    }
    if (!data_path(fname, out, (size_t)size)) return 0;
    fd = sceIoOpen(out, PSP_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    sceIoClose(fd);
    return 1;
}

int ADXF_LoadPartitionNw(int ptid, char *fname, void *dir, void *ptinfo) {
    Partition *pt = &sPart[ptid & 7];
    char path[300];
    uint32_t head[2];
    SceOff fileSize;
    uint32_t i;

    (void)dir;
    (void)ptinfo;
    if (pt->fd > 0) {
        return 0;
    }
    if (!data_path(fname, path, sizeof(path))) {
        Psp_Fatal("archive path is too long or empty");
    }
    pt->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (pt->fd < 0) {
        Psp_Fatal("archive not found:\n  %s\n(error 0x%08X)", path, (unsigned)pt->fd);
    }
    fileSize = sceIoLseek(pt->fd, 0, PSP_SEEK_END);
    if (fileSize < 8 || sceIoPread(pt->fd, head, 8, 0) != 8 ||
        memcmp(head, "AFS\0", 4) != 0 || head[1] == 0 ||
        head[1] > 0x100000u || (uint64_t)head[1] * 8u + 8u > (uint64_t)fileSize) {
        Psp_Fatal("not an AFS archive:\n  %s", path);
    }
    pt->count = head[1];
    pt->table = malloc((size_t)pt->count * 8);
    if (pt->table == NULL || sceIoPread(pt->fd, pt->table, pt->count * 8, 8) != (int)(pt->count * 8)) {
        Psp_Fatal("cannot read the file table of\n  %s", path);
    }
    for (i = 0; i < pt->count; ++i) {
        uint64_t end = (uint64_t)pt->table[2u * i] + pt->table[2u * i + 1u];
        if (end > (uint64_t)fileSize) {
            Psp_Fatal("AFS member %u exceeds archive length:\n  %s", i, path);
        }
    }
    return 0;
}

int ADXF_GetPtStat(int ptid) {
    return sPart[ptid & 7].fd > 0 ? STAT_READEND : STAT_STOP;
}

void *ADXF_OpenAfs(int ptid, int flid) {
    Partition *pt = &sPart[ptid & 7];
    PortFile *f;

    if (pt->fd <= 0 || flid < 0 || (uint32_t)flid >= pt->count) {
        fprintf(stderr, "bt3: file not found: partition %d, index %d\n", ptid, flid);
        return NULL;
    }
    f = Port_LowAlloc(sizeof(PortFile));
    f->fd = pt->fd;
    f->base = pt->table[flid * 2];
    f->size = pt->table[flid * 2 + 1];
    f->sizeSct = (int32_t)((f->size + SECTOR - 1) / SECTOR);
    f->stat = STAT_STOP;
    gPspFileOpens++;
    return f;
}

/* A file outside the archives, named relative to the disc's data directory. */
void *ADXF_Open(char *fname, void *atr) {
    char path[300];
    PortFile *f;
    SceUID fd;
    SceOff size;
    size_t n;

    (void)atr;
    if (!data_path(fname, path, sizeof(path))) {
        Psp_Log("psp: invalid or truncated DATA path\n");
        return NULL;
    }
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "bt3: file not found: %s\n", path);
        return NULL;
    }
    n = strlen(path);
    if (n > 4 && strcmp(&path[n - 4], ".PSS") == 0) { /* a movie: its decoder (plat_movie.c) needs the path */
        snprintf(gPortMoviePath, sizeof(gPortMoviePath), "%s", path);
    }
    size = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (size < 0 || (uint64_t)size > 0x7FFFFFFFu) {
        sceIoClose(fd);
        Psp_Log("psp: failed to determine DATA file size: %s\n", path);
        return NULL;
    }
    f = Port_LowAlloc(sizeof(PortFile));
    f->fd = fd;
    f->owned = 1;
    f->base = 0;
    f->size = (uint32_t)size;
    f->sizeSct = (int32_t)((size + SECTOR - 1) / SECTOR);
    f->stat = STAT_STOP;
    gPspFileOpens++;
    return f;
}

void ADXF_Close(void *h) {
    PortFile *f = h;

    if (f != NULL) {
        if (f->owned) {
            sceIoClose(f->fd);
        }
        Port_LowFree(f);
    }
}

int ADXF_GetFsizeSct(void *h) {
    return ((PortFile *)h)->sizeSct;
}

/* Reads nsct sectors at the current position; the part of the last sector behind the end of the file is zero. */
int ADXF_ReadNw(void *h, int nsct, void *buf) {
    PortFile *f = h;
    uint32_t pos = (uint32_t)f->posSct * SECTOR, want, have;
    int got;

    if (nsct > f->sizeSct - f->posSct) {
        nsct = f->sizeSct - f->posSct;
    }
    if (nsct <= 0) {
        f->stat = STAT_READEND;
        return 0;
    }
    want = (uint32_t)nsct * SECTOR;
    have = pos < f->size ? f->size - pos : 0;
    if (have > want) {
        have = want;
    }
    {
        unsigned long long t0 = Psp_TimeUs(), dt;
        got = have != 0 ? sceIoPread(f->fd, buf, have, (SceOff)f->base + pos) : 0;
        dt = Psp_TimeUs() - t0;
        gPspFileReads++;
        gPspFileUs += dt;
    }
    if (got < 0 || (uint32_t)got != have) {
        Psp_Log("psp: AFS short/error read fd=%d pos=%u expected=%u got=%d\n",
                (int)f->fd, (unsigned)pos, (unsigned)have, got);
        f->stat = STAT_ERROR;
        /* Never silently pad missing source bytes and claim success. */
        return 0;
    }
    memset((uint8_t *)buf + got, 0, want - (uint32_t)got);
    f->posSct += nsct;
    f->stat = STAT_READEND;
    gPspFileBytes += (unsigned)got;
    return nsct;
}

int ADXF_Stop(void *h) {
    ((PortFile *)h)->stat = STAT_STOP;
    return 0;
}

int ADXF_GetStat(void *h) {
    return ((PortFile *)h)->stat;
}

int ADXF_Tell(void *h) {
    return ((PortFile *)h)->posSct;
}

/* CRI set-up and servicing: nothing to do without a drive or a second thread. */
void ADXPS2_ExecVint(int mode) { (void)mode; }
void ADXPS2_SetupDvdFs(void *param) { (void)param; }
void ADXPS2_LoadFcacheDvd(void *param) { (void)param; }
void ADXPS2_SetupThrd(void *param, int unk) { (void)param; (void)unk; }
void ADXM_ExecMain(void) {}
extern void Port_VBlank(void);
void ADXM_WaitVsync(void) { Port_VBlank(); }
void ADXERR_EntryErrFunc(void (*func)(void *obj, char *msg), void *obj) { (void)func; (void)obj; }

/* The drive: a PS2 DVD is always present and idle (sceCdGetDiskTypeSafe 0x14 = PS2 DVD, sceCdStatus 10 = paused). */
int sceCdGetDiskTypeSafe(void) { return 0x14; }
int sceCdStatus(void) { return 10; }

/* A whole file into memory (malloc): a path, or a name Port_FilePath gave for an archive member. The sound
   streams (port/src/gs/snd_adx.c) decode from memory. NULL when it cannot be read. */
void *Port_LoadWhole(const char *path, uint32_t *size) {
    int ptid, flid;
    uint8_t *p;

    if (sscanf(path, "afs:%d:%d", &ptid, &flid) == 2) {
        Partition *pt = &sPart[ptid & 7];
        uint32_t n;
        if (pt->fd <= 0 || flid < 0 || (uint32_t)flid >= pt->count) {
            return NULL;
        }
        n = pt->table[flid * 2 + 1];
        p = malloc(n ? n : 1);
        if (p == NULL || sceIoPread(pt->fd, p, n, pt->table[flid * 2]) != (int)n) {
            free(p);
            return NULL;
        }
        *size = n;
        return p;
    } else {
        SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
        SceOff n;
        if (fd < 0) {
            return NULL;
        }
        n = sceIoLseek(fd, 0, PSP_SEEK_END);
        p = n > 0 && n < 0x4000000 ? malloc((size_t)n) : NULL;
        if (p == NULL || sceIoPread(fd, p, (SceSize)n, 0) != (int)n) {
            free(p);
            sceIoClose(fd);
            return NULL;
        }
        sceIoClose(fd);
        *size = (uint32_t)n;
        return p;
    }
}
