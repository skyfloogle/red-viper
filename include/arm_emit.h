/*
 * V810 dynamic recompiler for ARM
 *
 * This file is distributed under the MIT License.
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

#ifndef ARM_EMIT_H
#define ARM_EMIT_H

#include "arm_types.h"

WORD* pool_ptr;
WORD* pool_start;
arm_inst* inst_ptr;

// Conditional instructions

// Data processing immediate
static inline void new_data_proc_imm(BYTE cond, BYTE op, BYTE s, BYTE Rn, BYTE Rd, BYTE rot, BYTE imm) {
    inst_ptr->type = ARM_DATA_PROC_IMM;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->dpi = (arm_inst_dpi) {
            op,
            s,
            Rn,
            Rd,
            rot,
            imm
    };

    inst_ptr++;
}

// Data processing immediate shift
static inline void new_data_proc_imm_shift(BYTE cond, BYTE opcode, BYTE s, BYTE Rn, BYTE Rd, BYTE shift_imm, BYTE shift, BYTE Rm) {
    inst_ptr->type = ARM_DATA_PROC_IMM_SHIFT;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->dpis = (arm_inst_dpis) {
            opcode,
            s,
            Rn,
            Rd,
            shift_imm,
            shift,
            Rm
    };
    
    inst_ptr++;
}

// Data processing register shift
static inline void new_data_proc_reg_shift(BYTE cond, BYTE opcode, BYTE s, BYTE Rn, BYTE Rd, BYTE Rs, BYTE shift, BYTE Rm) {
    inst_ptr->type = ARM_DATA_PROC_REG_SHIFT;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->dprs = (arm_inst_dprs) {
            opcode,
            s,
            Rn,
            Rd,
            Rs,
            shift,
            Rm
    };

    inst_ptr++;
}

// Multiply
static inline void new_multiply(BYTE cond, BYTE a, BYTE s, BYTE Rd, BYTE Rn, BYTE Rs, BYTE Rm) {
    inst_ptr->type = ARM_MUL;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->mul = (arm_inst_mul) {
            a,
            s,
            Rd,
            Rn,
            Rs,
            Rm
    };

    inst_ptr++;
}

// Multiply long
static inline void new_multiply_long(BYTE cond, BYTE u, BYTE a, BYTE s, BYTE RdHi, BYTE RdLo, BYTE Rn, BYTE Rm) {
    inst_ptr->type = ARM_MULL;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->mull = (arm_inst_mull) {
            u,
            a,
            s,
            RdHi,
            RdLo,
            Rn,
            Rm
    };

    inst_ptr++;
}

// Move from status register
static inline void new_move_from_cpsr(BYTE cond, BYTE r, BYTE sbo, BYTE Rd, HWORD sbz) {
    inst_ptr->type = ARM_MOV_FROM_CPSR;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->mfcpsr = (arm_inst_mfcpsr) {
            r,
            sbo,
            Rd,
            sbz
    };

    inst_ptr++;
}

// Move immediate to status register
static inline void new_move_imm_to_cpsr(BYTE cond, BYTE r, BYTE mask, BYTE sbo, BYTE rot, BYTE imm) {
    inst_ptr->type = ARM_MOV_IMM_CPSR;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->micpsr = (arm_inst_micpsr) {
            r,
            mask,
            sbo,
            rot,
            imm
    };

    inst_ptr++;
}

// Move register to status register
static inline void new_move_reg_to_cpsr(BYTE cond, BYTE r, BYTE mask, BYTE sbo, BYTE sbz, BYTE Rm) {
    inst_ptr->type = ARM_MOV_REG_CPSR;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->mrcpsr = (arm_inst_mrcpsr) {
            r,
            mask,
            sbo,
            sbz,
            Rm
    };

    inst_ptr++;
}

// Branch/exchange instruction set
static inline void new_branch_exchange(BYTE cond, BYTE sbo1, BYTE sbo2, BYTE sbo3, BYTE Rm) {
    inst_ptr->type = ARM_BR;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->br = (arm_inst_br) {
            sbo1,
            sbo2,
            sbo3,
            Rm
    };

    inst_ptr++;
}

// Load/store multiple
static inline void new_ldst_imm_off(BYTE cond, BYTE p, BYTE u, BYTE b, BYTE w, BYTE l, BYTE Rn, BYTE Rd, HWORD imm) {
    inst_ptr->type = ARM_LDST_IMM_OFF;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->ldst_io = (arm_inst_ldst_io) {
            p,
            u,
            b,
            w,
            l,
            Rn,
            Rd,
            imm
    };

    inst_ptr++;
}

// Load/store register offset
static inline void new_ldst_reg_off(BYTE cond, BYTE p, BYTE u, BYTE b, BYTE w, BYTE l, BYTE Rn, BYTE Rd, BYTE shift_imm, BYTE shift, BYTE Rm) {
    inst_ptr->type = ARM_LDST_REG_OFF;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->ldst_ro = (arm_inst_ldst_ro) {
            p,
            u,
            b,
            w,
            l,
            Rn,
            Rd,
            shift_imm,
            shift,
            Rm
    };

    inst_ptr++;
}

// Load/store halfword/signed byte
static inline void new_ldst_hb1(BYTE cond, BYTE p, BYTE u, BYTE w, BYTE l, BYTE Rn, BYTE Rd, BYTE high_off, BYTE s, BYTE h, BYTE Rm) {
    inst_ptr->type = ARM_LDST_HB1;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->ldst_hb1 = (arm_inst_ldst_hb1) {
            p,
            u,
            w,
            l,
            Rn,
            Rd,
            high_off,
            s,
            h,
            Rm
    };

    inst_ptr++;
}

// Load/store halfword/signed byte
static inline void new_ldst_hb2(BYTE cond, BYTE p, BYTE u, BYTE w, BYTE l, BYTE Rn, BYTE Rd, BYTE sbz, BYTE s, BYTE h, BYTE Rm) {
    inst_ptr->type = ARM_LDST_HB2;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->ldst_hb2 = (arm_inst_ldst_hb2) {
            p,
            u,
            w,
            l,
            Rn,
            Rd,
            sbz,
            s,
            h,
            Rm
    };

    inst_ptr++;
}

// Swab/swap byte
static inline void new_swap_byte(BYTE cond, BYTE b, BYTE Rn, BYTE Rd, BYTE sbz, BYTE Rm) {
    inst_ptr->type = ARM_SWAP;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->swp = (arm_inst_swp) {
            b,
            Rn,
            Rd,
            sbz,
            Rm
    };

    inst_ptr++;
}

// Load/store multiple
static inline void new_ldst_multiple(BYTE cond, BYTE p, BYTE u, BYTE s, BYTE w, BYTE l, BYTE Rn, HWORD regs) {
    inst_ptr->type = ARM_LDST_MULT;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->ldstm = (arm_inst_ldstm) {
            p,
            u,
            s,
            w,
            l,
            Rn,
            regs
    };

    inst_ptr++;
}

// Branch and branch with link
static inline void new_branch_link(BYTE cond, BYTE l, WORD imm) {
    inst_ptr->type = ARM_BRANCH_LINK;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->b_bl = (arm_inst_b_bl) {
            l,
            imm
    };

    inst_ptr++;
}

/**
* Higher level macros
*/

