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
#include <v810_cpu.h>

#include <3ds.h>

#include "v810_mem.h"
#include "v810_opt.h"

#include "arm_emit.h"

#define CACHE_SIZE  0x10000
#define MAX_INST    1024

// Write instruction to block
#define w(inst) block->phys_loc[block_pos++] = inst
// Write data to block
#define data(word) w((unsigned int)(word))

BYTE reg_usage[32];
WORD* cache_start = NULL;

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

void v810_translateBlock(exec_block* block) {
    unsigned int num_inst, i, block_pos = 0;
    bool finished = false;
    inst inst_cache[MAX_INST];
    BYTE lowB, highB, lowB2, highB2; // Up to 4 bytes for instruction (either 16 or 32 bits)
    BYTE phys_regs[32];

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

        block->cycles += opcycle[inst_cache[num_inst].opcode];

        switch(optable[inst_cache[num_inst].opcode].addr_mode) {
            case AM_I:
                inst_cache[num_inst].arg1 = (unsigned)((lowB & 0x1F));
                inst_cache[num_inst].arg2 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[num_inst].arg1]++;
                reg_usage[inst_cache[num_inst].arg2]++;
                PC += 2; // 16 bit instruction
                break;
            case AM_II:
                inst_cache[num_inst].arg1 = (unsigned)((lowB & 0x1F));
                inst_cache[num_inst].arg2 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[num_inst].arg2]++;
                PC += 2; // 16 bit instruction
                break;
            case AM_III: // Branch instructions
                inst_cache[num_inst].arg1 = (unsigned)(((highB & 0x1) << 8) + (lowB & 0xFE));
                if (inst_cache[num_inst].opcode != V810_OP_NOP) {
                    // Exit the block
                    finished = true;
                } else {
                    PC += 2;
                }
                break;
            case AM_IV: // Middle distance jump
                inst_cache[num_inst].arg1 = (unsigned)(((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2);
                // Exit the block
                finished = true;
                break;
            case AM_V:
                inst_cache[num_inst].arg3 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].arg2 = (unsigned)((lowB & 0x1F));
                inst_cache[num_inst].arg1 = (highB2 << 8) + lowB2;
                reg_usage[inst_cache[num_inst].arg2]++;
                reg_usage[inst_cache[num_inst].arg3]++;
                PC += 4; // 32 bit instruction
                break;
            case AM_VIa: // Mode6 form1
                inst_cache[num_inst].arg1 = (highB2 << 8) + lowB2;
                inst_cache[num_inst].arg2 = (unsigned)((lowB & 0x1F));
                inst_cache[num_inst].arg3 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                reg_usage[inst_cache[num_inst].arg2]++;
                reg_usage[inst_cache[num_inst].arg3]++;
                PC += 4; // 32 bit instruction
                break;
            case AM_VIb: // Mode6 form2
                inst_cache[num_inst].arg1 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].arg2 = (highB2 << 8) + lowB2; // Whats the order??? 2,3,1 or 1,3,2
                inst_cache[num_inst].arg3 = (unsigned)((lowB & 0x1F));
                reg_usage[inst_cache[num_inst].arg1]++;
                reg_usage[inst_cache[num_inst].arg3]++;
                PC += 4; // 32 bit instruction
                break;
            case AM_VII: // Unhandled
                PC +=4; // 32 bit instruction
                break;
            case AM_VIII: // Unhandled
                PC += 4; // 32 bit instruction
                break;
            case AM_IX:
                inst_cache[num_inst].arg1 = (unsigned)((lowB & 0x1)); // Mode ID, Ignore for now
                PC += 2; // 16 bit instruction
                break;
            case AM_BSTR: // Bit String Subopcodes
                inst_cache[num_inst].arg1 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].arg2 = (unsigned)((lowB & 0x1F));
                PC += 2; // 16 bit instruction
                break;
            case AM_FPP: // Floating Point Subcode
                inst_cache[num_inst].arg1 = (unsigned)((lowB >> 5) + ((highB & 0x3) << 3));
                inst_cache[num_inst].arg2 = (unsigned)((lowB & 0x1F));
                inst_cache[num_inst].arg3 = (unsigned)(((highB2 >> 2)&0x3F));
                PC += 4; // 32 bit instruction
                break;
            case AM_UDEF: // Invalid opcode.
            default: // Invalid opcode.
                PC += 2;
                break;
        }

        if (inst_cache[num_inst].opcode == V810_OP_JMP) {
                finished = true;
        }
    }

    v810_mapRegs(block);
    for (i = 0; i < 32; i++)
        phys_regs[i] = getPhysReg(i, block->reg_map);

    // Second pass: map registers and memory addresses
    for (i = 0; i < num_inst; i++) {
        switch (inst_cache[i].opcode) {
            case V810_OP_JMP: // jmp [reg1]
                // str reg1, [r11, #(33*4)] ; r11 has v810_state
                w(STR_IO(phys_regs[inst_cache[i].arg1], 11, 33*4));
                // pop {pc} (or "ldmfd sp!, {pc}")
                w(POP(1 << 15));
                break;
            case V810_OP_JR: // jr imm26
                // ldr r0, [pc, #4] ; Loads the value of (PC+8)+4 (because when it executes an instruction, pc is
                // already 2 instructions ahead), which will have the new PC
                w(LDR_IO(0, 15, 4));
                // str r0, [r11, #(33*4)] ; Save the new PC
                w(STR_IO(0, 11, 33*4));
                // pop {pc} (or "ldmfd sp!, {pc}")
                w(POP(1 << 15));

                // Save the address of the new PC at the end of the block
                data(PC + sign_26(inst_cache[i].arg1));
                break;
            case V810_OP_JAL:
                w(LDR_IO(0, 15, 12));
                w(LDR_IO(1, 15, 12));
                // Save the new PC
                w(STR_IO(0, 11, 33*4));
                // Link the return address
                if (phys_regs[31])
                    w(MOV(phys_regs[31], 1));
                else
                    w(STR_IO(1, 11, 31*4));
                w(POP(1 << 15));

                // Save the address of the new PC and the linked PC at the end
                // of the block
                data(PC + sign_26(inst_cache[i].arg1));
                data(PC + 4);
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
                w(LDR_IO(0, 11, 33*4));
                // mov r1, #(disp9 ror 9)
                w(MOV_I(1, inst_cache[i].arg1>>1, 8));
                BYTE arm_cond = cond_map[inst_cache[i].opcode & 0xF];
                // add<cond> r0, r0, r1, asr #23
                w(gen_data_proc_imm_shift(arm_cond, ARM_OP_ADD, 0, 0, 0, 23, ARM_SHIFT_ASR, 1));
                // There's no inverse condition for BR
                if (inst_cache[i].opcode != V810_OP_BR) {
                    // add<inv_cond> r0, r0, #2
                    w(gen_data_proc_imm((arm_cond + (arm_cond % 2 ? -1 : 1)), ARM_OP_ADD, 0, 0, 0, 0, 2));
                }
                // str r0, [r11, #(33*4)] ; Save the new PC
                w(STR_IO(0, 11, 33*4));
                // pop {pc} (or "ldmfd sp!, {pc}")
                w(POP(1 << 15));
                break;
            case V810_OP_MOVHI: // movhi imm16, reg1, reg2:
                // mov r0, #(imm16_hi ror 8)
                w(MOV_I(0, (inst_cache[i].arg1 >> 8), 8));
                // orr r0, #(imm16_lo ror 16)
                w(ORR_I(0, (inst_cache[i].arg1 & 0xFF), 16));
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].arg2 == 0) {
                    // mov reg2, r0
                    w(MOV(phys_regs[inst_cache[i].arg3], 0));
                } else {
                    // add reg2, r0, reg1
                    w(ADD(phys_regs[inst_cache[i].arg3], 0, phys_regs[inst_cache[i].arg2]));
                }
                break;
            case V810_OP_MOVEA: // movea imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                w(MOV_I(0, (inst_cache[i].arg1 >> 8), 8));
                // orr r0, #(imm16_lo ror 16)
                w(ORR_I(0, (inst_cache[i].arg1 & 0xFF), 16));
                // The zero-register will always be zero, so don't add it
                if (inst_cache[i].arg2 == 0)
                    // mov reg2, r0, asr #16
                    w(MOV_IS(phys_regs[inst_cache[i].arg3], 0, ARM_SHIFT_ASR, 16));
                else
                    // add reg2, reg1, r0, asr #16
                    w(ADD_IS(phys_regs[inst_cache[i].arg3], phys_regs[inst_cache[i].arg2], 0, ARM_SHIFT_ASR, 16));
                break;
            case V810_OP_MOV: // mov reg1, reg2
                if (inst_cache[i].arg1 != 0)
                    w(MOV(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                else
                    w(MOV_I(phys_regs[inst_cache[i].arg2], 0, 0));
                break;
            case V810_OP_ADD: // add reg1, reg2
                w(ADDS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_SUB: // sub reg1, reg2
                w(SUBS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_CMP: // cmp reg1, reg2
                w(CMP(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_SHL: // shl reg1, reg2
                // lsl reg2, reg2, reg1
                w(LSLS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_SHR: // shr reg1, reg2
                // lsr reg2, reg2, reg1
                w(LSRS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_SAR: // sar reg1, reg2
                // asr reg2, reg2, reg1
                w(ASRS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_MUL: // mul reg1, reg2
                // smulls RdLo, RdHi, Rm, Rs
                w(SMULLS(phys_regs[inst_cache[i].arg2], phys_regs[30], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                // If the 30th register isn't being used in the block, the high
                // word of the multiplication will be in r0 (because
                // phys_regs[30] == 0) and we'll have to save it manually
                if (!phys_regs[30]) {
                    w(STR_IO(0, 11, 30*4));
                }
                break;
            case V810_OP_OR: // or reg1, reg2
                w(ORRS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_AND: // and reg1, reg2
                w(ANDS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_XOR: // xor reg1, reg2
                w(EORS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg1]));
                break;
            case V810_OP_MOV_I: // mov imm5, reg2
                w(MOV_I(0, (sign_5(inst_cache[i].arg1) & 0xFF), 8));
                w(MOV_IS(phys_regs[inst_cache[i].arg2], 0, ARM_SHIFT_ASR, 24));
                break;
            case V810_OP_ADD_I: // add imm5, reg2
                w(MOV_I(0, (sign_5(inst_cache[i].arg1) & 0xFF), 8));
                w(ADDS_IS(phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg2], 0, ARM_SHIFT_ASR, 24));
                break;
            case V810_OP_CMP_I: // cmp imm5, reg2
                w(MOV_I(0, (sign_5(inst_cache[i].arg1) & 0xFF), 8));
                w(CMP_IS(phys_regs[inst_cache[i].arg2], 0, ARM_SHIFT_ASR, 24));
                break;
            case V810_OP_SHL_I: // shl imm5, reg2
                // lsl reg2, reg2, #imm5
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, phys_regs[inst_cache[i].arg2], inst_cache[i].arg1, ARM_SHIFT_LSL, phys_regs[inst_cache[i].arg2]));
                break;
            case V810_OP_SHR_I: // shr imm5, reg2
                // lsr reg2, reg2, #imm5
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, phys_regs[inst_cache[i].arg2], inst_cache[i].arg1, ARM_SHIFT_LSR, phys_regs[inst_cache[i].arg2]));
                break;
            case V810_OP_SAR_I: // sar imm5, reg2
                // asr reg2, reg2, #imm5
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, phys_regs[inst_cache[i].arg2], inst_cache[i].arg1, ARM_SHIFT_ASR, phys_regs[inst_cache[i].arg2]));
                break;
            case V810_OP_ANDI: // andi imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                w(MOV_I(0, (inst_cache[i].arg1 >> 8), 8));
                // orr r0, #(imm16_lo ror 16)
                w(ORR_I(0, (inst_cache[i].arg1 & 0xFF), 16));
                // asr r0, r0, #16
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0));
                // and reg2, reg1, r0
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_AND, 1, phys_regs[inst_cache[i].arg2], phys_regs[inst_cache[i].arg3], 0, 0, 0));
                break;
            case V810_OP_ORI: // ori imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                w(MOV_I(0, (inst_cache[i].arg1 >> 8), 8));
                // orr r0, #(imm16_lo ror 16)
                w(ORR_I(0, (inst_cache[i].arg1 & 0xFF), 16));
                // asr r0, r0, #16
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0));
                // orr reg2, reg1, r0
                w(ORRS(phys_regs[inst_cache[i].arg3], phys_regs[inst_cache[i].arg2], 0));
                break;
            case V810_OP_ADDI: // addi imm16, reg1, reg2
                // mov r0, #(imm16_hi ror 8)
                w(MOV_I(0, (inst_cache[i].arg1 >> 8), 8));
                // orr r0, #(imm16_lo ror 16)
                w(ORR_I(0, (inst_cache[i].arg1 & 0xFF), 16));
                // asr r0, r0, #16
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_ASR, 0));
                // add reg2, reg1, r0
                w(ADDS(phys_regs[inst_cache[i].arg3], phys_regs[inst_cache[i].arg2], 0));
                break;
            case V810_OP_LD_B: // ld.b disp16 [reg1], reg2
                if (inst_cache[i].arg2 != 0) {
                    // ldr r0, [pc, #12]
                    w(LDR_IO(0, 15, 12));
                    // add r0, r0, reg1
                    w(ADD(0, 0, phys_regs[inst_cache[i].arg2]));
                } else {
                    // ldr r0, [pc, #8]
                    w(LDR_IO(0, 15, 8));
                }
                // ldr r2, [pc, #8]
                w(LDR_IO(2, 15, 8));
                // add lr, pc, #8 ; link skipping the data at the end of the block
                w(ADD_I(14, 15, 8, 0));
                // mov pc, r2
                w(MOV(15, 2));

                data(sign_16(inst_cache[i].arg1));
                data(&mem_rbyte);

                // TODO: Figure out the sxtb opcode
                // lsl r0, r0, #8
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 8, ARM_SHIFT_LSL, 0));
                // asr reg2, r0, #8
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, phys_regs[inst_cache[i].arg3], 8, ARM_SHIFT_ASR, 0));
                break;
            case V810_OP_LD_H: // ld.h disp16 [reg1], reg2
                if (inst_cache[i].arg2 != 0) {
                    // ldr r0, [pc, #12]
                    w(LDR_IO(0, 15, 12));
                    // add r0, r0, reg1
                    w(ADD(0, 0, phys_regs[inst_cache[i].arg2]));
                } else {
                    // ldr r0, [pc, #8]
                    w(LDR_IO(0, 15, 8));
                }
                // ldr r2, [pc, #8]
                w(LDR_IO(2, 15, 8));
                // add lr, pc, #8 ; link skipping the data at the end of the block
                w(ADD_I(14, 15, 8, 0));
                // mov pc, r2
                w(MOV(15, 2));

                data(sign_16(inst_cache[i].arg1));
                data(&mem_rhword);

                // TODO: Figure out the sxth opcode
                // lsl r0, r0, #16
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, 0, 16, ARM_SHIFT_LSL, 0));
                // asr reg2, r0, #16
                w(gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, phys_regs[inst_cache[i].arg3], 16, ARM_SHIFT_ASR, 0));
                break;
            case V810_OP_LD_W: // ld.w disp16 [reg1], reg2
                if (inst_cache[i].arg2 != 0) {
                    // ldr r0, [pc, #12]
                    w(LDR_IO(0, 15, 12));
                    // add r0, r0, reg1
                    w(ADD(0, 0, phys_regs[inst_cache[i].arg2]));
                } else {
                    // ldr r0, [pc, #8]
                    w(LDR_IO(0, 15, 8));
                }
                // ldr r2, [pc, #8]
                w(LDR_IO(2, 15, 8));
                // add lr, pc, #8 ; link skipping the data at the end of the block
                w(ADD_I(14, 15, 8, 0));
                // mov pc, r2
                w(MOV(15, 2));

                data(sign_16(inst_cache[i].arg1));
                data(&mem_rword);

                // mov reg2, r0
                w(MOV(phys_regs[inst_cache[i].arg3], 0));
                break;
            case V810_OP_ST_B: // st.h reg2, disp16 [reg1]
                if (inst_cache[i].arg3 != 0) {
                    // ldr r0, [pc, #16]
                    w(LDR_IO(0, 15, 16));
                    // add r0, r0, reg1
                    w(ADD(0, 0, phys_regs[inst_cache[i].arg3]));
                } else {
                    // ldr r0, [pc, #12]
                    w(LDR_IO(0, 15, 12));
                }
                if (inst_cache[i].arg1 == 0)
                    // mov r1, #0
                    w(MOV_I(1, 0, 0));
                else
                    // mov r1, reg2
                    w(MOV(1, phys_regs[inst_cache[i].arg1]));
                // ldr r2, [pc, #8]
                w(LDR_IO(2, 15, 8));
                // add lr, pc, #8 ; link skipping the data at the end of the block
                w(ADD_I(14, 15, 8, 0));
                // mov pc, r2
                w(MOV(15, 2));

                data(sign_16(inst_cache[i].arg2));
                data(&mem_wbyte);
                break;
            case V810_OP_ST_H: // st.h reg2, disp16 [reg1]
                if (inst_cache[i].arg3 != 0) {
                    // ldr r0, [pc, #16]
                    w(LDR_IO(0, 15, 16));
                    // add r0, r0, reg1
                    w(ADD(0, 0, phys_regs[inst_cache[i].arg3]));
                } else {
                    // ldr r0, [pc, #12]
                    w(LDR_IO(0, 15, 12));
                }
                if (inst_cache[i].arg1 == 0)
                    // mov r1, #0
                    w(MOV_I(1, 0, 0));
                else
                    // mov r1, reg2
                    w(MOV(1, phys_regs[inst_cache[i].arg1]));
                // ldr r2, [pc, #8]
                w(LDR_IO(2, 15, 8));
                // add lr, pc, #8 ; link skipping the data at the end of the block
                w(ADD_I(14, 15, 8, 0));
                // mov pc, r2
                w(MOV(15, 2));

                data(sign_16(inst_cache[i].arg2));
                data(&mem_whword);
                break;
            case V810_OP_ST_W: // st.h reg2, disp16 [reg1]
                if (inst_cache[i].arg3 != 0) {
                    // ldr r0, [pc, #16]
                    w(LDR_IO(0, 15, 16));
                    // add r0, r0, reg1
                    w(ADD(0, 0, phys_regs[inst_cache[i].arg3]));
                } else {
                    // ldr r0, [pc, #12]
                    w(LDR_IO(0, 15, 12));
                }
                if (inst_cache[i].arg1 == 0)
                    // mov r1, #0
                    w(MOV_I(1, 0, 0));
                else
                    // mov r1, reg2
                    w(MOV(1, phys_regs[inst_cache[i].arg1]));
                // ldr r2, [pc, #8]
                w(LDR_IO(2, 15, 8));
                // add lr, pc, #8 ; link skipping the data at the end of the block
                w(ADD_I(14, 15, 8, 0));
                // mov pc, r2
                w(MOV(15, 2));

                data(sign_16(inst_cache[i].arg2));
                data(&mem_wword);
                break;
            case V810_OP_LDSR: // ldsr reg2, regID
                // str reg2, [r11, #((35+regID)*4)] ; Stores reg2 in v810_state->S_REG[regID]
                w(STR_IO(phys_regs[inst_cache[i].arg1], 11, (35+phys_regs[inst_cache[i].arg2])*4));
                break;
            case V810_OP_SEI: // sei
                // Sets the 12th bit in v810_state->S_REG[PSW]
                w(LDR_IO(0, 11, (35+PSW)*4));
                w(ORR_I(0, 1, 20));
                w(STR_IO(0, 11, (35+PSW)*4));
                break;
            case V810_OP_NOP:
                w(NOP());
                break;
            default:
                sprintf(str, "Unimplemented instruction: 0x%x", inst_cache[i].opcode);
                svcOutputDebugString(str, strlen(str));
                // Fill unimplemented instructions with a nop and hope the game still runs
                w(NOP());
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

        //sprintf(str, "PC - 0x%x", cur_block->end_pc);
        //svcOutputDebugString(str, strlen(str));

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

int v810_trc() {
    // Testing code
    v810_drc();

    return 0;
}
