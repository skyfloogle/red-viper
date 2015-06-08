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

#include <3ds.h>

#include "drc_core.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"

#include "arm_emit.h"
#include "arm_codegen.h"

// Maps the most used registers in the block to V810 registers
void drc_mapRegs(exec_block* block) {
    int i, j, max;

    for (i = 0; i < 7; i++) {
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
    for (i = 0; i < 7; i++) {
        if (reg_map[i] == vb_reg) {
            // The first usable register will be r4
            return (BYTE) (i + 4);
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

    while(!finished) {
        // TODO: implement reading from RAM
        cur_PC = cur_PC & V810_ROM1.highaddr;

        BYTE lowB   = ((BYTE *)(V810_ROM1.off + cur_PC))[0];
        BYTE highB  = ((BYTE *)(V810_ROM1.off + cur_PC))[1];
        BYTE lowB2  = ((BYTE *)(V810_ROM1.off + cur_PC))[2];
        BYTE highB2 = ((BYTE *)(V810_ROM1.off + cur_PC))[3];
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
                branch_addr = cur_PC + sign_9(((highB & 0x1) << 8) + (lowB & 0xFE)) + 2;

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

// Decodes the instructions from start_PC to end_PC and stores them in
// inst_cache.
// Returns the number of instructions decoded.
unsigned int drc_decodeInstructions(exec_block *block, v810_instruction *inst_cache, WORD start_PC, WORD end_PC) {
    unsigned int i;
    BYTE lowB, highB, lowB2, highB2; // Up to 4 bytes for instruction (either 16 or 32 bits)
    WORD curPC = start_PC;

    for (i = 0; (i < MAX_INST) && (curPC <= end_PC); i++) {
        curPC = (curPC&0x07FFFFFE);

        if ((curPC>>24) == 0x05) { // RAM
            curPC     = (curPC & V810_VB_RAM.highaddr);
            lowB   = ((BYTE *)(V810_VB_RAM.off + curPC))[0];
            highB  = ((BYTE *)(V810_VB_RAM.off + curPC))[1];
            lowB2  = ((BYTE *)(V810_VB_RAM.off + curPC))[2];
            highB2 = ((BYTE *)(V810_VB_RAM.off + curPC))[3];
        } else if ((curPC>>24) >= 0x07) { // ROM
            curPC     = (curPC & V810_ROM1.highaddr);
            lowB   = ((BYTE *)(V810_ROM1.off + curPC))[0];
            highB  = ((BYTE *)(V810_ROM1.off + curPC))[1];
            lowB2  = ((BYTE *)(V810_ROM1.off + curPC))[2];
            highB2 = ((BYTE *)(V810_ROM1.off + curPC))[3];
        } else {
            return 0;
        }

        inst_cache[i].PC = curPC;

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
                    reg_usage[inst_cache[i].reg1]++;
                } else {
                    inst_cache[i].reg2 = (BYTE)(-1);
                }
                break;
            case AM_II:
                inst_cache[i].imm = (unsigned)((lowB & 0x1F));
                inst_cache[i].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[i].reg2]++;

                inst_cache[i].reg1 = (BYTE)(-1);
                break;
            case AM_III: // Branch instructions
                inst_cache[i].imm = (unsigned)(((highB & 0x1) << 8) + (lowB & 0xFE));
                inst_cache[i].branch_offset = sign_9(inst_cache[i].imm);

                inst_cache[i].reg1 = (BYTE)(-1);
                inst_cache[i].reg2 = (BYTE)(-1);
                break;
            case AM_IV: // Middle distance jump
                inst_cache[i].imm = (unsigned)(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                inst_cache[i].branch_offset = sign_26(inst_cache[i].imm);

                inst_cache[i].reg1 = (BYTE)(-1);
                inst_cache[i].reg2 = (BYTE)(-1);
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

                inst_cache[i].reg1 = (BYTE)(-1);
                inst_cache[i].reg2 = (BYTE)(-1);
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
                inst_cache[i].reg1 = (BYTE)(-1);
                inst_cache[i].reg2 = (BYTE)(-1);
                break;
            default: // Invalid opcode.
                inst_cache[i].reg1 = (BYTE)(-1);
                inst_cache[i].reg2 = (BYTE)(-1);
                curPC += 2;
                break;
        }

        curPC += am_size_table[optable[inst_cache[i].opcode].addr_mode];
        block->cycles += opcycle[inst_cache[i].opcode];
    }

    return i - 1;
}

// Translates a V810 block into ARM code
void drc_translateBlock(exec_block *block) {
    unsigned int num_v810_inst = 0, num_arm_inst = 0, i, j, cycles = 0;
    BYTE phys_regs[32];
    BYTE arm_reg1, arm_reg2, arm_cond;
    WORD start_PC = PC;
    WORD end_PC;
    bool unmapped_registers;
    bool reg1_modified;
    bool reg2_modified;

    v810_instruction *inst_cache = linearAlloc(MAX_INST*sizeof(v810_instruction));
    arm_inst* trans_cache = linearAlloc(4*MAX_INST*sizeof(arm_inst));
    WORD* pool_cache_start = NULL;
#ifdef LITERAL_POOL
    pool_cache_start = linearAlloc(256*4);
#endif
    WORD pool_offset = 0;

    drc_scanBlockBounds(&start_PC, &end_PC);
    fprintf(stderr, "BLOCK: 0x%x -> 0x%x\n", start_PC, end_PC);

    // Clear previous block register stats
    memset(reg_usage, 0, 32);

    // First pass: decode V810 instructions
    num_v810_inst = drc_decodeInstructions(block, inst_cache, start_PC, end_PC);

    // Second pass: map registers and memory addresses
    drc_mapRegs(block);
    for (i = 0; i < 32; i++)
        phys_regs[i] = drc_getPhysReg(i, block->reg_map);

    inst_ptr = &trans_cache[0];
    pool_ptr = pool_cache_start;

    // Third pass: generate ARM instructions
    for (i = 0; i < num_v810_inst; i++) {
        if (inst_cache[i].reg1 != -1)
            arm_reg1 = phys_regs[inst_cache[i].reg1];
        else
            arm_reg1 = 0;

        if (inst_cache[i].reg2 != -1)
            arm_reg2 = phys_regs[inst_cache[i].reg2];
        else
            arm_reg1 = 0;

        inst_cache[i].start_pos = (HWORD) (inst_ptr - trans_cache + pool_offset);
        arm_inst* inst_ptr_start = inst_ptr;
        drc_setEntry(inst_cache[i].PC, block->phys_loc + inst_cache[i].start_pos, block);
        cycles += opcycle[inst_cache[i].opcode];

        unmapped_registers = false;
        reg1_modified = false;
        reg2_modified = false;

        // Preload unmapped VB registers
        if (!arm_reg1 && arm_reg2) {
            if (inst_cache[i].reg1)
                LDR_IO(2, 11, inst_cache[i].reg1 * 4);
            else
                MOV_I(2, 0, 0);
            arm_reg1 = 2;
            unmapped_registers = true;
        } else if (arm_reg1 && !arm_reg2) {
            if (inst_cache[i].reg2)
                LDR_IO(2, 11, inst_cache[i].reg2 * 4);
            else
                MOV_I(2, 0, 0);
            arm_reg2 = 2;
            unmapped_registers = true;
        } else if (!arm_reg1 && !arm_reg2) {
            if (inst_cache[i].reg2)
                LDR_IO(2, 11, inst_cache[i].reg1 * 4);
            else
                MOV_I(2, 0, 0);
            if (inst_cache[i].reg2)
                LDR_IO(3, 11, inst_cache[i].reg2 * 4);
            else
                MOV_I(3, 0, 0);
            arm_reg1 = 2;
            arm_reg2 = 3;
            unmapped_registers = true;
        }

        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                STR_IO(arm_reg1, 11, 33 * 4);
                ADDCYCLES();
                POP(1 << 15);
                break;
            case V810_OP_JR: // jr imm26
                ADDCYCLES();
                if (abs(inst_cache[i].branch_offset) < 1024) {
                    HANDLEINT(inst_cache[i].PC + inst_cache[i].branch_offset);
                    B(ARM_COND_AL, 0);
                } else {
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
                // ldreq r2, S_REG[FEPSW]
                new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 2, (35 + EIPSW) * 4);

                STR_IO(1, 11, 33 * 4);
                STR_IO(2, 11, (35 + PSW) * 4);
                POP(1 << 15);
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
                arm_cond = cond_map[inst_cache[i].opcode & 0xF];
                ADDCYCLES();
                HANDLEINT(inst_cache[i].PC);
                B(arm_cond, 0);
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
                reg2_modified = true;
                break;
            case V810_OP_CMP: // cmp reg1, reg2
                if (inst_cache[i].reg1 == 0)
                    CMP_I(arm_reg2, 0, 0);
                else if (inst_cache[i].reg2 == 0)
                    RSBS_I(0, arm_reg1, 0, 0);
                else
                    CMP(arm_reg2, arm_reg1);
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
            case V810_OP_DIV: // div reg1, reg2
                // reg2/reg1 -> reg2 (__divsi3)
                // reg2%reg1 -> r30 (__modsi3)
                MOV(0, arm_reg2);
                MOV(1, arm_reg1);
                LDW_I(2, &__divsi3);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                MOV(3, arm_reg2);
                MOV(1, arm_reg1);
                MOV(arm_reg2, 0);
                MOV(0, 3);

                LDW_I(2, &__modsi3);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                if (!phys_regs[30])
                    STR_IO(0, 11, 30 * 4);
                else
                    MOV(phys_regs[30], 0);

                reg2_modified = true;
                break;
            case V810_OP_DIVU: // divu reg1, reg2
                MOV(0, arm_reg2);
                MOV(1, arm_reg1);
                LDW_I(2, &__udivsi3);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                MOV(3, arm_reg2);
                MOV(1, arm_reg1);
                MOV(arm_reg2, 0);
                MOV(0, 3);

                LDW_I(2, &__umodsi3);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

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
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                ANDS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                EORS(arm_reg2, arm_reg1, 0);
                reg2_modified = true;
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
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
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                LDW_I(1, &mem_rbyte);
                BLX(ARM_COND_AL, 1);

                // TODO: Implement sxtb
                // lsl r0, r0, #8
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 8, ARM_SHIFT_LSL, 0);
                // asr reg2, r0, #8
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 8, ARM_SHIFT_ASR, 0);
                reg2_modified = true;
                break;
            case V810_OP_LD_H: // ld.h disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                LDW_I(1, &mem_rhword);
                BLX(ARM_COND_AL, 1);

                // TODO: Implement sxth
                // lsl r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_LSL, 0);
                // asr reg2, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 16, ARM_SHIFT_ASR, 0);
                reg2_modified = true;
                break;
            case V810_OP_LD_W: // ld.w disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                LDW_I(1, &mem_rword);
                BLX(ARM_COND_AL, 1);

                MOV(arm_reg2, 0);
                reg2_modified = true;
                break;
            case V810_OP_ST_B: // st.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    MOV(1, arm_reg2);

                LDW_I(2, &mem_wbyte);
                BLX(ARM_COND_AL, 2);
                break;
            case V810_OP_ST_H: // st.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    MOV(1, arm_reg2);

                LDW_I(2, &mem_whword);
                BLX(ARM_COND_AL, 2);
                break;
            case V810_OP_ST_W: // st.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    MOV_I(1, 0, 0);
                else
                    MOV(1, arm_reg2);

                LDW_I(2, &mem_wword);
                BLX(ARM_COND_AL, 2);
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // Stores reg2 in v810_state->S_REG[regID]
                STR_IO(arm_reg2, 11, (35 + phys_regs[inst_cache[i].imm]) * 4);
                break;
            case V810_OP_STSR: // stsr regID, reg2
                // Loads v810_state->S_REG[regID] into reg2
                LDR_IO(arm_reg2, 11, (35 + phys_regs[inst_cache[i].imm]) * 4);
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
            case V810_OP_NOP:
                NOP();
                break;
            case END_BLOCK:
                POP(1 << 15);
                break;
            default:
                fprintf(stderr, "Unimplemented instruction: 0x%x\n", inst_cache[i].opcode);
                // Fill unimplemented instructions with a nop and hope the game still runs
                NOP();
                break;
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
                int arm_offset = (int)(drc_getEntry(inst_cache[i].PC + v810_offset, NULL) - &block->phys_loc[j] - 2);

                trans_cache[j].b_bl.imm = arm_offset & 0xffffff;
            }

            drc_assemble(&block->phys_loc[j], &trans_cache[j]);
        }
    }

