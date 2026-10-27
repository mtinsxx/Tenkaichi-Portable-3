/*
 * Memory card library (Sony libmc) on PC: slot 1 is a formatted card whose contents are ordinary files in a folder;
 * slot 2 is empty.
 *
 *     saves/card1/BASLUS-21678DBZT3/...     (the folder is `saves` in the current directory, or $BT3_SAVES)
 *
 * The game's paths ("/BASLUS-21678DBZT3/icon.sys") are used as they are below that folder, so a save made here
 * has the same files as one on a console card (icon.sys, the icon, the data file).
 *
 * The library is asynchronous: a call only starts a request and returns 0; sceMcSync reports the request's number
 * and its result. Here every request is carried out at once and sceMcSync hands the stored result over.
 * Results follow libmc (what src/battle/stgm_a_b.c and src/sys/mcflow_a.c test):
 *     sceMcGetInfo   0 = the card seen last time, -1 = a formatted card seen for the first time, -10 = no card;
 *                    type 2 = a PS2 card, free = free clusters of 1 KB, format 1 = formatted
 *     sceMcOpen      a file handle >= 0, or -4 = no such file      (mode: 1 read, 2 write, 0x200 create)
 *     sceMcMkdir     0, or -4 = it exists already
 *     sceMcGetDir    the number of entries that match (the game only asks "is this directory there": 1 or 0)
 *     sceMcRead / sceMcWrite   bytes transferred;   sceMcSeek   the new position;   sceMcClose / sceMcFormat   0
 * The names are the library's addresses in the PS2 executable; the game sources #define the sce names onto them.
 */
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path) /* Windows: no permission bits */
#endif

#define MC_FILES 8
#define MC_FREE_CLUSTERS 8000 /* an empty 8 MB card */

static int sLastCmd, sLastResult;
static int sSeen;             /* slot 1 was reported once: later sceMcGetInfo calls answer "same card" */
static FILE *sFiles[MC_FILES];

static int request(int cmd, int result) {
    sLastCmd = cmd;
    sLastResult = result;
    return 0;
}

/* Validate the virtual card's real directories; a failed mkdir must not
   advertise a formatted writable card and then lose saves. */
