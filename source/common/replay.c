#include <stdio.h>
#include <zlib.h>
#include "replay.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_types.h"

typedef struct {
    HWORD inputs;
    HWORD count;
} ReplayEntry;

#define REPLAY_COUNT 100000

// "RVRP"
static const int MAGIC = 0x50525652;
static const int REPLAY_VERSION = 0;

static BYTE *initial_sram = NULL;
static int initial_sram_size;
static bool has_initial_sram;
static ReplayEntry *replay_buf = NULL, *replay_cursor;
static bool overflowed = false;

void replay_init(void) {
    initial_sram = malloc(vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr);
    replay_buf = malloc(sizeof(ReplayEntry) * REPLAY_COUNT);
}

void replay_reset(bool with_sram) {
    has_initial_sram = with_sram;
    if (with_sram) {
        initial_sram_size = vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr;
        memcpy(initial_sram, vb_state->V810_GAME_RAM.pmemory, initial_sram_size);
    } else {
        initial_sram_size = 0;
    }
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
        replay_cursor->count = 0;
    }
    replay_cursor->count++;
}

// returns 0 on success
static int gz_write_all(gzFile f, void *data, size_t size) {
    int cursor = 0;
    while (cursor < size) {
        int bytes_written = gzwrite(f, data + cursor, size - cursor);
        if (bytes_written == 0) return -1;
        cursor += bytes_written;
    }
    return 0;
}

void replay_save(char *fn) {
    gzFile f = gzopen(fn, "wb");
    if (!f) return;
    uint32_t header[4] = {MAGIC, REPLAY_VERSION, tVBOpt.CRC32, initial_sram_size};
    if (gz_write_all(f, header, sizeof(header))) goto bail;
    if (gz_write_all(f, initial_sram, initial_sram_size)) goto bail;
    if (gz_write_all(f, replay_buf, (replay_cursor + 1 - replay_buf) * sizeof(*replay_buf))) goto bail;
    bail:
    gzclose(f);
}

static FILE *current_replay;
static ReplayEntry current_entry;

void replay_load(char *fn) {
    current_entry.count = 0;
    current_replay = fopen(fn, "rb");
    if (!current_replay) return;
    uint32_t buf;
    fread(&buf, 4, 1, current_replay);
    if (buf != MAGIC) goto err;
    fread(&buf, 4, 1, current_replay);
    if (buf != REPLAY_VERSION) goto err;
    fread(&buf, 4, 1, current_replay);
    if (buf != tVBOpt.CRC32) goto err;
    fread(&buf, 4, 1, current_replay);
    if (buf > vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr) goto err;
    fread(vb_state->V810_GAME_RAM.pmemory, 1, buf, current_replay);
    return;
    err:
    fclose(current_replay);
    current_replay = NULL;
    return;
}

bool replay_playing(void) {
    return (bool)current_replay;
}

HWORD replay_read(void) {
    while (current_entry.count == 0) {
        fread(&current_entry, 4, 1, current_replay);
        if (feof(current_replay)) {
            fclose(current_replay);
            current_replay = NULL;
            return 0;
        }
    }
    current_entry.count--;
    return current_entry.inputs;
}