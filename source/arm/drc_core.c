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

#ifdef __3DS__
#include <3ds.h>
#include <citro3d.h>
#endif

#include "utils.h"
#include "drc_alloc.h"
#include "drc_core.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_set.h"
#include "vb_types.h"

#include "arm_emit.h"
#include "arm_codegen.h"

#include "replay.h"

#include "vb_dsp.h"

HWORD* rom_block_map;
HWORD* rom_entry_map;
BYTE* rom_data_code_map;
BYTE reg_usage[32];
WORD* cache_start;
WORD* cache_pos;
exec_block* block_ptr_start;
int block_pos = 1;

static v810_instruction *inst_cache;
static arm_inst *trans_cache;
arm_inst *inst_ptr;

static WORD* drc_getEntry(WORD loc, exec_block **p_block);

// Maps the most used registers in the block to V810 registers
static void drc_mapRegs(exec_block* block) {
    int i, j, max, max_pos;

    block->reg_map = 0;

    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        max = max_pos = 0;
        // We don't care about P_REG[0] because it will always be 0 and it will
        // be optimized out
        for (j = 1; j < 32; j++) {
            if (reg_usage[j] > max) {
                max_pos = j;
                max = reg_usage[j];
            }
        }
        block->reg_map |= max_pos << (5 * i);
        if (max)
            reg_usage[(block->reg_map >> (5 * i)) & 0x1f] = 0;
        else
            // Use P_REG[0] as a placeholder if the register isn't
            // used in the block
            block->reg_map &= ~(0x1f << (5 * i));
    }
}

// Gets the ARM register corresponding to a cached V810 register
static BYTE drc_getPhysReg(BYTE vb_reg, WORD reg_map) {
    int i;
    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        if (((reg_map >> (i * 5)) & 0x1f) == vb_reg) {
            // The first usable register will be r4
            return (BYTE) (i + ARM_CACHE_REG_START);
        }
    }
    return 0;
}

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
#ifdef __3DS__
    C3D_FrameBegin(0);
    for (int i = 0; i < 2; i++) {
        C3D_RenderTargetClear(screenTargetHard[i], C3D_CLEAR_COLOR, 0, 0);
    }
    C3D_FrameEnd(0);
#endif
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
                if (memcmp(tVBOpt.GAME_ID, "01VNFE", 6) == 0 && (
                    inst_cache[i].PC + inst_cache[i].branch_offset == 0x07005326 ||
                    inst_cache[i].PC + inst_cache[i].branch_offset == 0x07001f2c
                )) break;
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
        block->cycles += opcycle[inst_cache[i].opcode];

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
    arm_inst* inst_ptr_start;
    
    // Games with specific hacks; additional explanation follows where each check is used.
    bool is_waterworld = memcmp(tVBOpt.GAME_ID, "67VWEE", 6) == 0;
    bool is_virtual_lab = memcmp(tVBOpt.GAME_ID, "AHVJVJ", 6) == 0;
    bool is_golf_us = memcmp(tVBOpt.GAME_ID, "01VVGE", 6) == 0;
    bool is_golf_jp = memcmp(tVBOpt.GAME_ID, "E4VVGJ", 6) == 0;
    bool is_baseball_2 = memcmp(tVBOpt.GAME_ID, "7FVVQE", 6) == 0 && V810_ROM1.size >= 0x100000; // size check for memory safety
    bool is_space_invaders = memcmp(tVBOpt.GAME_ID, "C0VSPJ", 6) == 0;
    bool is_jack_bros = memcmp(tVBOpt.GAME_ID, "EBVJBE", 6) == 0 || memcmp(tVBOpt.GAME_ID, "EBVJBJ", 6) == 0;
    bool chcw_load_seen = (vb_state->v810_state.S_REG[CHCW] & 2) != 0;
    bool is_marios_tennis_multiplayer = memcmp(tVBOpt.GAME_ID, "01VMTJ", 6) == 0 &&
        memcmp((u8*)V810_ROM1.pmemory + (0x1FFDB0 & V810_ROM1.highaddr), "MULTIPLAYER HACK V0.1 BY MARTIN KUJACZYNSKI ", 44) == 0;

    // Virtual Bowling and Niko-Chan Battle need their interrupts to run a little slower
    // in order for the samples to play at the right speed.
    bool is_virtual_bowling = memcmp(tVBOpt.GAME_ID, "E7VVBJ", 6) == 0;
    bool is_niko_chan = memcmp(tVBOpt.GAME_ID, "8BVTRJ", 6) == 0;
    bool slow_memory = is_virtual_bowling || is_niko_chan ||
        // If memory is too fast, Blox 2's intro jingle doesn't finish.
        memcmp(tVBOpt.GAME_ID, "CRVB2M", 6) == 0;

    // Emulating memory clocks introduces lag to Galactic Pinball's UFO table.
    bool is_pinball = memcmp(tVBOpt.GAME_ID, "01VGPJ", 6) == 0;

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

    // Second pass: map the most used V810 registers to ARM registers
    drc_mapRegs(block);
    phys_regs[0] = 0;
    for (i = 1; i < 32; i++)
        phys_regs[i] = drc_getPhysReg(i, block->reg_map);

    inst_ptr = &trans_cache[0];
#ifdef LITERAL_POOL
    pool_ptr = pool_cache_start;