// pop {<regs>}
#define POP(regs) \
    new_ldst_multiple(ARM_COND_AL, 0, 1, 0, 1, 1, 13, regs)

// str Rd, [Rn, #off]
// Store with immediate offset
#define STR_IO(Rd, Rn, off) \
    new_ldst_imm_off(ARM_COND_AL, 1, 1, 0, 0, 0, Rn, Rd, off)

// str Rd, [Rn, #off]
// Load with immediate offset
#define LDR_IO(Rd, Rn, off) \
    new_ldst_imm_off(ARM_COND_AL, 1, 1, 0, 0, 1, Rn, Rd, off)

// cmp Rn, imm8, ror #rot
// Compare immediate
// imm8 can be rotated an even number of times
#define CMP_I(Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_CMP, 1, Rn, 0, rot>>1, imm8)

// tst Rn, imm8, ror #rot
// Test immediate
// imm8 can be rotated an even number of times
#define TST_I(Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_TST, 1, Rn, 0, rot>>1, imm8)

// mov Rd, imm8, ror #rot
// Mov immediate
// imm8 can be rotated an even number of times
#define MOV_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, rot>>1, imm8)

// add Rd, Rn, imm8, ror #rot
// Add immediate
#define ADD_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ADD, 0, Rn, Rd, rot, imm8)

