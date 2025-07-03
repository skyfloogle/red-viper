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

#ifdef LITERAL_POOL
WORD* pool_ptr;
WORD* pool_start;
#endif
extern arm_inst* inst_ptr;

// Conditional instructions

// Data processing immediate
static inline void new_data_proc_imm(BYTE cond, BYTE op, BYTE s, BYTE Rn, BYTE Rd, BYTE rot, BYTE imm) {
    inst_ptr->type = ARM_DATA_PROC_IMM;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
static inline void new_branch_exchange(BYTE cond, BYTE l, BYTE Rm) {
    inst_ptr->type = ARM_BR;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->needs_branch = false;
    inst_ptr->br = (arm_inst_br) {
            l,
            Rm
    };

    inst_ptr++;
}

// Load/store multiple
static inline void new_ldst_imm_off(BYTE cond, BYTE p, BYTE u, BYTE b, BYTE w, BYTE l, BYTE Rn, BYTE Rd, HWORD imm) {
    inst_ptr->type = ARM_LDST_IMM_OFF;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
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
    inst_ptr->needs_branch = false;
    inst_ptr->b_bl = (arm_inst_b_bl) {
            l,
            imm
    };

    inst_ptr++;
}


// Floating-point data-processing instructions
static inline void new_floating_point(BYTE cond, BYTE opc1, BYTE opc2, BYTE b12, BYTE b8, BYTE opc3, BYTE b4, BYTE opc4) {
    inst_ptr->type = ARM_FLOATING_POINT;
    inst_ptr->cond = cond;
    inst_ptr->needs_pool = false;
    inst_ptr->needs_branch = false;
    inst_ptr->fp = (arm_inst_fp) {
            opc1,
            opc2,
            b12,
            b8,
            opc3,
            b4,
            opc4
    };

    inst_ptr++;
}

/**
* Higher level macros
*/

// push {<regs>}
#define PUSH(regs) \
    new_ldst_multiple(ARM_COND_AL, 1, 0, 0, 1, 0, 13, regs)

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
    new_data_proc_imm(ARM_COND_AL, ARM_OP_CMP, 1, Rn, 0, (rot)>>1, imm8)

// cmn Rn, imm8, ror #rot
// Compare immediate negated
// imm8 can be rotated an even number of times
#define CMN_I(Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_CMN, 1, Rn, 0, (rot)>>1, imm8)

// tst Rn, imm8, ror #rot
// Test immediate
// imm8 can be rotated an even number of times
#define TST_I(Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_TST, 1, Rn, 0, (rot)>>1, imm8)

// mov Rd, imm8, ror #rot
// Mov immediate
// imm8 can be rotated an even number of times
#define MOV_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, (rot)>>1, imm8)

// mvn Rd, imm8, ror #rot
// Move and NOT immediate
// imm8 can be rotated an even number of times
#define MVN_I(Rd, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_MVN, 0, 0, Rd, (rot)>>1, imm8)

// add Rd, Rn, imm8, ror #rot
// Add immediate
#define ADD_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ADD, 0, Rn, Rd, (rot)>>1, imm8)

// adds Rd, Rn, imm8, ror #rot
#define ADDS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ADD, 1, Rn, Rd, (rot)>>1, imm8)

// sub Rd, Rn, imm8, ror #rot
// Subtract immediate
#define SUB_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_SUB, 0, Rn, Rd, (rot)>>1, imm8)

// subs Rd, Rn, imm8, ror #rot
#define SUBS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_SUB, 1, Rn, Rd, (rot)>>1, imm8)

// and Rd, Rn, imm, ror #rot
// And immediate
// imm8 can be rotated an even number of times
#define AND_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_AND, 0, Rn, Rd, (rot)>>1, imm8)

// ands Rd, Rn, imm, ror #rot
#define ANDS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_AND, 1, Rn, Rd, (rot)>>1, imm8)

// orrcc Rd, imm, ror #rot
// Or immediate with condition
// imm8 can be rotated an even number of times
#define ORRCC_I(arm_cond, Rd, imm8, rot) \
    new_data_proc_imm(arm_cond, ARM_OP_ORR, 0, Rd, Rd, (rot)>>1, imm8)

// orr Rd, imm, ror #rot
// Or immediate
// imm8 can be rotated an even number of times
#define ORR_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ORR, 0, Rn, Rd, (rot)>>1, imm8)

// eor Rd, imm, ror #rot
// Xor immediate
// imm8 can be rotated an even number of times
#define EOR_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_EOR, 0, Rn, Rd, (rot)>>1, imm8)

// eors Rd, imm, ror #rot
#define EORS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_EOR, 1, Rn, Rd, (rot)>>1, imm8)

// bic Rd, Rn, imm, ror #rot
// Bit clear immediate
// imm8 can be rotated an even number of times
#define BIC_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_BIC, 0, Rn, Rd, (rot)>>1, imm8)

// bics Rd, Rn, imm, ror #rot
#define BICS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_BIC, 1, Rn, Rd, (rot)>>1, imm8)

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

// and Rd, Rn, Rm, shift #shift_imm
#define AND_IS(Rd, Rn, Rm, shift, shift_imm) \
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_AND, 0, Rn, Rd, shift_imm, shift, Rm)

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
    new_data_proc_imm(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, (rot)>>1, imm8)

