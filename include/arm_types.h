
#ifndef ARM_TYPES_H
#define ARM_TYPES_H

#include "vb_types.h"

// Condition codes
enum ARM_COND_CODE {
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
enum ARM_OPCODE {
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
enum ARM_SHIFT {
    ARM_SHIFT_LSL, // Logical shift left
    ARM_SHIFT_LSR, // Logical shift right
    ARM_SHIFT_ASR, // Arithmetic shift right
    ARM_SHIFT_ROR  // Rotate right
};

typedef enum {
    ARM_DATA_PROC_IMM,
    ARM_DATA_PROC_IMM_SHIFT,
    ARM_DATA_PROC_REG_SHIFT,
    ARM_MUL,
    ARM_MULL,
    ARM_MEDIA,
    ARM_EXTEND,
    ARM_MOV_FROM_CPSR,
    ARM_MOV_IMM_CPSR,
    ARM_MOV_REG_CPSR,
    ARM_BR,
    ARM_LDST_IMM_OFF,
    ARM_LDST_REG_OFF,
    ARM_LDST_HB1,
    ARM_LDST_HB2,
    ARM_SWAP,
    ARM_LDST_MULT,
    ARM_BRANCH_LINK,
    ARM_FLOATING_POINT,
} arm_inst_type;

#endif //ARM_TYPES_H