static int card_dir(const char *path) {
    struct stat st;
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return 0;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The host path of a card path; creates the card's folder on first use. Returns 0 for slot 2 (no card). */
static int host_path(int port, const char *name, char *out, size_t size) {
    const char *root = getenv("BT3_SAVES") != NULL ? getenv("BT3_SAVES") : "saves";
    char dir[512];
    int n;

    if (port != 0 || out == NULL || size == 0 || name == NULL ||
        !card_dir(root)) return 0;
    n = snprintf(dir, sizeof(dir), "%s/card1", root);
    if (n < 0 || (size_t)n >= sizeof(dir) || !card_dir(dir)) return 0;
    while (*name == '/') name++;
    if (strstr(name, "..") != NULL) return 0;
    n = snprintf(out, size, "%s/%s", dir, name);
    return n >= 0 && (size_t)n < size;
}

int func_002A1A48(void) { return 0; } /* sceMcInit */

/* sceMcGetInfo: the outputs are filled when the request completes. */
int func_002A25B8(int port, int slot, int *type, int *freeClusters, int *format) {
    char path[600];
    int card = slot == 0 && host_path(port, "", path, sizeof(path));

    if (type != NULL) { *type = card ? 2 : 0; }
    if (freeClusters != NULL) { *freeClusters = card ? MC_FREE_CLUSTERS : 0; }
    if (format != NULL) { *format = card ? 1 : 0; }
    if (!card) {
        return request(1, -10);
    }
    if (!sSeen) {
        sSeen = 1;
        return request(1, -1);
    }
    return request(1, 0);
}

/* sceMcOpen */
int func_002A1E58(int port, int slot, char *name, int mode) {
    char path[600];
    FILE *f;
    int fd;

    if (slot != 0 || !host_path(port, name, path, sizeof(path))) {
        return request(2, -10);
    }
    for (fd = 0; fd < MC_FILES && sFiles[fd] != NULL; fd++) {
    }
    if (fd == MC_FILES) {
        return request(2, -7); /* too many open files */
    }
    f = fopen(path, (mode & 2) ? "r+b" : "rb");
    if (f == NULL && (mode & 0x200)) {
        f = fopen(path, "w+b");
    }
    if (f == NULL) {
        return request(2, -4);
    }
    sFiles[fd] = f;
    return request(2, fd);
}

/* sceMcMkdir */
int func_002A1F80(int port, int slot, char *name) {
    char path[600];

    if (slot != 0 || !host_path(port, name, path, sizeof(path))) {
        return request(11, -10);
    }
    return request(11, mkdir(path, 0755) == 0 ? 0 : -4);
}

/* sceMcClose */
int func_002A1FB8(int fd) {
    if (fd < 0 || fd >= MC_FILES || sFiles[fd] == NULL) {
        return request(3, -4);
    }
    {
        int rc = fclose(sFiles[fd]);
        sFiles[fd] = NULL;
        return request(3, rc == 0 ? 0 : -1);
    }
}

/* sceMcSeek: origin 0 = start, 1 = current, 2 = end */
int func_002A2078(int fd, int offset, int origin) {
    if (fd < 0 || fd >= MC_FILES || sFiles[fd] == NULL) {
        return request(4, -4);
    }
    if (origin < 0 || origin > 2 ||
        fseek(sFiles[fd], offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END) != 0)
        return request(4, -1);
    return request(4, (int)ftell(sFiles[fd]));
}

/* sceMcRead */
int func_002A2208(int fd, void *buf, int size) {
    if (fd < 0 || fd >= MC_FILES || sFiles[fd] == NULL || size < 0 || (size && buf == NULL)) {
        return request(5, -4);
    }
    {
        size_t n = fread(buf, 1, (size_t)size, sFiles[fd]);
        return request(5, n == (size_t)size || !ferror(sFiles[fd]) ? (int)n : -1);
    }
}

/* sceMcWrite */
int func_002A2320(int fd, void *buf, int size) {
    int n;

    if (fd < 0 || fd >= MC_FILES || sFiles[fd] == NULL || size < 0 || (size && buf == NULL)) {
        return request(6, -4);
    }
    n = (int)fwrite(buf, 1, (size_t)size, sFiles[fd]);
    if (n != size || fflush(sFiles[fd]) != 0) {
        return request(6, -1); /* do not announce a corrupt/unsaved file as written */
    }
    return request(6, n);
}

/* sceMcGetDir: the number of entries matching `name` ("*" = everything in the root, otherwise one name). The
   table is not filled: nothing in the game reads it. */
int func_002A27A8(int port, int slot, char *name, int mode, int maxent, void *table) {
    char path[600];
    struct stat st;
    int n = 0;

    (void)mode; (void)maxent; (void)table;
    if (slot != 0 || !host_path(port, strcmp(name, "*") == 0 ? "" : name, path, sizeof(path))) {
        return request(13, -10);
    }
    if (strcmp(name, "*") == 0) {
        DIR *d = opendir(path);
        struct dirent *e;
        while (d != NULL && (e = readdir(d)) != NULL) {
            if (e->d_name[0] != '.') {
                n++;
            }
        }
        if (d != NULL) {
            closedir(d);
        }
    } else {
        n = stat(path, &st) == 0 ? 1 : 0;
    }
    return request(13, n);
}

/* sceMcFormat: the folder is always "formatted"; nothing is erased. */
int func_002A2AC8(int port, int slot) {
    (void)slot;
    return request(16, port == 0 ? 0 : -10);
}

/* sceMcSync: 1 = the request has finished (its number and result are returned), -1 = nothing was pending. */
int func_002A2498(int mode, int *cmd, int *result) {
    (void)mode;
    if (sLastCmd == 0) {
        return -1;
    }
    if (cmd != NULL) { *cmd = sLastCmd; }
    if (result != NULL) { *result = sLastResult; }
    sLastCmd = 0;
    return 1;
}
