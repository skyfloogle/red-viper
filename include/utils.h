#ifndef _UTILS_H
#define _UTILS_H

#include "vb_types.h"

extern bool is_citra;

s32 k_patchSVC(void);
s32 k_flushCaches(void);
void hbHaxInit(void);
void detectCitra(WORD *code);
void hbHaxExit(void);
void FlushInvalidateCache(void *addr, size_t len);
Result ReprotectMemory(u32* addr, u32 pages, u32 mode);

#endif // _UTILS_H
