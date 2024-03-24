#include "vb_types.h"

void replay_init(bool has_sram);
void replay_update(HWORD inputs);
void replay_save(char *fn);
void replay_load(char *fn);
bool replay_playing();
HWORD replay_read();