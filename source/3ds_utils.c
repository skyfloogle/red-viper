#include <3ds.h>
#include "3ds_utils.h"
#include "vb_set.h"
#include "libkhax/khax.h"

s32 k_patchSVC() {
    __asm__ volatile("cpsid aif");

    u32*  svc_access_control = *(*(u32***)0xFFFF9000 + 0x22) - 0x6;
    svc_access_control[0]=0xFFFFFFFE;
    svc_access_control[1]=0xFFFFFFFF;
    svc_access_control[2]=0xFFFFFFFF;
    svc_access_control[3]=0x3FFFFFFF;

    return 0;
}


s32 k_flushCaches() {
    __asm__ volatile(
        "cpsid aif\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c5, 0\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c10, 0\n\t"
    );

    return 0;
}

void hbHaxInit() {
    Handle tempHandle;
    u32 pages;

    if (tVBOpt.DYNAREC) {
        if (srvGetServiceHandle(&tempHandle, "am:u")) {
            khaxInit();
        } else {
            svcCloseHandle(tempHandle);
            svcBackdoor(k_patchSVC);
        }
    }
}

void hbHaxExit() {
}

void FlushInvalidateCache() {
    if (tVBOpt.DYNAREC)
        svcBackdoor(k_flushCaches);
}

// https://github.com/smealum/ninjhax/blob/master/ro_command_handler/source/main.c
Result ReprotectMemory(u32* addr, u32 pages, u32 mode, u32* reprotectedPages) {
    if (!tVBOpt.DYNAREC)
        return 0xFFFFFFFF;

    u32 mode = mode & 0x7;
    if (!mode)mode = 0x7;

    Handle processHandle;
    svcDuplicateHandle(&processHandle, 0xFFFF8001);

    if (addr < 0x00108000 || addr >= 0x10000000 || pages > 0x1000 ||
        addr + pages * 0x1000 > 0x10000000) {
        // Send error
        return 0xFFFFFFFF;
    }

    u32 ret = 0;
    int i;
    for (i = 0; i < pages && !ret; i++)
        ret = svcControlProcessMemory(processHandle, addr + i * 0x1000, 0x0, 0x1000, MEMOP_PROT, mode);

    *reprotectedPages = i; // Number of pages successfully reprotected
    return ret; // Error code (if any)
}