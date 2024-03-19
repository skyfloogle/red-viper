#include <stdio.h>
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_types.h"

typedef struct {
    HWORD inputs;
    HWORD count;
} ReplayEntry;

#define REPLAY_COUNT 200000

static const int version = 0;
static BYTE *initial_sram = NULL;
static int initial_sram_size;
static ReplayEntry *replay_buf = NULL, *replay_cursor;
static bool overflowed = false;

void replay_init(bool has_sram) {
    if (initial_sram) free(initial_sram);
    if (has_sram) {
        initial_sram_size = V810_GAME_RAM.highaddr + 1 - V810_GAME_RAM.lowaddr;
        initial_sram = malloc(initial_sram_size);
        memcpy(initial_sram, V810_GAME_RAM.pmemory, initial_sram_size);
    } else {
        initial_sram_size = 0;
        initial_sram = NULL;
    }
    if (!replay_buf) replay_buf = malloc(sizeof(ReplayEntry) * REPLAY_COUNT);
    replay_cursor = replay_buf;
    replay_cursor->inputs = 0;
    replay_cursor->count = 0;
    overflowed = false;
}

void replay_update(HWORD inputs) {
    if (overflowed) return;
    if (inputs != replay_cursor->inputs || replay_cursor->count == UINT16_MAX) {
        if (replay_cursor->count != 0) replay_cursor++;
        if (replay_cursor >= replay_buf + REPLAY_COUNT) {
            overflowed = true;
            return;
        }
        replay_cursor->inputs = inputs;
    }
    replay_cursor->count++;
}

void replay_save(char *fn) {
    FILE *f = fopen(fn, "wb");
    if (!f) return;
    fwrite("RVRP", 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&tVBOpt.CRC32, 4, 1, f);
    fwrite(&initial_sram_size, 4, 1, f);
    if (initial_sram_size) {
        fwrite(initial_sram, 1, initial_sram_size, f);
    }
    fwrite(replay_buf, sizeof(ReplayEntry), replay_cursor + 1 - replay_buf, f);
    fclose(f);
}