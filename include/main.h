////////////////////////////////////////////////////////////////
// Defines for the VB Core
#ifndef MAIN_H_
#define MAIN_H_

#include <stdio.h>
#include "v810_cpu.h"

#ifndef DEBUGLEVEL
#define DEBUGLEVEL 0
#endif

char rom_name[128];

// Opens up a VB rom (passed in) and initializes the rom space
int v810_init(char* rom_name);

#endif