// orr Rd, imm, ror #rot
// Or immediate
// imm8 can be rotated an even number of times
#define ORR_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ORR, 0, Rd, Rd, rot>>1, imm8)

// bic Rd, imm, ror #rot
// Bit clear immediate
// imm8 can be rotated an even number of times
#define BIC_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_BIC, 0, Rd, Rd, rot>>1, imm8)

// add Rd, Rn, Rm, shift #shift_imm
// Add immediate shift
#define ADD_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ADD, 0, Rn, Rd, shift_imm, shift, Rm)

// cmp Rn, Rm, shift #shift_imm
// Compare immediate shift
#define CMP_IS(Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_CMP, 1, Rn, 0, shift_imm, shift, Rm)

// mov Rs, Rm, shift #shift_imm
// Mov immediate shift
#define MOV_IS(Rd, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, shift_imm, shift, Rm)

// orr Rd, Rn, Rm, shift #shift_imm
// Or immediate shift
#define ORR_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ORR, 0, Rn, Rd, shift_imm, shift, Rm)

// eor Rd, Rn, Rm, shift #shift_imm
// XOR immediate shift
#define EOR_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_EOR, 0, Rn, Rd, shift_imm, shift, Rm)

// movs Rd, imm8, ror #rot
// Mov immediate
// imm8 can be rotated an even number of times
#define MOVS_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, rot>>1, imm8)

// orrs Rd, imm, ror #rot
// Or immediate
// imm8 can be rotated an even number of times
#define ORRS_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ORR, 1, Rd, Rd, rot>>1, imm8)

// rsbs Rd, Rn, imm8, ror #rot
// Reverse substract immediate
// imm8 can be rotated an even number of times
#define RSBS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_RSB, 1, Rn, Rd, rot>>1, imm8)

// adds Rd, Rn, Rm, shift #shift_imm
// Add immediate shift
#define ADDS_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ADD, 1, Rn, Rd, shift_imm, shift, Rm)

// ands Rd, Rn, Rm, shift #shift_imm
// And immediate shift
#define ANDS_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_AND, 1, Rn, Rd, shift_imm, shift, Rm)

// subs Rd, Rn, Rm, shift #shift_imm
// Substract immediate shift
#define SUBS_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_SUB, 1, Rn, Rd, shift_imm, shift, Rm)

// movs Rd, Rm, shift #shift_imm
// Mov immediate shift
#define MOVS_IS(Rd, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, shift_imm, shift, Rm)

// mvns Rd, Rm, shift #shift_imm
// Move and NOT immediate shift
#define MVNS_IS(Rd, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MVN, 1, 0, Rd, shift_imm, shift, Rm)

// orrs Rd, Rn, Rm, shift #shift_imm
// Or immediate shift
#define ORRS_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ORR, 1, Rn, Rd, shift_imm, shift, Rm)

// eors Rd, Rn, Rm, shift #shift_imm
// XOR immediate shift
#define EORS_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_EOR, 1, Rn, Rd, shift_imm, shift, Rm)

// add Rd, Rn, Rm
#define ADD(Rd, Rn, Rm) \
    ADD_IS(Rd, Rn, Rm, 0, 0)

