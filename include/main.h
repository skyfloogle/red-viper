////////////////////////////////////////////////////////////////
// Defines for the VB Core
#ifndef MAIN_H_
#define MAIN_H_

#include <stdio.h>
#include "v810_cpu.h"

char errmsg[256];

void clrScreen(int screen);
static inline void unicodeToChar(char* dst, uint16_t* src, int max);
int romSelect(char* path);
int LoadBitmap(char* path, u32 width, u32 height, void* buf, u32 alpha, u32 startx, u32 stride, u32 flags);
void setKeys();
int v810_init();
int v810_loadRom(char * rom_name);
void system_checkPolls();

//~ void save_sram(void);


#endif
