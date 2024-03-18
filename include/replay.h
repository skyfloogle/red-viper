#include "vb_types.h"

void replay_init(bool has_sram);
void replay_update(HWORD inputs);
void replay_save(char *fn);