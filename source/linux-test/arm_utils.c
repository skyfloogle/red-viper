#include <stdio.h>
#include <sys/mman.h>

#include "utils.h"
#include "drc_core.h"
#include "vb_types.h"
#include "vb_set.h"

typedef int32_t Result;
typedef uint32_t u32;

#if ARM_DRC
void FlushInvalidateCache(void *addr, size_t len) {
    __builtin___clear_cache(addr, addr + len);
}

Result ReprotectMemory(void* addr, u32 pages, u32 mode) {
    int ret = mprotect(addr, pages*0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
    dprintf(0, "[DRC]: mprotect returned %d\n", ret);
    return ret;
}
#endif
