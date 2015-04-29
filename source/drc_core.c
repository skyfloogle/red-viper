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

BYTE reg_usage[32];
WORD* cache_start = NULL;

char str[32];

// It maps the most used registers in the block to V810 registers
void v810_mapRegs(exec_block* block) {
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

BYTE getPhysReg(BYTE vb_reg, BYTE reg_map[]) {
    int i;
    for (i = 0; i < 7; i++) {
        if (reg_map[i] == vb_reg) {
            // The first usable register will be r4
            return (BYTE) (i + 4);
        }
    }
    return 0;
}

void v810_scanBlockBoundaries(WORD* start_PC, WORD* end_PC) {
    *start_PC = *start_PC & V810_ROM1.highaddr;
    WORD cur_PC = *end_PC = *start_PC;
    WORD branch_addr;
    int branch_offset;
    BYTE opcode;
    bool finished = false;

    while(!finished) {
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
                if (abs(branch_offset) < 1<<10) {
                    branch_addr = cur_PC + branch_offset;
                    if (branch_addr < *start_PC)
                        *start_PC = branch_addr;
                    else if (branch_addr > *end_PC)
                        *end_PC = branch_addr;
                    break;
                }
            case V810_OP_JMP:
            case V810_OP_JAL:
            case V810_OP_RETI:
                if (cur_PC >= *end_PC) {
                    *end_PC = cur_PC;
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

                if (branch_addr < *start_PC)
                    *start_PC = branch_addr;
                else if (branch_addr > *end_PC)
                    *end_PC = branch_addr;
                break;
        }

        cur_PC += am_size_table[optable[opcode].addr_mode];

        // FIXME: Change the addressing mode
        if (optable[opcode].addr_mode == 3)
            cur_PC += 2;
        else if (optable[opcode].addr_mode == 4)
            cur_PC += 4;
        if (cur_PC > *end_PC)
            *end_PC = cur_PC;
    }
}

unsigned int v810_decodeInstructions(exec_block* block, v810_instruction *inst_cache, WORD startPC, WORD endPC) {
    unsigned int num_inst;
    BYTE lowB, highB, lowB2, highB2; // Up to 4 bytes for instruction (either 16 or 32 bits)
    WORD curPC = startPC;

    for (num_inst = 0; (num_inst < MAX_INST) && (curPC <= endPC); num_inst++) {
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

        inst_cache[num_inst].opcode = highB >> 2;
        if ((highB & 0xE0) == 0x80)                      // Special opcode format for
            inst_cache[num_inst].opcode = (highB >> 1); // type III instructions.

        if ((inst_cache[num_inst].opcode > 0x4F) || (inst_cache[num_inst].opcode < 0))
            return 0;

        switch (optable[inst_cache[num_inst].opcode].addr_mode) {
            case AM_I:
                inst_cache[num_inst].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[num_inst].reg1]++;
                reg_usage[inst_cache[num_inst].reg2]++;
                break;
            case AM_II:
                inst_cache[num_inst].imm = (unsigned)((lowB & 0x1F));
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[num_inst].reg2]++;

                inst_cache[num_inst].reg1 = (BYTE)(-1);
                break;
            case AM_III: // Branch instructions
                inst_cache[num_inst].imm = (unsigned)(((highB & 0x1) << 8) + (lowB & 0xFE));
                curPC += 2;

                inst_cache[num_inst].reg1 = (BYTE)(-1);
                inst_cache[num_inst].reg2 = (BYTE)(-1);
                break;
            case AM_IV: // Middle distance jump
                inst_cache[num_inst].imm = (unsigned)(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);

                inst_cache[num_inst].reg1 = (BYTE)(-1);
                inst_cache[num_inst].reg2 = (BYTE)(-1);
                break;
            case AM_V:
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[num_inst].imm = (highB2 << 8) + lowB2;
                reg_usage[inst_cache[num_inst].reg1]++;
                reg_usage[inst_cache[num_inst].reg2]++;
                break;
            case AM_VIa: // Mode6 form1
                inst_cache[num_inst].imm = (highB2 << 8) + lowB2;
                inst_cache[num_inst].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[num_inst].reg1]++;
                reg_usage[inst_cache[num_inst].reg2]++;
                break;
            case AM_VIb: // Mode6 form2
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].imm = (highB2 << 8) + lowB2; // Whats the order??? 2,3,1 or 1,3,2
                inst_cache[num_inst].reg1 = (BYTE)((lowB & 0x1F));
                reg_usage[inst_cache[num_inst].reg1]++;
                reg_usage[inst_cache[num_inst].reg2]++;
                break;
            case AM_VII: // Unhandled
                break;
            case AM_VIII: // Unhandled
                break;
            case AM_IX:
                inst_cache[num_inst].imm = (unsigned)((lowB & 0x1)); // Mode ID, Ignore for now

                inst_cache[num_inst].reg1 = (BYTE)(-1);
                inst_cache[num_inst].reg2 = (BYTE)(-1);
                break;
            case AM_BSTR: // Bit String Subopcodes
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].reg1 = (BYTE)((lowB & 0x1F));
                reg_usage[inst_cache[num_inst].reg1]++;
                reg_usage[inst_cache[num_inst].reg2]++;
                break;
            case AM_FPP: // Floating Point Subcode
                inst_cache[num_inst].reg2 = (BYTE)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].reg1 = (BYTE)((lowB & 0x1F));
                inst_cache[num_inst].imm = (unsigned)(((highB2 >> 2)&0x3F));
                reg_usage[inst_cache[num_inst].reg1]++;
                reg_usage[inst_cache[num_inst].reg2]++;
                break;
            case AM_UDEF: // Invalid opcode.
                inst_cache[num_inst].reg1 = (BYTE)(-1);
                inst_cache[num_inst].reg2 = (BYTE)(-1);
                break;
            default: // Invalid opcode.
                inst_cache[num_inst].reg1 = (BYTE)(-1);
                inst_cache[num_inst].reg2 = (BYTE)(-1);
                curPC += 2;
                break;
        }

        inst_cache[num_inst].PC = curPC;

        curPC += am_size_table[optable[inst_cache[num_inst].opcode].addr_mode];
        block->cycles += opcycle[inst_cache[num_inst].opcode];
    }

    return num_inst-1;
}

