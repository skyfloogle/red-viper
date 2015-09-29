#ifndef _3DS_UTILS_H
#define _3DS_UTILS_H

s32 k_patchSVC();
s32 k_flushCaches();
void hbHaxInit();
void FlushInvalidateCache();
Result ReprotectMemory(u32* addr, u32 pages, u32 mode, u32* reprotectedPages);

#endif // _3DS_UTILS_H
