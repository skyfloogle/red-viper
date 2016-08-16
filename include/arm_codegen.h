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

#ifndef ARM_CODEGEN_H
#define ARM_CODEGEN_H

/**
* Machine code generation macros
* Arguments:
*   - S: set flags
*   - ROT: rotate IMM right rot*2 times
*   - IMM : immediate value
*   - SHIFT: type of shift
*   - P: 1 for pre-increment and 0 for post-increment
*   - U: 1 for increment and 0 for decrement
*   - S: set condition flags
*   - B: 1 for byte and 0 for word
*   - W: write back to base register
*   - L: 1 for load and 0 for store
*/

// Conditional instructions

// 31   28    25   21  20   16   12     8     0
// +-------------------------------------------+
// | COND | 001 | OP | S | Rn | Rd | ROT | IMM |
// +-------------------------------------------+
// Data processing immediate
#include "arm_types.h"

#define gen_data_proc_imm(cond, op, s, Rn, Rd, rot, imm) \
                    (\
    (cond)  <<28    |\
    (1)     <<25    |\
    (op)    <<21    |\
    (s)     <<20    |\
    (Rn)    <<16    |\
    (Rd)    <<12    |\
    (rot)   <<8     |\
    (imm)           )

// 31   28    25   21  20   16   12           7       5   4    0
// +------------------------------------------------------------+
// | COND | 000 | OP | S | Rn | Rd | SHIFT_IMM | SHIFT | 0 | Rm |
// +------------------------------------------------------------+
// Data processing immediate shift
#define gen_data_proc_imm_shift(cond, opcode, s, Rn, Rd, shift_imm, shift, Rm) \
                        (\
    (cond)      <<28    |\
    (opcode)    <<21    |\
    (s)         <<20    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (shift_imm) <<7     |\
    (shift)     <<5     |\
    (Rm)                )

// 31   28    25   21  20   16   12    8   7       5   4    0
// +---------------------------------------------------------+
// | COND | 000 | OP | S | Rn | Rd | Rs | 0 | SHIFT | 1 | Rm |
// +---------------------------------------------------------+
// Data processing register shift
#define gen_data_proc_reg_shift(cond, opcode, s, Rn, Rd, Rs, shift, Rm) \
                        (\
    (cond)      <<28    |\
    (opcode)    <<21    |\
    (s)         <<20    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (Rs)        <<8     |\
    (shift)     <<5     |\
    (1)         <<4     |\
    (Rm)                )

// 31   28       22  21  20   16   12    8      4    0
// +--------------------------------------------------+
// | COND | 000000 | A | S | Rd | Rn | Rs | 1001 | Rm |
// +--------------------------------------------------+
// Multiply
#define gen_multiply(cond, a, s, Rd, Rn, Rs, Rm) \
                        (\
    (cond)      <<28    |\
    (a)         <<21    |\
    (s)         <<20    |\
    (Rd)        <<16    |\
    (Rn)        <<12    |\
    (Rs)        <<8     |\
    (0b1001)    <<4     |\
    (Rm)                )

// 31   28      23  22  21  20     16     12    8      4    0
// +---------------------------------------------------------+
// | COND | 00001 | U | A | S | RdHi | RdLo | Rn | 1001 | Rm |
// +---------------------------------------------------------+
// Multiply long
#define gen_multiply_long(cond, u, a, s, RdHi, RdLo, Rn, Rm) \
                        (\
    (cond)      <<28    |\
    (1)         <<23    |\
    (u)         <<22    |\
    (a)         <<21    |\
    (s)         <<20    |\
    (RdHi)      <<16    |\
    (RdLo)      <<12    |\
    (Rn)        <<8     |\
    (0b1001)    <<4     |\
    (Rm)                )

// 31   28      23  22    16   12     0
// +-----------------------------------+
// | COND | 00010 | R | SBO | Rd | SBZ |
// +-----------------------------------+
// Move from status register
#define gen_move_from_cpsr(cond, r, sbo, Rd, sbz) \
                        (\
    (cond)      <<28    |\
    (1)         <<24    |\
    (r)         <<22    |\
    (sbo)       <<16    |\
    (Rd)        <<12    |\
    (sbz)               )