void v810_translateBlock(exec_block* block) {
    unsigned int num_v810_inst = 0, num_arm_inst = 0, i, j, block_pos = 0;
    bool finished = false;
    BYTE phys_regs[32];
    BYTE arm_reg1, arm_reg2;
    WORD start_PC = PC;
    WORD end_PC, block_len;

    v810_instruction *inst_cache = linearAlloc(MAX_INST*sizeof(v810_instruction));
    arm_inst* trans_cache = linearAlloc(MAX_INST*4*sizeof(arm_inst));
    WORD* pool_cache_start = NULL;
#ifdef LITERAL_POOL
    pool_cache_start = linearAlloc(256*4);
#endif
    WORD pool_offset = 0;

    v810_scanBlockBoundaries(&start_PC, &end_PC);
    sprintf(str, "BLOCK: 0x%x -> 0x%x", start_PC, end_PC);
    svcOutputDebugString(str, strlen(str));

    // Clear previous block register stats
    memset(reg_usage, 0, 32);

    num_v810_inst = v810_decodeInstructions(block, inst_cache, start_PC, end_PC);

    v810_mapRegs(block);
    for (i = 0; i < 32; i++)
        phys_regs[i] = getPhysReg(i, block->reg_map);

    inst_ptr = &trans_cache[0];
    pool_ptr = pool_cache_start;

    // Second pass: map registers and memory addresses
    for (i = 0; i < num_v810_inst; i++) {
        arm_reg1 = phys_regs[inst_cache[i].reg1];
        arm_reg2 = phys_regs[inst_cache[i].reg2];

        trans_cache[i].PC = inst_cache[i].PC;
        inst_cache[i].start_pos = (HWORD) (inst_ptr - trans_cache + pool_offset);
        arm_inst* inst_ptr_start = inst_ptr;

        bool unmapped_registers = 0;

        // Preload unmapped VB registers
        if (!arm_reg1 && arm_reg2) {
            if (inst_cache[i].reg1)
                LDR_IO(2, 11, inst_cache[i].reg1 * 4);
            else
                MOV_I(2, 0, 0);
            arm_reg1 = 2;
            unmapped_registers = 1;
        } else if (arm_reg1 && !arm_reg2) {
            if (inst_cache[i].reg2)
                LDR_IO(2, 11, inst_cache[i].reg2 * 4);
            else
                MOV_I(2, 0, 0);
            arm_reg2 = 2;
            unmapped_registers = 1;
        } else if (!arm_reg1 && !arm_reg2) {
            LDR_IO(2, 11, inst_cache[i].reg1 * 4);
            LDR_IO(3, 11, inst_cache[i].reg2 * 4);
            arm_reg1 = 2;
            arm_reg2 = 3;
            unmapped_registers = 1;
        }

        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                // str reg1, [r11, #(33*4)] ; r11 has v810_state
                STR_IO(arm_reg1, 11, 33 * 4);
                // pop {pc} (or "ldmfd sp!, {pc}")
                POP(1 << 15);
                break;
            case V810_OP_JR: // jr imm26
                // ldr r0, [pc, #4] ; Loads the value of (PC+8)+4 (because when it executes an instruction, pc is
                // already 2 instructions ahead), which will have the new PC
                LDW_I(0, PC + sign_26(inst_cache[i].imm));
                // str r0, [r11, #(33*4)] ; Save the new PC
                STR_IO(0, 11, 33 * 4);
                // pop {pc} (or "ldmfd sp!, {pc}")
                POP(1 << 15);
                break;
            case V810_OP_JAL: // jal disp26
                LDW_I(0, PC + sign_26(inst_cache[i].imm));
                LDW_I(1, PC + 4);
                // Save the new PC
                STR_IO(0, 11, 33 * 4);
                // Link the return address
                if (phys_regs[31])
                    MOV(phys_regs[31], 1);
                else
                    STR_IO(1, 11, 31 * 4);
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
                // ldr r0, [r11, #(33*4)] ; load the current PC
                LDR_IO(0, 11, 33 * 4);
                // mov r1, #(disp9 ror 9)
                MOV_I(1, inst_cache[i].imm >> 1, 8);
                BYTE arm_cond = cond_map[inst_cache[i].opcode & 0xF];
                // add<cond> r0, r0, r1, asr #23
                new_data_proc_imm_shift(arm_cond, ARM_OP_ADD, 0, 0, 0, 23, ARM_SHIFT_ASR, 1);
                // There's no inverse condition for BR
                if (inst_cache[i].opcode != V810_OP_BR) {
                    // add<inv_cond> r0, r0, #2
                    new_data_proc_imm((arm_cond + (arm_cond % 2 ? -1 : 1)), ARM_OP_ADD, 0, 0, 0, 0, 2);
                }
                // str r0, [r11, #(33*4)] ; Save the new PC
                STR_IO(0, 11, 33 * 4);
                // pop {pc} (or "ldmfd sp!, {pc}")
                POP(1 << 15);
                break;
            case V810_OP_MOVHI: // movhi imm16, reg1, reg2:
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].reg1 == 0) {
                    // mov reg2, r0
                    MOV(arm_reg2, 0);
                } else {
                    // add reg2, r0, reg1
                    ADD(arm_reg2, 0, arm_reg1);
                }
                break;
            case V810_OP_MOVEA: // movea imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].reg1 == 0)
                    // mov reg2, r0, asr #16
                    MOV_IS(arm_reg2, 0, ARM_SHIFT_ASR, 16);
                else
                    // add reg2, reg1, r0, asr #16
                    ADD_IS(arm_reg2, arm_reg1, 0, ARM_SHIFT_ASR, 16);
                break;
            case V810_OP_MOV: // mov reg1, reg2
                if (inst_cache[i].reg1 != 0)
                    MOV(arm_reg2, arm_reg1);
                else
                    MOV_I(arm_reg2, 0, 0);
                break;
            case V810_OP_ADD: // add reg1, reg2
                ADDS(arm_reg2, arm_reg2, arm_reg1);
                break;
            case V810_OP_SUB: // sub reg1, reg2
                SUBS(arm_reg2, arm_reg2, arm_reg1);
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
                // lsl reg2, reg2, reg1
                LSLS(arm_reg2, arm_reg2, arm_reg1);
                break;
            case V810_OP_SHR: // shr reg1, reg2
                // lsr reg2, reg2, reg1
                LSRS(arm_reg2, arm_reg2, arm_reg1);
                break;
            case V810_OP_SAR: // sar reg1, reg2
                // asr reg2, reg2, reg1
                ASRS(arm_reg2, arm_reg2, arm_reg1);
                break;
            case V810_OP_MUL: // mul reg1, reg2
                // smulls RdLo, RdHi, Rm, Rs
                SMULLS(arm_reg2, phys_regs[30], arm_reg2, arm_reg1);
                // If the 30th register isn't being used in the block, the high
                // word of the multiplication will be in r0 (because
                // phys_regs[30] == 0) and we'll have to save it manually
                if (!phys_regs[30]) {
                    STR_IO(0, 11, 30 * 4);
                }
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
                break;
            case V810_OP_AND: // and reg1, reg2
                ANDS(arm_reg2, arm_reg2, arm_reg1);
                break;
            case V810_OP_XOR: // xor reg1, reg2
                EORS(arm_reg2, arm_reg2, arm_reg1);
                break;
            case V810_OP_NOT: // not reg1, reg2
                MVNS(arm_reg2, arm_reg1);
                break;
            case V810_OP_MOV_I: // mov imm5, reg2
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                MOV_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
                break;
            case V810_OP_ADD_I: // add imm5, reg2
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                ADDS_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_ASR, 24);
                break;
            case V810_OP_CMP_I: // cmp imm5, reg2
                MOV_I(0, (sign_5(inst_cache[i].imm) & 0xFF), 8);
                CMP_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
                break;
            case V810_OP_SHL_I: // shl imm5, reg2
                // lsl reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSL, arm_reg2);
                break;
            case V810_OP_SHR_I: // shr imm5, reg2
                // lsr reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSR, arm_reg2);
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                // asr reg2, reg2, #imm5
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_ASR, arm_reg2);
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // ands reg2, reg1, r0
                ANDS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // eors reg2, reg1, r0
                EORS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // orr reg2, reg1, r0
                ORRS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // add reg2, reg1, r0
                ADDS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_LD_B: // ld.b disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                }

                LDW_I(2, &mem_rbyte);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                // TODO: Figure out the sxtb opcode
                // lsl r0, r0, #8
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 8, ARM_SHIFT_LSL, 0);
                // asr reg2, r0, #8
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 8, ARM_SHIFT_ASR, 0);
                break;
            case V810_OP_LD_H: // ld.h disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                }

                LDW_I(2, &mem_rhword);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                // TODO: Figure out the sxth opcode
                // lsl r0, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_LSL, 0);
                // asr reg2, r0, #16
                new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 16, ARM_SHIFT_ASR, 0);
                break;
            case V810_OP_LD_W: // ld.w disp16 [reg1], reg2
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                }

                LDW_I(2, &mem_rword);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                // mov reg2, r0
                MOV(arm_reg2, 0);
                break;
            case V810_OP_ST_B: // st.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    // mov r1, #0
                    MOV_I(1, 0, 0);
                else
                    // mov r1, reg2
                    MOV(1, arm_reg2);

                LDW_I(2, &mem_wbyte);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);
                break;
            case V810_OP_ST_H: // st.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    // mov r1, #0
                    MOV_I(1, 0, 0);
                else
                    // mov r1, reg2
                    MOV(1, arm_reg2);

                LDW_I(2, &mem_whword);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);
                break;
            case V810_OP_ST_W: // st.h reg2, disp16 [reg1]
                LDW_I(0, sign_16(inst_cache[i].imm));
                if (inst_cache[i].reg1 != 0) {
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                }

                if (inst_cache[i].reg2 == 0)
                    // mov r1, #0
                    MOV_I(1, 0, 0);
                else
                    // mov r1, reg2
                    MOV(1, arm_reg2);

                LDW_I(2, &mem_wword);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // str reg2, [r11, #((35+regID)*4)] ; Stores reg2 in v810_state->S_REG[regID]
                STR_IO(arm_reg2, 11, (35 + phys_regs[inst_cache[i].imm]) * 4);
                break;
            case V810_OP_STSR: // stsr regID, reg2
                // ldr reg2, [r11, #((35+regID)*4)] ; Loads v810_state->S_REG[regID] into reg2
                LDR_IO(arm_reg2, 11, (35 + phys_regs[inst_cache[i].imm]) * 4);
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
                sprintf(str, "Unimplemented instruction: 0x%x", inst_cache[i].opcode);
                svcOutputDebugString(str, (int) strlen(str));
                // Fill unimplemented instructions with a nop and hope the game still runs
                NOP();
                break;
        }

        if (unmapped_registers) {
            if (arm_reg1 < 4)
                STR_IO(arm_reg1, 11, inst_cache[i].reg1 * 4);
            if (arm_reg2 < 4)
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

    for (i = 0; i <= num_v810_inst; i++) {
        HWORD start_pos = inst_cache[i].start_pos;
        v810_setEntry(trans_cache[start_pos].PC, block->phys_loc + start_pos, block);
        for (j = start_pos; j < (start_pos + inst_cache[i].trans_size); j++) {
#ifdef LITERAL_POOL
            if (trans_cache[j].needs_pool) {
                // The literal pool is located at the end of the current block
                // Figure out the offset from the pc register, which points two
                // instructions ahead of the current one
                trans_cache[j].ldst_io.imm = (HWORD) ((trans_cache[j].pool_start + trans_cache[j].pool_pos) - (&block->phys_loc[j + 2]));
            }
#endif

            drc_assemble(&block->phys_loc[j], &trans_cache[j]);
        }
    }

#ifdef LITERAL_POOL
    linearFree(pool_cache_start);
#endif
    linearFree(trans_cache);
    linearFree(inst_cache);

    // In ARM mode, each instruction is 4 bytes
    block->size = num_arm_inst + pool_offset;
    block->end_pc = PC;
}

