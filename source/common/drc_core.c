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
#include "drc_core.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_set.h"
#include "vb_gui.h"
#include "vb_types.h"

#include "arm_emit.h"
#include "arm_codegen.h"

WORD* cache_start;
WORD* cache_pos;
int block_pos = 0;

// Maps the most used registers in the block to V810 registers
void drc_mapRegs(exec_block* block) {
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
BYTE drc_getPhysReg(BYTE vb_reg, BYTE reg_map[]) {
    int i;
    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        if (reg_map[i] == vb_reg) {
            // The first usable register will be r4
            return (BYTE) (i + ARM_CACHE_REG_START);
        }
    }
    return 0;
}

// Finds the starting and ending address of a V810 code block. It stops after a
// jmp, jal, reti or a long jr unless it branches further.
void drc_scanBlockBounds(WORD* p_start_PC, WORD* p_end_PC) {
    WORD start_PC = *p_start_PC & V810_ROM1.highaddr;
    WORD end_PC = start_PC;
    WORD cur_PC = start_PC;
    WORD branch_addr;
    int branch_offset;
    BYTE opcode;
    bool finished = false;
    BYTE lowB, highB, lowB2, highB2;

    while(!finished) {
        // TODO: implement reading from RAM
        if ((cur_PC >>24) == 0x05) { // RAM
            cur_PC = (cur_PC & V810_VB_RAM.highaddr);
            lowB   = ((BYTE *)(V810_VB_RAM.off + cur_PC))[0];
            highB  = ((BYTE *)(V810_VB_RAM.off + cur_PC))[1];
            lowB2  = ((BYTE *)(V810_VB_RAM.off + cur_PC))[2];
            highB2 = ((BYTE *)(V810_VB_RAM.off + cur_PC))[3];
        } else if ((cur_PC >>24) >= 0x07) { // ROM
            cur_PC = (cur_PC & V810_ROM1.highaddr);
            lowB   = ((BYTE *)(V810_ROM1.off + cur_PC))[0];
            highB  = ((BYTE *)(V810_ROM1.off + cur_PC))[1];
            lowB2  = ((BYTE *)(V810_ROM1.off + cur_PC))[2];
            highB2 = ((BYTE *)(V810_ROM1.off + cur_PC))[3];
        } else {
            return;
        }
        if ((highB & 0xE0) == 0x80)
            opcode = highB>>1;
        else
            opcode = highB>>2;

        switch (opcode) {
            case V810_OP_JR:
                branch_offset = (signed)sign_26(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                if (abs(branch_offset) < 1024) {
                    branch_addr = cur_PC + branch_offset;
                    if (branch_addr < start_PC)
                        start_PC = branch_addr;
                    else if (branch_addr > end_PC)
                        end_PC = branch_addr;
                    break;
                }
            case V810_OP_JMP:
            case V810_OP_JAL:
            case V810_OP_RETI:
                if (cur_PC >= end_PC) {
                    end_PC = cur_PC;
                    finished = true;
                }
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
                branch_addr = cur_PC + sign_9(((highB & 0x1) << 8) + (lowB & 0xFE));

                if (branch_addr < start_PC)
                    start_PC = branch_addr;
                else if (branch_addr > end_PC)
                    end_PC = branch_addr;
                break;
        }

        cur_PC += am_size_table[optable[opcode].addr_mode];

        if (cur_PC > end_PC)
            end_PC = cur_PC;
    }

    *p_start_PC = start_PC;
    *p_end_PC = end_PC;
}

// Workaround for an issue where the CPSR is modified outside of the block
// before a conditional branch.
// Sets save_flags for all unconditional instructions prior to a branch.
void drc_findLastConditionalInst(v810_instruction *inst_cache, int pos) {
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
                inst_cache[i].save_flags = true;
                break;
            default:
                return;
        }
    }
}

