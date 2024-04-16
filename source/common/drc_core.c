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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "utils.h"
#include "drc_alloc.h"
#include "drc_core.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_set.h"
#include "vb_gui.h"
#include "vb_types.h"

#include "arm_emit.h"
#include "arm_codegen.h"

#include "replay.h"

WORD* cache_start;
WORD* cache_pos;
int block_pos = 1;

static v810_instruction *inst_cache;
static arm_inst *trans_cache;

// Maps the most used registers in the block to V810 registers
static void drc_mapRegs(exec_block* block) {
    int i, j, max;

    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        max = 0;
        // We don't care about P_REG[0] because it will always be 0 and it will
        // be optimized out
        for (j = 1; j < 32; j++) {
            if (reg_usage[j] > max) {
                max = reg_usage[j];
                block->reg_map[i] = (BYTE) j;
            }
        }
        if (max)
            reg_usage[block->reg_map[i]] = 0;
        else
            // Use the "33rd register" as a placeholder if the register isn't
            // used in the block
            block->reg_map[i] = 32;
    }
}

// Gets the ARM register corresponding to a cached V810 register
static BYTE drc_getPhysReg(BYTE vb_reg, BYTE reg_map[]) {
    int i;
    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        if (reg_map[i] == vb_reg) {
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
            if (cur_PC < existing_block->start_pc || cur_PC > existing_block->end_pc) {
                for (WORD PC = existing_block->start_pc; PC <= existing_block->end_pc; PC += 2)
                    drc_markData(PC);
                if (existing_block->start_pc < cur_PC) cur_PC = start_PC;
            } else {
                if (existing_block->start_pc < start_PC) start_PC = existing_block->start_pc;
                if (existing_block->end_pc > end_PC) end_PC = existing_block->end_pc;
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

// Workaround for an issue where the CPSR is modified outside of the block
// before a conditional branch.
// Sets save_flags for all unconditional instructions prior to a branch.
static void drc_findLastConditionalInst(int pos) {
    bool save_flags = true, busywait = inst_cache[pos].branch_offset <= 0 && inst_cache[pos].opcode != V810_OP_SETF;
    int i;
    for (i = pos - 1; i >= 0; i--) {
        switch (inst_cache[i].opcode) {
            case V810_OP_LD_B:
            case V810_OP_LD_H:
            case V810_OP_LD_W:
            case V810_OP_IN_B:
            case V810_OP_IN_H:
            case V810_OP_IN_W:
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
            case V810_OP_ADD_I:
                // teleroboxer increments an otherwise unused memory value while busywaiting,
                // which makes it not a busywait
                // therefore we need to catch it manually
                if (tVBOpt.CRC32 == 0x36103000 && inst_cache[i].PC == 0x702e9dc)
                    break;
                // similar for wario land
                if (tVBOpt.CRC32 == 0x133E9372 && inst_cache[i].PC == 0x71c2cda)
                    break;
            case V810_OP_JAL:
                // nester's funky bowling calls a function to do its busywait read
                // and it does this several times
                if (tVBOpt.CRC32 == 0xDF4D56B4 && (
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
            default:
                if (busywait && inst_cache[i].PC < inst_cache[pos].PC + inst_cache[pos].branch_offset) {
                    dprintf(1, "busywait at %lx to %lx\n", inst_cache[pos].PC, inst_cache[pos].PC + inst_cache[pos].branch_offset);
                    inst_cache[pos].busywait = true;
                }
                return;
        }
    }
    if (busywait && inst_cache[0].PC <= inst_cache[pos].PC + inst_cache[pos].branch_offset) {
        dprintf(1, "busywait at %lx to %lx\n", inst_cache[pos].PC, inst_cache[pos].PC + inst_cache[pos].branch_offset);
        inst_cache[pos].busywait = true;
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

    WORD entry_PC = v810_state->PC;

    for (; (i < MAX_V810_INST) && (cur_PC <= end_PC); i++) {
        cur_PC = (cur_PC & V810_ROM1.highaddr);
        lowB   = ((BYTE *)(V810_ROM1.off + cur_PC))[0];
        highB  = ((BYTE *)(V810_ROM1.off + cur_PC))[1];
        lowB2  = ((BYTE *)(V810_ROM1.off + cur_PC))[2];
        highB2 = ((BYTE *)(V810_ROM1.off + cur_PC))[3];

        inst_cache[i].PC = cur_PC;
        inst_cache[i].save_flags = false;
        inst_cache[i].busywait = false;
        inst_cache[i].is_branch_target = false;

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

                // innsmouth no yakata speedhack
                if (cur_PC == 0x7019040 && (tVBOpt.CRC32 == 0x83CB6A00 || tVBOpt.CRC32 == 0xEFD0AC36 || tVBOpt.CRC32 == 0x04CCBE94)) {
                    inst_cache[i].branch_offset = -0x10;
                }

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
static int drc_translateBlock() {
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
    WORD start_PC = v810_state->PC;
    WORD end_PC;
    // For each V810 instruction, tells if either reg1 or reg2 is cached
    bool unmapped_registers;
    BYTE next_available_reg;
    // Tells if reg1 or reg2 has been modified by the current V810 instruction
    bool reg1_modified;
    bool reg2_modified;
    // The value of inst_ptr at the start of a V810 instruction
    arm_inst* inst_ptr_start;

    exec_block *block = NULL;

    WORD* pool_cache_start = NULL;
#ifdef LITERAL_POOL
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
    block->end_pc = end_PC;

    // First pass: decode V810 instructions
    num_v810_inst = drc_decodeInstructions(block, start_PC, end_PC);
    dprintf(3, "[DRC]: V810 block size - %d\n", num_v810_inst);

    // Waterworld-excluive pass: find busywaits
    if (tVBOpt.CRC32 == 0x82A95E51)
        drc_findWaterworldBusywait(num_v810_inst);

    // Second pass: map the most used V810 registers to ARM registers
    drc_mapRegs(block);
    for (i = 0; i < 32; i++)
        phys_regs[i] = drc_getPhysReg(i, block->reg_map);

    inst_ptr = &trans_cache[0];
    pool_ptr = pool_cache_start;

    #define LOAD_REG1() \
        if (arm_reg1 < 4) { \
            if (inst_cache[i].reg1) \
                LDR_IO(arm_reg1, 11, inst_cache[i].reg1 * 4); \
            else \
                MOV_I(arm_reg1, 0, 0); \
        }

    #define RELOAD_REG1(r) \
        if (arm_reg1 < 4) { \
            if (inst_cache[i].reg1) \
                LDR_IO(r, 11, inst_cache[i].reg1 * 4); \
            else \
                MOV_I(r, 0, 0); \
        } else { \
            MOV(r, arm_reg1); \
        }

    #define LOAD_REG2() \
        if (arm_reg2 < 4) { \
            if (inst_cache[i].reg2) \
                LDR_IO(arm_reg2, 11, inst_cache[i].reg2 * 4); \
            else \
                MOV_I(arm_reg2, 0, 0); \
        }

    #define RELOAD_REG2(r) \
        if (arm_reg2 < 4) { \
            if (inst_cache[i].reg2) \
                LDR_IO(r, 11, inst_cache[i].reg2 * 4); \
            else \
                MOV_I(r, 0, 0); \
        } else { \
            MOV(r, arm_reg2); \
        }

    #define SAVE_REG2(r) \
        if (arm_reg2 < 4) STR_IO(r, 11, inst_cache[i].reg2 * 4); \
        else MOV(arm_reg2, r);

    // Third pass: generate ARM instructions
    for (i = 0; i < num_v810_inst; i++) {

        // The longest replacement sequence (bitstring) is 32 ARM instructions
        if (inst_ptr - trans_cache >= MAX_ARM_INST - 32) break;

        inst_cache[i].start_pos = (HWORD) (inst_ptr - trans_cache + pool_offset);
        inst_ptr_start = inst_ptr;
        cycles += opcycle[inst_cache[i].opcode];

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
                STR_IO(arm_reg1, 11, 33 * 4);
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
                    STR_IO(0, 11, 33 * 4);
                    POP(1 << 15);
                }
                break;
            case V810_OP_JAL: // jal disp26
                LDW_I(0, inst_cache[i].PC + inst_cache[i].branch_offset);
                LDW_I(1, inst_cache[i].PC + 4);
                // Save the new PC
                STR_IO(0, 11, 33 * 4);
                // Link the return address
                if (phys_regs[31])
                    MOV(phys_regs[31], 1);
                else
                    STR_IO(1, 11, 31 * 4);
                ADDCYCLES();
                POP(1 << 15);
                break;
            case V810_OP_RETI:
                LDR_IO(0, 11, (35 + PSW) * 4);
                TST_I(0, PSW_NP >> 8, 24);
                // ldrne r1, S_REG[FEPC]
                new_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 1, (35 + FEPC) * 4);
                // ldrne r2, S_REG[FEPSW]
                new_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 2, (35 + FEPSW) * 4);
                // ldreq r1, S_REG[EIPC]
                new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 1, (35 + EIPC) * 4);
                // ldreq r2, S_REG[EIPSW]
                new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 2, (35 + EIPSW) * 4);

                STR_IO(1, 11, 33 * 4);
                STR_IO(2, 11, (35 + PSW) * 4);

                ADDCYCLES();

                // restore flags
                LDR_IO(1, 11, 70 * 4);
                MSR(1);

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
                if ((tVBOpt.CRC32 == 0x4C32BA5E || tVBOpt.CRC32 == 0x9E9B8B92) && inst_cache[i].PC == 0x07000c08) {
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
                    if (inst_cache[i].branch_offset <= 0) {
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
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].reg1 == 0) {
                    SAVE_REG2(0);
                } else {
                    LOAD_REG1();
                    ADD(arm_reg2, 0, arm_reg1);
                    reg2_modified = true;
                }

                break;
            case V810_OP_MOVEA: // movea imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].reg1 == 0) {
                    MOV_IS(arm_reg2, 0, ARM_SHIFT_ASR, 16);
                } else {
                    LOAD_REG1();
                    ADD_IS(arm_reg2, arm_reg1, 0, ARM_SHIFT_ASR, 16);
                }

                reg2_modified = true;
                break;
            case V810_OP_MOV: // mov reg1, reg2
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    SAVE_REG2(arm_reg1);
                } else {
                    MOV_I(arm_reg2, 0, 0);
                    reg2_modified = true;
                }
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
                AND_I(0, 0x1F, 0);
                LSLS(arm_reg2, arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_SHR: // shr reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                MOV(0, arm_reg1);
                AND_I(0, 0x1F, 0);
                LSRS(arm_reg2, arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_SAR: // sar reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                MOV(0, arm_reg1);
                AND_I(0, 0x1F, 0);
                ASRS(arm_reg2, arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_MUL: // mul reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                SMULLS(arm_reg2, phys_regs[30], arm_reg2, arm_reg1);
                // If the 30th register isn't being used in the block, the high
                // word of the multiplication will be in r0 (because
                // phys_regs[30] == 0) and we'll have to save it manually
                if (!phys_regs[30]) {
                    STR_IO(0, 11, 30 * 4);
                }
                reg2_modified = true;
                break;
            case V810_OP_MULU: // mul reg1, reg2
                LOAD_REG1();
                LOAD_REG2();
                UMULLS(arm_reg2, phys_regs[30], arm_reg2, arm_reg1);
                if (!phys_regs[30]) {
                    STR_IO(0, 11, 30 * 4);
                }
                reg2_modified = true;
                break;
            case V810_OP_DIV: // div reg1, reg2
                // reg2/reg1 -> reg2 (__divsi3)
                // reg2%reg1 -> r30 (__modsi3)
                RELOAD_REG2(0);
                RELOAD_REG1(1);
                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_MODSI*4, 0);
                BLX(ARM_COND_AL, 2);

                RELOAD_REG1(1);
                RELOAD_REG2(2);
                if (!phys_regs[30])
                    STR_IO(0, 11, 30 * 4);
                else
                    MOV(phys_regs[30], 0);
                MOV(0, 2);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_DIVSI*4, 0);
                BLX(ARM_COND_AL, 2);

                SAVE_REG2(0);

                // flags
                ORRS(0, 0, 0);

                break;
            case V810_OP_DIVU: // divu reg1, reg2
                RELOAD_REG2(0);
                RELOAD_REG1(1);
                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_UMODSI*4, 0);
                BLX(ARM_COND_AL, 2);

                RELOAD_REG1(1);
                RELOAD_REG2(2);
                if (!phys_regs[30])
                    STR_IO(0, 11, 30 * 4);
                else
                    MOV(phys_regs[30], 0);
                MOV(0, 2);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_UDIVSI*4, 0);
                BLX(ARM_COND_AL, 2);

                SAVE_REG2(0);

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
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                MOV_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
                reg2_modified = true;
                break;
            case V810_OP_ADD_I: // add imm5, reg2
                LOAD_REG2();
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                ADDS_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_ASR, 24);
                reg2_modified = true;
                break;
            case V810_OP_CMP_I: // cmp imm5, reg2
                LOAD_REG2();
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                CMP_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
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
                LOAD_REG2();
                // lsr reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSR, arm_reg2);
                reg2_modified = true;
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                LOAD_REG2();
                // asr reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_ASR, arm_reg2);
                reg2_modified = true;
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                LOAD_REG1();
                MOV_I(0, (inst_cache[i].imm >> 8), 24);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 0);
                ANDS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                LOAD_REG1();
                MOV_I(0, (inst_cache[i].imm >> 8), 24);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 0);
                EORS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                LOAD_REG1();
                MOV_I(0, (inst_cache[i].imm >> 8), 24);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 0);
                ORRS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
                LOAD_REG1();
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                ADDS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_LD_B: // ld.b disp16 [reg1], reg2
            case V810_OP_IN_B: // in.b disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    ADD(0, 0, arm_reg1);
                }

                LDR_IO(1, 11, 69 * 4);
                ADD_I(1, 1, DRC_RELOC_RBYTE*4, 0);
                BLX(ARM_COND_AL, 1);

                if (inst_cache[i].opcode == V810_OP_IN_B) {
                    // lsl r0, r0, #24
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 24, ARM_SHIFT_LSL, 0);
                    // lsr reg2, r0, #24
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 24, ARM_SHIFT_LSR, 0);
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
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    ADD(0, 0, arm_reg1);
                }

                LDR_IO(1, 11, 69 * 4);
                ADD_I(1, 1, DRC_RELOC_RHWORD*4, 0);
                BLX(ARM_COND_AL, 1);

                if (inst_cache[i].opcode == V810_OP_IN_H) {
                    // lsl r0, r0, #16
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_LSL, 0);
                    // lsr reg2, r0, #16
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 16, ARM_SHIFT_LSR, 0);
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
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    ADD(0, 0, arm_reg1);
                }

                LDR_IO(1, 11, 69 * 4);
                ADD_I(1, 1, DRC_RELOC_RWORD*4, 0);
                BLX(ARM_COND_AL, 1);

                SAVE_REG2(0);

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
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    RELOAD_REG2(1);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_WBYTE*4, 0);
                BLX(ARM_COND_AL, 2);

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 2 cycles instead of 1
                    cycles += 1;
                }
                break;
            case V810_OP_ST_H:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_H: // out.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    RELOAD_REG2(1);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_WHWORD*4, 0);
                BLX(ARM_COND_AL, 2);

                if (i > 1 && (inst_cache[i - 1].opcode & 0x34) == 0x34 && (inst_cache[i - 1].opcode & 3) != 2) {
                    // with two consecutive stores, the second takes 2 cycles instead of 1
                    cycles += 1;
                }
                break;
            case V810_OP_ST_W:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_W: // out.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    LOAD_REG1();
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    RELOAD_REG2(1);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_WWORD*4, 0);
                BLX(ARM_COND_AL, 2);

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
                    B(ARM_COND_AL, 0);
                }
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // Stores reg2 in v810_state->S_REG[regID]
                LOAD_REG2();
                STR_IO(arm_reg2, 11, (35 + inst_cache[i].imm) * 4);
                if (inst_cache[i].imm == PSW || inst_cache[i].imm == EIPSW) {
                    // load status register
                    if (inst_cache[i].imm == PSW)
                        MRS(0);
                    else
                        LDR_IO(0, 11, 70 * 4);
                    // clear out condition flags
                    MVN_I(1, 0xf, 4);
                    AND(0, 0, 1);
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
                        STR_IO(0, 11, 70 * 4);
                }
                break;
            case V810_OP_STSR: // stsr regID, reg2
                // Loads v810_state->S_REG[regID] into reg2
                LOAD_REG2();
                LDR_IO(arm_reg2, 11, (35 + inst_cache[i].imm) * 4);
                if (inst_cache[i].imm == PSW || inst_cache[i].imm == EIPSW) {
                    // clear out condition flags
                    MVN_I(0, 0xf, 0);
                    AND(arm_reg2, 0, arm_reg2);
                    // load except flags if relevant
                    if (inst_cache[i].imm == EIPSW) {
                        MRS(0);
                        LDR_IO(1, 11, 70 * 4);
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
                // Set the 12th bit in v810_state->S_REG[PSW]
                LDR_IO(0, 11, (35 + PSW) * 4);
                ORR_I(0, 1, 20);
                STR_IO(0, 11, (35 + PSW) * 4);
                break;
            case V810_OP_CLI: // cli
                // Clear the 12th bit in v810_state->S_REG[PSW]
                LDR_IO(0, 11, (35 + PSW) * 4);
                BIC_I(0, 1, 20);
                STR_IO(0, 11, (35 + PSW) * 4);
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
                HALT(inst_cache[i].PC + 2);
                break;
            case V810_OP_BSTR:
                MOV_I(2, 31, 0);
                if (inst_cache[i].imm >= 4) {
                    // non-search, we have a destination
                    // v810 r26 -> arm r3 hi
                    if (!phys_regs[26]) LDR_IO(0, 11, 26 * 4);
                    AND(3, phys_regs[26], 2);
                    // v810 r27 -> arm r3 lo
                    if (!phys_regs[27]) LDR_IO(0, 11, 27 * 4);
                    AND(0, phys_regs[27], 2);
                    ORR_IS(3, 0, 3, ARM_SHIFT_LSL, 16);
                } else {
                    // search, we only have a source
                    // v810 r27 -> arm r3 lo
                    if (!phys_regs[27]) LDR_IO(0, 11, 27 * 4);
                    AND(3, phys_regs[27], 2);
                }

                // mov r2, ~3
                new_data_proc_imm(ARM_COND_AL, ARM_OP_MVN, 0, 0, 2, 0, 3);
                if (inst_cache[i].imm >= 4) {
                    // non-search, clear the bottom two bits
                    // v810 r29 & (~3) -> arm r1
                    if (!phys_regs[29]) LDR_IO(0, 11, 29 * 4);
                    AND(1, phys_regs[29], 2);
                } else {
                    // search, leave as-is
                    // v810 r29 -> arm r1
                    if (!phys_regs[29]) LDR_IO(1, 11, 29 * 4);
                    else MOV(1, phys_regs[29]);
                }

                // v810 r30 & (~3) -> arm r0
                if (!phys_regs[30]) LDR_IO(0, 11, 30 * 4);
                AND(0, phys_regs[30], 2);

                // v810 r28 -> arm r2
                if (!phys_regs[28]) LDR_IO(2, 11, 28 * 4);
                else MOV(2, phys_regs[28]);

                // call the function
                PUSH(1<<5);
                LDR_IO(5, 11, 69 * 4);
                ADD_I(5, 5, (DRC_RELOC_BSTR+inst_cache[i].imm)*4, 0);
                BLX(ARM_COND_AL, 5);
                POP(1<<5);
                // reload registers
                for (int j = inst_cache[i].imm >= 4 ? 26 : 27; j <= 30; j++)
                    if (phys_regs[j])
                        LDR_IO(phys_regs[j], 11, j * 4);
                // zero flag
                if (inst_cache[i].imm < 4) {
                    ORRS(0, 0, 0);
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
                case V810_OP_TRNC_SW:
                    LOAD_REG1();
                    VMOV_SR(0, arm_reg1);
                    TRUNC(0, 0);
                    VMOV_RS(arm_reg2, 0);
                    ORRS(arm_reg2, arm_reg2, arm_reg2);
                    reg2_modified = true;
                    break;
                default:
                    // TODO: Implement me!
                    LOAD_REG1();
                    MOV_IS(0, arm_reg1, 0, 0);
                    RELOAD_REG2(1);
                    LDR_IO(2, 11, 69 * 4);
                    ADD_I(2, 2, (DRC_RELOC_FPP+inst_cache[i].imm)*4, 0);
                    BLX(ARM_COND_AL, 2);
                    MOV_IS(arm_reg2, 0, 0, 0);
                    reg2_modified = true;
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
                STR_IO(arm_reg1, 11, inst_cache[i].reg1 * 4);
            if (arm_reg2 < 4 && reg2_modified)
                STR_IO(arm_reg2, 11, inst_cache[i].reg2 * 4);
        }

        if (i + 1 < num_v810_inst) {
            if ((tVBOpt.CRC32 == 0x8989FE0A || tVBOpt.CRC32 == 0x1B1E5CB7) && inst_cache[i + 1].PC == 0x07002446) {
                // virtual lab hack
                // interrupts don't save registers, and clearing levels relies on
                // registers getting dirty
                HALT(0x07002446);
            } else if (cycles >= 200) {
                HANDLEINT(inst_cache[i + 1].PC);
            } else if (cycles != 0 && inst_cache[i + 1].is_branch_target) {
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
void drc_clearCache() {
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
WORD* drc_getEntry(WORD loc, exec_block **p_block) {
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
void drc_init() {
    // V810 instructions are 16-bit aligned, so we can ignore the last bit of the PC
    rom_block_map = calloc(sizeof(rom_block_map[0]), BLOCK_MAP_COUNT);
    rom_entry_map = linearAlloc(sizeof(rom_entry_map[0]) * BLOCK_MAP_COUNT);
    rom_data_code_map = calloc(sizeof(rom_data_code_map[0]), BLOCK_MAP_COUNT >> 3);
    block_ptr_start = linearAlloc(MAX_NUM_BLOCKS*sizeof(exec_block));

    inst_cache = linearAlloc(MAX_V810_INST*sizeof(v810_instruction));
    trans_cache = linearAlloc(MAX_ARM_INST*sizeof(arm_inst));

    hbHaxInit();

    if (tVBOpt.DYNAREC) {
        cache_start = linearMemAlign(CACHE_SIZE, 0x1000);
        ReprotectMemory(cache_start, CACHE_SIZE/0x1000, 0x7);
        detectCitra(cache_start);
    } else {
        // cache_start = &cache_dump_bin;
        drc_loadSavedCache();
    }

    *cache_start = -1;
    cache_pos = cache_start + 1;
    dprintf(0, "[DRC]: cache_start = %p\n", cache_start);
}

void drc_reset() {
    memset(rom_data_code_map, 0, sizeof(rom_data_code_map[0])*(BLOCK_MAP_COUNT >> 3));
    drc_clearCache();
}

// Cleanup and exit
void drc_exit() {
    if (tVBOpt.DYNAREC)
        linearFree(cache_start);
    free(rom_block_map);
    linearFree(rom_entry_map);
    free(rom_data_code_map);
    linearFree(block_ptr_start);
    linearFree(trans_cache);
    linearFree(inst_cache);
    hbHaxExit();
}

exec_block* drc_getNextBlockStruct() {
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
int drc_run() {
    unsigned int clocks = v810_state->cycles;
    exec_block* cur_block = NULL;
    WORD* entrypoint;
    WORD entry_PC;

    // set up arm flags
    {
        WORD psw = v810_state->S_REG[PSW];
        WORD cpsr = v810_state->flags;
        cpsr &= 0x0fffffff;
        cpsr |= (psw & 0x3) << 30;
        cpsr |= (psw & 0xc) << 26;
        v810_state->flags = cpsr;
    }

    while (true) {
        serviceDisplayInt(clocks, v810_state->PC);
        do {
            tVBOpt.MAXCYCLES = serviceInt(clocks, v810_state->PC);
        } while (tVBOpt.MAXCYCLES <= 0);

        v810_state->PC &= V810_ROM1.highaddr;
        entry_PC = v810_state->PC;

        // Try to find a cached block
        // TODO: make sure we have enough free space
        entrypoint = drc_getEntry(v810_state->PC, &cur_block);
        if (tVBOpt.DYNAREC && (entrypoint == cache_start || entry_PC < cur_block->start_pc || entry_PC > cur_block->end_pc)) {
            int result = drc_translateBlock();
            if (result == DRC_ERR_CACHE_FULL || result == DRC_ERR_NO_BLOCKS) {
                drc_clearCache();
                continue;
            } else if (result) {
                return result;
            }

//            drc_dumpCache("cache_dump_rf.bin");

            entrypoint = drc_getEntry(entry_PC, &cur_block);
            dprintf(3, "[DRC]: ARM block size - %ld\n", cur_block->size);

            FlushInvalidateCache(cur_block->phys_offset, cur_block->size * 4);
        }
        dprintf(3, "[DRC]: entry - 0x%lx (0x%x)\n", entry_PC, (int)(entrypoint - cache_start)*4);
        if ((entrypoint <= cache_start) || (entrypoint > cache_start + CACHE_SIZE)) {
            dprintf(0, "Bad entry %p\n", drc_getEntry(entry_PC, NULL));
            return DRC_ERR_BAD_ENTRY;
        }

        v810_state->cycles = clocks;
        drc_executeBlock(entrypoint, cur_block);

        v810_state->PC &= V810_ROM1.highaddr;
        clocks = v810_state->cycles;

        dprintf(4, "[DRC]: end - 0x%lx\n", v810_state->PC);
        if (v810_state->PC < V810_ROM1.lowaddr || v810_state->PC > V810_ROM1.highaddr) {
            //dprintf(0, "Last entry: 0x%lx\n", entry_PC);
            //return DRC_ERR_BAD_PC;
            break;
        }

        if (v810_state->ret) {
            break;
        }
    }

    // sync arm flags to PSW
    {
        WORD cpsr = v810_state->flags;
        WORD psw = v810_state->S_REG[PSW];
        psw &= ~0xf;
        psw |= cpsr >> 30;
        psw |= (cpsr >> 26) & 0xc;
        v810_state->S_REG[PSW] = psw;
    }

    return 0;
}

void drc_loadSavedCache() {
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
    fprintf(f, "PC: 0x%08lx\n", v810_state->PC);
    for (i = 0; i < 32; i++)
        fprintf(f, "r%d: 0x%08lx\n", i, v810_state->P_REG[i]);

    for (i = 0; i < 32; i++)
        fprintf(f, "s%d: 0x%08lx\n", i, v810_state->S_REG[i]);

    fprintf(f, "Cycles: %ld\n", v810_state->cycles);
    fprintf(f, "Cache start: %p\n", cache_start);
    fprintf(f, "Cache pos: %p\n", cache_pos);

    if (tVBOpt.DEBUG) {
        debug_dumpdrccache();
        debug_dumpvbram();
    }

    replay_save("debug_replay.bin");

    fclose(f);
}
