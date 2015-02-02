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

// Condition codes
enum {
    ARM_COND_EQ, // Equal
    ARM_COND_NE, // Not Equal
    ARM_COND_CS, // Carry Set
    ARM_COND_CC, // Carry Clear
    ARM_COND_MI, // MInus
    ARM_COND_PL, // PLus
    ARM_COND_VS, // oVerflow Set
    ARM_COND_VC, // oVerflow Clear
    ARM_COND_HI, // HIgher
    ARM_COND_LS, // Lower or Same
    ARM_COND_GE, // Greater or Equal
    ARM_COND_LT, // Less Than
    ARM_COND_GT, // Greater Than
    ARM_COND_LE, // Less or Equal
    ARM_COND_AL, // Always
    ARM_COND_NV  // NeVer
};

// Data processing opcodes
enum {
    ARM_OP_AND, // AND
    ARM_OP_EOR, // XOR
    ARM_OP_SUB, // Subtract
    ARM_OP_RSB, // Reverse subtract
    ARM_OP_ADD, // Add
    ARM_OP_ADC, // Add with carry
    ARM_OP_SBC, // Subtract with carry
    ARM_OP_RSC, // Reverse subtract with carry
    ARM_OP_TST, // Test
    ARM_OP_TEQ, // Test equals
    ARM_OP_CMP, // Compare
    ARM_OP_CMN, // Compare negative
    ARM_OP_ORR, // OR
    ARM_OP_MOV, // Move
    ARM_OP_BIC, // Bit clear
    ARM_OP_MVN  // Move negative
};

