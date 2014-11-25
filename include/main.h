////////////////////////////////////////////////////////////////
// Defines for the VB Core
#ifndef MAIN_H_
#define MAIN_H_

#include <stdio.h>
#include "v810_cpu.h"

char errmsg[256];

// Opens up a VB rom (pased in) and initializes the rom
// space, some day it will initialize ram as well
int v810_init(char * rom_name);

//~ void save_sram(void);

#endif
