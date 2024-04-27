#include <stddef.h>
#include <stdio.h>
#include "vb_types.h"
#include "drc_alloc.h"

typedef struct {
    WORD *start;
    int size;
} FreeBlock;

FreeBlock free_blocks[MAX_NUM_BLOCKS];
int free_block_count = 0;

static void mark_block(WORD *start, WORD size, SHWORD id) {
    ((SHWORD*)(start - 1))[1] = id;
    ((SHWORD*)(start + size))[0] = id;
}

WORD *drc_alloc(uint32_t inst_count) {
    int block = -1;
    for (int i = 0; i < free_block_count; i++) {
        if (free_blocks[i].size == inst_count) {
            block = i;
            break;
        }
        if (free_blocks[i].size > inst_count) {
            if (block == -1 || free_blocks[i].size > free_blocks[block].size) {
                block = i;
            }
        }
    }
    if (block != -1) {
        WORD *new_block = free_blocks[block].start;
        free_blocks[block].size -= inst_count + 1;
        free_blocks[block].start += inst_count + 1;
        if (free_blocks[block].size < 0) {
            free_blocks[block] = free_blocks[--free_block_count];
        }
        // mark shrunk or moved block
        if (block < free_block_count) {
            mark_block(free_blocks[block].start, free_blocks[block].size, block);
        }
        mark_block(new_block, inst_count, -1);
        return new_block;
    }
    if ((cache_pos - cache_start + inst_count)*4 >= CACHE_SIZE)
        return NULL;
    WORD *new_block = cache_pos;
    cache_pos += inst_count + 1;
    mark_block(new_block, inst_count, -1);
    // additionally set the next (unallocated) block
    new_block[inst_count] = -1;
    return new_block;
}

void drc_free(exec_block *p_block) {
    p_block->free = true;
    SHWORD last_block = ((SHWORD*)(p_block->phys_offset - 1))[0];
    SHWORD next_block = ((SHWORD*)(p_block->phys_offset + p_block->size))[1];
    bool block_found = false;
    if (last_block >= 0) {
        if (last_block >= free_block_count) {
            dprintf(0, "<invalid block %d at %p\n", last_block, p_block->phys_offset);
        }
        if (free_blocks[last_block].start + free_blocks[last_block].size + 1 != p_block->phys_offset) {
            dprintf(0, "<invalid block %p..%d != %p\n", free_blocks[last_block].start, free_blocks[last_block].size, p_block->phys_offset);
        }
        block_found = true;
        free_blocks[last_block].size += 1 + p_block->size;
        /*
        // optimization for when the last block is freed
        // currently breaks guns in red alarm for some reason
        if (free_blocks[last_block].start + free_blocks[last_block].size + 1 == cache_pos) {
            cache_pos = free_blocks[last_block].start + free_blocks[last_block].size + 1;
            free_blocks[last_block] = free_blocks[--free_block_count];
            if (last_block == free_block_count) {
                ((SHWORD*)(cache_pos - 1))[1] = -1;
            }
        }
        */
        if (last_block < free_block_count)
            mark_block(free_blocks[last_block].start, free_blocks[last_block].size, last_block);
    } else if (p_block->phys_offset + p_block->size + 1 == cache_pos) {
        // optimization for when the last block is freed
        cache_pos = p_block->phys_offset;
        return;
    }
    if (next_block >= 0) {
        if (next_block >= free_block_count) {
            dprintf(0, ">invalid block %d at %p\n", next_block, p_block->phys_offset);
        }
        if (p_block->phys_offset + p_block->size + 1 != free_blocks[next_block].start) {
            dprintf(0, ">invalid block %p..%ld != %p\n", p_block->phys_offset, p_block->size, free_blocks[next_block].start);
        }
        block_found = true;
        if (last_block >= 0) {
            free_blocks[last_block].size += 1 + free_blocks[next_block].size;
            mark_block(free_blocks[last_block].start, free_blocks[last_block].size, last_block);
            free_blocks[next_block] = free_blocks[--free_block_count];
            mark_block(free_blocks[next_block].start, free_blocks[next_block].size, next_block);
        } else {
            free_blocks[next_block].size += 1 + p_block->size;
            free_blocks[next_block].start -= 1 + p_block->size;
            mark_block(free_blocks[next_block].start, free_blocks[next_block].size, next_block);
        }
    }
    if (block_found) return;
    if (free_block_count < MAX_NUM_BLOCKS) {
        free_blocks[free_block_count].start = p_block->phys_offset;
        free_blocks[free_block_count].size = p_block->size;
        for (int i = 0; i < p_block->size; i++) {
            // bkpt
            p_block->phys_offset[i] = 0xe1200070;
        }
        mark_block(p_block->phys_offset, p_block->size, free_block_count++);
    }
}