#include "vb_types.h"
#include "drc_core.h"

extern int free_block_count;

WORD *drc_alloc(uint32_t inst_count);
void drc_free(exec_block *p_block);