#ifdef LITERAL_POOL
    linearFree(pool_cache_start);
#endif
    linearFree(trans_cache);
    linearFree(inst_cache);

    block->size = num_arm_inst + pool_offset;
    block->end_pc = PC;
}

// Returns the entrypoint for the V810 instruction in location loc if it exists
// and NULL if it needs to be translated. If p_block != NULL it will point to
// the block structure.
WORD* drc_getEntry(WORD loc, exec_block **p_block) {
    unsigned int map_pos = ((loc-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
    if (p_block)
        *p_block = block_map[map_pos];
    return entry_map[map_pos];
}

// Sets a new entrypoint for the V810 instruction in location loc and the
// corresponding block
void drc_setEntry(WORD loc, WORD *entry, exec_block *block) {
    if (loc < V810_ROM1.lowaddr)
        return;

    unsigned int map_pos = ((loc-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
    block_map[map_pos] = block;
    entry_map[map_pos] = entry;
}

// Initialize the dynarec
void drc_init() {
        cache_start = memalign(0x1000, CACHE_SIZE);
        cache_pos = cache_start;

        u32 pages;
        HB_ReprotectMemory(0x00108000, 10, 0x7, &pages);
        *((u32*)0x00108000) = 0xDEADBABE;
        HB_ReprotectMemory(cache_start, 10, 0x7, &pages);
        HB_FlushInvalidateCache();

        // V810 instructions are 16-bit aligned, so we can ignore the last bit of the PC
        block_map = calloc(1, ((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1) * sizeof(exec_block**));
        entry_map = calloc(1, ((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1) * sizeof(WORD*));
}

void drc_exit() {
    free(cache_start);

    int i;
    for (i = 0; i < ((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1); i++) {
        free(block_map[i]);
    }

    free(block_map);
    free(entry_map);
}

// Run V810 code until
int drc_run() {
    static unsigned int clocks;
    exec_block* cur_block;
    WORD* entrypoint;

    PC = (PC&0x07FFFFFE);

    while (!serviceDisplayInt(clocks)) {
        serviceInt(clocks);

        WORD entry_PC = PC;

        // Try to find a cached block
        // TODO: make sure we have enough free space
        entrypoint = drc_getEntry(PC, &cur_block);
        if (!entrypoint) {
            cur_block = calloc(1, sizeof(exec_block));

            cur_block->phys_loc = cache_pos;

            drc_translateBlock(cur_block);
            //drc_dumpCache("cache_dump_rf.bin");
            //HB_FlushInvalidateCache();

            cache_pos += cur_block->size;
            entrypoint = drc_getEntry(entry_PC, NULL);
        }
        //fprintf(stderr, "BLOCK ENTRY - 0x%x (%p)\n", entry_PC, entrypoint);

        v810_state->PC = cur_block->end_pc;
        v810_state->cycles = clocks;

        drc_executeBlock(entrypoint, cur_block);

        PC = v810_state->PC & 0xFFFFFFFE;
        if (v810_state->cycles == (WORD)(-1))
            break;
        clocks = v810_state->cycles;
        //fprintf(stderr, "BLOCK END - 0x%x\n", PC);
    }

    // TODO: Handle errors
    return 0;
}

// Dumps the translation cache onto a file
void drc_dumpCache(char* filename) {
    FILE* f = fopen(filename, "w");
    fwrite(cache_start, CACHE_SIZE, 1, f);
    fclose(f);
}

// Dumps the VB RAM and the game RAM onto vb_ram.bin and game_ram.bin
void vb_dumpRAM() {
    FILE* f = fopen("vb_ram.bin", "w");
    fwrite(V810_VB_RAM.pmemory, V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr,1, f);
    fclose(f);

    f = fopen("game_ram.bin", "w");
    fwrite(V810_GAME_RAM.pmemory, V810_GAME_RAM.highaddr - V810_GAME_RAM.lowaddr,1, f);
    fclose(f);
}

