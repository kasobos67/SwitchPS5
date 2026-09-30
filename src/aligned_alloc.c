// SPDX-License-Identifier: GPL-3.0-or-later
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* Use the native boilerplate's proven allocation/free pair. */
void *__wrap_aligned_alloc(size_t alignment, size_t size) {
    if (!alignment || (alignment & (alignment - 1)) || size % alignment) {
        errno = EINVAL;
        return NULL;
    }
    void *address = NULL;
    int result = posix_memalign(&address, alignment < sizeof(void *) ? sizeof(void *) : alignment, size);
    if (result) {
        errno = result;
        fprintf(stderr, "posix_memalign failed: size=%zu alignment=%zu error=%d\n", size, alignment, result);
    }
    return result ? NULL : address;
}
