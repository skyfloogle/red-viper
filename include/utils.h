#ifndef _UTILS_H
#define _UTILS_H

#include "vb_types.h"

extern bool is_citra;

void hbHaxInit(void);
void detectCitra(void *code);
void hbHaxExit(void);
void FlushInvalidateCache(void *addr, size_t len);
int32_t ReprotectMemory(void* addr, uint32_t pages, uint32_t mode);

#endif // _UTILS_H