// 31   28      23  22   20     16    12     8     0
// +------------------------------------------------+
// | COND | 00110 | R | 10 | Mask | SBO | ROT | IMM |
// +------------------------------------------------+
// Move immediate to status register
#define gen_move_imm_to_cpsr(cond, r, mask, sbo, rot, imm) \
                        (\
    (cond)      <<28    |\
    (0b11)      <<24    |\
    (r)         <<22    |\
    (1)         <<21    |\
    (mask)      <<16    |\
    (sbo)       <<12    |\
    (rot)       <<8     |\
    (imm)               )

// 31   28      23  22   20     16    12     5   4    0
// +---------------------------------------------------+
// | COND | 00010 | R | 10 | Mask | SBO | SBZ | 0 | Rm |
// +---------------------------------------------------+
// Move register to status register
#define gen_move_reg_to_cpsr(cond, r, mask, sbo, sbz, Rm) \
                        (\
    (cond)      <<28    |\
    (1)         <<24    |\
    (r)         <<22    |\
    (1)         <<21    |\
    (mask)      <<16    |\
    (sbo)       <<12    |\
    (sbz)       <<5     |\
    (Rm)                )

// 31   28         20    16    12     8      4    0
// +-----------------------------------------------+
// | COND | 00010010 | SBO | SBO | SBO | 0001 | Rm |
// +-----------------------------------------------+
// Branch/exchange instruction set
#define gen_branch_exchange(cond, l, Rm) \
                        (\
    (cond)      <<28    |\
    (0b1001)    <<21    |\
    (0b1111)    <<16    |\
    (0b1111)    <<12    |\
    (0b1111)    <<8     |\
    (l)         <<5     |\
    (1)         <<4     |\
    (Rm)                )

// 31   28    25  24   23  22  21  20   16   12     0
// +-------------------------------------------------+
// | COND | 010 | P |  U | B | W | L | Rn | Rd | IMM |
// +-------------------------------------------------+
// Load/store multiple
#define gen_ldst_imm_off(cond, p, u, b, w, l, Rn, Rd, imm) \
                    (\
    (cond)  <<28    |\
    (1)     <<26    |\
    (p)     <<24    |\
    (u)     <<23    |\
    (b)     <<22    |\
    (w)     <<21    |\
    (l)     <<20    |\
    (Rn)    <<16    |\
    (Rd)    <<12    |\
    (imm)           )

// 31   28    25  24   23  22  21  20   16   12           7       5   4    0
// +------------------------------------------------------------------------+
// | COND | 011 | P |  U | B | W | L | Rn | Rd | SHIFT_IMM | SHIFT | 0 | Rm |
// +------------------------------------------------------------------------+
// Load/store register offset
#define gen_ldst_reg_off(cond, p, u, b, w, l, Rn, Rd, shift_imm, shift, Rm) \
                        (\
    (cond)      <<28    |\
    (0b011)     <<25    |\
    (p)         <<24    |\
    (u)         <<23    |\
    (b)         <<22    |\
    (w)         <<21    |\
    (l)         <<20    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (shift_imm) <<7     |\
    (shift)     <<5     |\
    (Rm)                )

// 31   28    25  24   23  22  21  20   16   12          8   7   6   5   4    0
// +---------------------------------------------------------------------------+
// | COND | 000 | P |  U | 1 | W | L | Rn | Rd | HIGH_OFF | 1 | S | H | 1 | Rm |
// +---------------------------------------------------------------------------+
// Load/store halfword/signed byte
#define gen_ldst_hb1(cond, p, u, w, l, Rn, Rd, high_off, s, h, Rm) \
                        (\
    (cond)      <<28    |\
    (p)         <<24    |\
    (u)         <<23    |\
    (w)         <<21    |\
    (l)         <<20    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (high_off)  <<8     |\
    (1)         <<7     |\
    (s)         <<6     |\
    (h)         <<5     |\
    (1)         <<4     |\
    (Rm)                )

// 31   28    25  24   23  22  21  20   16   12     8   7   6   5   4    0
// +----------------------------------------------------------------------+
// | COND | 000 | P |  U | 0 | W | L | Rn | Rd | SBZ | 1 | S | H | 1 | Rm |
// +----------------------------------------------------------------------+
// Load/store halfword/signed byte
#define gen_ldst_hb2(cond, p, u, w, l, Rn, Rd, sbz, s, h, Rm) \
                        (\
    (cond)      <<28    |\
    (p)         <<24    |\
    (u)         <<23    |\
    (w)         <<21    |\
    (l)         <<20    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (sbz)       <<8     |\
    (1)         <<7     |\
    (s)         <<6     |\
    (h)         <<5     |\
    (1)         <<4     |\
    (Rm)                )