// Shift types
enum {
    ARM_SHIFT_LSL, // Logical shift left
    ARM_SHIFT_LSR, // Logical shift right
    ARM_SHIFT_ASR, // Arithmetic shift right
    ARM_SHIFT_ROR  // Rotate right
};

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
#define gen_data_proc_imm(cond, op, s, Rn, Rd, rot, imm) \
    (unsigned)      (\
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
    (unsigned)          (\
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
    (unsigned)          (\
    (cond)      <<28    |\
    (opcode)    <<21    |\
    (s)         <<20    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (Rs)        <<8     |\
    (shift)     <<5     |\
    (Rm)                )

// 31   28       22  21  20   16   12    8      4    0
// +--------------------------------------------------+
// | COND | 000000 | A | S | Rd | Rn | Rs | 1001 | Rm |
// +--------------------------------------------------+
// Multiply
#define gen_multiply(cond, a, s, Rd, Rn, Rs, Rm) \
    (unsigned)          (\
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
    (unsigned)          (\
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
    (unsigned)          (\
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
    (unsigned)          (\
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
    (unsigned)          (\
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
#define gen_branch_exchange(cond, sbo1, sbo2, sbo3, Rm) \
    (unsigned)          (\
    (cond)      <<28    |\
    (0b1001)    <<21    |\
    (sbo1)      <<16    |\
    (sbo2)      <<12    |\
    (sbo3)      <<8     |\
    (0b0001)    <<8     |\
    (Rm)                )

// 31   28    25  24   23  22  21  20   16   12     0
// +-------------------------------------------------+
// | COND | 010 | P |  U | B | W | L | Rn | Rd | IMM |
// +-------------------------------------------------+
// Load/store multiple
#define gen_ldst_imm_off(cond, p, u, b, w, l, Rn, Rd, imm) \
    (unsigned)      (\
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
    (unsigned)          (\
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
    (unsigned)          (\
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
    (unsigned)          (\
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
    (unsigned)          (\
    (cond)      <<28    |\
    (1)         <<24    |\
    (b)         <<22    |\
    (Rn)        <<16    |\
    (Rd)        <<12    |\
    (sbz)       <<8     |\
    (0b1001)    <<4     |\
    (Rm)                )

// 31   28    25   24  23  22  21  20   16      0
// +---------------------------------------------+
// | COND | 100 | P |  U | S | W | L | Rn | REGS |
// +---------------------------------------------+
// Load/store multiple
#define gen_ldst_multiple(cond, p, u, s, w, l, Rn, regs) \
    (unsigned)      (\
    (cond)  <<28    |\
    (1)     <<27    |\
    (p)     <<24    |\
    (u)     <<23    |\
    (s)     <<22    |\
    (w)     <<21    |\
    (l)     <<20    |\
    (Rn)    <<16    |\
    (regs)          )

// Unconditional instructions
// TODO: Implement unconditional instructions

/**
* Higher level macros
*/

// pop {<regs>}
#define POP(regs) \
    gen_ldst_multiple(ARM_COND_AL, 0, 1, 0, 1, 1, 13, regs)

// str Rd, [Rn, #off]
// Store with immediate offset
#define STR_IO(Rd, Rn, off) \
    gen_ldst_imm_off(ARM_COND_AL, 1, 1, 0, 0, 0, Rn, Rd, off)

// str Rd, [Rn, #off]
// Load with immediate offset
#define LDR_IO(Rd, Rn, off) \
    gen_ldst_imm_off(ARM_COND_AL, 1, 1, 0, 0, 1, Rn, Rd, off)

// mov Rd, imm8, ror #rot
// Mov immediate
// imm8 can be rotated an even number of times
#define MOV_I(Rd, imm8, rot) \
    gen_data_proc_imm(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, rot>>1, imm8)

// add Rd, Rn, imm8, ror #rot
// Add immediate
#define ADD_I(Rd, Rn, imm8, rot) \
    gen_data_proc_imm(ARM_COND_AL, ARM_OP_ADD, 0, Rn, Rd, rot, imm8)

// orr Rd, imm, ror #rot
// Or immediate
// imm8 can be rotated an even number of times
#define ORR_I(Rd, imm8, rot) \
    gen_data_proc_imm(ARM_COND_AL, ARM_OP_ORR, 0, Rd, Rd, rot>>1, imm8)

// add Rd, Rn, Rm, shift #shift_imm
// Add immediate shift
#define ADD_IS(Rd, Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ADD, 0, Rn, Rd, shift_imm, shift, Rm)

// cmp Rn, Rm, shift #shift_imm
// Compare immediate shift
#define CMP_IS(Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_CMP, 1, Rn, 0, shift_imm, shift, Rm)

// mov Rs, Rm, shift #shift_imm
// Mov immediate shift
#define MOV_IS(Rd, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, shift_imm, shift, Rm)

// orr Rd, Rn, Rm, shift #shift_imm
// Or immediate shift
#define ORR_IS(Rd, Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ORR, 0, Rn, Rd, shift_imm, shift, Rm)

// eor Rd, Rn, Rm, shift #shift_imm
// XOR immediate shift
#define EOR_IS(Rd, Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_EOR, 0, Rn, Rd, shift_imm, shift, Rm)

// movs Rd, imm8, ror #rot
// Mov immediate
// imm8 can be rotated an even number of times
#define MOVS_I(Rd, imm8, rot) \
    gen_data_proc_imm(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, rot>>1, imm8)

// orrs Rd, imm, ror #rot
// Or immediate
// imm8 can be rotated an even number of times
#define ORRS_I(Rd, imm8, rot) \
    gen_data_proc_imm(ARM_COND_AL, ARM_OP_ORR, 1, Rd, Rd, rot>>1, imm8)

// adds Rd, Rn, Rm, shift #shift_imm
// Add immediate shift
#define ADDS_IS(Rd, Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ADD, 1, Rn, Rd, shift_imm, shift, Rm)

// movs Rs, Rm, shift #shift_imm
// Mov immediate shift
#define MOVS_IS(Rd, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, shift_imm, shift, Rm)

// orrs Rd, Rn, Rm, shift #shift_imm
// Or immediate shift
#define ORRS_IS(Rd, Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_ORR, 1, Rn, Rd, shift_imm, shift, Rm)

// eors Rd, Rn, Rm, shift #shift_imm
// XOR immediate shift
#define EORS_IS(Rd, Rn, Rm, shift, shift_imm) \
    gen_data_proc_imm_shift(ARM_COND_AL, ARM_OP_EOR, 1, Rn, Rd, shift_imm, shift, Rm)

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
    ORRS_IS(Rd, Rn, Rm, 0, 0)

// eor Rd, Rn, Rm
#define EOR(Rd, Rn, Rm) \
    EORS_IS(Rd, Rn, Rm, 0, 0)

// lsl Rd, Rn, Rm
#define LSL(Rd, Rm, Rs) \
    gen_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, Rs, ARM_SHIFT_LSL, Rm)

// lsr Rd, Rn, Rm
#define LSR(Rd, Rm, Rs) \
    gen_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 0, 0, Rd, Rs, ARM_SHIFT_LSR, Rm)

// adds Rd, Rn, Rm
#define ADDS(Rd, Rn, Rm) \
    ADDS_IS(Rd, Rn, Rm, 0, 0)

// movs Rd, Rm
#define MOVS(Rd, Rm) \
    MOVS_IS(Rd, Rm, 0, 0)

// orrs Rd, Rn, Rm
#define ORRS(Rd, Rn, Rm) \
    ORRS_IS(Rd, Rn, Rm, 0, 0)

// eors Rd, Rn, Rm
#define EORS(Rd, Rn, Rm) \
    EORS_IS(Rd, Rn, Rm, 0, 0)

// lsls Rd, Rn, Rm
#define LSLS(Rd, Rm, Rs) \
    gen_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, Rs, ARM_SHIFT_LSL, Rm)

// lsrs Rd, Rn, Rm
#define LSRS(Rd, Rm, Rs) \
    gen_data_proc_reg_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, Rd, Rs, ARM_SHIFT_LSR, Rm)

// nop
#define NOP() \
    MOV(0, 0)

// Maps the V810 branch condition code (opcode & 0xF) to ARM conditions
const BYTE cond_map[] = {
        // V810_OP_BV, V810_OP_BL, V810_OP_BE, V810_OP_BNH, V810_OP_BN, V810_OP_BR
        ARM_COND_VS, ARM_COND_CC, ARM_COND_EQ, ARM_COND_LS, ARM_COND_MI, ARM_COND_AL,
        // V810_OP_BLT, V810_OP_BLE, V810_OP_BNV, V810_OP_BNL, V810_OP_BNE, V810_OP_BH,
        ARM_COND_LT, ARM_COND_LE, ARM_COND_VC, ARM_COND_CS, ARM_COND_NE, ARM_COND_HI,
        // V810_OP_BP, NOP, V810_OP_BGE, V810_OP_BGT
        ARM_COND_PL, ARM_COND_NV, ARM_COND_GE, ARM_COND_GT
};

#endif