// cmp Rn, Rm
#define CMP(Rn, Rm) \
    CMP_IS(Rn, Rm, 0, 0)

// mov Rd, Rm
#define MOV(Rd, Rm) \
    MOV_IS(Rd, Rm, 0, 0)

// orr Rd, Rn, Rm
#define ORR(Rd, Rn, Rm) \
    ORR_IS(Rd, Rn, Rm, 0, 0)

// eor Rd, Rn, Rm
#define EOR(Rd, Rn, Rm) \
    EOR_IS(Rd, Rn, Rm, 0, 0)

// lsl Rd, Rn, Rm
#define LSL(Rd, Rm, Rs) \
    new_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, Rs, ARM_SHIFT_LSL, Rm)

// lsr Rd, Rn, Rm
#define LSR(Rd, Rm, Rs) \
    new_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, Rs, ARM_SHIFT_LSR, Rm)

// adds Rd, Rn, Rm
#define ADDS(Rd, Rn, Rm) \
    ADDS_IS(Rd, Rn, Rm, 0, 0)

// ands Rd, Rn, Rm
#define ANDS(Rd, Rn, Rm) \
    ANDS_IS(Rd, Rn, Rm, 0, 0)

// subs Rd, Rn, Rm
#define SUBS(Rd, Rn, Rm) \
    SUBS_IS(Rd, Rn, Rm, 0, 0)

// movs Rd, Rm
#define MOVS(Rd, Rm) \
    MOVS_IS(Rd, Rm, 0, 0)

// mvn Rd, Rm
#define MVNS(Rd, Rm) \
    MVNS_IS(Rd, Rm, 0, 0)

// orrs Rd, Rn, Rm
#define ORRS(Rd, Rn, Rm) \
    ORRS_IS(Rd, Rn, Rm, 0, 0)

// eors Rd, Rn, Rm
#define EORS(Rd, Rn, Rm) \
    EORS_IS(Rd, Rn, Rm, 0, 0)

// lsls Rd, Rn, Rm
#define LSLS(Rd, Rm, Rs) \
    new_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, Rs, ARM_SHIFT_LSL, Rm)

// lsrs Rd, Rn, Rm
#define LSRS(Rd, Rm, Rs) \
    new_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, Rs, ARM_SHIFT_LSR, Rm)

// asrs Rd, Rn, Rm
#define ASRS(Rd, Rm, Rs) \
    new_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, Rs, ARM_SHIFT_ASR, Rm)

// smulls RdLo, RdHi, Rn, Rm
// Signed multiply long
#define SMULLS(RdLo, RdHi, Rn, Rm) \
    new_multiply_long(ARM_COND_AL, 1, 0, 1, RdHi, RdLo, Rn, Rm)

// nop
#define NOP() \
    MOV(0, 0)

// b<cond> imm
// Branch
#define B(cond, imm) \
    new_branch_link(cond, 0, imm)

// bl<cond> imm
// Branch
#define BL(cond, imm) \
    new_branch_link(cond, 1, imm)

// Load word into register using a literal pool
#ifdef LITERAL_POOL
#define LDW_I(reg, word) { \
    *(pool_ptr++) = word; \
    LDR_IO(reg, 15, 0); \
    (inst_ptr-1)->needs_pool = true; \
    (inst_ptr-1)->pool_start = pool_start; \
    (inst_ptr-1)->pool_pos = pool_ptr - pool_start; \
}
#else
#define LDW_I(reg, word) { \
    MOV_I(reg, (WORD)(word) & 0x000000FF, 0); \
    if ((WORD)(word) & 0x0000FF00) \
        ORR_I(reg, ((WORD)(word) & 0x0000FF00)>>8, 24); \
    if ((WORD)(word) & 0x00FF0000) \
        ORR_I(reg, ((WORD)(word) & 0x00FF0000)>>16, 16); \
    if ((WORD)(word) & 0xFF000000) \
        ORR_I(reg, ((WORD)(word) & 0xFF000000)>>24, 8); \
}
#endif

#endif