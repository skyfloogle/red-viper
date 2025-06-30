#include <3ds.h>
#include "utils.h"
#include "vb_set.h"
#include <stdio.h>

s32 k_patchSVC(void) {
    __asm__ volatile("cpsid aif");

    u32*  svc_access_control = *(*(u32***)0xFFFF9000 + 0x22) - 0x6;
    svc_access_control[0]=0xFFFFFFFE;
    svc_access_control[1]=0xFFFFFFFF;
    svc_access_control[2]=0xFFFFFFFF;
    svc_access_control[3]=0x3FFFFFFF;

    return 0;
}


s32 k_flushCaches(void) {
    __asm__ volatile(
        "cpsid aif\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c5, 0\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c10, 0\n\t"
    );

    return 0;
}

bool is_citra = false;
void detectCitra(WORD *test_code) {
    // only do the check once to prevent wackiness
    static bool tested = false;
    if (tested) return;
    tested = true;

    is_citra = false;
    test_code[0] = 0xe3a00001; // mov r0, #1
    test_code[1] = 0xe12fff1e; // bx lr
    FlushInvalidateCache(test_code, 8);
    bool (*code_func)() = (bool(*)())test_code;
    code_func();
    test_code[0] = 0xe3a00000; // mov r0, #0
    FlushInvalidateCache(test_code, 4);
    is_citra = code_func();
}

void hbHaxInit(void) {
    Handle tempHandle;

    if (!srvGetServiceHandle(&tempHandle, "am:u")) {
        svcCloseHandle(tempHandle);
        svcBackdoor(k_patchSVC);
    }
}


void hbHaxExit(void) {
}

void FlushInvalidateCache(void *addr, size_t len) {
    register void *addr_asm asm("r0") = addr;
    register size_t len_asm asm("r1") = len;
    if (!is_citra) {
        // works on hardware, does nothing on citra
        __asm__ volatile(
            "ldr r0, =k_flushCaches\n\t"
            "svc 0x80\n\t"
            :::"r0"
        );
    } else {
        // works on citra, crashes on hardware
        __asm__ volatile("svc 0x93"::"r"(addr_asm),"r"(len_asm));
    }
}

Result ReprotectMemory(u32* addr, u32 pages, u32 mode) {
    Handle processHandle;
    svcDuplicateHandle(&processHandle, 0xFFFF8001);
    return svcControlProcessMemory(processHandle, (u32)addr, 0x0, pages*0x1000, MEMOP_PROT, mode);
}
