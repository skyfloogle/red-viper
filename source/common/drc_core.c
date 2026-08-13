/*
 * V810 dynamic recompiler for ARM
 *
 * This file is distributed under the MIT License. However, some of the code
 * (the V810 instruction decoding) is based on Reality Boy's interpreter and
 * was written by David Tucker. For more information on the original license,
 * check the README.
 *
 * Copyright (c) 2015 danielps
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <inttypes.h>

#include "video_hard.h"

#include "utils.h"
#include "drc_alloc.h"
#include "drc_core.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_set.h"
#include "vb_types.h"

#include "replay.h"

#include "vb_dsp.h"

HWORD* rom_block_map;
HWORD* rom_entry_map;
BYTE* rom_data_code_map;
BYTE reg_usage[32];
drc_unit* cache_start;
drc_unit* cache_pos;
exec_block* block_ptr_start;
int block_pos = 1;

v810_instruction *inst_cache;
static ir_inst *trans_cache;
ir_inst *inst_ptr;

static bool is_byte_getter(WORD start_PC) {
    static BYTE byte_getter_func[] = {
        0x46, 0xc1, 0x00, 0x00, // ld.b [r6], r10
        0x1f, 0x18,             // jmp  [lp]
    };
    BYTE* dest = (BYTE*)V810_ROM1.off + start_PC;
    return !memcmp(dest, byte_getter_func, sizeof(byte_getter_func));
}

static bool is_hword_getter(WORD start_PC) {
    static BYTE hword_getter_func[] = {
        0x46, 0xc5, 0x00, 0x00, // ld.h [r6], r10
        0x1f, 0x18,             // jmp  [lp]
    };
    static BYTE hword_getter_jr_func[] = {
        0x46, 0xc5, 0x00, 0x00, // ld.h [r6], r10
        0x00, 0xa8, 0x04, 0x00, // jr   +4
        0x1f, 0x18,             // jmp  [lp]
    };
    BYTE* dest = (BYTE*)V810_ROM1.off + start_PC;
    return !memcmp(dest, hword_getter_func, sizeof(hword_getter_func))
        || !memcmp(dest, hword_getter_jr_func, sizeof(hword_getter_jr_func));
}

static void drc_markCode(WORD PC) {
    rom_data_code_map[(((PC & V810_ROM1.highaddr) >> 1) & (BLOCK_MAP_COUNT - 1)) >> 3] |= 1 << ((PC >> 1) & 7);
}

static void drc_markData(WORD PC) {
    rom_data_code_map[(((PC & V810_ROM1.highaddr) >> 1) & (BLOCK_MAP_COUNT - 1)) >> 3] &= ~(1 << ((PC >> 1) & 7));
}

static bool drc_isCode(WORD PC) {
    return !!(rom_data_code_map[(((PC & V810_ROM1.highaddr) >> 1) & (BLOCK_MAP_COUNT - 1)) >> 3] & (1 << ((PC >> 1) & 7)));
}

// Finds the starting and ending address of a V810 code block. It stops after a
// jmp, jal, reti or a long jr unless it branches further.
// All code accessible from the entry point is accounted for.
static void drc_scanBlockBounds(WORD* p_start_PC, WORD* p_end_PC) {
    WORD start_PC = *p_start_PC & V810_ROM1.highaddr;
    WORD end_PC = start_PC;
    WORD cur_PC;
    WORD branch_addr;
    int branch_offset;
    BYTE opcode;
    bool finished;
    BYTE lowB, highB, lowB2, highB2;

    cur_PC = start_PC;
    finished = false;
    while(!finished) {
        bool potentiallyDone = false;

        exec_block *existing_block;
        if (drc_getEntry(cur_PC, &existing_block) != cache_start) {
            drc_free(existing_block);
            WORD existing_start = existing_block->start_pc;
            WORD existing_end = existing_start + existing_block->pc_range;
            if (cur_PC < existing_start || cur_PC > existing_end) {
                for (WORD PC = existing_start; PC <= existing_end; PC += 2)
                    drc_markData(PC);
                if (existing_start < cur_PC) cur_PC = start_PC;
            } else {
                if (existing_start < start_PC) start_PC = existing_start;
                if (existing_end > end_PC) end_PC = existing_end;
            }
        }

        drc_markCode(cur_PC);
        if (cur_PC > end_PC)
            end_PC = cur_PC;

        cur_PC = (cur_PC & V810_ROM1.highaddr);
        lowB   = ((BYTE *)(V810_ROM1.off + cur_PC))[0];
        highB  = ((BYTE *)(V810_ROM1.off + cur_PC))[1];
        lowB2  = ((BYTE *)(V810_ROM1.off + cur_PC))[2];
        highB2 = ((BYTE *)(V810_ROM1.off + cur_PC))[3];

        if ((highB & 0xE0) == 0x80)
            opcode = highB>>1;
        else
            opcode = highB>>2;

        switch (opcode) {
            case V810_OP_JR:
                branch_offset = (signed)sign_26(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                if (abs(branch_offset) < 1024) {
                    branch_addr = cur_PC + branch_offset;
                    bool should_backjump = false;
                    if (branch_addr < start_PC) {
                        start_PC = branch_addr;
                        should_backjump = true;
                    } else if (branch_addr > end_PC) {
                        end_PC = branch_addr;
                    }

                    bool was_code = drc_isCode(branch_addr);
                    drc_markCode(branch_addr);
                    if (branch_offset < 0) {
                        if (!was_code) {
                            // Not already scanned, so scan it.
                            cur_PC = branch_addr;
                            continue;
                        } else {
                            // Was previously scanned, so as long as we scanned it just now,
                            // anything following from it should be accounted for.
                            if (should_backjump) {
                                cur_PC = branch_addr;
                                continue;
                            }
                        }
                    }

                    potentiallyDone = true;
                    break;
                }
            case V810_OP_JAL:
                branch_addr = cur_PC + (signed)sign_26(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                if (is_byte_getter(branch_addr) || is_hword_getter(branch_addr)) break;
            case V810_OP_JMP:
            case V810_OP_RETI:
                potentiallyDone = true;
                break;
            case V810_OP_BV:
            case V810_OP_BL:
            case V810_OP_BE:
            case V810_OP_BNH:
            case V810_OP_BN:
            case V810_OP_BR:
            case V810_OP_BLT:
            case V810_OP_BLE:
            case V810_OP_BNV:
            case V810_OP_BNL:
            case V810_OP_BNE:
            case V810_OP_BH:
            case V810_OP_BP:
            case V810_OP_BGE:
            case V810_OP_BGT:
                branch_offset = (signed)sign_9(((highB & 0x1) << 8) + (lowB & 0xFE));
                branch_addr = cur_PC + branch_offset;
                bool should_backjump = false;
                if (branch_addr < start_PC) {
                    start_PC = branch_addr;
                    should_backjump = true;
                } else if (branch_addr > end_PC) {
                    end_PC = branch_addr;
                }

                if (opcode == V810_OP_BR) {
                    potentiallyDone = true;
                }

                bool was_code = drc_isCode(branch_addr);
                drc_markCode(branch_addr);
                if (branch_offset < 0) {
                    if (!was_code) {
                        // Not already scanned, so scan it.
                        cur_PC = branch_addr;
                        continue;
                    } else {
                        // Was previously scanned, so as long as we scanned it just now,
                        // anything following from it should be accounted for.
                        if (should_backjump) {
                            cur_PC = branch_addr;
                            continue;
                        }
                    }
                }
                break;
        }

        if (potentiallyDone) {
            if (cur_PC >= end_PC) {
                end_PC = cur_PC;
                finished = true;
            } else {
                // end_PC should always be marked as code, so we don't need to bounds check
                do {
                    cur_PC += 2;
                } while (!drc_isCode(cur_PC));
            }
        } else {
            cur_PC += am_size_table[optable[opcode].addr_mode];
        }
    }

    *p_start_PC = start_PC;
    *p_end_PC = end_PC;
}

// Finds an instruction in the given range at the given PC, or the one just after it.
static v810_instruction *drc_findInstruction(v810_instruction *left, v810_instruction *right, WORD goal_PC) {
    while (left != right) {
        v810_instruction *pivot = left + (right - left) / 2;
        if (pivot->PC < goal_PC) left = pivot + 1;
        else if (pivot->PC > goal_PC) right = pivot;
        else return pivot;
    }
    return right;
}

// Finds the target of a branch, or the instruction just after it.
static v810_instruction *drc_findBranchTarget(int size, int pos) {
    // attempt to narrow down
    int close = pos;
    int far = pos + inst_cache[pos].branch_offset / 2;
    int left = close < far ? close : far;
    int right = close < far ? far : close;
    if (left < 0) left = 0;
    if (right >= size) right = size - 1;
    // if it's somehow outside our guessed range, look at the rest of it
    WORD goal_PC = inst_cache[pos].PC + inst_cache[pos].branch_offset;
    if (inst_cache[left].PC > goal_PC) {
        right = left;
        left = left > pos ? pos : 0;
    } else if (inst_cache[right].PC < goal_PC) {
        left = right;
        right = right < pos ? pos : size - 1;
    }
    return drc_findInstruction(&inst_cache[left], &inst_cache[right], goal_PC);
}

static void drc_findWaterworldBusywait(int size) {
    for (int i = 3; i < size; i++) {
        // scan for this pattern:
        // ld.h <...>[gp], r10
        // cmp <...>, r10
        // b<...> +
        // jr <...>
        // + ...
        if (inst_cache[i].opcode == V810_OP_JR && abs(inst_cache[i].branch_offset) < 1024 &&
            inst_cache[i - 1].branch_offset == 6 &&
            inst_cache[i - 2].opcode == V810_OP_CMP_I && inst_cache[i - 2].reg2 == 10 &&
            inst_cache[i - 3].opcode == V810_OP_LD_H && inst_cache[i - 3].reg1 == 4 && inst_cache[i - 3].reg2 == 10
        ) {
            // check some known combinations
            if ((inst_cache[i - 1].opcode == V810_OP_BNE && inst_cache[i - 2].imm == 0 && inst_cache[i - 3].imm == 0x8030) ||
                (inst_cache[i - 1].opcode == V810_OP_BE && inst_cache[i - 2].imm == 1 && inst_cache[i - 3].imm == 0x8010)
            ) {
                // it's probably safe at this point
                inst_cache[i].busywait = true;
                dprintf(1, "waterworld busywait at %lx\n", inst_cache[i].PC);
            }
        }
    }
}

void drc_clearScreenForGolf(void) {
    if (!emulating_self) return;
    gpu_clear_screen(true);
}

// Baseball 2 unpacked sprite cache. Not strictly required for performance,
// but since we're HLE'ing this anyway, might as well.
#define BASEBALL2_SPRITES_COUNT 512
static bool baseball2_sprites_is_unpacked[BASEBALL2_SPRITES_COUNT];
static WORD baseball2_sprites_address[BASEBALL2_SPRITES_COUNT];
static BYTE baseball2_sprites_unpacked[BASEBALL2_SPRITES_COUNT][32][32];

void baseball2_scaling(WORD in_img, WORD out_img, WORD scale_fixed) {
    // The input/output format is 4x4 tiles
    void *in_ptr = (void*)(V810_ROM1.off + in_img);
    void *out_ptr = (void*)(vb_state->V810_VB_RAM.off + out_img);

    // Get cached sprite if possible
    unsigned sprite_id = (in_img >> 8) % BASEBALL2_SPRITES_COUNT;
    BYTE (*in_unpacked)[32] = baseball2_sprites_unpacked[sprite_id];
    bool is_unpacked = baseball2_sprites_is_unpacked[sprite_id] && baseball2_sprites_address[sprite_id] == in_img;
    if (!is_unpacked) {
        // Cached doesn't exist, so unpack input image
        baseball2_sprites_is_unpacked[sprite_id] = true;
        baseball2_sprites_address[sprite_id] = in_img;
        for (int ty = 0; ty < 4; ty++) {
            for (int tx = 0; tx < 4; tx++) {
                for (int y = 0; y < 8; y++) {
                    HWORD row = ((HWORD*)in_ptr)[ty*8*4+tx*8+y];
                    for (int x = 0; x < 8; x++) {
                        in_unpacked[ty*8+y][tx*8+x] = (row >> (x*2)) & 3;
                    }
                }
            }
        }
    }

    // Pre-compute x offsets
    int xcount = 32;
    BYTE x_offsets[32];
    for (int i = 0; i < 32; i++) {
        unsigned x_offset = (i * scale_fixed) >> 16;
        if (x_offset >= 32) {
            xcount = i;
            break;
        }
        x_offsets[i] = x_offset;
    }

    // Scale
    static BYTE out_unpacked[32][32];
    memset(out_unpacked, 0, sizeof(out_unpacked));
    for (
        unsigned y = 0, scaled_y_fp = 0, scaled_y = 0;
        y < 32 && scaled_y < 32;
        y++, scaled_y_fp += scale_fixed, scaled_y = scaled_y_fp >> 16
    ) {
        unsigned scaled_y = scaled_y_fp >> 16;
        for (unsigned x = 0; x < xcount; x++) {
            unsigned scaled_x = x_offsets[x];
            out_unpacked[y][x] = in_unpacked[scaled_y][scaled_x];
        }
    }

    // Re-pack into output
    for (int ty = 0; ty < 4; ty++) {
        for (int tx = 0; tx < 4; tx++) {
            for (int y = 0; y < 8; y++) {
                HWORD row = 0;
                for (int x = 0; x < 8; x++) {
                    row |= out_unpacked[ty*8+y][tx*8+x] << (x*2);
                }
                ((HWORD*)out_ptr)[ty*8*4+tx*8+y] = row;
            }
        }
    }
}

void baseball2_sort(void) {
    u8 ids[13];
    typedef struct {
        WORD padding1;
        HWORD key;
        HWORD padding2[sizeof(ids)];
    } SortableItem;
    SortableItem *out = (SortableItem*)(vb_state->V810_VB_RAM.pmemory + 0x93a0);
    SortableItem originals[sizeof(ids)];
    memcpy(originals, out, sizeof(originals));
    for (int i = 0; i < sizeof(ids); i++) ids[i] = i;
    // insertion sort
    for (int i = 1; i < sizeof(ids); i++) {
        u8 x = ids[i];
        u8 key = originals[x].key;
        int j;
        for (j = i; j > 0 && originals[ids[j - 1]].key > key; j--) {
            ids[j] = ids[j - 1];
        }
        ids[j] = x;
    }
    for (int i = 0; i < sizeof(ids); i++) {
        memcpy(&out[i], &originals[ids[i]], sizeof(out[i]));
    }
}

// Workaround for an issue where the CPSR is modified outside of the block
// before a conditional branch.
// Sets save_flags for all unconditional instructions prior to a branch.
static void drc_findLastConditionalInst(int pos) {
    bool save_flags = true, busywait = inst_cache[pos].branch_offset <= 0 && inst_cache[pos].opcode != V810_OP_SETF;
    if (inst_cache[pos].branch_offset == 0) {
        // catch edge case of block that starts with branch to self
        dprintf(0, "busywait at %lx to %lx\n", inst_cache[pos].PC, inst_cache[pos].PC + inst_cache[pos].branch_offset);
        inst_cache[pos].busywait = true;
        busywait = false;
    }
    for (int i = pos - 1; i >= 0; i--) {
        switch (inst_cache[i].opcode) {
            case V810_OP_LD_W:
            case V810_OP_IN_W:
                inst_cache[i].save_flags = save_flags;
                // if a register is loading itself, it might not be a busywait
                if (inst_cache[i].reg1 == inst_cache[i].reg2) {
                    busywait = false;
                }
                break;
            case V810_OP_LD_B:
            case V810_OP_LD_H:
            case V810_OP_IN_B:
            case V810_OP_IN_H:
            case V810_OP_ST_B:
            case V810_OP_ST_H:
            case V810_OP_ST_W:
            case V810_OP_OUT_B:
            case V810_OP_OUT_H:
            case V810_OP_OUT_W:
            case V810_OP_MOV:
            case V810_OP_MOV_I:
            case V810_OP_MOVEA:
            case V810_OP_MOVHI:
                inst_cache[i].save_flags = save_flags;
                break;
            case V810_OP_AND:
            case V810_OP_ANDI:
            case V810_OP_CMP:
            case V810_OP_CMP_I:
                // affects flags but is used in busywait
                save_flags = false;
                break;
            case V810_OP_JAL:
                // nester's funky bowling calls a function to do its busywait read
                // and it does this several times
                if (CHECK_GAMEID("01VNFE") && (
                    inst_cache[i].PC + inst_cache[i].branch_offset == 0x07005326 ||
                    inst_cache[i].PC + inst_cache[i].branch_offset == 0x07001f2c
                )) break;
                // zero racers calls a function to do its busywait read
                if (CHECK_GAMEID("01VZRE") && inst_cache[i].PC + inst_cache[i].branch_offset == 0x07013754) {
                    break;
                }
            case V810_OP_ADD:
            case V810_OP_OR:
                // only certain operators are ok for busywait here, otherwise fallthrough
                if (
                    (inst_cache[i].opcode == V810_OP_OR && inst_cache[i].reg1 == inst_cache[i].reg2) ||
                    (inst_cache[i].opcode == V810_OP_ADD && inst_cache[i].reg1 == 0)
                ) {
                    save_flags = false;
                    break;
                }
            case V810_OP_SHR_I:
                // virtual league baseball 2 uses a shr in busywaits in several places
                if (i == pos - 1 && i >= 2
                    && inst_cache[i - 2].opcode == V810_OP_MOVHI
                    && inst_cache[i - 2].reg1 == 0
                    && inst_cache[i - 1].opcode == V810_OP_LD_H
                    && inst_cache[i - 1].reg1 == inst_cache[i - 2].reg2
                    && inst_cache[i].opcode == V810_OP_SHR_I
                    && inst_cache[i].reg2 == inst_cache[i - 1].reg2
                    && inst_cache[pos].PC + inst_cache[pos].branch_offset == inst_cache[i - 2].PC
                ) {
                    save_flags = false;
                    break;
                }
                // dragon hopper uses a shr in busywaits too sometimes
                if (i == pos - 1 && i >= 1
                    && inst_cache[i - 1].opcode == V810_OP_LD_B
                    && inst_cache[i - 1].reg1 != inst_cache[i - 1].reg2
                    && inst_cache[i].opcode == V810_OP_SHR_I
                    && inst_cache[i].reg2 == inst_cache[i - 1].reg2
                    && inst_cache[pos].PC + inst_cache[pos].branch_offset == inst_cache[i - 1].PC
                ) {
                    save_flags = false;
                    break;
                }
            case V810_OP_SAR_I:
                // zero racers uses complex shifting in multiple busywaits
                if (CHECK_GAMEID("01VZRE")
                    && i >= 5
                    && (i == pos - 2 || i == pos - 3)
                    && inst_cache[i - 5].opcode == V810_OP_LD_H
                    && inst_cache[i - 4].opcode == V810_OP_ANDI
                    && inst_cache[i - 4].imm == 0xc
                    && inst_cache[i - 4].reg1 == inst_cache[i - 5].reg2
                    && inst_cache[i - 3].opcode == V810_OP_SAR_I
                    && inst_cache[i - 3].imm == 2
                    && inst_cache[i - 3].reg2 == inst_cache[i - 4].reg2
                    && inst_cache[i - 2].opcode == V810_OP_MOV
                    && inst_cache[i - 2].reg1 == inst_cache[i - 3].reg2
                    && inst_cache[i - 1].opcode == V810_OP_SHL_I
                    && inst_cache[i - 1].imm == 0x10
                    && inst_cache[i - 1].reg2 == inst_cache[i - 2].reg2
                    && inst_cache[i - 0].opcode == V810_OP_SAR_I
                    && inst_cache[i - 0].imm == 0x10
                    && inst_cache[i - 0].reg2 == inst_cache[i - 1].reg2
                    && inst_cache[pos].PC + inst_cache[pos].branch_offset == inst_cache[i - 5].PC
                ) {
                    dprintf(0, "busywait at %lx to %lx\n", inst_cache[pos].PC, inst_cache[pos].PC + inst_cache[pos].branch_offset);
                    inst_cache[pos].busywait = true;
                }
            default:
                return;
        }
        if (busywait && inst_cache[i].PC <= inst_cache[pos].PC + inst_cache[pos].branch_offset) {
            dprintf(0, "busywait at %lx to %lx\n", inst_cache[pos].PC, inst_cache[pos].PC + inst_cache[pos].branch_offset);
            inst_cache[pos].busywait = true;
            busywait = false;
        }
    }
}

// Decodes the instructions from start_PC to end_PC and stores them in
// inst_cache.
// Returns the number of instructions decoded.
static unsigned int drc_decodeInstructions(exec_block *block, WORD start_PC, WORD end_PC) {
    unsigned int i = 0;
    // Up to 4 bytes for instruction (either 16 or 32 bits)
    BYTE lowB, highB, lowB2, highB2;
    WORD cur_PC = start_PC;
    bool finished;

    WORD entry_PC = vb_state->v810_state.PC;

    for (; (i < MAX_V810_INST) && (cur_PC <= end_PC); i++) {
        cur_PC = (cur_PC & V810_ROM1.highaddr);
        lowB   = ((BYTE *)(V810_ROM1.off + cur_PC))[0];
        highB  = ((BYTE *)(V810_ROM1.off + cur_PC))[1];
        lowB2  = ((BYTE *)(V810_ROM1.off + cur_PC))[2];
        highB2 = ((BYTE *)(V810_ROM1.off + cur_PC))[3];

        if (cur_PC == 0x07004e1a) {
            dprintf(0, "iaupsdfhjasdjklfhasdlf %lx", cur_PC);
        }

        inst_cache[i].PC = cur_PC;
        inst_cache[i].save_flags = false;
        inst_cache[i].busywait = false;
        inst_cache[i].is_branch_target = false;
        inst_cache[i].branch_offset = 0;

        inst_cache[i].opcode = highB >> 2;
        if ((highB & 0xE0) == 0x80)              // Special opcode format for
            inst_cache[i].opcode = (highB >> 1); // type III instructions.

        if ((inst_cache[i].opcode > 0x4F) || (inst_cache[i].opcode < 0))
            return 0;

        switch (optable[inst_cache[i].opcode].addr_mode) {
            case AM_I:
                inst_cache[i].reg1 = (BYTE)((lowB & 0x1F));
                reg_usage[inst_cache[i].reg1]++;

                // jmp [reg1] doesn't use the second register
                if (inst_cache[i].opcode != V810_OP_JMP) {
                    inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                    reg_usage[inst_cache[i].reg2]++;
                } else {
                    inst_cache[i].reg2 = 0xFF;
                }
                break;
            case AM_II:
                inst_cache[i].imm = (unsigned)((lowB & 0x1F));
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[i].reg2]++;

                inst_cache[i].reg1 = 0xFF;

                if (inst_cache[i].opcode == V810_OP_SETF) {
                    drc_findLastConditionalInst(i);
                }
                break;
            case AM_III: // Branch instructions
                inst_cache[i].imm = (unsigned)(((highB & 0x1) << 8) + (lowB & 0xFE));
                inst_cache[i].branch_offset = sign_9(inst_cache[i].imm);

                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;

                if (inst_cache[i].opcode != V810_OP_BR &&
                    inst_cache[i].opcode != V810_OP_NOP)
                    drc_findLastConditionalInst(i);
                break;
            case AM_IV: // Middle distance jump
                inst_cache[i].imm = (unsigned)(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                inst_cache[i].branch_offset = (signed)sign_26(inst_cache[i].imm);

                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;

                // inlining
                static BYTE hword_getter_func[] = {
                    0x46, 0xc5, 0x00, 0x00, // ld.h [r6], r10
                    0x1f, 0x18,             // jmp  [lp]
                };
                static BYTE hword_getter_jr_func[] = {
                    0x46, 0xc5, 0x00, 0x00, // ld.h [r6], r10
                    0x00, 0xa8, 0x04, 0x00, // jr   +4
                    0x1f, 0x18,             // jmp  [lp]
                };
                if (inst_cache[i].opcode == V810_OP_JAL) {
                    if (is_hword_getter(inst_cache[i].PC + inst_cache[i].branch_offset)) {
                        inst_cache[i].opcode = V810_OP_LD_H;
                        inst_cache[i].imm = 0;
                        inst_cache[i].reg1 = 6;
                        inst_cache[i].reg2 = 10;
                    } else if (is_byte_getter(inst_cache[i].PC + inst_cache[i].branch_offset)) {
                        inst_cache[i].opcode = V810_OP_LD_B;
                        inst_cache[i].imm = 0;
                        inst_cache[i].reg1 = 6;
                        inst_cache[i].reg2 = 10;
                    }
                }
                break;
            case AM_V:
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[i].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[i].imm = (highB2 << 8) + lowB2;
                reg_usage[inst_cache[i].reg1]++;
                reg_usage[inst_cache[i].reg2]++;
                break;
            case AM_VIa: // Mode6 form1
                inst_cache[i].imm = (highB2 << 8) + lowB2;
                inst_cache[i].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[i].reg1]++;
                reg_usage[inst_cache[i].reg2]++;
                break;
            case AM_VIb: // Mode6 form2
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[i].imm = (highB2 << 8) + lowB2; // Whats the order??? 2,3,1 or 1,3,2
                inst_cache[i].reg1 = (BYTE)((lowB & 0x1F));
                reg_usage[inst_cache[i].reg1]++;
                reg_usage[inst_cache[i].reg2]++;
                break;
            case AM_VII: // Unhandled
                break;
            case AM_VIII: // Unhandled
                break;
            case AM_IX:
                inst_cache[i].imm = (unsigned)((lowB & 0x1)); // Mode ID, Ignore for now

                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;
                break;
            case AM_BSTR: // Bit String Subopcodes
                inst_cache[i].imm = (unsigned)((lowB & 0x1F));
                reg_usage[26]++;
                reg_usage[27]++;
                reg_usage[28]++;
                reg_usage[29]++;
                reg_usage[30]++;

                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;
                break;
            case AM_FPP: // Floating Point Subcode
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[i].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[i].imm = (unsigned)(((highB2 >> 2)&0x3F));
                reg_usage[inst_cache[i].reg1]++;
                reg_usage[inst_cache[i].reg2]++;
                break;
            case AM_UDEF: // Invalid opcode.
                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;
                break;
            default: // Invalid opcode.
                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;
                cur_PC += 2;
                break;
        }

        cur_PC += am_size_table[optable[inst_cache[i].opcode].addr_mode];

        while (!drc_isCode(cur_PC) && cur_PC < end_PC) {
            cur_PC += 2;
        }
    }

    // mark branch targets
    for (int j = 0; j < i; j++) {
        if (optable[inst_cache[j].opcode].addr_mode != AM_III && inst_cache[j].opcode != V810_OP_JR)
            continue;
        if (inst_cache[j].branch_offset == 0)
            continue;
        if (inst_cache[j].opcode != V810_OP_JR || abs(inst_cache[j].branch_offset) < 1024) {
            // find the branch target
            v810_instruction *target = drc_findBranchTarget(i, j);
            WORD target_PC = inst_cache[j].PC + inst_cache[j].branch_offset;
            if (target->PC != target_PC) {
                // this really should not happen anymore
                dprintf(0, "Invalid jump from %lx to %lx (found %lx between %lx and %lx)\n", inst_cache[j].PC, target_PC, target->PC, inst_cache[0].PC, inst_cache[i-1].PC);
                break;
            } else {
                // it's a valid target, so mark it as such
                target->is_branch_target = true;
            }
        }
    }

    if (i == MAX_V810_INST) {
        dprintf(0, "WARN:%lx-%lx exceeds max instrs\n", start_PC, end_PC);
    }

    return i;
}

// Translates a V810 block into ARM code
static int drc_translateBlock(void) {
    int i, j;
    int err = 0;
    // Stores the number of clock cycles since the last branch
    unsigned int cycles = 0;
    unsigned int num_v810_inst, num_arm_inst;
    // Maps V810 registers to ARM registers
    BYTE phys_regs[32];
    // For each V810 instruction, the ARM registers mapped to reg1 and reg2. If
    // they're not cached, they will be mapped to r2 and r3.
    BYTE arm_reg1, arm_reg2;
    BYTE arm_cond;
    WORD start_PC = vb_state->v810_state.PC;
    WORD end_PC;
    // For each V810 instruction, tells if either reg1 or reg2 is cached
    bool unmapped_registers;
    BYTE next_available_reg;
    // Tells if reg1 or reg2 has been modified by the current V810 instruction
    bool reg1_modified;
    bool reg2_modified;
    // The value of inst_ptr at the start of a V810 instruction
    ir_inst* inst_ptr_start;

    // Games with specific hacks; additional explanation follows where each check is used.
    bool is_waterworld = CHECK_GAMEID("67VWEE");
    bool is_virtual_lab = CHECK_GAMEID("AHVJVJ");
    bool is_golf_us = CHECK_GAMEID("01VVGE");
    bool is_golf_jp = CHECK_GAMEID("E4VVGJ");
    bool is_baseball_2 = CHECK_GAMEID("7FVVQE") && V810_ROM1.size >= 0x100000; // size check for memory safety
    bool is_space_invaders = CHECK_GAMEID("C0VSPJ");
    bool is_jack_bros = CHECK_GAMEID("EBVJBE") || CHECK_GAMEID("EBVJBJ");
    bool chcw_load_seen = (vb_state->v810_state.S_REG[CHCW] & 2) != 0;
    bool is_marios_tennis_multiplayer = CHECK_GAMEID("01VMTJ") &&
        memcmp((u8*)V810_ROM1.pmemory + (0x1FFDB0 & V810_ROM1.highaddr), "MULTIPLAYER HACK V0.1 BY MARTIN KUJACZYNSKI ", 44) == 0;

    // Virtual Bowling and Niko-Chan Battle need their interrupts to run a little slower
    // in order for the samples to play at the right speed.
    bool is_virtual_bowling = CHECK_GAMEID("E7VVBJ");
    bool is_niko_chan = CHECK_GAMEID("8BVTRJ");
    bool slow_memory = is_virtual_bowling || is_niko_chan ||
        // If memory is too fast, Blox 2's intro jingle doesn't finish.
        CHECK_GAMEID("CRVB2M") ||
        // Artificially slow down Test Chamber to keep it within the o3DS frame budget.
        CHECK_GAMEID("PRCHMB");

    // Emulating memory clocks introduces lag to Galactic Pinball's UFO table.
    bool is_pinball = CHECK_GAMEID("01VGPJ");

    bool is_waterworld_sample = is_waterworld && (start_PC == 0x0701b2b2);

    exec_block *block = NULL;

#ifdef LITERAL_POOL
    WORD* pool_cache_start = NULL;
    pool_cache_start = linearAlloc(256*4);
#endif
    WORD pool_offset = 0;

    drc_scanBlockBounds(&start_PC, &end_PC);
    dprintf(3, "[DRC]: new block - 0x%lx->0x%lx\n", start_PC, end_PC);

    // Clear previous block register stats
    memset(reg_usage, 0, 32);

    block = drc_getNextBlockStruct();
    if (block == NULL)
        return DRC_ERR_NO_BLOCKS;
    block->free = false;

    block->start_pc = start_PC;
    block->pc_range = end_PC - start_PC;

    // First pass: decode V810 instructions
    num_v810_inst = drc_decodeInstructions(block, start_PC, end_PC);
    dprintf(3, "[DRC]: V810 block size - %d\n", num_v810_inst);

    // Waterworld-excluive pass: find busywaits
    if (is_waterworld)
        drc_findWaterworldBusywait(num_v810_inst);

    inst_ptr = &trans_cache[0];

    // Second pass: map the most used V810 registers to ARM registers
    drc_prepare(block);
#ifdef LITERAL_POOL
    pool_ptr = pool_cache_start;
#endif

    // Third pass: generate ARM instructions
    for (i = 0; i < num_v810_inst; i++) {

        // As of this writing, the longest replacement sequence is bitstring at 43 instructions.
        // However, let's keep some buffer, just in case.
        if (inst_ptr - trans_cache >= MAX_ARM_INST - 64) break;

        inst_cache[i].start_pos = (HWORD) (inst_ptr - trans_cache + pool_offset);
        inst_ptr_start = inst_ptr;
        cycles += opcycle[inst_cache[i].opcode];


        // Golf hack: this function clears the screen, so we should do the same
        if (unlikely((is_golf_us && inst_cache[i].PC == 0x0700ca64) ||
                    (is_golf_jp && inst_cache[i].PC == 0x0701602a))) {
            drc_bake_golf_hack();
        }

        // In Virtual League Baseball 2's overhead view, the draw order of the
        // fielders is sorted very inefficiently: each fielder is a 32-byte
        // object, and every swap in the sort swaps the entire set of 32 bytes.
        // Replace with code that sorts the same array much more efficiently.
        if (unlikely(is_baseball_2 && inst_cache[i].PC == 0x07007428)) {
            drc_bake_ballsort();
            // skip to after sorting code
            inst_cache[i].branch_offset = 0x070074b8 - 0x07007428;
            drc_bake_jump_short(&inst_cache[i]);
        }

        // Waterworld hack: slow down the sample at the start.
        // This roughly emulates register hazards to a certain extent,
        // with some tweaks to bring it as close as possible to a hardware recording.
        // Emulating hazards for every game slows down games like Red Alarm too much.
        if (is_waterworld_sample) {
            if (inst_cache[i].PC == 0x0701b2b4) cycles -= 1;
            if (opcycle[inst_cache[i].opcode] == 1 && (inst_cache[i].PC & 6) == 0) {
                if (i > 0 && inst_cache[i-1].reg2 != 0xFF && (inst_cache[i].reg1 == inst_cache[i-1].reg2)) {
                    cycles++;
                }
            }
        }

        // save PC, for debugging purposes
        // LDW_I(0, inst_cache[i].PC);
        // STR_IO(0, 11, offsetof(cpu_state, PC));

        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                drc_bake_add_cycles(&cycles);
                drc_bake_jmp(&inst_cache[i]);
                break;
            case V810_OP_JR: // jr imm26
                if (abs(inst_cache[i].branch_offset) < 1024) {
                    if (inst_cache[i].busywait) {
                        drc_bake_halt(inst_cache[i].PC + inst_cache[i].branch_offset, &cycles);
                    } else {
                        if (inst_cache[i].branch_offset <= 0) {
                            drc_bake_handle_interrupts(inst_cache[i].PC + inst_cache[i].branch_offset, &cycles);
                        } else {
                            drc_bake_add_cycles(&cycles);
                        }
                        drc_bake_jump_short(&inst_cache[i]);
                    }
                } else {
                    drc_bake_add_cycles(&cycles);
                    drc_bake_jump_long(&inst_cache[i]);
                }
                break;
            case V810_OP_JAL: // jal disp26
            {
                if (is_space_invaders && inst_cache[i].PC == 0x07007fb6) {
                    // Make sure the Space Invaders intro FMV runs at the correct speed (ish).
                    // Value determined through trial and error.
                    // Correct for intro video, but attract video is very slightly slow.
                    cycles += 24;
                }
                drc_bake_add_cycles(&cycles);

                bool ballscale = is_baseball_2 && inst_cache[i].PC + inst_cache[i].branch_offset == 0x070077ca;

                if (unlikely(ballscale)) {
                    // In the overhead view in Virtual League Baseball 2,
                    // the fielders are scaled in software.
                    // This algorithm is slow when recompiled, so we override it
                    // with a faster native implementation.
                    drc_bake_ballscale_start();
                }

                drc_bake_link_reg(&inst_cache[i]);
                drc_bake_jump_long(&inst_cache[i]);
                // fix the skip if needed
                if (unlikely(ballscale)) drc_bake_ballscale_end();
                break;
            }
            case V810_OP_RETI:
                drc_bake_add_cycles(&cycles);
                drc_bake_reti();
                break;
            case V810_OP_BR:
                if (inst_cache[i].branch_offset == 0) {
                    drc_bake_halt(inst_cache[i].PC, &cycles);
                    break;
                }
                // vertical force doesn't have a tight spinloop like most games, so we can't detect it
                // it just continuously loops through entities, doing nothing more when each one done
                // so let's just artificially skip a bunch of time so that the game isn't slow
                if ((CHECK_GAMEID("01VH3E") || CHECK_GAMEID("18VH3J"))
                    && inst_cache[i].PC == 0x07000c08
                ) {
                    drc_bake_vertical_force_hack();
                    drc_bake_handle_interrupts(inst_cache[i].PC + inst_cache[i].branch_offset, &cycles);
                    drc_bake_jump_short(&inst_cache[i]);
                    break;
                }
            case V810_OP_BV:
            case V810_OP_BL:
            case V810_OP_BE:
            case V810_OP_BN:
            case V810_OP_BLT:
            case V810_OP_BLE:
            case V810_OP_BNV:
            case V810_OP_BNL:
            case V810_OP_BNE:
            case V810_OP_BP:
            case V810_OP_BGE:
            case V810_OP_BGT:
            case V810_OP_BNH:
            case V810_OP_BH:
                if (inst_cache[i].busywait) {
                    drc_bake_busywait(&inst_cache[i], &cycles);
                } else {
                    // If we just got back from a JAL, an interrupt check already happened, so don't bother.
                    if (inst_cache[i].branch_offset <= 0 && (inst_cache[i].is_branch_target || (i > 0 && inst_cache[i-1].opcode != V810_OP_JAL))) {
                        // The Jack Bros. intro chime is the only part of any game where
                        // the instruction cache is turned off and performance is meaningful:
                        // the delay in the chime consists of "add; bne" loops.
                        // According to PizzaRollsRoyce's Slow VB:
                        // https://www.platonicreactor.com/projects/slow-vb/
                        // Regardless of alignment, an "add; bne" loop with cache disabled
                        // will take 24 cycles per iteration.
                        // We've already added 4, so 20 remain.
                        // The result seems to roughly line up in audio recordings,
                        // though it's not exact.
                        // Adding 20 cycles to every branch before the cache is turned on
                        // will not affect any runtime code other than the spinloop.
                        if (is_jack_bros && !chcw_load_seen) {
                            cycles += 20;
                        }
                        drc_bake_handle_interrupts(inst_cache[i].PC, &cycles);
                    } else {
                        drc_bake_add_cycles(&cycles);
                    }
                    drc_bake_branch(&inst_cache[i]);
                }
                // branch not taken, so it only took 1 cycle
                drc_bake_subtract_cycles_runtime(2);
                break;
            case V810_OP_MOVHI: // movhi imm16, reg1, reg2:
                drc_bake_movhi(&inst_cache[i]);
                break;
            case V810_OP_MOVEA: // movea imm16, reg1, reg2
                drc_bake_movea(&inst_cache[i]);
                break;
            case V810_OP_MOV: // mov reg1, reg2
                drc_bake_mov(&inst_cache[i]);
                break;
            case V810_OP_ADD: // add reg1, reg2
                drc_bake_add(&inst_cache[i]);
                break;
            case V810_OP_SUB: // sub reg1, reg2
                drc_bake_sub(&inst_cache[i]);
                break;
            case V810_OP_CMP: // cmp reg1, reg2
                drc_bake_cmp(&inst_cache[i]);
                break;
            case V810_OP_SHL: // shl reg1, reg2
                drc_bake_shl(&inst_cache[i]);
                break;
            case V810_OP_SHR: // shr reg1, reg2
                drc_bake_shr(&inst_cache[i]);
                break;
            case V810_OP_SAR: // sar reg1, reg2
                drc_bake_sar(&inst_cache[i]);
                break;
            case V810_OP_MUL: // mul reg1, reg2
                drc_bake_mul(&inst_cache[i]);
                break;
            case V810_OP_MULU: // mul reg1, reg2
                drc_bake_mulu(&inst_cache[i]);
                break;
            case V810_OP_DIV: // div reg1, reg2
                drc_bake_div(&inst_cache[i]);
                break;
            case V810_OP_DIVU: // divu reg1, reg2
                drc_bake_divu(&inst_cache[i]);
                break;
            case V810_OP_OR: // or reg1, reg2
                drc_bake_or(&inst_cache[i]);
                break;
            case V810_OP_AND: // and reg1, reg2
                drc_bake_and(&inst_cache[i]);
                break;
            case V810_OP_XOR: // xor reg1, reg2
                drc_bake_xor(&inst_cache[i]);
                break;
            case V810_OP_NOT: // not reg1, reg2
                drc_bake_not(&inst_cache[i]);
                break;
            case V810_OP_MOV_I: // mov imm5, reg2
                drc_bake_mov_i(&inst_cache[i]);
                break;
            case V810_OP_ADD_I: // add imm5, reg2
                drc_bake_add_i(&inst_cache[i]);
                break;
            case V810_OP_CMP_I: // cmp imm5, reg2
                drc_bake_cmp_i(&inst_cache[i]);
                break;
            case V810_OP_SHL_I: // shl imm5, reg2
                drc_bake_shl_i(&inst_cache[i]);
                break;
            case V810_OP_SHR_I: // shr imm5, reg2
                drc_bake_shr_i(&inst_cache[i]);
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                drc_bake_sar_i(&inst_cache[i]);
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                drc_bake_andi(&inst_cache[i]);
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                drc_bake_xori(&inst_cache[i]);
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                drc_bake_ori(&inst_cache[i]);
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
                drc_bake_addi(&inst_cache[i]);
                break;
            case V810_OP_LD_B: // ld.b disp16 [reg1], reg2
            case V810_OP_IN_B: // in.b disp16 [reg1], reg2
                drc_bake_ld_b(&inst_cache[i], !is_pinball);

                if (slow_memory) cycles += 2;

                if (i > 0 && (inst_cache[i - 1].opcode & 0x34) == 0x30 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // load immediately following another load takes 2 cycles instead of 3
                    cycles -= 1;
                } else if (i > 0 && opcycle[inst_cache[i - 1].opcode] > 4) {
                    // load following instruction taking "many" cycles only takes 1 cycles
                    // guessing "many" is 4 for now
                    cycles -= 2;
                }
                break;
            case V810_OP_LD_H: // ld.h disp16 [reg1], reg2
            case V810_OP_IN_H: // in.h disp16 [reg1], reg2
                drc_bake_ld_h(&inst_cache[i], !is_pinball);

                if (slow_memory) cycles += 2;

                if (i > 0 && (inst_cache[i - 1].opcode & 0x34) == 0x30 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // load immediately following another load takes 2 cycles instead of 3
                    cycles -= 1;
                } else if (i > 0 && opcycle[inst_cache[i - 1].opcode] > 4) {
                    // load following instruction taking "many" cycles only takes 1 cycles
                    // guessing "many" is 4 for now
                    cycles -= 2;
                }
                break;
            case V810_OP_LD_W: // ld.w disp16 [reg1], reg2
            case V810_OP_IN_W: // in.w disp16 [reg1], reg2
                drc_bake_ld_w(&inst_cache[i], !is_pinball);

                if (slow_memory) cycles += 4;

                if (i > 0 && (inst_cache[i - 1].opcode & 0x34) == 0x30 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // load immediately following another load takes 4 cycles instead of 5
                    cycles -= 1;
                } else if (i > 0 && opcycle[inst_cache[i - 1].opcode] > 4) {
                    // load following instruction taking "many" cycles only takes 1 cycles
                    // guessing "many" is 4 for now
                    cycles -= 4;
                }
                break;
            case V810_OP_ST_B:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_B: // out.h reg2, disp16 [reg1]
                drc_bake_st_b(&inst_cache[i], !is_pinball);

                if (slow_memory) cycles += 2;

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 2 cycles instead of 1
                    cycles += 1;
                }
                break;
            case V810_OP_ST_H:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_H: // out.h reg2, disp16 [reg1]
                drc_bake_st_h(&inst_cache[i], !is_pinball);

                if (slow_memory) cycles += 2;

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 2 cycles instead of 1
                    cycles += 1;
                }

                break;
            case V810_OP_ST_W:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_W: // out.h reg2, disp16 [reg1]
                drc_bake_st_w(&inst_cache[i], !is_pinball);


                if (slow_memory) cycles += 4;

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 4 cycles instead of 1
                    cycles += 3;
                }

                // if we load the same thing immediately after saving it, skip the loading
                if (i + 1 < num_v810_inst &&
                    (inst_cache[i + 1].opcode == V810_OP_LD_W || inst_cache[i + 1].opcode == V810_OP_IN_W) &&
                    inst_cache[i + 1].imm == inst_cache[i].imm && inst_cache[i + 1].reg1 == inst_cache[i].reg1 &&
                    inst_cache[i + 1].reg2 == inst_cache[i].reg2
                ) {
                    cycles += 5;
                    inst_cache[i].branch_offset = 8;
                    drc_bake_jump_short(&inst_cache[i]);
                }
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                if (inst_cache[i].imm == CHCW) chcw_load_seen = true;
                drc_bake_ldsr(&inst_cache[i]);
                break;
            case V810_OP_STSR: // stsr regID, reg2
                drc_bake_stsr(&inst_cache[i]);
                break;
            case V810_OP_SEI: // sei
                drc_bake_sei(&inst_cache[i]);
                break;
            case V810_OP_CLI: // cli
                drc_bake_cli(&inst_cache[i]);
                break;
            case V810_OP_SETF: // setf imm5, reg2
                drc_bake_setf(&inst_cache[i]);
                break;
            case V810_OP_HALT: // halt
                drc_bake_halt(inst_cache[i].PC, &cycles);
                break;
            case V810_OP_BSTR:
                drc_bake_bstr(&inst_cache[i], &cycles);
                break;
            case V810_OP_FPP:
                switch (inst_cache[i].imm) {
                    case V810_OP_DIVF_S:
                        cycles += 44;
                        break;
                    case V810_OP_XB:
                        cycles += 6;
                        break;
                    case V810_OP_XH:
                        cycles += 1;
                        break;
                    case V810_OP_REV:
                        cycles += 22;
                        break;
                    case V810_OP_MPYHW:
                        cycles += 9;
                        break;
                }
                drc_bake_fpp(&inst_cache[i]);
                break;
            case V810_OP_NOP:
                drc_bake_nop();
                break;
            case END_BLOCK:
                drc_bake_end_block();
                break;
            default:
                dprintf(0, "[DRC]: %s (0x%x) not implemented\n", optable[inst_cache[i].opcode].opname, inst_cache[i].opcode);
                // Fill unimplemented instructions with a nop and hope the game still runs
                drc_bake_nop();
                break;
        }

        if (i + 1 < num_v810_inst) {
            if (is_virtual_lab && inst_cache[i + 1].PC == 0x07002446) {
                // virtual lab hack
                // interrupts don't save registers, and clearing levels relies on
                // registers getting dirty
                drc_bake_halt(0x07002446, &cycles);
            } else if (inst_cache[i+1].opcode == V810_OP_ST_B
                    && inst_cache[i+1].imm == 0x20
                    && inst_cache[i].opcode == V810_OP_MOVEA
                    && inst_cache[i].reg1 == 0
                    && inst_cache[i].reg2 == inst_cache[i+1].reg2
                    && inst_cache[i].imm == 0x1d) {
                // Hack for Virtual Bowling and Niko-Chan Battle:
                // These games acknowledge the timer in a way that only works
                // if the timer is not zero at this point.
                // Therefore, we need to handle the interrupt to update it,
                // so that it doesn't accidentally run an extra time.
                drc_bake_bowling_nikochan_hack(&inst_cache[i], cycles);
                cycles = 0;
            } else if (is_marios_tennis_multiplayer && inst_cache[i + 1].PC == 0x07010442) {
                // Mario's Tennis multiplayer hack:
                // Some setup code flips CC-Wr off, flips it on, then loops if CC-Rd is on.
                // Getting out of this loop requires the two systems to be desynced:
                // one system has to check CC-Rd while the other has CC-Wr off.
                // To allow them to desync, we place an interrupt check between the writes.
                drc_bake_handle_interrupts(inst_cache[i + 1].PC, &cycles);
            } else if (cycles >= 200) {
                drc_bake_handle_interrupts(inst_cache[i + 1].PC, &cycles);
            } else if (cycles != 0 && (inst_cache[i + 1].is_branch_target || inst_cache[i + 1].opcode == V810_OP_BSTR)) {
                // branch target or bitstring instruction coming up
                drc_bake_add_cycles(&cycles);
            } else if (inst_cache[i + 1].PC > (0xfffffe00 & V810_ROM1.highaddr) && !(inst_cache[i + 1].PC & 0xf)) {
                // potential interrupt handler coming up
                drc_bake_add_cycles(&cycles);
            }
        }

        inst_cache[i].trans_size = (BYTE) (inst_ptr - inst_ptr_start);

#ifdef LITERAL_POOL
        if ((inst_ptr - trans_cache) >= (1<<10) || (i == num_v810_inst-1 && (pool_ptr != pool_start))) {
            int pool_size = (int)(pool_ptr-pool_cache_start);
            // FIXME: Implement branch instructions
            ADD_I(15, 15, (BYTE)(pool_size-2), 30);

            num_arm_inst = (unsigned int)(inst_ptr - trans_cache);
            pool_start = &block->phys_loc[num_arm_inst];
            memcpy(pool_start, pool_cache_start, pool_size*sizeof(WORD));

            pool_ptr = pool_cache_start;
            pool_offset += pool_size;
        }
#endif
    }

    num_arm_inst = (unsigned int)(inst_ptr - trans_cache);

    // Fourth pass: align to new memory block
    translated_inst *cache_ptr = (translated_inst*)drc_alloc(num_arm_inst * sizeof(translated_inst) / sizeof(drc_unit));
    if (cache_ptr == NULL) {
        err = DRC_ERR_CACHE_FULL;
        goto cleanup;
    }
    block->phys_offset = (drc_unit*)cache_ptr;
    for (i = 0; i < num_v810_inst; i++) {
        drc_setEntry(inst_cache[i].PC, (drc_unit*)(cache_ptr + inst_cache[i].start_pos), block);
    }

    // Fifth pass: assemble and link
    for (i = 0; i < num_v810_inst; i++) {
        HWORD start_pos = inst_cache[i].start_pos;
        for (j = start_pos; j < (start_pos + inst_cache[i].trans_size); j++) {
#ifdef LITERAL_POOL
            if (trans_cache[j].needs_pool) {
                // The literal pool is located at the end of the current block
                // Figure out the offset from the pc register, which points two
                // instructions ahead of the current one
                trans_cache[j].ldst_io.imm = (HWORD) ((trans_cache[j].pool_start + trans_cache[j].pool_pos) - (&block->phys_loc[j + 2]));
            }
#endif
            drc_assemble(cache_ptr + j, &trans_cache[j], &inst_cache[i]);
        }
    }

    block->size = num_arm_inst * sizeof(translated_inst) / sizeof(drc_unit) + pool_offset;

#if ARM_DRC
    FlushInvalidateCache(block->phys_offset, block->size * sizeof(drc_unit));
#endif

cleanup:
#ifdef LITERAL_POOL
    linearFree(pool_cache_start);
#endif
    return err;
}

// Clear and invalidate the dynarec cache
void drc_clearCache(void) {
    dprintf(0, "[DRC]: clearing cache...\n");
    cache_pos = cache_start + 1;
    block_pos = 1;
    free_block_count = 0;

    memset(cache_start, 0, CACHE_SIZE);
    memset(rom_block_map, 0, sizeof(rom_block_map[0])*BLOCK_MAP_COUNT);
    memset(rom_entry_map, 0, sizeof(rom_entry_map[0])*BLOCK_MAP_COUNT);

    *cache_start = -1;
}

// Returns the entrypoint for the V810 instruction in location loc if it exists
// and NULL if it needs to be translated. If p_block != NULL it will point to
// the block structure.
drc_unit* drc_getEntry(WORD loc, exec_block **p_block) {
    unsigned int map_pos;
    exec_block *block;

    map_pos = ((loc&V810_ROM1.highaddr)>>1)&(BLOCK_MAP_COUNT-1);
    block = block_ptr_start + rom_block_map[map_pos];
    if (block == block_ptr_start || block->free) return cache_start;
    if (p_block)
        *p_block = block;
    return block->phys_offset + rom_entry_map[map_pos];
}

// Sets a new entrypoint for the V810 instruction in location loc and the
// corresponding block
void drc_setEntry(WORD loc, drc_unit *entry, exec_block *block) {
    unsigned int map_pos = ((loc&V810_ROM1.highaddr)>>1)&(BLOCK_MAP_COUNT-1);
    rom_block_map[map_pos] = block - block_ptr_start;
    rom_entry_map[map_pos] = entry - block->phys_offset;
}

// Initialize the dynarec
void drc_init(void) {
    // V810 instructions are 16-bit aligned, so we can ignore the last bit of the PC
    rom_block_map = calloc(sizeof(rom_block_map[0]), BLOCK_MAP_COUNT);
    rom_entry_map = linearAlloc(sizeof(rom_entry_map[0]) * BLOCK_MAP_COUNT);
    rom_data_code_map = calloc(sizeof(rom_data_code_map[0]), BLOCK_MAP_COUNT >> 3);
    block_ptr_start = linearAlloc(MAX_NUM_BLOCKS*sizeof(exec_block));

    inst_cache = linearAlloc(MAX_V810_INST*sizeof(v810_instruction));
    trans_cache = linearAlloc(MAX_ARM_INST*sizeof(ir_inst));

    cache_start = linearMemAlign(CACHE_SIZE, 0x1000);
#ifdef __3DS__
    hbHaxInit();
    detectCitra(cache_start);
#endif
#if ARM_DRC
    ReprotectMemory(cache_start, CACHE_SIZE/0x1000, 0x7);
#endif

    *cache_start = -1;
    cache_pos = cache_start + 1;
    dprintf(0, "[DRC]: cache_start = %p\n", cache_start);
}

void drc_reset(void) {
    memset(rom_data_code_map, 0, sizeof(rom_data_code_map[0])*(BLOCK_MAP_COUNT >> 3));
    memset(baseball2_sprites_is_unpacked, 0, sizeof(baseball2_sprites_is_unpacked));
    drc_clearCache();
}

// Cleanup and exit
void drc_exit(void) {
    linearFree(cache_start);
    free(rom_block_map);
    linearFree(rom_entry_map);
    free(rom_data_code_map);
    linearFree(block_ptr_start);
    linearFree(trans_cache);
    linearFree(inst_cache);
#ifdef __3DS__
    hbHaxExit();
#endif
}

exec_block* drc_getNextBlockStruct(void) {
    if (block_pos >= MAX_NUM_BLOCKS) {
        for (int i = 0; i < MAX_NUM_BLOCKS; i++) {
            if (block_ptr_start[i].free) {
                return &block_ptr_start[i];
            }
        }
        return NULL;
    }
    return &block_ptr_start[block_pos++];
}

// Run V810 code until the next frame interrupt
int drc_run(void) {
    exec_block* cur_block = NULL;
    drc_unit* entrypoint;
    WORD entry_PC;

    vb_state->v810_state.PC &= V810_ROM1.highaddr;

    drc_flags_to_native();

    serviceInt(vb_state->v810_state.cycles, vb_state->v810_state.PC);

    while (true) {
        // extra interrupt check in case we're jumping functions without looping
        if (unlikely(vb_state->v810_state.cycles_until_event_partial <= 0)) {
            serviceInt(vb_state->v810_state.cycles, vb_state->v810_state.PC);
            if (unlikely(vb_state->v810_state.ret)) break;
        }

        entry_PC = vb_state->v810_state.PC;

        // Try to find a cached block
        entrypoint = drc_getEntry(vb_state->v810_state.PC, &cur_block);
        // entry_PC < cur_block->start_pc || entry_PC > cur_block->end_pc
        if (unlikely(entrypoint == cache_start || entry_PC - cur_block->start_pc > cur_block->pc_range)) {
            int result = drc_translateBlock();
            if (unlikely(result == DRC_ERR_CACHE_FULL || result == DRC_ERR_NO_BLOCKS)) {
                drc_clearCache();
                continue;
            } else if (unlikely(result)) {
                return result;
            }

//            drc_dumpCache("cache_dump_rf.bin");

            entrypoint = drc_getEntry(entry_PC, &cur_block);
            dprintf(3, "[DRC]: ARM block size - %ld\n", cur_block->size);
        }
        dprintf(3, "[DRC]: entry - 0x%lx (0x%x)\n", entry_PC, (int)(entrypoint - cache_start)*4);
        // entrypoint <= cache_start || entrypoint >= cache_start + CACHE_SIZE
        if (unlikely(entrypoint - (cache_start + 1) >= CACHE_SIZE - 1)) {
            dprintf(0, "Bad entry %p\n", drc_getEntry(entry_PC, NULL));
            return DRC_ERR_BAD_ENTRY;
        }

        drc_executeBlock(entrypoint, cur_block);

        vb_state->v810_state.PC &= V810_ROM1.highaddr;

        dprintf(4, "[DRC]: end - 0x%lx\n", vb_state->v810_state.PC);
        if (unlikely(vb_state->v810_state.PC - V810_ROM1.lowaddr >= V810_ROM1.size)) {
            dprintf(0, "Last entry: 0x%lx\n", entry_PC);
            //return DRC_ERR_BAD_PC;
            break;
        }

        if (unlikely(vb_state->v810_state.ret)) {
            break;
        }
    }

    drc_flags_to_v810();

    return 0;
}

void drc_loadSavedCache(void) {
    FILE* f;
    f = fopen("rom_block_map", "r");
    assert(fread(rom_block_map, sizeof(rom_block_map[0]), BLOCK_MAP_COUNT, f) == sizeof(rom_block_map[0]) * BLOCK_MAP_COUNT);
    fclose(f);
    f = fopen("rom_entry_map", "r");
    assert(fread(rom_entry_map, sizeof(rom_entry_map[0]), BLOCK_MAP_COUNT, f) == sizeof(rom_entry_map[0]) * BLOCK_MAP_COUNT);
    fclose(f);
    f = fopen("block_heap", "r");
    assert(fread(block_ptr_start, sizeof(exec_block), MAX_NUM_BLOCKS, f) == sizeof(exec_block) * MAX_NUM_BLOCKS);
    fclose(f);
}

// Dumps the translation cache onto a file
void drc_dumpCache(char* filename) {
    FILE* f = fopen(filename, "w");
    fwrite(cache_start, CACHE_SIZE, 1, f);
    fclose(f);

    f = fopen("rom_block_map", "w");
    fwrite(rom_block_map, sizeof(rom_block_map[0]), BLOCK_MAP_COUNT, f);
    fclose(f);
    f = fopen("rom_entry_map", "w");
    fwrite(rom_entry_map, sizeof(rom_entry_map[0]), BLOCK_MAP_COUNT, f);
    fclose(f);
    f = fopen("block_heap", "w");
    fwrite(block_ptr_start, sizeof(exec_block), MAX_NUM_BLOCKS, f);
    fclose(f);
}

void drc_dumpDebugInfo(int code) {
    int i;
    FILE* f = fopen("debug_info.txt", "w");

    fprintf(f, "Error code: %d\n", code);
    fprintf(f, "PC: 0x%08" PRIx32 "\n", vb_state->v810_state.PC);
    for (i = 0; i < 32; i++)
        fprintf(f, "r%d: 0x%08" PRIx32 "\n", i, vb_state->v810_state.P_REG[i]);

    for (i = 0; i < 32; i++)
        fprintf(f, "s%d: 0x%08" PRIx32 "\n", i, vb_state->v810_state.S_REG[i]);

    fprintf(f, "Cycles: %" PRIu32 "\n", vb_state->v810_state.cycles);
    fprintf(f, "Cache start: %p\n", cache_start);
    fprintf(f, "Cache pos: %p\n", cache_pos);

    fprintf(f, "VIP overclock: %d\n", tVBOpt.VIP_OVERCLOCK);

    replay_save("debug_replay.bin.gz");

    fclose(f);
}
