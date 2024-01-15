#include <stddef.h>
#include "drc_alloc.h"

WORD *drc_alloc(uint32_t inst_count) {
    if ((cache_pos - cache_start + inst_count)*4 > CACHE_SIZE)
        return NULL;
    WORD *new_block = cache_pos;
    cache_pos += inst_count;
    return new_block;
}

void drc_free(exec_block *p_block) {
    p_block->free = true;
}