// orrs Rd, imm, ror #rot
// Or immediate
// imm8 can be rotated an even number of times
#define ORRS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_ORR, 1, Rn, Rd, (rot)>>1, imm8)

// rsbs Rd, Rn, imm8, ror #rot
// Reverse substract immediate
// imm8 can be rotated an even number of times
#define RSBS_I(Rd, Rn, imm8, rot) \
    new_data_proc_imm(ARM_COND_AL, ARM_OP_RSB, 1, Rn, Rd, (rot)>>1, imm8)

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

#define AND(Rd, Rn, Rm) \
    AND_IS(Rd, Rn, Rm, 0, 0)

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

// umulls RdLo, RdHi, Rn, Rm
// Unsigned multiply long
#define UMULLS(RdLo, RdHi, Rn, Rm) \
    new_multiply_long(ARM_COND_AL, 0, 0, 1, RdHi, RdLo, Rn, Rm)

// vmov Sn, Rt
#define VMOV_SR(Sn, Rt) \
    new_floating_point(ARM_COND_AL, 0, Sn>>1, Rt, 0b1010, (Sn&1)<<1, 1, 0)

// vmov Rt, Sn
#define VMOV_RS(Rt, Sn) \
    new_floating_point(ARM_COND_AL, 1, Sn>>1, Rt, 0b1010, (Sn&1)<<1, 1, 0)

// vcvt.f32.u32
#define VCVT_F32_U32(Sd, Sm) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b1000, Sd>>1, 0, 1, (Sm&1)<<1, Sm>>1)

// vcvt.u32.f32
#define VCVT_U32_F32(Sd, Sm) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b1100, Sd>>1, 0, 1, (Sm&1)<<1, Sm>>1)

// vcvt.f32.s32
#define VCVT_F32_S32(Sd, Sm) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b1000, Sd>>1, 0, 0b11, (Sm&1)<<1, Sm>>1)

// vcvt.s32.f32
#define VCVT_S32_F32(Sd, Sm) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b1101, Sd>>1, 0, 1, (Sm&1)<<1, Sm>>1)

#define TRUNC(Sd, Sm) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b1101, Sd>>1, 0, 0b11, (Sm&1)<<1, Sm>>1)

// vadd.f32 Sd, Sn, Sm
#define VADD_F32(Sd, Sn, Sm) \
    new_floating_point(ARM_COND_AL, 0b11|((Sd&1)<<2), Sn>>1, Sd>>1, 0, (Sn&1)<<1, (Sm&1)<<1, Sm>>1)

// vsub.f32 Sd, Sn, Sm
#define VSUB_F32(Sd, Sn, Sm) \
    new_floating_point(ARM_COND_AL, 0b11|((Sd&1)<<2), Sn>>1, Sd>>1, 0, 1|((Sn&1)<<1), (Sm&1)<<1, Sm>>1)

// vmul.f32 Sd, Sn, Sm
#define VMUL_F32(Sd, Sn, Sm) \
    new_floating_point(ARM_COND_AL, 0b10|((Sd&1)<<2), Sn>>1, Sd>>1, 0, (Sn&1)<<1, (Sm&1)<<1, Sm>>1)

// vdiv.f32 Sd, Sn, Sm
#define VDIV_F32(Sd, Sn, Sm) \
    new_floating_point(ARM_COND_AL, 0b1000|((Sd&1)<<2), Sn>>1, Sd>>1, 0, (Sn&1)<<1, (Sm&1)<<1, Sm>>1)

// vcmp.f32 Sd, Sm
#define VCMP_F32(Sd, Sm) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b0100, Sd>>1, 0, 1, (Sm&1)<<1, Sm>>1)

// vcmp.f32 Sd, #0.0
#define VCMP_F32_0(Sd) \
    new_floating_point(ARM_COND_AL, 0b1011|((Sd&1)<<2), 0b0101, Sd>>1, 0, 1, 0, 0)