#endif

    #define LOAD_REG1() \
        if (arm_reg1 < 4) { \
            if (inst_cache[i].reg1) \
                LDR_IO(arm_reg1, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg1])); \
            else \
                MOV_I(arm_reg1, 0, 0); \
        }

    #define RELOAD_REG1(r) \
        if (arm_reg1 < 4) { \
            if (inst_cache[i].reg1) \
                LDR_IO(r, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg1])); \
            else \
                MOV_I(r, 0, 0); \
        } else if (r != arm_reg1) { \
            MOV(r, arm_reg1); \
        }

    #define LOAD_REG2() \
        if (arm_reg2 < 4) { \
            if (inst_cache[i].reg2) \
                LDR_IO(arm_reg2, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg2])); \
            else \
                MOV_I(arm_reg2, 0, 0); \
        }

    #define RELOAD_REG2(r) \
        if (arm_reg2 < 4) { \
            if (inst_cache[i].reg2) \
                LDR_IO(r, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg2])); \
            else \
                MOV_I(r, 0, 0); \
        } else if(r != arm_reg2) { \
            MOV(r, arm_reg2); \
        }
    
    #define LOAD_REG(arm,vb) \
        if (!phys_regs[vb]) LDR_IO(arm, 11, offsetof(cpu_state, P_REG[vb])); \
        else MOV(arm, phys_regs[vb]);

    #define SAVE_REG2(r) \
        if (arm_reg2 < 4) STR_IO(r, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg2])); \
        else if(arm_reg2 != r) MOV(arm_reg2, r);

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
            LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
            LDR_IO(2, 2, DRC_RELOC_GOLFHACK*4);
            BLX(ARM_COND_AL, 2);
        }

        // In Virtual League Baseball 2's overhead view, the draw order of the
        // fielders is sorted very inefficiently: each fielder is a 32-byte
        // object, and every swap in the sort swaps the entire set of 32 bytes.
        // Replace with code that sorts the same array much more efficiently.
        if (unlikely(is_baseball_2 && inst_cache[i].PC == 0x07007428)) {
            LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
            LDR_IO(2, 2, DRC_RELOC_BALLSORT*4);
            BLX(ARM_COND_AL, 2);
            // skip to after sorting code
            inst_cache[i].branch_offset = 0x070074b8 - 0x07007428;
            B(ARM_COND_AL, 0);
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

        reg1_modified = false;
        reg2_modified = false;
        unmapped_registers = false;
        next_available_reg = 2;
        arm_reg1 = 0;
        arm_reg2 = 0;

        // Map V810 registers and preload them if unmapped
        if (inst_cache[i].reg1 != 0xFF) {
            arm_reg1 = phys_regs[inst_cache[i].reg1];
            if (!arm_reg1) {
                unmapped_registers = true;
                arm_reg1 = next_available_reg++;
            }
        }

        if (inst_cache[i].reg2 != 0xFF) {
            arm_reg2 = phys_regs[inst_cache[i].reg2];
            if (!arm_reg2) {
                unmapped_registers = true;
                arm_reg2 = next_available_reg++;
            }
        }

        if (inst_cache[i].save_flags) {
            MRS(0);
            PUSH(1<<0);
        }

        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                LOAD_REG1();
                STR_IO(arm_reg1, 11, offsetof(cpu_state, PC));
                ADDCYCLES();
                POP(1 << 15);
                break;
            case V810_OP_JR: // jr imm26
                if (abs(inst_cache[i].branch_offset) < 1024) {
                    if (inst_cache[i].busywait) {
                        HALT(inst_cache[i].PC + inst_cache[i].branch_offset);
                    } else {
                        if (inst_cache[i].branch_offset <= 0) {
                            HANDLEINT(inst_cache[i].PC + inst_cache[i].branch_offset);
                        } else {
                            ADDCYCLES();
                        }
                        B(ARM_COND_AL, 0);
                    }
                } else {
                    ADDCYCLES();
                    LDW_I(0, inst_cache[i].PC + inst_cache[i].branch_offset);
                    // Save the new PC
                    STR_IO(0, 11, offsetof(cpu_state, PC));
                    POP(1 << 15);
                }
                break;
            case V810_OP_JAL: // jal disp26
            {
                arm_inst *branch_to_tweak = NULL;
                if (unlikely(is_baseball_2 && inst_cache[i].PC + inst_cache[i].branch_offset == 0x070077ca)) {
                    // In the overhead view in Virtual League Baseball 2,
                    // the fielders are scaled in software.
                    // This algorithm is slow when recompiled, so we override it
                    // with a faster native implementation.

                    // Verify that our values make sense, otherwise revert to original
                    LOAD_REG(0, 17);
                    LOAD_REG(1, 18);
                    MOV_IS(2, 0, ARM_SHIFT_LSR, 20);
                    CMP_I(2, 0x70, 0);
                    Boff(ARM_COND_NE, 9);
                    MOV_IS(2, 1, ARM_SHIFT_LSR, 16);
                    CMP_I(2, 0x5, 24);
                    Boff(ARM_COND_NE, 6);
                    // Do HLE
                    LDR_IO(3, 11, offsetof(cpu_state, reloc_table));
                    LDR_IO(3, 3, DRC_RELOC_BALLSCALE*4);
                    LOAD_REG(2, 19);
                    BLX(ARM_COND_AL, 3);
                    branch_to_tweak = inst_ptr;
                    Boff(ARM_COND_AL, 0);
                }
                if (is_space_invaders && inst_cache[i].PC == 0x07007fb6) {
                    // Make sure the Space Invaders intro FMV runs at the correct speed (ish).
                    // Value determined through trial and error.
                    // Correct for intro video, but attract video is very slightly slow.
                    cycles += 24;
                }

                LDW_I(0, inst_cache[i].PC + inst_cache[i].branch_offset);
                LDW_I(1, inst_cache[i].PC + 4);
                // Save the new PC
                STR_IO(0, 11, offsetof(cpu_state, PC));
                // Link the return address
                if (phys_regs[31])
                    MOV(phys_regs[31], 1);
                else
                    STR_IO(1, 11, offsetof(cpu_state, P_REG[31]));
                ADDCYCLES();
                POP(1 << 15);
                // fix the skip if needed
                if (branch_to_tweak) branch_to_tweak->b_bl.imm = inst_ptr - branch_to_tweak - 2;
                break;
            }
            case V810_OP_RETI:
                LDR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
                TST_I(0, PSW_NP >> 8, 24);
                // ldrne r1, S_REG[FEPC]
                new_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 1, offsetof(cpu_state, S_REG[FEPC]));
                // ldrne r2, S_REG[FEPSW]
                new_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 2, offsetof(cpu_state, S_REG[FEPSW]));
                // ldreq r1, S_REG[EIPC]
                new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 1, offsetof(cpu_state, S_REG[EIPC]));
                // ldreq r2, S_REG[EIPSW]
                new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 2, offsetof(cpu_state, S_REG[EIPSW]));

                STR_IO(1, 11, offsetof(cpu_state, PC));
                STR_IO(2, 11, offsetof(cpu_state, S_REG[PSW]));

                ADDCYCLES();

                // restore flags and handle any lingering interrupts
                LDR_IO(0, 11, offsetof(cpu_state, except_flags));
                LDR_IO(2, 11, offsetof(cpu_state, irq_handler));
                STR_IO(0, 11, offsetof(cpu_state, flags));
                BLX(ARM_COND_AL, 2);

                // if we didn't exit already, restore state
                MSR(0);
                POP(1 << 15);
                break;
            case V810_OP_BR:
                if (inst_cache[i].branch_offset == 0) {
                    HALT(inst_cache[i].PC);
                    break;
                }
                // vertical force doesn't have a tight spinloop like most games, so we can't detect it
                // it just continuously loops through entities, doing nothing more when each one done
                // so let's just artificially skip a bunch of time so that the game isn't slow
                if ((memcmp(tVBOpt.GAME_ID, "01VH3E", 6) == 0 || memcmp(tVBOpt.GAME_ID, "18VH3J", 6) == 0)
                    && inst_cache[i].PC == 0x07000c08
                ) {
                    MOV_I(0, 1, 25);
                    ADD(10, 10, 0);
                    HANDLEINT(inst_cache[i].PC + inst_cache[i].branch_offset);
                    B(ARM_COND_AL, 0);
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
                arm_cond = cond_map[inst_cache[i].opcode & 0xF];
                if (inst_cache[i].busywait) {
                    BUSYWAIT(arm_cond, inst_cache[i].PC + inst_cache[i].branch_offset);
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
                        HANDLEINT(inst_cache[i].PC);
                    } else {
                        ADDCYCLES();
                    }
                    B(arm_cond, 0);
                }
                // branch not taken, so it only took 1 cycle
                SUB_I(10, 10, 2, 0);
                break;
            // Special case: bnh and bh can't be directly translated to ARM
            case V810_OP_BNH:
                if (inst_cache[i].busywait) {
                    BUSYWAIT_BNH(inst_cache[i].PC + inst_cache[i].branch_offset);
                } else {
                    if (inst_cache[i].branch_offset <= 0) {
                        HANDLEINT(inst_cache[i].PC);
                    } else {
                        ADDCYCLES();
                    }
                    // Branch if C == 1 or Z == 1
                    B(ARM_COND_CS, 0);
                    B(ARM_COND_EQ, 0);
                }
                // branch not taken, so it only took 1 cycle
                SUB_I(10, 10, 2, 0);
                break;
            case V810_OP_BH:
                if (inst_cache[i].busywait) {
                    BUSYWAIT_BH(inst_cache[i].PC + inst_cache[i].branch_offset);
                } else {
                    if (inst_cache[i].branch_offset <= 0) {
                        HANDLEINT(inst_cache[i].PC);
                    } else {
                        ADDCYCLES();
                    }
                    // Branch if C == 0 and Z == 0
                    Boff(ARM_COND_CS, 3);
                    Boff(ARM_COND_EQ, 2);
                    B(ARM_COND_AL, 0);
                }
                // branch not taken, so it only took 1 cycle
                SUB_I(10, 10, 2, 0);
                break;
            case V810_OP_MOVHI: // movhi imm16, reg1, reg2:
                // we need to check if it's 0 to avoid UB with ctz and clz
                if (inst_cache[i].imm != 0) {
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int neg_ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int neg_clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int cto = __builtin_ctz(~inst_cache[i].imm) & ~1;
                    int clo = __builtin_clz(~inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    int neg_width = (16 - neg_clz) - neg_ctz;
                    int inv_width = (16 - clo) - cto;
                    if (width <= 8) {
                        // normal
                        if (inst_cache[i].reg1 != 0) {
                            LOAD_REG1();
                            ADD_I(arm_reg2, arm_reg1, inst_cache[i].imm >> ctz, 16 - ctz);
                        } else {
                            MOV_I(arm_reg2, inst_cache[i].imm >> ctz, 16 - ctz);
                        }
                    } else if ((inst_cache[i].imm & 0x8000) && neg_width <= 8 && inst_cache[i].reg1 != 0) {
                        // negative
                        LOAD_REG1();
                        SUB_I(arm_reg2, arm_reg1, -(inst_cache[i].imm >> neg_ctz) & 0xff, 16 - neg_ctz);
                    } else {
                        // full-size
                        if (inst_cache[i].reg1 != 0) {
                            LOAD_REG1();
                            ADD_I(arm_reg2, arm_reg1, inst_cache[i].imm >> 8, 8);
                        } else {
                            MOV_I(arm_reg2, inst_cache[i].imm >> 8, 8);
                        }
                        ADD_I(arm_reg2, arm_reg2, inst_cache[i].imm & 0xFF, 16);
                    }
                    reg2_modified = true;
                } else {
                    // it's just a mov at this point
                    RELOAD_REG1(arm_reg2);
                    if (arm_reg1 != arm_reg2) reg2_modified = true;
                }

                break;
            case V810_OP_MOVEA: // movea imm16, reg1, reg2
                // we need to check if it's 0 to avoid UB with ctz and clz
                if (inst_cache[i].imm != 0) {
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int neg_ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int neg_clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int cto = __builtin_ctz(~inst_cache[i].imm) & ~1;
                    int clo = __builtin_clz(~inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    int neg_width = (16 - neg_clz) - neg_ctz;
                    int inv_width = (16 - clo) - cto;
                    if (!(inst_cache[i].imm & 0x8000) && width <= 8) {
                        // normal
                        if (inst_cache[i].reg1 != 0) {
                            LOAD_REG1();
                            ADD_I(arm_reg2, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                        } else {
                            MOV_I(arm_reg2, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                        }
                    } else if ((inst_cache[i].imm & 0x8000) && neg_width <= 8 && inst_cache[i].reg1 != 0) {
                        // negative, with alt register
                        LOAD_REG1();
                        SUB_I(arm_reg2, arm_reg1, (-inst_cache[i].imm & 0xffff) >> neg_ctz, (32 - neg_ctz) & 31);
                    } else if (inst_cache[i].imm == 0xFFFF || ((inst_cache[i].imm & 0x8000) && inv_width <= 8)) {
                        // inverted
                        if (inst_cache[i].reg1 != 0) {
                            LOAD_REG1();
                            MVN_I(0, ((~inst_cache[i].imm) & 0xffff) >> cto, (32 - cto) & 31);
                            ADD(arm_reg2, arm_reg1, 0);
                        } else {
                            MVN_I(arm_reg2, ((~inst_cache[i].imm & 0xffff) >> cto), (32 - cto) & 31);
                        }
                    } else if ((inst_cache[i].imm & 0x8000) && neg_width <= 8 && inst_cache[i].reg1 == 0) {
                        // negative, with zero register
                        LOAD_REG1();
                        SUB_I(arm_reg2, arm_reg1, (-inst_cache[i].imm & 0xffff) >> neg_ctz, (32 - neg_ctz) & 31);
                    } else {
                        if (!(inst_cache[i].imm & 0x8000)) {
                            if (inst_cache[i].reg1 != 0) {
                                LOAD_REG1();
                                ADD_I(arm_reg2, arm_reg1, inst_cache[i].imm >> 8, 24);
                            } else {
                                MOV_I(arm_reg2, inst_cache[i].imm >> 8, 24);
                            }
                            ADD_I(arm_reg2, arm_reg2, inst_cache[i].imm & 0xFF, 0);
                        } else {
                            LOAD_REG1();
                            SUB_I(arm_reg2, arm_reg1, (-inst_cache[i].imm & 0xffff) >> 8, 24);
                            SUB_I(arm_reg2, arm_reg2, -inst_cache[i].imm & 0xff, 0);
                        }
                    }
                    reg2_modified = true;
                } else {
                    // it's just a mov at this point
                    RELOAD_REG1(arm_reg2);
                    if (arm_reg1 != arm_reg2) reg2_modified = true;
                }
                break;
            case V810_OP_MOV: // mov reg1, reg2
                RELOAD_REG1(arm_reg2);
                if (arm_reg1 != arm_reg2) reg2_modified = true;
                break;
            case V810_OP_ADD: // add reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                ADDS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_SUB: // sub reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                SUBS(arm_reg2, arm_reg2, arm_reg1);
                INV_CARRY();
                reg2_modified = true;
                break;
            case V810_OP_CMP: // cmp reg1, reg2
                LOAD_REG2();
                if (inst_cache[i].reg1 == 0) {
                    CMP_I(arm_reg2, 0, 0);
                } else {
                    LOAD_REG1();
                    if (inst_cache[i].reg2 == 0)
                        RSBS_I(0, arm_reg1, 0, 0);
                    else
                        CMP(arm_reg2, arm_reg1);
                }
                INV_CARRY();
                break;
            case V810_OP_SHL: // shl reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                MOV(0, arm_reg1);
                AND_I(0, 0, 0x1F, 0);
                LSLS(arm_reg2, arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_SHR: // shr reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                MOV(0, arm_reg1);
                AND_I(0, 0, 0x1F, 0);
                LSRS(arm_reg2, arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_SAR: // sar reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                MOV(0, arm_reg1);
                AND_I(0, 0, 0x1F, 0);
                ASRS(arm_reg2, arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_MUL: // mul reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                SMULLS(arm_reg2, inst_cache->reg2 != 30 ? phys_regs[30] : 0, arm_reg2, arm_reg1);
                // If the 30th register isn't being used in the block, the high
                // word of the multiplication will be in r0 (because
                // phys_regs[30] == 0) and we'll have to save it manually
                if (!phys_regs[30]) {
                    STR_IO(0, 11, offsetof(cpu_state, P_REG[30]));
                }
                reg2_modified = true;
                break;
            case V810_OP_MULU: // mul reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                UMULLS(arm_reg2, inst_cache->reg2 != 30 ? phys_regs[30] : 0, arm_reg2, arm_reg1);
                if (!phys_regs[30]) {
                    STR_IO(0, 11, offsetof(cpu_state, P_REG[30]));
                }
                reg2_modified = true;
                break;
            case V810_OP_DIV: // div reg1, reg2
                // reg2/reg1 -> reg2 (r0)
                // reg2%reg1 -> r30 (r1)

                // load function and save flags (interleaved)
                MRS(0);
                LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
                PUSH(1 << 0);
                LDR_IO(2, 2, DRC_RELOC_IDIVMOD*4);

                RELOAD_REG2(0);
                RELOAD_REG1(1);
                BLX(ARM_COND_AL, 2);

                if (inst_cache[i].reg2 != 30) {
                    if (!phys_regs[30])
                        STR_IO(1, 11, offsetof(cpu_state, P_REG[30]));
                    else
                        MOV(phys_regs[30], 1);
                }
                SAVE_REG2(0);

                // restore flags
                POP(1 << 1);
                MSR(1);

                // flags
                ORRS(0, 0, 0);

                break;
            case V810_OP_DIVU: // divu reg1, reg2
                // reg2/reg1 -> reg2 (r0)
                // reg2%reg1 -> r30 (r1)

                // load function and save flags (interleaved)
                MRS(0);
                LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
                PUSH(1 << 0);
                LDR_IO(2, 2, DRC_RELOC_UIDIVMOD*4);

                RELOAD_REG2(0);
                RELOAD_REG1(1);
                BLX(ARM_COND_AL, 2);

                if (inst_cache[i].reg2 != 30) {
                    if (!phys_regs[30])
                        STR_IO(1, 11, offsetof(cpu_state, P_REG[30]));
                    else
                        MOV(phys_regs[30], 1);
                }
                SAVE_REG2(0);

                // restore flags
                POP(1 << 1);
                MSR(1);

                // flags
                ORRS(0, 0, 0);

                break;
            case V810_OP_OR: // or reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                ORRS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_AND: // and reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                ANDS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_XOR: // xor reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                EORS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_NOT: // not reg1, reg2
                LOAD_REG1();
                MVNS(arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_MOV_I: // mov imm5, reg2
                if (!(inst_cache[i].imm & 0x10)) {
                    MOV_I(arm_reg2, inst_cache[i].imm, 0);
                } else {
                    MVN_I(arm_reg2, ~sign_5(inst_cache[i].imm), 0);
                }
                reg2_modified = true;
                break;
            case V810_OP_ADD_I: // add imm5, reg2
                LOAD_REG2();
                if (!(inst_cache[i].imm & 0x10)) {
                    ADDS_I(arm_reg2, arm_reg2, inst_cache[i].imm, 0);
                } else {
                    MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                    ADDS_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_ASR, 24);
                }
                reg2_modified = true;
                break;
            case V810_OP_CMP_I: // cmp imm5, reg2
                LOAD_REG2();
                if (!(inst_cache[i].imm & 0x10)) {
                    CMP_I(arm_reg2, inst_cache[i].imm, 0);
                } else {
                    MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                    CMP_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
                }
                INV_CARRY();
                reg2_modified = true;
                break;
            case V810_OP_SHL_I: // shl imm5, reg2
                LOAD_REG2();
                // lsl reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSL, arm_reg2);
                reg2_modified = true;
                break;
            case V810_OP_SHR_I: // shr imm5, reg2
                // arm doesn't do 0-bit immediate right shifts
                if (inst_cache[i].imm != 0) {
                    LOAD_REG2();
                    // lsr reg2, reg2, #imm5
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSR, arm_reg2);
                    reg2_modified = true;
                }
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                // arm doesn't do 0-bit immediate right shifts
                if (inst_cache[i].imm != 0) {
                    LOAD_REG2();
                    // asr reg2, reg2, #imm5
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_ASR, arm_reg2);
                    reg2_modified = true;
                }
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                if (inst_cache[i].imm == 0 || inst_cache[i].reg1 == 0) {
                    MOVS_I(arm_reg2, 0, 0);
                } else if (inst_cache[i].imm == 0xFFFF) {
                    LOAD_REG1();
                    BIC_I(arm_reg2, arm_reg1, 0xff, 8);
                    BICS_I(arm_reg2, arm_reg2, 0xff, 16);
                } else {
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int cto = __builtin_ctz(~inst_cache[i].imm) & ~1;
                    int clo = __builtin_clz(~inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    int inv_width = (16 - clo) - cto;
                    LOAD_REG1();
                    if (width <= 8) {
                        ANDS_I(arm_reg2, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else if (inv_width <= 8) {
                        UXTH(arm_reg2, arm_reg1, 0);
                        BICS_I(arm_reg2, arm_reg2, (~inst_cache[i].imm & 0xFFFF) >> cto, (32 - cto) & 31);
                    } else {
                        MOV_I(0, inst_cache[i].imm >> 8, 24);
                        ORR_I(0, 0, inst_cache[i].imm & 0xFF, 0);
                        ANDS(arm_reg2, arm_reg1, 0);
                    }
                }
                reg2_modified = true;
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                LOAD_REG1();
                if (inst_cache[i].imm != 0) {
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        EORS_I(arm_reg2, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        EOR_I(arm_reg2, arm_reg1, inst_cache[i].imm >> 8, 24);
                        EORS_I(arm_reg2, arm_reg2, inst_cache[i].imm & 0xFF, 0);
                    }
                    reg2_modified = true;
                } else {
                    // it's effectively a mov with flags at this point
                    MOVS(arm_reg2, arm_reg1);
                    if (arm_reg1 != arm_reg2) reg2_modified = true;
                }
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                LOAD_REG1();
                if (inst_cache[i].imm != 0) {
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ORRS_I(arm_reg2, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ORR_I(arm_reg2, arm_reg1, inst_cache[i].imm >> 8, 24);
                        ORRS_I(arm_reg2, arm_reg2, inst_cache[i].imm & 0xFF, 0);
                    }
                    reg2_modified = true;
                } else {
                    // it's effectively a mov with flags at this point
                    MOVS(arm_reg2, arm_reg1);
                    if (arm_reg1 != arm_reg2) reg2_modified = true;
                }
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
                LOAD_REG1();
                if (inst_cache[i].imm != 0) {
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (clz != 0 && width <= 8) {
                        ADDS_I(arm_reg2, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        if (clz == 0) {
                            if (inst_cache[i].imm == 0xFFFF) {
                                MVN_I(0, 0, 0);
                            } else {
                                int inv_ctz = __builtin_ctz(~inst_cache[i].imm) & ~1;
                                int inv_clz = __builtin_clz(~inst_cache[i].imm << 16);
                                int inv_width = (16 - inv_clz) - inv_ctz;
                                if (inv_width <= 8) {
                                    MVN_I(0, (~inst_cache[i].imm & 0xffff) >> inv_ctz, (32 - inv_ctz) & 31);
                                } else {
                                    MVN_I(0, ~inst_cache[i].imm & 0xff, 0);
                                    BIC_I(0, 0, ~inst_cache[i].imm >> 8, 24);
                                }
                            }
                        } else {
                            MOV_I(0, (inst_cache[i].imm >> 8), 24);
                            ORR_I(0, 0, (inst_cache[i].imm & 0xFF), 0);
                        }
                        ADDS(arm_reg2, arm_reg1, 0);
                    }
                    reg2_modified = true;
                } else {
                    // it's effectively a mov with flags at this point
                    LOAD_REG1();
                    MOVS(arm_reg2, arm_reg1);
                    if (arm_reg1 != arm_reg2) reg2_modified = true;
                }
                break;
            case V810_OP_LD_B: // ld.b disp16 [reg1], reg2
            case V810_OP_IN_B: // in.b disp16 [reg1], reg2
                if (arm_reg1 < 4) arm_reg1 = 0;
                LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(1, 1, DRC_RELOC_RBYTE*4);
                
                if (inst_cache[i].imm == 0) {
                    RELOAD_REG1(0);
                } else if ((short)inst_cache[i].imm > 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ADD_I(0, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ADD_I(0, arm_reg1, inst_cache[i].imm & 0xff, 0);
                        ADD_I(0, 0, inst_cache[i].imm >> 8, 24);
                    }
                } else if ((short)inst_cache[i].imm < 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        SUB_I(0, arm_reg1, (-inst_cache[i].imm & 0xffff) >> ctz, (32 - ctz) & 31);
                    } else {
                        SUB_I(0, arm_reg1, -inst_cache[i].imm & 0xff, 0);
                        SUB_I(0, 0, (-inst_cache[i].imm >> 8) & 0xff, 24);
                    }
                }

                BLX(ARM_COND_AL, 1);

                // Add cycles returned in r1.
                if (!is_pinball) ADD(10, 10, 1);

                if (slow_memory) cycles += 2;

                if (inst_cache[i].opcode == V810_OP_IN_B) {
                    UXTB(arm_reg2, 0, 0);
                    reg2_modified = true;
                } else {
                    SAVE_REG2(0);
                }

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
                if (arm_reg1 < 4) arm_reg1 = 0;
                LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(1, 1, DRC_RELOC_RHWORD*4);

                if (inst_cache[i].imm == 0) {
                    RELOAD_REG1(0);
                } else if ((short)inst_cache[i].imm > 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ADD_I(0, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ADD_I(0, arm_reg1, inst_cache[i].imm & 0xff, 0);
                        ADD_I(0, 0, inst_cache[i].imm >> 8, 24);
                    }
                } else if ((short)inst_cache[i].imm < 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        SUB_I(0, arm_reg1, (-inst_cache[i].imm & 0xffff) >> ctz, (32 - ctz) & 31);
                    } else {
                        SUB_I(0, arm_reg1, -inst_cache[i].imm & 0xff, 0);
                        SUB_I(0, 0, (-inst_cache[i].imm >> 8) & 0xff, 24);
                    }
                }

                BLX(ARM_COND_AL, 1);

                // Add cycles returned in r1.
                if (!is_pinball) ADD(10, 10, 1);

                if (slow_memory) cycles += 2;

                if (inst_cache[i].opcode == V810_OP_IN_H) {
                    UXTH(arm_reg2, 0, 0);
                    reg2_modified = true;
                } else {
                    SAVE_REG2(0);
                }

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
                if (arm_reg1 < 4) arm_reg1 = 0;
                LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(1, 1, DRC_RELOC_RWORD*4);

                if (inst_cache[i].imm == 0) {
                    RELOAD_REG1(0);
                } else if ((short)inst_cache[i].imm > 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ADD_I(0, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ADD_I(0, arm_reg1, inst_cache[i].imm & 0xff, 0);
                        ADD_I(0, 0, inst_cache[i].imm >> 8, 24);
                    }
                } else if ((short)inst_cache[i].imm < 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        SUB_I(0, arm_reg1, (-inst_cache[i].imm & 0xffff) >> ctz, (32 - ctz) & 31);
                    } else {
                        SUB_I(0, arm_reg1, -inst_cache[i].imm & 0xff, 0);
                        SUB_I(0, 0, (-inst_cache[i].imm >> 8) & 0xff, 24);
                    }
                }

                BLX(ARM_COND_AL, 1);

                // Add cycles returned in r1.
                if (!is_pinball) ADD(10, 10, 1);

                SAVE_REG2(0);

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
                if (arm_reg1 < 4) arm_reg1 = 0;
                LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(2, 2, DRC_RELOC_WBYTE*4);

                if (inst_cache[i].imm == 0) {
                    RELOAD_REG1(0);
                } else if ((short)inst_cache[i].imm > 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ADD_I(0, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ADD_I(0, arm_reg1, inst_cache[i].imm & 0xff, 0);
                        ADD_I(0, 0, inst_cache[i].imm >> 8, 24);
                    }
                } else if ((short)inst_cache[i].imm < 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        SUB_I(0, arm_reg1, (-inst_cache[i].imm & 0xffff) >> ctz, (32 - ctz) & 31);
                    } else {
                        SUB_I(0, arm_reg1, -inst_cache[i].imm & 0xff, 0);
                        SUB_I(0, 0, (-inst_cache[i].imm >> 8) & 0xff, 24);
                    }
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    RELOAD_REG2(1);

                BLX(ARM_COND_AL, 2);

                if (slow_memory) cycles += 2;

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 2 cycles instead of 1
                    cycles += 1;
                }

                // Add cycles returned in r0.
                if (!is_pinball) ADD(10, 10, 0);
                break;
            case V810_OP_ST_H:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_H: // out.h reg2, disp16 [reg1]
                if (arm_reg1 < 4) arm_reg1 = 0;
                LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(2, 2, DRC_RELOC_WHWORD*4);

                if (inst_cache[i].imm == 0) {
                    RELOAD_REG1(0);
                } else if ((short)inst_cache[i].imm > 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ADD_I(0, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ADD_I(0, arm_reg1, inst_cache[i].imm & 0xff, 0);
                        ADD_I(0, 0, inst_cache[i].imm >> 8, 24);
                    }
                } else if ((short)inst_cache[i].imm < 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        SUB_I(0, arm_reg1, (-inst_cache[i].imm & 0xffff) >> ctz, (32 - ctz) & 31);
                    } else {
                        SUB_I(0, arm_reg1, -inst_cache[i].imm & 0xff, 0);
                        SUB_I(0, 0, (-inst_cache[i].imm >> 8) & 0xff, 24);
                    }
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    RELOAD_REG2(1);

                BLX(ARM_COND_AL, 2);

                if (slow_memory) cycles += 2;

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 2 cycles instead of 1
                    cycles += 1;
                }

                // Add cycles returned in r0.
                if (!is_pinball) ADD(10, 10, 0);
                break;
            case V810_OP_ST_W:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_W: // out.h reg2, disp16 [reg1]
                if (arm_reg1 < 4) arm_reg1 = 0;
                LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(2, 2, DRC_RELOC_WWORD*4);

                if (inst_cache[i].imm == 0) {
                    RELOAD_REG1(0);
                } else if ((short)inst_cache[i].imm > 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        ADD_I(0, arm_reg1, inst_cache[i].imm >> ctz, (32 - ctz) & 31);
                    } else {
                        ADD_I(0, arm_reg1, inst_cache[i].imm & 0xff, 0);
                        ADD_I(0, 0, inst_cache[i].imm >> 8, 24);
                    }
                } else if ((short)inst_cache[i].imm < 0) {
                    LOAD_REG1();
                    int ctz = __builtin_ctz(-inst_cache[i].imm) & ~1;
                    int clz = __builtin_clz(-inst_cache[i].imm << 16);
                    int width = (16 - clz) - ctz;
                    if (width <= 8) {
                        SUB_I(0, arm_reg1, (-inst_cache[i].imm & 0xffff) >> ctz, (32 - ctz) & 31);
                    } else {
                        SUB_I(0, arm_reg1, -inst_cache[i].imm & 0xff, 0);
                        SUB_I(0, 0, (-inst_cache[i].imm >> 8) & 0xff, 24);
                    }
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    RELOAD_REG2(1);

                BLX(ARM_COND_AL, 2);

                if (slow_memory) cycles += 4;

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 4 cycles instead of 1
                    cycles += 3;
                }

                // Add cycles returned in r0.
                if (!is_pinball) ADD(10, 10, 0);

                // if we load the same thing immediately after saving it, skip the loading
                if (i + 1 < num_v810_inst &&
                    (inst_cache[i + 1].opcode == V810_OP_LD_W || inst_cache[i + 1].opcode == V810_OP_IN_W) &&
                    inst_cache[i + 1].imm == inst_cache[i].imm && inst_cache[i + 1].reg1 == inst_cache[i].reg1 &&
                    inst_cache[i + 1].reg2 == inst_cache[i].reg2
                ) {
                    cycles += 5;
                    inst_cache[i].branch_offset = 8;
                    B(ARM_COND_AL, 0);
                }
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // Stores reg2 in vb_state->v810_state.S_REG[regID]
                LOAD_REG2();
                STR_IO(arm_reg2, 11, offsetof(cpu_state, S_REG[inst_cache[i].imm]));
                if (inst_cache[i].imm == CHCW) chcw_load_seen = true;
                if (inst_cache[i].imm == PSW || inst_cache[i].imm == EIPSW) {
                    // load status register
                    if (inst_cache[i].imm == PSW)
                        MRS(0);
                    else
                        LDR_IO(0, 11, offsetof(cpu_state, except_flags));
                    // clear out condition flags
                    BIC_I(0, 0, 0xf, 4);
                    // zero flag
                    TST_I(arm_reg2, 1, 0);
                    ORRCC_I(ARM_COND_NE, 0, 1, 2);
                    // sign flag
                    TST_I(arm_reg2, 2, 0);
                    ORRCC_I(ARM_COND_NE, 0, 2, 2);
                    // overflow flag
                    TST_I(arm_reg2, 4, 0);
                    ORRCC_I(ARM_COND_NE, 0, 1, 4);
                    // carry flag
                    TST_I(arm_reg2, 8, 0);
                    ORRCC_I(ARM_COND_NE, 0, 2, 4);
                    // save status register
                    if (inst_cache[i].imm == PSW)
                        MSR(0);
                    else
                        STR_IO(0, 11, offsetof(cpu_state, except_flags));
                }
                break;
            case V810_OP_STSR: // stsr regID, reg2
                // Loads vb_state->v810_state.S_REG[regID] into reg2
                LOAD_REG2();
                LDR_IO(arm_reg2, 11, offsetof(cpu_state, S_REG[inst_cache[i].imm]));
                if (inst_cache[i].imm == PSW || inst_cache[i].imm == EIPSW) {
                    // clear out condition flags
                    BIC_I(arm_reg2, arm_reg2, 0xf, 0);
                    // load except flags if relevant
                    if (inst_cache[i].imm == EIPSW) {
                        MRS(0);
                        LDR_IO(1, 11, offsetof(cpu_state, except_flags));
                        MSR(1);
                    }
                    // fill in the actual condition flags
                    ORRCC_I(ARM_COND_EQ, arm_reg2, 1, 0);
                    ORRCC_I(ARM_COND_MI, arm_reg2, 2, 0);
                    ORRCC_I(ARM_COND_VS, arm_reg2, 4, 0);
                    ORRCC_I(ARM_COND_CS, arm_reg2, 8, 0);
                    // reload original flags
                    if (inst_cache[i].imm == EIPSW)
                        MSR(0);
                }
                reg2_modified = true;
                break;
            case V810_OP_SEI: // sei
                // Set the 12th bit in vb_state->v810_state.S_REG[PSW]
                LDR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
                ORR_I(0, 0, 1, 20);
                STR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
                break;
            case V810_OP_CLI: // cli
                // Clear the 12th bit in vb_state->v810_state.S_REG[PSW]
                LDR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
                BIC_I(0, 0, 1, 20);
                STR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
                break;
            case V810_OP_SETF: // setf imm5, reg2
                if ((inst_cache[i].imm & 0xF) == (V810_OP_BNH & 0xF)) {
                    // C or Z
                    MOV_I(arm_reg2, 0, 0);
                    new_data_proc_imm(ARM_COND_EQ, ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
                    new_data_proc_imm(ARM_COND_CS, ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
                } else if ((inst_cache[i].imm & 0xF) == (V810_OP_BH & 0xF)) {
                    // !C and !Z
                    MOV_I(arm_reg2, 1, 0);
                    new_data_proc_imm(ARM_COND_EQ, ARM_OP_MOV, 0, 0, arm_reg2, 0, 0);
                    new_data_proc_imm(ARM_COND_CS, ARM_OP_MOV, 0, 0, arm_reg2, 0, 0);
                } else {
                    MOV_I(arm_reg2, 0, 0);
                    // mov<cond> reg2, 1
                    new_data_proc_imm(cond_map[inst_cache[i].imm & 0xF], ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
                }
                reg2_modified = true;
                break;
            case V810_OP_HALT: // halt
                HALT(inst_cache[i].PC);
                break;
            case V810_OP_BSTR:
                MOV_I(2, 31, 0);
                if (inst_cache[i].imm >= 4) {
                    // non-search, we have a destination
                    // v810 r26 << 5 -> arm r3
                    if (!phys_regs[26]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[26]));
                    AND(3, phys_regs[26], 2);
                    // v810 r27 -> arm r3
                    if (!phys_regs[27]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[27]));
                    AND(0, phys_regs[27], 2);
                    ORR_IS(3, 0, 3, ARM_SHIFT_LSL, 5);

                    // cycle count << 10 -> arm r3
                    LDR_IO(0, 11, offsetof(cpu_state, cycles_until_event_partial));
                    ORR_IS(3, 3, 0, ARM_SHIFT_LSL, 10);
                } else {
                    // search, we only have a source
                    // v810 r27 -> arm r3 lo
                    if (!phys_regs[27]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[27]));
                    AND(3, phys_regs[27], 2);
                }

                // mov r2, ~3
                new_data_proc_imm(ARM_COND_AL, ARM_OP_MVN, 0, 0, 2, 0, 3);
                if (inst_cache[i].imm >= 4) {
                    // non-search, clear the bottom two bits
                    // v810 r29 & (~3) -> arm r1
                    if (!phys_regs[29]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[29]));
                    AND(1, phys_regs[29], 2);
                } else {
                    // search, leave as-is
                    // v810 r29 -> arm r1
                    if (!phys_regs[29]) LDR_IO(1, 11, offsetof(cpu_state, P_REG[29]));
                    else MOV(1, phys_regs[29]);
                }

                // v810 r30 & (~3) -> arm r0
                if (!phys_regs[30]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[30]));
                AND(0, phys_regs[30], 2);

                // v810 r28 -> arm r2
                if (!phys_regs[28]) LDR_IO(2, 11, offsetof(cpu_state, P_REG[28]));
                else MOV(2, phys_regs[28]);

                // call the function
                PUSH(1<<5);
                LDR_IO(5, 11, offsetof(cpu_state, reloc_table));
                LDR_IO(5, 5, (DRC_RELOC_BSTR+inst_cache[i].imm)*4);
                BLX(ARM_COND_AL, 5);
                POP(1<<5);

                // reload registers
                for (int j = inst_cache[i].imm >= 4 ? 26 : 27; j <= 30; j++)
                    if (phys_regs[j])
                        LDR_IO(phys_regs[j], 11, offsetof(cpu_state, P_REG[j]));
                if (inst_cache[i].imm < 4) {
                    // zero flag for search
                    ORRS(0, 0, 0);
                } else {
                    // add cycles and check interrupt
                    ADD(10, 10, 0);
                    HANDLEINT(inst_cache[i].PC);
                    int len_reg = phys_regs[28];
                    if (!len_reg) {
                        LDR_IO(0, 11, offsetof(cpu_state, P_REG[28]));
                    }
                    MRS(1);
                    CMP_I(len_reg, 0, 0);
                    Boff(ARM_COND_EQ, 3);
                    MRS(1);
                    B(ARM_COND_AL, 0); // branches to start of instruction
                    MRS(1);
                }
                break;
            case V810_OP_FPP:
                switch (inst_cache[i].imm) {
                case V810_OP_CVT_WS:
                    LOAD_REG1();
                    VMOV_SR(0, arm_reg1);
                    VCVT_F32_S32(0, 0);
                    VCMP_F32_0(0);
                    VMRS();
                    INV_CARRY();
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_CVT_SW:
                    LOAD_REG1();
                    VMOV_SR(0, arm_reg1);
                    VCVT_S32_F32(0, 0);
                    VMOV_RS(arm_reg2, 0);
                    ORRS(arm_reg2, arm_reg2, arm_reg2);
                    reg2_modified = true;
                    break;
                case V810_OP_CMPF_S:
                    LOAD_REG1();
                    LOAD_REG2();
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VCMP_F32(1, 0);
                    VMRS();
                    INV_CARRY();
                    break;
                case V810_OP_ADDF_S:
                    LOAD_REG1();
                    LOAD_REG2();
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VADD_F32(0, 1, 0);
                    VCMP_F32_0(0);
                    VMRS();
                    INV_CARRY();
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_SUBF_S:
                    LOAD_REG1();
                    LOAD_REG2();
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VSUB_F32(0, 1, 0);
                    VCMP_F32_0(0);
                    VMRS();
                    INV_CARRY();
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_MULF_S:
                    LOAD_REG1();
                    LOAD_REG2();
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VMUL_F32(0, 1, 0);
                    VCMP_F32_0(0);
                    VMRS();
                    INV_CARRY();
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_DIVF_S:
                    cycles += 44;
                    LOAD_REG1();
                    LOAD_REG2();
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VDIV_F32(0, 1, 0);
                    VCMP_F32_0(0);
                    VMRS();
                    INV_CARRY();
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_XB:
                    cycles += 6;
                    LOAD_REG2();
                    REV(0, arm_reg2);
                    MOV_IS(arm_reg2, arm_reg2, ARM_SHIFT_LSR, 16);
                    MOV_IS(arm_reg2, arm_reg2, ARM_SHIFT_LSL, 16);
                    ORR_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_LSR, 16);
                    reg2_modified = true;
                    break;
                case V810_OP_XH:
                    cycles += 1;
                    LOAD_REG2();
                    MOV_IS(arm_reg2, arm_reg2, ARM_SHIFT_ROR, 16);
                    reg2_modified = true;
                    break;
                case V810_OP_REV:
                    cycles += 22;
                    // RBIT would be great here, but that's only in ARMv6T2, so we'll do it manually.
                    LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
                    LDR_IO(1, 1, DRC_RELOC_REV*4);
                    RELOAD_REG1(0);
                    BLX(ARM_COND_AL, 1);
                    MOV(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_TRNC_SW:
                    LOAD_REG1();
                    VMOV_SR(0, arm_reg1);
                    TRUNC(0, 0);
                    VMOV_RS(arm_reg2, 0);
                    ORRS(arm_reg2, arm_reg2, arm_reg2);
                    reg2_modified = true;
                    break;
                case V810_OP_MPYHW:
                    cycles += 9;
                    LOAD_REG1();
                    LOAD_REG2();
                    MOV_IS(0, arm_reg1, ARM_SHIFT_LSL, 15);
                    MOV_IS(0, 0, ARM_SHIFT_ASR, 15);
                    MUL(arm_reg2, 0, arm_reg2);
                    reg2_modified = true;
                    break;
                default:
                    dprintf(0, "[DRC]: Invalid FPU subop 0x%lx\n", inst_cache[i].imm);
                    NOP();
                    break;
                }
                break;
            case V810_OP_NOP:
                NOP();
                break;
            case END_BLOCK:
                POP(1 << 15);
                break;
            default:
                dprintf(0, "[DRC]: %s (0x%x) not implemented\n", optable[inst_cache[i].opcode].opname, inst_cache[i].opcode);
                // Fill unimplemented instructions with a nop and hope the game still runs
                NOP();
                break;
        }

        if (inst_cache[i].save_flags) {
            POP(1<<0);
            MSR(0);
        }

        if (unmapped_registers) {
            if (arm_reg1 < 4 && reg1_modified)
                STR_IO(arm_reg1, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg1]));
            if (arm_reg2 < 4 && reg2_modified)
                STR_IO(arm_reg2, 11, offsetof(cpu_state, P_REG[inst_cache[i].reg2]));
        }

        if (i + 1 < num_v810_inst) {
            if (is_virtual_lab && inst_cache[i + 1].PC == 0x07002446) {
                // virtual lab hack
                // interrupts don't save registers, and clearing levels relies on
                // registers getting dirty
                HALT(0x07002446);
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
                LDR_IO(2, 11, offsetof(cpu_state, irq_handler));
                MRS(0);
                LDW_I(1, inst_cache[i+1].PC);
                ADD_I(10, 10, cycles & 0xFF, 0);
                BLX(ARM_COND_AL, 2);
                MSR(0);
                cycles = 0;
            } else if (is_marios_tennis_multiplayer && inst_cache[i + 1].PC == 0x07010442) {
                // Mario's Tennis multiplayer hack:
                // Some setup code flips CC-Wr off, flips it on, then loops if CC-Rd is on.
                // Getting out of this loop requires the two systems to be desynced:
                // one system has to check CC-Rd while the other has CC-Wr off.
                // To allow them to desync, we place an interrupt check between the writes.
                HANDLEINT(inst_cache[i + 1].PC);
            } else if (cycles >= 200) {
                HANDLEINT(inst_cache[i + 1].PC);
            } else if (cycles != 0 && (inst_cache[i + 1].is_branch_target || inst_cache[i + 1].opcode == V810_OP_BSTR)) {
                // branch target or bitstring instruction coming up
                ADDCYCLES();
            } else if (inst_cache[i + 1].PC > (0xfffffe00 & V810_ROM1.highaddr) && !(inst_cache[i + 1].PC & 0xf)) {
                // potential interrupt handler coming up
                ADDCYCLES();
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
    WORD *cache_ptr = drc_alloc(num_arm_inst);
    if (cache_ptr == NULL) {
        err = DRC_ERR_CACHE_FULL;
        goto cleanup;
    }
    block->phys_offset = cache_ptr;
    for (i = 0; i < num_v810_inst; i++) {
        drc_setEntry(inst_cache[i].PC, cache_ptr + inst_cache[i].start_pos, block);
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
            if (trans_cache[j].needs_branch) {
                WORD v810_dest = inst_cache[i].PC + inst_cache[i].branch_offset;
                WORD* arm_dest = drc_getEntry(v810_dest, NULL);
                int arm_offset = (int)(arm_dest - (cache_ptr + j) - 2);

                if (arm_dest == cache_start) {
                    // Should be fixed, but just in case
                    dprintf(0, "WARN:can't jump from %lx to %lx\n", inst_cache[i].PC, v810_dest);
                }

                trans_cache[j].b_bl.imm = arm_offset & 0xffffff;
            }

            drc_assemble(cache_ptr + j, &trans_cache[j]);
        }
    }

    block->size = num_arm_inst + pool_offset;

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
static WORD* drc_getEntry(WORD loc, exec_block **p_block) {
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
void drc_setEntry(WORD loc, WORD *entry, exec_block *block) {
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
    trans_cache = linearAlloc(MAX_ARM_INST*sizeof(arm_inst));

    hbHaxInit();

    cache_start = linearMemAlign(CACHE_SIZE, 0x1000);
    ReprotectMemory(cache_start, CACHE_SIZE/0x1000, 0x7);
    detectCitra(cache_start);

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
    hbHaxExit();
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
    WORD* entrypoint;
    WORD entry_PC;

    vb_state->v810_state.PC &= V810_ROM1.highaddr;

    // set up arm flags
    {
        WORD psw = vb_state->v810_state.S_REG[PSW];
        WORD cpsr;
        asm volatile ("mrs %0, CPSR" : "=r" (cpsr));
        cpsr &= 0x0fffffff;
        cpsr |= (psw & 0x3) << 30;
        cpsr |= (psw & 0xc) << 26;
        vb_state->v810_state.flags = cpsr;
    }

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

            FlushInvalidateCache(cur_block->phys_offset, cur_block->size * 4);
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

    // sync arm flags to PSW
    {
        WORD cpsr = vb_state->v810_state.flags;
        WORD psw = vb_state->v810_state.S_REG[PSW];
        psw &= ~0xf;
        psw |= cpsr >> 30;
        psw |= (cpsr >> 26) & 0xc;
        vb_state->v810_state.S_REG[PSW] = psw;
    }

    return 0;
}

void drc_loadSavedCache(void) {
    FILE* f;
    f = fopen("rom_block_map", "r");
    fread(rom_block_map, sizeof(rom_block_map[0]), BLOCK_MAP_COUNT, f);
    fclose(f);
    f = fopen("rom_entry_map", "r");
    fread(rom_entry_map, sizeof(rom_entry_map[0]), BLOCK_MAP_COUNT, f);
    fclose(f);
    f = fopen("block_heap", "r");
    fread(block_ptr_start, sizeof(exec_block*), MAX_NUM_BLOCKS, f);
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
    fwrite(block_ptr_start, sizeof(exec_block*), MAX_NUM_BLOCKS, f);
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
