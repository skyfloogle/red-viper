
#ifndef ARM_TYPES_H
#define ARM_TYPES_H

#include "vb_types.h"

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
} ARM_COND_CODE;

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
} ARM_OPCODE;

// Shift types
enum {
    ARM_SHIFT_LSL, // Logical shift left
    ARM_SHIFT_LSR, // Logical shift right
    ARM_SHIFT_ASR, // Arithmetic shift right
    ARM_SHIFT_ROR  // Rotate right
} ARM_SHIFT;

typedef enum {
    ARM_DATA_PROC_IMM,
    ARM_DATA_PROC_IMM_SHIFT,
    ARM_DATA_PROC_REG_SHIFT,
    ARM_MUL,
    ARM_MULL,
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
} arm_inst_type;

typedef struct {
    BYTE op;
    BYTE s;
    BYTE Rn;
    BYTE Rd;
    BYTE rot;
    BYTE imm;
} arm_inst_dpi;

typedef struct {
    BYTE opcode;
    BYTE s;
    BYTE Rn;
    BYTE Rd;
    BYTE shift_imm;
    BYTE shift;
    BYTE Rm;
} arm_inst_dpis;

typedef struct {
    BYTE opcode;
    BYTE s;
    BYTE Rn;
    BYTE Rd;
    BYTE Rs;
    BYTE shift;
    BYTE Rm;
} arm_inst_dprs;

typedef struct {
    BYTE a;
    BYTE s;
    BYTE Rd;
    BYTE Rn;
    BYTE Rs;
    BYTE Rm;
} arm_inst_mul;

typedef struct {
    BYTE u;
    BYTE a;
    BYTE s;
    BYTE RdHi;
    BYTE RdLo;
    BYTE Rn;
    BYTE Rm;
} arm_inst_mull;

typedef struct {
    BYTE r;
    BYTE sbo;
    BYTE Rd;
    HWORD sbz;
} arm_inst_mfcpsr;

typedef struct {
    BYTE r;
    BYTE mask;
    BYTE sbo;
    BYTE rot;
    BYTE imm;
} arm_inst_micpsr;

typedef struct {
    BYTE r;
    BYTE mask;
    BYTE sbo;
    BYTE sbz;
    BYTE Rm;
} arm_inst_mrcpsr;

typedef struct {
    BYTE sbo1;
    BYTE sbo2;
    BYTE sbo3;
    BYTE Rm;
} arm_inst_br;

typedef struct {
    BYTE p;
    BYTE u;
    BYTE b;
    BYTE w;
    BYTE l;
    BYTE Rn;
    BYTE Rd;
    HWORD imm;
} arm_inst_ldst_io;

typedef struct {
    BYTE p;
    BYTE u;
    BYTE b;
    BYTE w;
    BYTE l;
    BYTE Rn;
    BYTE Rd;
    BYTE shift_imm;
    BYTE shift;
    BYTE Rm;
} arm_inst_ldst_ro;

typedef struct {
    BYTE p;
    BYTE u;
    BYTE w;
    BYTE l;
    BYTE Rn;
    BYTE Rd;
    BYTE high_off;
    BYTE s;
    BYTE h;
    BYTE Rm;
} arm_inst_ldst_hb1;

typedef struct {
    BYTE p;
    BYTE u;
    BYTE w;
    BYTE l;
    BYTE Rn;
    BYTE Rd;
    BYTE sbz;
    BYTE s;
    BYTE h;
    BYTE Rm;
} arm_inst_ldst_hb2;

typedef struct {
    BYTE b;
    BYTE Rn;
    BYTE Rd;
    BYTE sbz;
    BYTE Rm;
} arm_inst_swp;

typedef struct {
    BYTE p;
    BYTE u;
    BYTE s;
    BYTE w;
    BYTE l;
    BYTE Rn;
    HWORD regs;
} arm_inst_ldstm;

typedef struct {
    BYTE l;
    int imm;
} arm_inst_b_bl;

typedef struct {
    arm_inst_type type;
    BYTE cond;
    WORD PC;
    bool needs_pool;
    bool needs_branch;
#ifdef LITERAL_POOL
    WORD* pool_start;
    HWORD pool_pos;
#endif
    union {
        arm_inst_dpi dpi;
        arm_inst_dpis dpis;
        arm_inst_dprs dprs;
        arm_inst_mul mul;
        arm_inst_mull mull;
        arm_inst_mfcpsr mfcpsr;
        arm_inst_micpsr micpsr;
        arm_inst_mrcpsr mrcpsr;
        arm_inst_br br;
        arm_inst_ldst_io ldst_io;
        arm_inst_ldst_ro ldst_ro;
        arm_inst_ldst_hb1 ldst_hb1;
        arm_inst_ldst_hb2 ldst_hb2;
        arm_inst_swp swp;
        arm_inst_ldstm ldstm;
        arm_inst_b_bl b_bl;
    };
} arm_inst;

#endif //ARM_TYPES_H
