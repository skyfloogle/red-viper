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

#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"

#include "arm_emit.h"

#define CACHE_SIZE  0x10000
#define MAX_INST    1024

BYTE reg_usage[32];
WORD* cache_start = NULL;

int __divsi3(int a, int b);
int __modsi3(int a, int b);
unsigned int __udivsi3(unsigned int a, unsigned int b);
unsigned int __umodsi3(unsigned int a, unsigned int b);

char str[32];

// There must be a better way to do it...
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

int numRegsUsed() {
    int i;
    int regs = 0;
    for (i = 1; i < 32; i++) {
        if (reg_usage[i])
            regs++;
    }
    return regs;
}

void v810_translateBlock(exec_block* block) {
    unsigned int num_inst, i, block_pos = 0;
    bool finished = false;
    v810_instruction inst_cache[MAX_INST];
    BYTE lowB, highB, lowB2, highB2; // Up to 4 bytes for instruction (either 16 or 32 bits)
    BYTE phys_regs[32];
    BYTE arm_reg1, arm_reg2;

    // Clear previous block register stats
    memset(reg_usage, 0, 32);

    // First pass: decode instructions
    for (num_inst = 0; (num_inst < MAX_INST) && !finished; num_inst++) {
        PC = (PC&0x07FFFFFE);

        if ((PC>>24) == 0x05) { // RAM
            PC     = (PC & V810_VB_RAM.highaddr);
            lowB   = ((BYTE *)(V810_VB_RAM.off + PC))[0];
            highB  = ((BYTE *)(V810_VB_RAM.off + PC))[1];
            lowB2  = ((BYTE *)(V810_VB_RAM.off + PC))[2];
            highB2 = ((BYTE *)(V810_VB_RAM.off + PC))[3];
        } else if ((PC>>24) >= 0x07) { // ROM
            PC     = (PC & V810_ROM1.highaddr);
            lowB   = ((BYTE *)(V810_ROM1.off + PC))[0];
            highB  = ((BYTE *)(V810_ROM1.off + PC))[1];
            lowB2  = ((BYTE *)(V810_ROM1.off + PC))[2];
            highB2 = ((BYTE *)(V810_ROM1.off + PC))[3];
        } else {
            return;
        }

        inst_cache[num_inst].opcode = highB >> 2;
        if((highB & 0xE0) == 0x80)                      // Special opcode format for
            inst_cache[num_inst].opcode = (highB >> 1); // type III instructions.

        if((inst_cache[num_inst].opcode > 0x4F) || (inst_cache[num_inst].opcode < 0))
            return;

        switch(optable[inst_cache[num_inst].opcode].addr_mode) {
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
                break;
            case AM_III: // Branch instructions
                inst_cache[num_inst].imm = (unsigned)(((highB & 0x1) << 8) + (lowB & 0xFE));
                if (inst_cache[num_inst].opcode != V810_OP_NOP) {
                    // Exit the block
                    finished = true;
                } else {
                    PC += 2;
                }
                break;
            case AM_IV: // Middle distance jump
                inst_cache[num_inst].imm = (unsigned)(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                // Exit the block
                finished = true;
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
                // Exit the block
                finished = true;
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
                break;
            default: // Invalid opcode.
                PC += 2;
                break;
        }

        if (inst_cache[num_inst].opcode == V810_OP_JMP) {
            finished = true;
        }

        if (numRegsUsed() > 7) {
            finished = true;
            inst_cache[num_inst].opcode = END_BLOCK;
        } else {
            PC += am_size_table[optable[inst_cache[num_inst].opcode].addr_mode];
            block->cycles += opcycle[inst_cache[num_inst].opcode];
        }
    }

    v810_mapRegs(block);
    for (i = 0; i < 32; i++)
        phys_regs[i] = getPhysReg(i, block->reg_map);

    // Second pass: map registers and memory addresses
    for (i = 0; i < num_inst; i++) {
        arm_reg1 = phys_regs[inst_cache[i].reg1];
        arm_reg2 = phys_regs[inst_cache[i].reg2];

        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                // str reg1, [r11, #(33*4)] ; r11 has v810_state
                STR_IO(arm_reg1, 11, 33*4);
                // pop {pc} (or "ldmfd sp!, {pc}")
                POP(1 << 15);
                break;
            case V810_OP_JR: // jr imm26
                // ldr r0, [pc, #4] ; Loads the value of (PC+8)+4 (because when it executes an instruction, pc is
                // already 2 instructions ahead), which will have the new PC
                LDR_IO(0, 15, 4);
                // str r0, [r11, #(33*4)] ; Save the new PC
                STR_IO(0, 11, 33*4);
                // pop {pc} (or "ldmfd sp!, {pc}")
                POP(1 << 15);

                // Save the address of the new PC at the end of the block
                data(PC + sign_26(inst_cache[i].imm));
                break;
            case V810_OP_JAL: // jal disp26
                LDR_IO(0, 15, 12);
                LDR_IO(1, 15, 12);
                // Save the new PC
                STR_IO(0, 11, 33*4);
                // Link the return address
                if (phys_regs[31])
                    MOV(phys_regs[31], 1);
                else
                    STR_IO(1, 11, 31*4);
                POP(1 << 15);

                // Save the address of the new PC and the linked PC at the end
                // of the block
                data(PC + sign_26(inst_cache[i].imm));
                data(PC + 4);
                break;
            case V810_OP_RETI:
                LDR_IO(0, 11, (35+PSW)*4);
                TST_I(0, PSW_NP>>8, 24);
                // ldrne r1, S_REG[FEPC]
                gen_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 1, (35+FEPC)*4);
                // ldrne r2, S_REG[FEPSW]
                gen_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 2, (35+FEPSW)*4);
                // ldreq r1, S_REG[EIPC]
                gen_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 1, (35+EIPC)*4);
                // ldreq r2, S_REG[FEPSW]
                gen_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 2, (35+EIPSW)*4);

                STR_IO(1, 11, 33*4);
                STR_IO(2, 11, (35+PSW)*4);
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
                LDR_IO(0, 11, 33*4);
                // mov r1, #(disp9 ror 9)
                MOV_I(1, inst_cache[i].imm>>1, 8);
                BYTE arm_cond = cond_map[inst_cache[i].opcode & 0xF];
                // add<cond> r0, r0, r1, asr #23
                gen_data_proc_imm_shift(arm_cond, ARM_OP_ADD, 0, 0, 0, 23, ARM_SHIFT_ASR, 1);
                // There's no inverse condition for BR
                if (inst_cache[i].opcode != V810_OP_BR) {
                    // add<inv_cond> r0, r0, #2
                    gen_data_proc_imm((arm_cond + (arm_cond % 2 ? -1 : 1)), ARM_OP_ADD, 0, 0, 0, 0, 2);
                }
                // str r0, [r11, #(33*4)] ; Save the new PC
                STR_IO(0, 11, 33*4);
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
                    STR_IO(0, 11, 30*4);
                }
                break;
            case V810_OP_DIV: // div reg1, reg2
                // reg2/reg1 -> reg2 (__divsi3)
                // reg2%reg1 -> r30 (__modsi3)
                MOV(0, arm_reg2);
                MOV(1, arm_reg1);
                LDR_IO(2, 15, 4);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                data(&__divsi3);

                MOV(3, arm_reg2);
                MOV(1, arm_reg1);
                MOV(arm_reg2, 0);
                MOV(0, 3);

                LDR_IO(2, 15, 4);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                data(&__modsi3);

                if (!phys_regs[30])
                    STR_IO(0, 11, 30*4);
                else
                    MOV(phys_regs[30], 0);
            case V810_OP_DIVU: // divu reg1, reg2
                MOV(0, arm_reg2);
                MOV(1, arm_reg1);
                LDR_IO(2, 15, 4);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                data(&__udivsi3);

                MOV(3, arm_reg2);
                MOV(1, arm_reg1);
                MOV(arm_reg2, 0);
                MOV(0, 3);

                LDR_IO(2, 15, 4);
                ADD_I(14, 15, 4, 0);
                MOV(15, 2);

                data(&__umodsi3);

                if (!phys_regs[30])
                    STR_IO(0, 11, 30*4);
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
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSL, arm_reg2);
                break;
            case V810_OP_SHR_I: // shr imm5, reg2
                // lsr reg2, reg2, #imm5
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_LSR, arm_reg2);
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                // asr reg2, reg2, #imm5
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, inst_cache[i].imm, ARM_SHIFT_ASR, arm_reg2);
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // ands reg2, reg1, r0
                ANDS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_XORI: // xori imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // eors reg2, reg1, r0
                EORS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // orr reg2, reg1, r0
                ORRS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                MOV_I(0, (inst_cache[i].imm >> 8), 8);
                // orr r0, #(imm16_lo ror 16)
                ORR_I(0, (inst_cache[i].imm & 0xFF), 16);
                // asr r0, r0, #16
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0);
                // add reg2, reg1, r0
                ADDS(arm_reg2, arm_reg1, 0);
                break;
            case V810_OP_LD_B: // ld.b disp16 [reg1], reg2
                if (inst_cache[i].reg1 != 0) {
                    // ldr r0, [pc, #12]
                    LDR_IO(0, 15, 12);
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                } else {
                    // ldr r0, [pc, #8]
                    LDR_IO(0, 15, 8);
                }
                // ldr r2, [pc, #8]
                LDR_IO(2, 15, 8);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                data(sign_16(inst_cache[i].imm));
                data(&mem_rbyte);

                // TODO: Figure out the sxtb opcode
                // lsl r0, r0, #8
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 8, ARM_SHIFT_LSL, 0);
                // asr reg2, r0, #8
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 8, ARM_SHIFT_ASR, 0);
                break;
            case V810_OP_LD_H: // ld.h disp16 [reg1], reg2
                if (inst_cache[i].reg1 != 0) {
                    // ldr r0, [pc, #12]
                    LDR_IO(0, 15, 12);
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                } else {
                    // ldr r0, [pc, #8]
                    LDR_IO(0, 15, 8);
                }
                // ldr r2, [pc, #8]
                LDR_IO(2, 15, 8);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                data(sign_16(inst_cache[i].imm));
                data(&mem_rhword);

                // TODO: Figure out the sxth opcode
                // lsl r0, r0, #16
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_LSL, 0);
                // asr reg2, r0, #16
                gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, arm_reg2, 16, ARM_SHIFT_ASR, 0);
                break;
            case V810_OP_LD_W: // ld.w disp16 [reg1], reg2
                if (inst_cache[i].reg1 != 0) {
                    // ldr r0, [pc, #12]
                    LDR_IO(0, 15, 12);
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                } else {
                    // ldr r0, [pc, #8]
                    LDR_IO(0, 15, 8);
                }
                // ldr r2, [pc, #8]
                LDR_IO(2, 15, 8);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                data(sign_16(inst_cache[i].imm));
                data(&mem_rword);

                // mov reg2, r0
                MOV(arm_reg2, 0);
                break;
            case V810_OP_ST_B: // st.h reg2, disp16 [reg1]
                if (inst_cache[i].reg1 != 0) {
                    // ldr r0, [pc, #16]
                    LDR_IO(0, 15, 16);
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                } else {
                    // ldr r0, [pc, #12]
                    LDR_IO(0, 15, 12);
                }
                if (inst_cache[i].reg2 == 0)
                    // mov r1, #0
                    MOV_I(1, 0, 0);
                else
                    // mov r1, reg2
                    MOV(1, arm_reg2);
                // ldr r2, [pc, #8]
                LDR_IO(2, 15, 8);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                data(sign_16(inst_cache[i].imm));
                data(&mem_wbyte);
                break;
            case V810_OP_ST_H: // st.h reg2, disp16 [reg1]
                if (inst_cache[i].reg1 != 0) {
                    // ldr r0, [pc, #16]
                    LDR_IO(0, 15, 16);
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                } else {
                    // ldr r0, [pc, #12]
                    LDR_IO(0, 15, 12);
                }
                if (inst_cache[i].reg2 == 0)
                    // mov r1, #0
                    MOV_I(1, 0, 0);
                else
                    // mov r1, reg2
                    MOV(1, arm_reg2);
                // ldr r2, [pc, #8]
                LDR_IO(2, 15, 8);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                data(sign_16(inst_cache[i].imm));
                data(&mem_whword);
                break;
            case V810_OP_ST_W: // st.h reg2, disp16 [reg1]
                if (arm_reg2 == 0)
                    LDR_IO(1, 11, inst_cache[i].reg2*4);

                if (inst_cache[i].reg1 != 0) {
                    // ldr r0, [pc, #16]
                    LDR_IO(0, 15, 16);
                    // add r0, r0, reg1
                    ADD(0, 0, arm_reg1);
                } else {
                    // ldr r0, [pc, #12]
                    LDR_IO(0, 15, 12);
                }
                if (inst_cache[i].reg2 == 0)
                    // mov r1, #0
                    MOV_I(1, 0, 0);
                else if (arm_reg2 == 0)
                    NOP();
                else
                    // mov r1, reg2
                    MOV(1, arm_reg2);
                // ldr r2, [pc, #8]
                LDR_IO(2, 15, 8);
                // add lr, pc, #8 ; link skipping the data at the end of the block
                ADD_I(14, 15, 8, 0);
                // mov pc, r2
                MOV(15, 2);

                data(sign_16(inst_cache[i].imm));
                data(&mem_wword);
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // str reg2, [r11, #((35+regID)*4)] ; Stores reg2 in v810_state->S_REG[regID]
                STR_IO(arm_reg2, 11, (35+phys_regs[inst_cache[i].imm])*4);
                break;
            case V810_OP_STSR: // stsr regID, reg2
                // ldr reg2, [r11, #((35+regID)*4)] ; Loads v810_state->S_REG[regID] into reg2
                LDR_IO(arm_reg2, 11, (35+phys_regs[inst_cache[i].imm])*4);
                break;
            case V810_OP_SEI: // sei
                // Sets the 12th bit in v810_state->S_REG[PSW]
                LDR_IO(0, 11, (35+PSW)*4);
                ORR_I(0, 1, 20);
                STR_IO(0, 11, (35+PSW)*4);
                break;
            case V810_OP_CLI: // cli
                // Sets the 12th bit in v810_state->S_REG[PSW]
                LDR_IO(0, 11, (35+PSW)*4);
                BIC_I(0, 1, 20);
                STR_IO(0, 11, (35+PSW)*4);
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
    }

    // In ARM mode, each instruction is 4 bytes
    block->size = block_pos;
    block->end_pc = PC;
}

