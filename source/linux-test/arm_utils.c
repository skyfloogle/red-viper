#include <stdio.h>
#include <sys/mman.h>

#include "utils.h"
#include "drc_core.h"
#include "vb_types.h"
#include "vb_set.h"

typedef int32_t Result;
typedef uint32_t u32;

void hbHaxInit(void) {
}

void hbHaxExit(void) {
}

void FlushInvalidateCache(void *addr, size_t len) {
    #if DRC_AVAILABLE
    __clear_cache(cache_start, cache_start + CACHE_SIZE - 1);
    #endif
}

void detectCitra(WORD *cache_start) {}

Result ReprotectMemory(u32* addr, u32 pages, u32 mode) {
    int ret = mprotect(addr, pages*0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
    dprintf(0, "[DRC]: mprotect returned %d\n", ret);
    return ret;
}