WORD* v810_getEntry(WORD loc, exec_block** block) {
    unsigned int map_pos = ((loc-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
    if (block)
        block = &block_map[map_pos];
    return entry_map[map_pos];
}

void v810_setEntry(WORD loc, WORD* entry, exec_block* block) {
    if (loc < V810_ROM1.lowaddr)
        return;

    unsigned int map_pos = ((loc-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
    block_map[map_pos] = block;
    entry_map[map_pos] = entry;
}

// V810 dynarec
void v810_drc() {
    static WORD* cache_pos = NULL;
    static unsigned int clocks;
    exec_block* cur_block;
    WORD* entrypoint;

    if (!cache_start) {
        cache_start = memalign(0x1000, CACHE_SIZE);
        cache_pos = cache_start;

        u32 pages;
        HB_ReprotectMemory(0x00108000, 10, 0x7, &pages);
        *((u32*)0x00108000) = 0xDEADBABE;
        HB_ReprotectMemory(cache_start, 10, 0x7, &pages);
        HB_FlushInvalidateCache();
    }
    if (!block_map) {
        // V810 instructions are 16-bit aligned, so we can ignore the last bit of the PC
        block_map = calloc(1, ((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1) * sizeof(exec_block**));
        entry_map = calloc(1, ((V810_ROM1.highaddr - V810_ROM1.lowaddr) >> 1) * sizeof(WORD*));
    }

    PC = (PC&0x07FFFFFE);

    while (!serviceDisplayInt(clocks)) {
        serviceInt(clocks);

        // Try to find a cached block
        entrypoint = v810_getEntry(PC, &cur_block);
        if (!entrypoint) {
            cur_block = calloc(1, sizeof(exec_block));

            cur_block->phys_loc = cache_pos;
            WORD entry_PC = PC;

            v810_translateBlock(cur_block);
            //HB_FlushInvalidateCache();
            //drc_dumpCache("cache_dump_rf.bin");

            cache_pos += cur_block->size;
            entrypoint = v810_getEntry(entry_PC, NULL);
        }
        sprintf(str, "BLOCK ENTRY - %p", entrypoint);
        svcOutputDebugString(str, strlen(str));

        //hidScanInput();
        //if (hidKeysHeld() & KEY_START) {
        //    sprintf(str, "PC - 0x%x", cur_block->end_pc);
        //    svcOutputDebugString(str, strlen(str));
        //}

        v810_state->PC = cur_block->end_pc;
        v810_executeBlock(entrypoint, cur_block);
        PC = v810_state->PC & 0xFFFFFFFE;

        clocks += cur_block->cycles;
    }
}

void drc_dumpCache(char* filename) {
    FILE* f = fopen(filename, "w");
    fwrite(cache_start, CACHE_SIZE, 1, f);
    fclose(f);
}

void vb_dumpRAM() {
    FILE* f = fopen("vb_ram.bin", "w");
    fwrite(V810_VB_RAM.pmemory, V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr,1, f);
    fclose(f);

    f = fopen("game_ram.bin", "w");
    fwrite(V810_GAME_RAM.pmemory, V810_GAME_RAM.highaddr - V810_GAME_RAM.lowaddr,1, f);
    fclose(f);
}

int v810_trc() {
    // Testing code
    v810_drc();

    return 0;
}