// V810 dynarec
void v810_drc() {
    static int block_num = 0;
    static exec_block** block_map = NULL;
    static WORD* cache_pos = NULL;
    static unsigned int clocks;
    exec_block* cur_block;
    unsigned int map_pos;

    if (!cache_start) {
        cache_start = memalign(0x1000, CACHE_SIZE);
        cache_pos = cache_start;

        u32 pages;
        HB_ReprotectMemory(0x00108000, 10, 0x7, &pages);
        *((u32*)0x00108000) = 0xDEADBABE;
        HB_ReprotectMemory(cache_start, 10, 0x7, &pages);
        HB_FlushInvalidateCache();
    }
    if (!block_map)
        // V810 instructions are 16-bit aligned, so we can ignore the last bit of the PC
        block_map = calloc(1, ((V810_ROM1.highaddr-V810_ROM1.lowaddr)>>1)*4);

    PC = (PC&0x07FFFFFE);

    while (!serviceDisplayInt(clocks)) {
        serviceInt(clocks);

        // Try to find a cached block
        map_pos = ((PC-V810_ROM1.lowaddr)&V810_ROM1.highaddr)>>1;
        cur_block = block_map[map_pos];
        if (!cur_block) {
            cur_block = calloc(1, sizeof(exec_block));

            cur_block->phys_loc = cache_pos;
            cur_block->virt_loc = PC;

            v810_translateBlock(cur_block);
            HB_FlushInvalidateCache();

            cache_pos += cur_block->size;
            block_map[map_pos] = cur_block;
            block_num++;
        }

        //hidScanInput();
        //if (hidKeysHeld() & KEY_START) {
        //    sprintf(str, "PC - 0x%x", cur_block->end_pc);
        //    svcOutputDebugString(str, strlen(str));
        //}

        v810_state->PC = cur_block->end_pc;
        v810_executeBlock(cur_block);
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
    FILE* f = fopen("vb_ram.in", "w");
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
