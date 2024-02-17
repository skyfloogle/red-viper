#ifndef _UTILS_H
#define _UTILS_H

#include "vb_types.h"

extern bool is_citra;

s32 k_patchSVC();
s32 k_flushCaches();
void hbHaxInit();
void detectCitra(WORD *code);
void hbHaxExit();
void FlushInvalidateCache();
Result ReprotectMemory(u32* addr, u32 pages, u32 mode);

#endif // _UTILS_H