// Decodes the instructions from start_PC to end_PC and stores them in
// inst_cache.
// Returns the number of instructions decoded.
unsigned int drc_decodeInstructions(exec_block *block, v810_instruction *inst_cache, WORD start_PC, WORD end_PC) {
    unsigned int i;
    // Up to 4 bytes for instruction (either 16 or 32 bits)
    BYTE lowB, highB, lowB2, highB2;
    WORD cur_PC = start_PC;

    for (i = 0; (i < MAX_INST) && (cur_PC < end_PC); i++) {
        cur_PC = (cur_PC &0x07FFFFFE);

        if ((cur_PC >>24) == 0x05) { // RAM
            cur_PC = (cur_PC & V810_VB_RAM.highaddr);
            lowB   = ((BYTE *)(V810_VB_RAM.off + cur_PC))[0];
            highB  = ((BYTE *)(V810_VB_RAM.off + cur_PC))[1];
            lowB2  = ((BYTE *)(V810_VB_RAM.off + cur_PC))[2];
            highB2 = ((BYTE *)(V810_VB_RAM.off + cur_PC))[3];
        } else if ((cur_PC >>24) >= 0x07) { // ROM
            cur_PC = (cur_PC & V810_ROM1.highaddr);
            lowB   = ((BYTE *)(V810_ROM1.off + cur_PC))[0];
            highB  = ((BYTE *)(V810_ROM1.off + cur_PC))[1];
            lowB2  = ((BYTE *)(V810_ROM1.off + cur_PC))[2];
            highB2 = ((BYTE *)(V810_ROM1.off + cur_PC))[3];
        } else {
            return 0;
        }

        inst_cache[i].PC = cur_PC;
        inst_cache[i].save_flags = false;

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
                break;
            case AM_III: // Branch instructions
                inst_cache[i].imm = (unsigned)(((highB & 0x1) << 8) + (lowB & 0xFE));
                inst_cache[i].branch_offset = sign_9(inst_cache[i].imm);

                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;

                if (inst_cache[i].opcode != V810_OP_BR &&
                    inst_cache[i].opcode != V810_OP_NOP)
                    drc_findLastConditionalInst(inst_cache, i);
                break;
            case AM_IV: // Middle distance jump
                inst_cache[i].imm = (unsigned)(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                inst_cache[i].branch_offset = (signed)sign_26(inst_cache[i].imm);

                inst_cache[i].reg1 = 0xFF;
                inst_cache[i].reg2 = 0xFF;
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
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[i].reg1 = (BYTE)((lowB & 0x1F));
                reg_usage[inst_cache[i].reg1]++;
                reg_usage[inst_cache[i].reg2]++;
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
    }

    return i;
}

// Translates a V810 block into ARM code
int drc_translateBlock(exec_block *block) {
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

    v810_instruction *inst_cache = linearAlloc(MAX_INST*sizeof(v810_instruction));
    arm_inst* trans_cache = linearAlloc(8*MAX_INST*sizeof(arm_inst));
    WORD* pool_cache_start = NULL;
#ifdef LITERAL_POOL
    pool_cache_start = linearAlloc(256*4);
#endif
    WORD pool_offset = 0;

    drc_scanBlockBounds(&start_PC, &end_PC);
    dprintf(0, "[DRC]: new block - 0x%lx->0x%lx\n", start_PC, end_PC);

    // Clear previous block register stats
    memset(reg_usage, 0, 32);

    // First pass: decode V810 instructions
    num_v810_inst = drc_decodeInstructions(block, inst_cache, start_PC, end_PC);
    dprintf(3, "[DRC]: V810 block size - %d\n", num_v810_inst);

    // Second pass: map the most used V810 registers to ARM registers
    drc_mapRegs(block);
    for (i = 0; i < 32; i++)
        phys_regs[i] = drc_getPhysReg(i, block->reg_map);

    inst_ptr = &trans_cache[0];
    pool_ptr = pool_cache_start;

    // Third pass: generate ARM instructions
    for (i = 0; i < num_v810_inst; i++) {
        inst_cache[i].start_pos = (HWORD) (inst_ptr - trans_cache + pool_offset);
        inst_ptr_start = inst_ptr;
        drc_setEntry(inst_cache[i].PC, cache_start + block->phys_offset + inst_cache[i].start_pos, block);
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
                if (inst_cache[i].reg1)
                    LDR_IO(arm_reg1, 11, inst_cache[i].reg1 * 4);
                else
                    MOV_I(arm_reg1, 0, 0);
            }
        }

        if (inst_cache[i].reg2 != 0xFF) {
            arm_reg2 = phys_regs[inst_cache[i].reg2];
            if (!arm_reg2) {
                unmapped_registers = true;
                arm_reg2 = next_available_reg++;
                if (inst_cache[i].reg2)
                    LDR_IO(arm_reg2, 11, inst_cache[i].reg2 * 4);
                else
                    MOV_I(arm_reg2, 0, 0);
            }
        }

        if (inst_cache[i].save_flags) {
            MRS(0);
            PUSH(1<<0);
        }

        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                STR_IO(arm_reg1, 11, 33 * 4);
                ADDCYCLES();
                POP(1 << 15);
                break;
            case V810_OP_JR: // jr imm26
                if (abs(inst_cache[i].branch_offset) < 1024) {
                    HANDLEINT(inst_cache[i].PC + inst_cache[i].branch_offset);
                    B(ARM_COND_AL, 0);
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
                POP(1 << 15);
                break;
            case V810_OP_BV:
            case V810_OP_BL:
            case V810_OP_BE:
            case V810_OP_BN:
            case V810_OP_BR:
            case V810_OP_BLT:
            case V810_OP_BLE:
            case V810_OP_BNV:
            case V810_OP_BNL:
            case V810_OP_BNE:
            case V810_OP_BP:
            case V810_OP_BGE:
            case V810_OP_BGT:
                arm_cond = cond_map[inst_cache[i].opcode & 0xF];
                HANDLEINT(inst_cache[i].PC);
                B(arm_cond, 0);
                break;
            // Special case: bnh and bh can't be directly translated to ARM
            case V810_OP_BNH:
                HANDLEINT(inst_cache[i].PC);
                // Branch if C == 1 or Z == 1
                B(ARM_COND_CS, 0);
                B(ARM_COND_EQ, 0);
                break;
            case V810_OP_BH:
                HANDLEINT(inst_cache[i].PC);
                // Branch if C == 0 and Z == 0
                Boff(ARM_COND_CS, 3);
                Boff(ARM_COND_EQ, 2);
                B(ARM_COND_AL, 0);
                break;
            case V810_OP_MOVHI: // movhi imm16, reg1, reg2:
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].reg1 == 0) {
                    MOV(arm_reg2, 0);
                } else {
                    ADD(arm_reg2, 0, arm_reg1);
                }

                reg2_modified = true;
                break;
            case V810_OP_MOVEA: // movea imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].reg1 == 0)
                    MOV_IS(arm_reg2, 0, ARM_SHIFT_ASR, 16);
                else
                    ADD_IS(arm_reg2, arm_reg1, 0, ARM_SHIFT_ASR, 16);

                reg2_modified = true;
                break;
            case V810_OP_MOV: // mov reg1, reg2
                if (inst_cache[i].reg1 != 0)
                    MOV(arm_reg2, arm_reg1);
                else
                    MOV_I(arm_reg2, 0, 0);

                reg2_modified = true;
                break;
            case V810_OP_ADD: // add reg1, reg2
                ADDS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_SUB: // sub reg1, reg2
                SUBS(arm_reg2, arm_reg2, arm_reg1);
                INV_CARRY();
                reg2_modified = true;
                break;
            case V810_OP_CMP: // cmp reg1, reg2
                if (inst_cache[i].reg1 == 0)
                    CMP_I(arm_reg2, 0, 0);
                else if (inst_cache[i].reg2 == 0)
                    RSBS_I(0, arm_reg1, 0, 0);
                else
                    CMP(arm_reg2, arm_reg1);
                INV_CARRY();
                break;
            case V810_OP_SHL: // shl reg1, reg2
                LSLS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_SHR: // shr reg1, reg2
                LSRS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_SAR: // sar reg1, reg2
                ASRS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_MUL: // mul reg1, reg2
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
                UMULLS(arm_reg2, phys_regs[30], arm_reg2, arm_reg1);
                if (!phys_regs[30]) {
                    STR_IO(0, 11, 30 * 4);
                }
                reg2_modified = true;
                break;
            case V810_OP_DIV: // div reg1, reg2
                // reg2/reg1 -> reg2 (__divsi3)
                // reg2%reg1 -> r30 (__modsi3)
                MOV(0, arm_reg2);
                MOV(1, arm_reg1);
                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_DIVSI*4, 0);
                BLX(ARM_COND_AL, 2);

                MOV(3, arm_reg2);
                MOV(1, arm_reg1);
                MOV(arm_reg2, 0);
                MOV(0, 3);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_MODSI*4, 0);
                BLX(ARM_COND_AL, 2);

                if (!phys_regs[30])
                    STR_IO(0, 11, 30 * 4);
                else
                    MOV(phys_regs[30], 0);

                reg2_modified = true;
                break;
            case V810_OP_DIVU: // divu reg1, reg2
                MOV(0, arm_reg2);
                MOV(1, arm_reg1);
                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_UDIVSI*4, 0);
                BLX(ARM_COND_AL, 2);

                MOV(3, arm_reg2);
                MOV(1, arm_reg1);
                MOV(arm_reg2, 0);
                MOV(0, 3);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_UMODSI*4, 0);
                BLX(ARM_COND_AL, 2);

                if (!phys_regs[30])
                    STR_IO(0, 11, 30 * 4);
                else
                    MOV(phys_regs[30], 0);
                break;
            case V810_OP_OR: // or reg1, reg2
                ORRS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_AND: // and reg1, reg2
                ANDS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_XOR: // xor reg1, reg2
                EORS(arm_reg2, arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_NOT: // not reg1, reg2
                MVNS(arm_reg2, arm_reg1);
                reg2_modified = true;
                break;
            case V810_OP_MOV_I: // mov imm5, reg2
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                MOV_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
                reg2_modified = true;
                break;
            case V810_OP_ADD_I: // add imm5, reg2
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                ADDS_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_ASR, 24);
                reg2_modified = true;
                break;
            case V810_OP_CMP_I: // cmp imm5, reg2
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                CMP_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
                INV_CARRY();
                reg2_modified = true;
                break;
            case V810_OP_SHL_I: // shl imm5, reg2
                // lsl reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSL, arm_reg2);
                reg2_modified = true;
                break;
            case V810_OP_SHR_I: // shr imm5, reg2
                // lsr reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSR, arm_reg2);
                reg2_modified = true;
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                // asr reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_ASR, arm_reg2);
                reg2_modified = true;
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 24);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 0);
                ANDS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 24);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 0);
                EORS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 24);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 0);
                ORRS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
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
                    ADD(0, 0, arm_reg1);
                }

                LDR_IO(1, 11, 69 * 4);
                ADD_I(1, 1, DRC_RELOC_RBYTE*4, 0);
                BLX(ARM_COND_AL, 1);

                if (inst_cache[i].opcode == V810_OP_LD_B) {
                    // TODO: Implement sxtb
                    // lsl r0, r0, #24
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 24, ARM_SHIFT_LSL, 0);
                    // asr reg2, r0, #24
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 24, ARM_SHIFT_ASR, 0);
                } else {
                    MOV(arm_reg2, 0);
                }
                reg2_modified = true;
                break;
            case V810_OP_LD_H: // ld.h disp16 [reg1], reg2
            case V810_OP_IN_H: // in.h disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                LDR_IO(1, 11, 69 * 4);
                ADD_I(1, 1, DRC_RELOC_RHWORD*4, 0);
                BLX(ARM_COND_AL, 1);

                if (inst_cache[i].opcode == V810_OP_LD_H) {
                    // TODO: Implement sxth
                    // lsl r0, r0, #16
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_LSL, 0);
                    // asr reg2, r0, #16
                    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 16, ARM_SHIFT_ASR, 0);
                } else {
                    MOV(arm_reg2, 0);
                }
                reg2_modified = true;
                break;
            case V810_OP_LD_W: // ld.w disp16 [reg1], reg2
            case V810_OP_IN_W: // in.w disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                LDR_IO(1, 11, 69 * 4);
                ADD_I(1, 1, DRC_RELOC_RWORD*4, 0);
                BLX(ARM_COND_AL, 1);

                MOV(arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_ST_B:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_B: // out.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    MOV(1, arm_reg2);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_WBYTE*4, 0);
                BLX(ARM_COND_AL, 2);
                break;
            case V810_OP_ST_H:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_H: // out.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    MOV(1, arm_reg2);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_WHWORD*4, 0);
                BLX(ARM_COND_AL, 2);
                break;
            case V810_OP_ST_W:  // st.h reg2, disp16 [reg1]
            case V810_OP_OUT_W: // out.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    MOV(1, arm_reg2);

                LDR_IO(2, 11, 69 * 4);
                ADD_I(2, 2, DRC_RELOC_WWORD*4, 0);
                BLX(ARM_COND_AL, 2);
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // Stores reg2 in v810_state->S_REG[regID]
                STR_IO(arm_reg2, 11, (35 + inst_cache[i].imm) * 4);
                break;
            case V810_OP_STSR: // stsr regID, reg2
                // Loads v810_state->S_REG[regID] into reg2
                LDR_IO(arm_reg2, 11, (35 + inst_cache[i].imm) * 4);
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
                MOV_I(arm_reg2, 0, 0);
                // mov<cond> reg2, 1
                new_data_proc_imm(cond_map[inst_cache[i].imm & 0xF], ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
                reg2_modified = true;
                break;
            case V810_OP_FPP:
                switch (inst_cache[i].imm) {
                case V810_OP_CVT_WS:
                    VMOV_SR(0, arm_reg1);
                    VCVT_F32_S32(0, 0);
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_CVT_SW:
                    VMOV_SR(0, arm_reg1);
                    VCVT_S32_F32(0, 0);
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_CMPF_S:
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VCMP_F32(1, 0);
                    break;
                case V810_OP_ADDF_S:
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VADD_F32(0, 1, 0);
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_SUBF_S:
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VSUB_F32(0, 1, 0);
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_MULF_S:
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VMUL_F32(0, 1, 0);
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                case V810_OP_DIVF_S:
                    VMOV_SR(0, arm_reg1);
                    VMOV_SR(1, arm_reg2);
                    VDIV_F32(0, 1, 0);
                    VMOV_RS(arm_reg2, 0);
                    reg2_modified = true;
                    break;
                default:
                    // TODO: Implement me!
                    MOV_IS(0, arm_reg1, 0, 0);
                    MOV_IS(1, arm_reg2, 0, 0);
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
    if ((cache_pos - cache_start + num_arm_inst)*4 > CACHE_SIZE) {
        err = DRC_ERR_CACHE_FULL;
        goto cleanup;
    }

    // Fourth pass: assemble and link
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
                int v810_offset = inst_cache[i].branch_offset;
                int arm_offset = (int)(drc_getEntry(inst_cache[i].PC + v810_offset, NULL) - &((cache_start + block->phys_offset)[j]) - 2);

                trans_cache[j].b_bl.imm = arm_offset & 0xffffff;
            }

            drc_assemble(&((cache_start + block->phys_offset)[j]), &trans_cache[j]);
        }
    }

    block->size = num_arm_inst + pool_offset;
    block->end_pc = v810_state->PC;

