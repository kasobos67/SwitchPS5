// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern int sceKernelUsleep(uint32_t microseconds);
extern int sceKernelDebugOutText(int channel, const char *text);
// Existing native-title convention: the managed runner closes this exact title.
__attribute__((noreturn)) void catchReturnFromMain(int status) {
    char marker[96];
    fflush(NULL);
    snprintf(marker, sizeof(marker), "EDEN_PPSA99121_MAIN_RETURN status=%d\n", status);
    sceKernelDebugOutText(0, marker);
    for (;;) sceKernelUsleep(100000);
}
__attribute__((noreturn)) void __assert(const char *function, const char *file,
                                      int line, const char *expression) {
    fprintf(stderr, "assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}
