#include <stdio.h>
#include <sys/mman.h>

#include "utils.h"
#include "vb_types.h"
#include "vb_set.h"

void hbHaxInit() {
	tVBOpt.DYNAREC = 1;
}

void hbHaxExit() {
}

void FlushInvalidateCache() {
}

Result ReprotectMemory(u32* addr, u32 pages, u32 mode, u32* reprotectedPages) {
    int ret = mprotect(addr, pages*0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
    dprintf(0, "[DRC]: mprotect returned %d\n", ret);
    return ret;
}
