#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_io_guard.h"
int main(void) {
    int data;
    int file;
    assert(!PspIo_BadFread(&data, 1, 1, &file));
    assert(!PspIo_BadFread(NULL, 0, 10, NULL));
    assert(!PspIo_BadFread(NULL, 10, 0, NULL));
    assert(PspIo_BadFread(NULL, 1, 1, &file));
    assert(PspIo_BadFread(&data, 1, 1, NULL));
    assert(PspIo_BadFread(&data, SIZE_MAX, 2, &file));
    puts("PSP application fread pointer/overflow guard passed");
    return 0;
}