cleanup:
#ifdef LITERAL_POOL
    linearFree(pool_cache_start);
#endif
    linearFree(trans_cache);
    linearFree(inst_cache);
    return err;
}

// Clear and invalidate the dynarec cache
void drc_clearCache() {
    dprintf(0, "[DRC]: clearing cache...\n");
    cache_pos = cache_start;
    block_pos = 0;

    memset(cache_start, 0, CACHE_SIZE);
    memset(rom_block_map, 0, sizeof(WORD)*((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1));
    memset(rom_entry_map, 0, sizeof(WORD)*((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1));
    memset(ram_block_map, 0, sizeof(WORD)*((V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1));
    memset(rom_entry_map, 0, sizeof(WORD)*((V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1));

    FlushInvalidateCache();
}

// Returns the entrypoint for the V810 instruction in location loc if it exists
// and NULL if it needs to be translated. If p_block != NULL it will point to
// the block structure.
WORD* drc_getEntry(WORD loc, exec_block **p_block) {
    unsigned int map_pos;

    switch (loc>>24) {
        case 5:
            map_pos = ((loc-V810_VB_RAM.lowaddr)&V810_VB_RAM.highaddr)>>1;
            if (p_block)
                *p_block = block_ptr_start + ram_block_map[map_pos];
            return cache_start + ram_entry_map[map_pos];
        case 7:
            map_pos = ((loc-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
            if (p_block)
                *p_block = block_ptr_start + rom_block_map[map_pos];
            return cache_start + rom_entry_map[map_pos];
        default:
            return NULL;
    }
}

// Sets a new entrypoint for the V810 instruction in location loc and the
// corresponding block
void drc_setEntry(WORD loc, WORD *entry, exec_block *block) {
    unsigned int map_pos;

    switch (loc>>24) {
        case 5:
            map_pos = ((loc-V810_VB_RAM.lowaddr)&V810_VB_RAM.highaddr)>>1;
            ram_block_map[map_pos] = block - block_ptr_start;
            ram_entry_map[map_pos] = entry - cache_start;
            break;
        case 7:
            map_pos = ((loc-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
            rom_block_map[map_pos] = block - block_ptr_start;
            rom_entry_map[map_pos] = entry - cache_start;
            break;
        default:
            return;
    }
}

// Initialize the dynarec
void drc_init() {
    // V810 instructions are 16-bit aligned, so we can ignore the last bit of the PC
    rom_block_map = calloc(sizeof(WORD), (V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1);
    rom_entry_map = calloc(sizeof(WORD), (V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1);
    ram_block_map = calloc(sizeof(WORD), (V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1);
    ram_entry_map = calloc(sizeof(WORD), (V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1);
    block_ptr_start = linearAlloc(MAX_NUM_BLOCKS*sizeof(exec_block));

    hbHaxInit();

    if (tVBOpt.DYNAREC) {
        cache_start = memalign(0x1000, CACHE_SIZE);
        ReprotectMemory(cache_start, CACHE_SIZE/0x1000, 0x7);
        FlushInvalidateCache();
    } else {
        // cache_start = &cache_dump_bin;
        drc_loadSavedCache();
    }

    cache_pos = cache_start;
    dprintf(0, "[DRC]: cache_start = %p\n", cache_start);
}

// Cleanup and exit
void drc_exit() {
    if (tVBOpt.DYNAREC)
        free(cache_start);
    free(rom_block_map);
    free(rom_entry_map);
    free(ram_block_map);
    free(ram_entry_map);
    linearFree(block_ptr_start);
    hbHaxExit();
}

exec_block* drc_getNextBlockStruct() {
    if (block_pos > MAX_NUM_BLOCKS)
        return NULL;
    return &block_ptr_start[block_pos++];
}

// Run V810 code until the next frame interrupt
int drc_run() {
    static unsigned int clocks;
    exec_block* cur_block = NULL;
    WORD* entrypoint;
    WORD entry_PC;

    while (!serviceDisplayInt(clocks, v810_state->PC)) {
        serviceInt(clocks, v810_state->PC);

        v810_state->PC &= V810_ROM1.highaddr;
        entry_PC = v810_state->PC;

        // Try to find a cached block
        // TODO: make sure we have enough free space
        entrypoint = drc_getEntry(v810_state->PC, &cur_block);
        if (tVBOpt.DYNAREC && (entrypoint == cache_start)) {
            cur_block = drc_getNextBlockStruct();
            if (!cur_block)
                return DRC_ERR_NO_BLOCKS;
            cur_block->phys_offset = (uint32_t) (cache_pos - cache_start);

            if (drc_translateBlock(cur_block) == DRC_ERR_CACHE_FULL) {
                drc_clearCache();
                continue;
            }

            dprintf(3, "[DRC]: ARM block size - %ld\n", cur_block->size);
//            drc_dumpCache("cache_dump_rf.bin");
            FlushInvalidateCache();

            cache_pos += cur_block->size;
            entrypoint = drc_getEntry(entry_PC, NULL);
        }
        dprintf(3, "[DRC]: entry - 0x%lx (0x%x)\n", entry_PC, (int)(entrypoint - cache_start)*4);
        if ((entrypoint < cache_start) || (entrypoint > cache_start + CACHE_SIZE))
            return DRC_ERR_BAD_ENTRY;

        v810_state->cycles = clocks;
        drc_executeBlock(entrypoint, cur_block);

        v810_state->PC &= V810_ROM1.highaddr;
        clocks = v810_state->cycles;

        dprintf(4, "[DRC]: end - 0x%lx\n", v810_state->PC);
        if (v810_state->PC < V810_VB_RAM.lowaddr || v810_state->PC > V810_ROM1.highaddr)
            return DRC_ERR_BAD_PC;

        if (v810_state->ret) {
            v810_state->ret = 0;
            break;
        }
    }

    return 0;
}

void drc_loadSavedCache() {
    FILE* f;
    f = fopen("rom_block_map", "r");
    fread(rom_block_map, sizeof(WORD), (V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("rom_entry_map", "r");
    fread(rom_entry_map, sizeof(WORD), (V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("ram_block_map", "r");
    fread(ram_block_map, sizeof(WORD), (V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("ram_entry_map", "r");
    fread(ram_entry_map, sizeof(WORD), (V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1, f);
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
    fwrite(rom_block_map, sizeof(WORD), (V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("rom_entry_map", "w");
    fwrite(rom_entry_map, sizeof(WORD), (V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("ram_block_map", "w");
    fwrite(ram_block_map, sizeof(WORD), (V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("ram_entry_map", "w");
    fwrite(ram_entry_map, sizeof(WORD), (V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr) >> 1, f);
    fclose(f);
    f = fopen("block_heap", "w");
    fwrite(block_ptr_start, sizeof(exec_block*), MAX_NUM_BLOCKS, f);
    fclose(f);
}

void drc_dumpDebugInfo() {
    int i;
    FILE* f = fopen("debug_info.txt", "w");

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

    fclose(f);
}