// vmrs APSR_nzcv, FPSCR
#define VMRS() \
    new_floating_point(ARM_COND_AL, 0b1111, 0b0001, 0b1111, 0, 0, 1, 0)

// nop
#define NOP() \
    MOV(0, 0)

// b<cond> imm
// Branch
#define B(cond, imm){ \
    new_branch_link(cond, 0, (imm) & 0xFFFFFF); \
    if (!(imm)) \
        (inst_ptr-1)->needs_branch = true; \
}

#define Boff(cond, off) \
    new_branch_link(cond, 0, ((off)-2) & 0xFFFFFF); \

// bl<cond> imm
// Branch and link
#define BL(cond, imm) {\
    new_branch_link(cond, 1, imm & 0xFFFFFF); \
    if (!imm) \
        (inst_ptr-1)->needs_branch = true;\
}

// blx<cond> Rm
// Branch and link (register)
#define BLX(cond, Rm) \
    new_branch_exchange(cond, 1, Rm)

// mrs Rn, cpsr
// Move from CPSR
#define MRS(Rn) \
    new_move_from_cpsr(ARM_COND_AL, 0, 0b1111, Rn, 0)

// msr cpsr, Rn
// Move from register to CPSR
#define MSR(Rn) \
    new_move_reg_to_cpsr(ARM_COND_AL, 0, 0b1000, 0b1111, 0, Rn)

// Invert the carry flag
#define INV_CARRY() { \
    MRS(0); \
    EOR_I(0, 0, 0b10, 4); \
    MSR(0); \
}

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
        ORR_I(reg, reg, ((WORD)(word) & 0x0000FF00)>>8, 24); \
    if ((WORD)(word) & 0x00FF0000) \
        ORR_I(reg, reg, ((WORD)(word) & 0x00FF0000)>>16, 16); \
    if ((WORD)(word) & 0xFF000000) \
        ORR_I(reg, reg, ((WORD)(word) & 0xFF000000)>>24, 8); \
}
#endif

#define ADDCYCLES() { \
    if (cycles != 0) { \
        LDR_IO(0, 11, offsetof(cpu_state, cycles_until_event_partial)); \
        SUB_I(0, 0, cycles & 0xFF, 0); \
        STR_IO(0, 11, offsetof(cpu_state, cycles_until_event_partial)); \
    } \
    cycles = 0; \
}

#define HANDLEINT(ret_PC) { \
    MRS(0); \
    LDW_I(1, ret_PC); \
    LDR_IO(2, 11, offsetof(cpu_state, cycles_until_event_partial)); \
    LDR_IO(3, 11, offsetof(cpu_state, irq_handler)); \
    SUBS_I(2, 2, cycles & 0xFF, 0); \
    STR_IO(2, 11, offsetof(cpu_state, cycles_until_event_partial)); \
    BLX(ARM_COND_LE, 3); \
    MSR(0); \
    cycles = 0; \
}

#define HALT_LOOP_BODY 9
#define HALT_SIZE HALT_LOOP_BODY + 2

#define HALT(next_PC) { \
    MRS(0); \
    /* LDW_I exploded to ensure a consistent loop size */ \
    MOV_I(1, (next_PC) & 0xff, 0); \
    ORR_I(1, 1, ((next_PC) & 0xff00)>>8, 24); \
    ORR_I(1, 1, ((next_PC) & 0xff0000)>>16, 16); \
    ORR_I(1, 1, ((next_PC) & 0xff000000)>>24, 8); \
    MOV_I(10, 0, 0); \
    MOV_I(2, 0, 0); \
    LDR_IO(3, 11, offsetof(cpu_state, irq_handler)); \
    STR_IO(2, 11, offsetof(cpu_state, cycles_until_event_partial)); \
    BLX(ARM_COND_AL, 3); \
    Boff(ARM_COND_AL, -(HALT_LOOP_BODY)); \
    cycles = 0; \
}

#define BUSYWAIT(cond, ret_PC) { \
    Boff(cond ^ 1, HALT_SIZE + 1); \
    HALT(ret_PC); \
}

#define BUSYWAIT_BNH(ret_PC) { \
    Boff(ARM_COND_CS, 3); \
    Boff(ARM_COND_EQ, 2); \
    Boff(ARM_COND_AL, HALT_SIZE + 1); \
    HALT(ret_PC); \
}

#define BUSYWAIT_BH(ret_PC) { \
    Boff(ARM_COND_CS, HALT_SIZE + 2); \
    Boff(ARM_COND_EQ, HALT_SIZE + 1); \
    HALT(ret_PC); \
}

#endif