// 31   28      23  22   20   16   12     8      4    0
// +---------------------------------------------------+
// | COND | 00010 | B | 00 | Rn | Rd | SBZ | 1001 | Rm |
// +---------------------------------------------------+
// Swab/swap byte
#define gen_swap_byte(cond, b, Rn, Rd, sbz, Rm) \
                        (\
    (cond)      <<28    |\
    (1)         <<24    |\
    (b)         <<22    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (sbz)       <<8     |\
    (0b1001)    <<4     |\
    (Rm)                )

// 31   28    25  24   23  22  21  20   16      0
// +---------------------------------------------+
// | COND | 100 | P |  U | S | W | L | Rn | REGS |
// +---------------------------------------------+
// Load/store multiple
#define gen_ldst_multiple(cond, p, u, s, w, l, Rn, regs) \
                    (\
    (cond)  <<28    |\
    (1)     <<27    |\
    (p)     <<24    |\
    (u)     <<23    |\
    (s)     <<22    |\
    (w)     <<21    |\
    (l)     <<20    |\
    (Rn)    <<16    |\
    (regs)          )

// 31   28    25  24                            0
// +---------------------------------------------+
// | COND | 101 | L |             IMM            |
// +---------------------------------------------+
// Branch and branch with link
#define gen_branch_link(cond, l, imm) \
                    (\
    (cond)  <<28    |\
    (0b101) <<25    |\
    (l)     <<24    |\
    (imm&0xffffff)  )

// 31   28     24     20     16    12     9    8      6    5   4      0
// +-------------------------------------------------------------------+
// | COND | 1110 | opc1 | opc2 | b12 | 101 | b8 | opc3 | b5 | 0 | opc4 |
// +-------------------------------------------------------------------+
// Floating-point
#define gen_floating_point(cond, opc1, opc2, b12, b8, opc3, b4, opc4) \
                        (\
    (cond)      <<28    |\
    (0b1110)    <<24    |\
    (opc1&0xf)  <<20    |\
    (opc2&0xf)  <<16    |\
    (b12&0xf)   <<12    |\
    (0b101)     <<9     |\
    (b8&1)      <<8     |\
    (opc3&0x3)  <<6     |\
    (b4&0x3)    <<4     |\
    (opc4&0xf)          )

// Unconditional instructions
// TODO: Implement unconditional instructions

// Maps the V810 branch condition code (opcode & 0xF) to ARM conditions
// NOTES:
//  * BNH -> bcs + beq
//  * BH  -> bcc + bne
const BYTE cond_map[] = {
        // V810_OP_BV, V810_OP_BL, V810_OP_BE, V810_OP_BNH, V810_OP_BN, V810_OP_BR
        ARM_COND_VS, ARM_COND_CS, ARM_COND_EQ, ARM_COND_CS, ARM_COND_MI, ARM_COND_AL,
        // V810_OP_BLT, V810_OP_BLE, V810_OP_BNV, V810_OP_BNL, V810_OP_BNE, V810_OP_BH,
        ARM_COND_LT, ARM_COND_LE, ARM_COND_VC, ARM_COND_CC, ARM_COND_NE, ARM_COND_CC,
        // V810_OP_BP, NOP, V810_OP_BGE, V810_OP_BGT
        ARM_COND_PL, ARM_COND_NV, ARM_COND_GE, ARM_COND_GT
};

static inline void drc_assemble(WORD* dst, arm_inst* src) {
    switch (src->type) {
        case ARM_DATA_PROC_IMM:
            *dst = gen_data_proc_imm(src->cond, src->dpi.op, src->dpi.s, src->dpi.Rn, src->dpi.Rd, src->dpi.rot, src->dpi.imm);
            break;
        case ARM_DATA_PROC_IMM_SHIFT:
            *dst = gen_data_proc_imm_shift(src->cond, src->dpis.opcode, src->dpis.s, src->dpis.Rn, src->dpis.Rd, src->dpis.shift_imm, src->dpis.shift, src->dpis.Rm);
            break;
        case ARM_DATA_PROC_REG_SHIFT:
            *dst = gen_data_proc_reg_shift(src->cond, src->dprs.opcode, src->dprs.s, src->dprs.Rn, src->dprs.Rd, src->dprs.Rs, src->dprs.shift, src->dprs.Rm);
            break;
        case ARM_MUL:
            *dst = gen_multiply(src->cond, src->mul.a, src->mul.s, src->mul.Rd, src->mul.Rn, src->mul.Rs, src->mul.Rm);
            break;
        case ARM_MULL:
            *dst = gen_multiply_long(src->cond, src->mull.u, src->mull.a, src->mull.s, src->mull.RdHi, src->mull.RdLo, src->mull.Rn, src->mull.Rm);
            break;
        case ARM_MOV_FROM_CPSR:
            *dst = gen_move_from_cpsr(src->cond, src->mfcpsr.r, src->mfcpsr.sbo, src->mfcpsr.Rd, src->mfcpsr.sbz);
            break;
        case ARM_MOV_IMM_CPSR:
            *dst = gen_move_imm_to_cpsr(src->cond, src->micpsr.r, src->micpsr.mask, src->micpsr.sbo, src->micpsr.rot, src->micpsr.imm);
            break;
        case ARM_MOV_REG_CPSR:
            *dst = gen_move_reg_to_cpsr(src->cond, src->mrcpsr.r, src->mrcpsr.mask, src->mrcpsr.sbo, src->mrcpsr.sbz, src->mrcpsr.Rm);
            break;
        case ARM_BR:
            *dst = gen_branch_exchange(src->cond, src->br.l, src->br.Rm);
            break;
        case ARM_LDST_IMM_OFF:
            *dst = gen_ldst_imm_off(src->cond, src->ldst_io.p, src->ldst_io.u, src->ldst_io.b, src->ldst_io.w, src->ldst_io.l, src->ldst_io.Rn, src->ldst_io.Rd, src->ldst_io.imm);
            break;
        case ARM_LDST_REG_OFF:
            *dst = gen_ldst_reg_off(src->cond, src->ldst_ro.p, src->ldst_ro.u, src->ldst_ro.b, src->ldst_ro.w, src->ldst_ro.l, src->ldst_ro.Rn, src->ldst_ro.Rd, src->ldst_ro.shift_imm, src->ldst_ro.shift, src->ldst_ro.Rm);
            break;
        case ARM_LDST_HB1:
            *dst = gen_ldst_hb1(src->cond, src->ldst_hb1.p, src->ldst_hb1.u, src->ldst_hb1.w, src->ldst_hb1.l, src->ldst_hb1.Rn, src->ldst_hb1.Rd, src->ldst_hb1.high_off, src->ldst_hb1.s, src->ldst_hb1.h, src->ldst_hb1.Rm);
            break;
        case ARM_LDST_HB2:
            *dst = gen_ldst_hb2(src->cond, src->ldst_hb2.p, src->ldst_hb2.u, src->ldst_hb2.w, src->ldst_hb2.l, src->ldst_hb2.Rn, src->ldst_hb2.Rd, src->ldst_hb2.sbz, src->ldst_hb2.s, src->ldst_hb2.h, src->ldst_hb2.Rm);
            break;
        case ARM_SWAP:
            *dst = gen_swap_byte(src->cond, src->swp.b, src->swp.Rn, src->swp.Rd, src->swp.sbz, src->swp.Rm);
            break;
        case ARM_LDST_MULT:
            *dst = gen_ldst_multiple(src->cond, src->ldstm.p, src->ldstm.u, src->ldstm.s, src->ldstm.w, src->ldstm.l, src->ldstm.Rn, src->ldstm.regs);
            break;
        case ARM_BRANCH_LINK:
            *dst = gen_branch_link(src->cond, src->b_bl.l, src->b_bl.imm);
            break;
        case ARM_FLOATING_POINT:
            *dst = gen_floating_point(src->cond, src->fp.opc1, src->fp.opc2, src->fp.b12, src->fp.b8, src->fp.opc3, src->fp.b4, src->fp.opc4);
            break;
    }
}

#endif //ARM_CODEGEN_H
