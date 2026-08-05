#include "arm_emit.h"
#include "arm_codegen.h"
#include "drc_core.h"
#include "v810_opt.h"

static BYTE phys_regs[32];

// Maps the most used registers in the block to V810 registers
void drc_mapRegs(exec_block* block) {
    int i, j, max, max_pos;

    block->reg_map = 0;

    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        max = max_pos = 0;
        // We don't care about P_REG[0] because it will always be 0 and it will
        // be optimized out
        for (j = 1; j < 32; j++) {
            if (reg_usage[j] > max) {
                max_pos = j;
                max = reg_usage[j];
            }
        }
        block->reg_map |= max_pos << (5 * i);
        if (max)
            reg_usage[(block->reg_map >> (5 * i)) & 0x1f] = 0;
        else
            // Use P_REG[0] as a placeholder if the register isn't
            // used in the block
            block->reg_map &= ~(0x1f << (5 * i));
    }
}

// Gets the ARM register corresponding to a cached V810 register
static BYTE drc_getPhysReg(BYTE vb_reg, WORD reg_map) {
    int i;
    for (i = 0; i < ARM_NUM_CACHE_REGS; i++) {
        if (((reg_map >> (i * 5)) & 0x1f) == vb_reg) {
            // The first usable register will be r4
            return (BYTE) (i + ARM_CACHE_REG_START);
        }
    }
    return 0;
}

void drc_prepare(exec_block *block) {
    drc_mapRegs(block);
    phys_regs[0] = 0;
    for (int i = 1; i < 32; i++)
        phys_regs[i] = drc_getPhysReg(i, block->reg_map);
}

#define INST_SETUP \
    int arm_reg1 = 0, arm_reg2 = 0, next_available_reg = 2; \
    bool reg1_modified = false, reg2_modified = false, unmapped_registers = false; \
    if (ins->reg1 != 0xFF) { \
        arm_reg1 = phys_regs[ins->reg1]; \
        if (!arm_reg1) { \
            unmapped_registers = true; \
            arm_reg1 = next_available_reg++; \
        } \
    } \
    if (ins->reg2 != 0xFF) { \
        arm_reg2 = phys_regs[ins->reg2]; \
        if (!arm_reg2) { \
            unmapped_registers = true; \
            arm_reg2 = next_available_reg++; \
        } \
    } \
    if (ins->save_flags) { \
        MRS(0); \
        PUSH(1<<0); \
    }

#define INST_TEARDOWN \
    if (ins->save_flags) { \
        POP(1<<0); \
        MSR(0); \
    } \
    if (unmapped_registers) { \
        if (arm_reg1 < 4 && reg1_modified) \
            STR_IO(arm_reg1, 11, offsetof(cpu_state, P_REG[ins->reg1])); \
        if (arm_reg2 < 4 && reg2_modified) \
            STR_IO(arm_reg2, 11, offsetof(cpu_state, P_REG[ins->reg2])); \
    }

#define BEGIN_INSTR(name) \
    void drc_##name(v810_instruction *ins) { \
        INST_SETUP

#define END_INSTR() \
        INST_TEARDOWN \
    }

#define LOAD_REG1() \
    if (arm_reg1 < 4) { \
        if (ins->reg1) \
            LDR_IO(arm_reg1, 11, offsetof(cpu_state, P_REG[ins->reg1])); \
        else \
            MOV_I(arm_reg1, 0, 0); \
    }

#define RELOAD_REG1(r) \
    if (arm_reg1 < 4) { \
        if (ins->reg1) \
            LDR_IO(r, 11, offsetof(cpu_state, P_REG[ins->reg1])); \
        else \
            MOV_I(r, 0, 0); \
    } else if (r != arm_reg1) { \
        MOV(r, arm_reg1); \
    }

#define LOAD_REG2() \
    if (arm_reg2 < 4) { \
        if (ins->reg2) \
            LDR_IO(arm_reg2, 11, offsetof(cpu_state, P_REG[ins->reg2])); \
        else \
            MOV_I(arm_reg2, 0, 0); \
    }

#define RELOAD_REG2(r) \
    if (arm_reg2 < 4) { \
        if (ins->reg2) \
            LDR_IO(r, 11, offsetof(cpu_state, P_REG[ins->reg2])); \
        else \
            MOV_I(r, 0, 0); \
    } else if(r != arm_reg2) { \
        MOV(r, arm_reg2); \
    }

#define LOAD_REG(arm,vb) \
    if (!phys_regs[vb]) LDR_IO(arm, 11, offsetof(cpu_state, P_REG[vb])); \
    else MOV(arm, phys_regs[vb]);

#define SAVE_REG2(r) \
    if (arm_reg2 < 4) STR_IO(r, 11, offsetof(cpu_state, P_REG[ins->reg2])); \
    else if(arm_reg2 != r) MOV(arm_reg2, r);

void drc_add_cycles(unsigned int *cycles) {
    if (cycles != 0) {
        LDR_IO(0, 11, offsetof(cpu_state, cycles_until_event_partial));
        SUB_I(0, 0, *cycles & 0xFF, 0);
        STR_IO(0, 11, offsetof(cpu_state, cycles_until_event_partial));
    }
    cycles = 0;
}

void drc_subtract_cycles_runtime(int cycles) {
    SUB_I(10, 10, cycles, 0);
}

#define HALT_LOOP_BODY 8
#define HALT_SIZE HALT_LOOP_BODY + 2

void drc_halt(WORD next_PC, unsigned *cycles) {
    MRS(0);
    /* LDW_I exploded to ensure a consistent loop size */
    MOV_I(1, (next_PC) & 0xff, 0);
    ORR_I(1, 1, ((next_PC) & 0xff00)>>8, 24);
    ORR_I(1, 1, ((next_PC) & 0xff0000)>>16, 16);
    ORR_I(1, 1, ((next_PC) & 0xff000000)>>24, 8);
    MOV_I(10, 0, 0);
    LDR_IO(3, 11, offsetof(cpu_state, irq_handler));
    STR_IO(10, 11, offsetof(cpu_state, cycles_until_event_partial));
    BLX(ARM_COND_AL, 3);
    Boff(ARM_COND_AL, -(HALT_LOOP_BODY));
    *cycles = 0;
}

void drc_busywait(v810_instruction *ins, unsigned *cycles) {
    // Special case: bnh and bh can't be directly translated to ARM
    if (ins->opcode == V810_OP_BH) {
        Boff(ARM_COND_CS, HALT_SIZE + 2);
        Boff(ARM_COND_EQ, HALT_SIZE + 1);
    } else if (ins->opcode == V810_OP_BNH) {
        Boff(ARM_COND_CS, 3);
        Boff(ARM_COND_EQ, 2);
        Boff(ARM_COND_AL, HALT_SIZE + 1);
    } else {
        Boff(cond_map[ins->opcode & 0xF] ^ 1, HALT_SIZE + 1);
    }
    drc_halt(ins->PC + ins->branch_offset, cycles);
}

void drc_handle_interrupts(WORD ret_PC, unsigned *cycles) {
    MRS(0);
    LDW_I(1, ret_PC);
    LDR_IO(2, 11, offsetof(cpu_state, cycles_until_event_partial));
    LDR_IO(3, 11, offsetof(cpu_state, irq_handler));
    SUB(2, 2, 10);
    MOV_I(10, 0, 0);
    SUBS_I(2, 2, *cycles & 0xFF, 0);
    STR_IO(2, 11, offsetof(cpu_state, cycles_until_event_partial));
    BLX(ARM_COND_LE, 3);
    MSR(0);
    *cycles = 0;
}

void drc_jump_short(v810_instruction *ins) {
    B(ARM_COND_AL, 0);
}

void drc_jump_long(v810_instruction *ins) {
    LDW_I(0, ins->PC + ins->branch_offset);
    // Save the new PC
    STR_IO(0, 11, offsetof(cpu_state, PC));
    POP(1 << 15);
}

void drc_branch(v810_instruction *ins) {
    // Special case: bnh and bh can't be directly translated to ARM
    if (ins->opcode == V810_OP_BH) {
        // Branch if C == 0 and Z == 0
        Boff(ARM_COND_CS, 3);
        Boff(ARM_COND_EQ, 2);
        B(ARM_COND_AL, 0);
    } else if (ins->opcode == V810_OP_BNH) {
        // Branch if C == 1 or Z == 1
        B(ARM_COND_CS, 0);
        B(ARM_COND_EQ, 0);
    } else {
        B(cond_map[ins->opcode & 0xF], 0);
    }
}

void drc_link_reg(v810_instruction *ins) {
    LDW_I(1, ins->PC + 4);
    if (phys_regs[31])
        MOV(phys_regs[31], 1);
    else
        STR_IO(1, 11, offsetof(cpu_state, P_REG[31]));
}

void drc_golf_hack() {
    LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
    LDR_IO(2, 2, DRC_RELOC_GOLFHACK*4);
    BLX(ARM_COND_AL, 2);
}

void drc_ballsort(void) {
    LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
    LDR_IO(2, 2, DRC_RELOC_BALLSORT*4);
    BLX(ARM_COND_AL, 2);
}

static arm_inst *branch_to_tweak;

void drc_ballscale_start(void) {
    // Verify that our values make sense, otherwise revert to original
    LOAD_REG(0, 17);
    LOAD_REG(1, 18);
    MOV_IS(2, 0, ARM_SHIFT_LSR, 20);
    CMP_I(2, 0x70, 0);
    Boff(ARM_COND_NE, 9);
    MOV_IS(2, 1, ARM_SHIFT_LSR, 16);
    CMP_I(2, 0x5, 24);
    Boff(ARM_COND_NE, 6);
    // Do HLE
    LDR_IO(3, 11, offsetof(cpu_state, reloc_table));
    LDR_IO(3, 3, DRC_RELOC_BALLSCALE*4);
    LOAD_REG(2, 19);
    BLX(ARM_COND_AL, 3);
    branch_to_tweak = inst_ptr;
    Boff(ARM_COND_AL, 0);
}

void drc_ballscale_end(void) {
    branch_to_tweak->b_bl.imm = inst_ptr - branch_to_tweak - 2;
}

void drc_vertical_force_hack(void) {
    MOV_I(0, 1, 25);
    ADD(10, 10, 0);
}

void drc_bowling_nikochan_hack(v810_instruction *ins, unsigned cycles) {
    LDR_IO(2, 11, offsetof(cpu_state, irq_handler));
    MRS(0);
    LDW_I(1, ins[1].PC);
    ADD_I(10, 10, cycles & 0xFF, 0);
    BLX(ARM_COND_AL, 2);
    MSR(0);
}

void drc_reti() {
    LDR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
    TST_I(0, PSW_NP >> 8, 24);
    // ldrne r1, S_REG[FEPC]
    new_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 1, offsetof(cpu_state, S_REG[FEPC]));
    // ldrne r2, S_REG[FEPSW]
    new_ldst_imm_off(ARM_COND_NE, 1, 1, 0, 0, 1, 11, 2, offsetof(cpu_state, S_REG[FEPSW]));
    // ldreq r1, S_REG[EIPC]
    new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 1, offsetof(cpu_state, S_REG[EIPC]));
    // ldreq r2, S_REG[EIPSW]
    new_ldst_imm_off(ARM_COND_EQ, 1, 1, 0, 0, 1, 11, 2, offsetof(cpu_state, S_REG[EIPSW]));

    STR_IO(1, 11, offsetof(cpu_state, PC));
    STR_IO(2, 11, offsetof(cpu_state, S_REG[PSW]));

    // restore flags and handle any lingering interrupts
    LDR_IO(0, 11, offsetof(cpu_state, except_flags));
    LDR_IO(2, 11, offsetof(cpu_state, irq_handler));
    STR_IO(0, 11, offsetof(cpu_state, flags));
    BLX(ARM_COND_AL, 2);

    // if we didn't exit already, restore state
    MSR(0);
    POP(1 << 15);
}

void drc_nop() {
    NOP();
}

void drc_end_block() {
    POP(1 << 15);
}

void drc_ld_b(v810_instruction *ins, bool access_time) {
    INST_SETUP
    if (arm_reg1 < 4) arm_reg1 = 0;

    if (ins->imm == 0) {
        RELOAD_REG1(0);
    } else if ((short)ins->imm > 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ADD_I(0, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ADD_I(0, arm_reg1, ins->imm & 0xff, 0);
            ADD_I(0, 0, ins->imm >> 8, 24);
        }
    } else if ((short)ins->imm < 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(-ins->imm) & ~1;
        int clz = __builtin_clz(-ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            SUB_I(0, arm_reg1, (-ins->imm & 0xffff) >> ctz, (32 - ctz) & 31);
        } else {
            SUB_I(0, arm_reg1, -ins->imm & 0xff, 0);
            SUB_I(0, 0, (-ins->imm >> 8) & 0xff, 24);
        }
    }

    LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
    AND_I(2, 0, 7, 8);
    ADD_IS(1, 1, 2, ARM_SHIFT_LSR, 22);
    LDR_IO(1, 1, DRC_RELOC_RBYTE*4);

    BLX(ARM_COND_AL, 1);

    // Add cycles returned in r1.
    if (access_time) ADD(10, 10, 1);

    if (ins->opcode == V810_OP_IN_B) {
        UXTB(arm_reg2, 0, 0);
        reg2_modified = true;
    } else {
        SAVE_REG2(0);
    }
    INST_TEARDOWN
}

void drc_ld_h(v810_instruction *ins, bool access_time) {
    INST_SETUP
    if (arm_reg1 < 4) arm_reg1 = 0;

    if (ins->imm == 0) {
        RELOAD_REG1(0);
    } else if ((short)ins->imm > 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ADD_I(0, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ADD_I(0, arm_reg1, ins->imm & 0xff, 0);
            ADD_I(0, 0, ins->imm >> 8, 24);
        }
    } else if ((short)ins->imm < 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(-ins->imm) & ~1;
        int clz = __builtin_clz(-ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            SUB_I(0, arm_reg1, (-ins->imm & 0xffff) >> ctz, (32 - ctz) & 31);
        } else {
            SUB_I(0, arm_reg1, -ins->imm & 0xff, 0);
            SUB_I(0, 0, (-ins->imm >> 8) & 0xff, 24);
        }
    }

    LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
    AND_I(2, 0, 7, 8);
    ADD_IS(1, 1, 2, ARM_SHIFT_LSR, 22);
    LDR_IO(1, 1, DRC_RELOC_RHWORD*4);

    BLX(ARM_COND_AL, 1);

    // Add cycles returned in r1.
    if (access_time) ADD(10, 10, 1);

    if (ins->opcode == V810_OP_IN_H) {
        UXTH(arm_reg2, 0, 0);
        reg2_modified = true;
    } else {
        SAVE_REG2(0);
    }
    INST_TEARDOWN
}


void drc_ld_w(v810_instruction *ins, bool access_time) {
    INST_SETUP
    if (arm_reg1 < 4) arm_reg1 = 0;

    if (ins->imm == 0) {
        RELOAD_REG1(0);
    } else if ((short)ins->imm > 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ADD_I(0, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ADD_I(0, arm_reg1, ins->imm & 0xff, 0);
            ADD_I(0, 0, ins->imm >> 8, 24);
        }
    } else if ((short)ins->imm < 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(-ins->imm) & ~1;
        int clz = __builtin_clz(-ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            SUB_I(0, arm_reg1, (-ins->imm & 0xffff) >> ctz, (32 - ctz) & 31);
        } else {
            SUB_I(0, arm_reg1, -ins->imm & 0xff, 0);
            SUB_I(0, 0, (-ins->imm >> 8) & 0xff, 24);
        }
    }

    LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
    AND_I(2, 0, 7, 8);
    ADD_IS(1, 1, 2, ARM_SHIFT_LSR, 22);
    LDR_IO(1, 1, DRC_RELOC_RWORD*4);

    BLX(ARM_COND_AL, 1);

    // Add cycles returned in r1.
    if (access_time) ADD(10, 10, 1);

    SAVE_REG2(0);
    INST_TEARDOWN
}

void drc_st_b(v810_instruction *ins, bool access_time) {
    INST_SETUP
    if (arm_reg1 < 4) arm_reg1 = 0;

    if (ins->imm == 0) {
        RELOAD_REG1(0);
    } else if ((short)ins->imm > 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ADD_I(0, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ADD_I(0, arm_reg1, ins->imm & 0xff, 0);
            ADD_I(0, 0, ins->imm >> 8, 24);
        }
    } else if ((short)ins->imm < 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(-ins->imm) & ~1;
        int clz = __builtin_clz(-ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            SUB_I(0, arm_reg1, (-ins->imm & 0xffff) >> ctz, (32 - ctz) & 31);
        } else {
            SUB_I(0, arm_reg1, -ins->imm & 0xff, 0);
            SUB_I(0, 0, (-ins->imm >> 8) & 0xff, 24);
        }
    }

    if (ins->reg2 == 0)
        MOV_I(1, 0, 0);
    else
        RELOAD_REG2(1);

    LDR_IO(3, 11, offsetof(cpu_state, reloc_table));
    AND_I(2, 0, 7, 8);
    ADD_IS(3, 3, 2, ARM_SHIFT_LSR, 22);
    LDR_IO(3, 3, DRC_RELOC_WBYTE*4);

    BLX(ARM_COND_AL, 3);

    // Add cycles returned in r0.
    if (access_time) ADD(10, 10, 0);
    INST_TEARDOWN
}

void drc_st_h(v810_instruction *ins, bool access_time) {
    INST_SETUP
    if (arm_reg1 < 4) arm_reg1 = 0;

    if (ins->imm == 0) {
        RELOAD_REG1(0);
    } else if ((short)ins->imm > 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ADD_I(0, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ADD_I(0, arm_reg1, ins->imm & 0xff, 0);
            ADD_I(0, 0, ins->imm >> 8, 24);
        }
    } else if ((short)ins->imm < 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(-ins->imm) & ~1;
        int clz = __builtin_clz(-ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            SUB_I(0, arm_reg1, (-ins->imm & 0xffff) >> ctz, (32 - ctz) & 31);
        } else {
            SUB_I(0, arm_reg1, -ins->imm & 0xff, 0);
            SUB_I(0, 0, (-ins->imm >> 8) & 0xff, 24);
        }
    }

    if (ins->reg2 == 0)
        MOV_I(1, 0, 0);
    else
        RELOAD_REG2(1);

    LDR_IO(3, 11, offsetof(cpu_state, reloc_table));
    AND_I(2, 0, 7, 8);
    ADD_IS(3, 3, 2, ARM_SHIFT_LSR, 22);
    LDR_IO(3, 3, DRC_RELOC_WHWORD*4);

    BLX(ARM_COND_AL, 3);

    // Add cycles returned in r0.
    if (access_time) ADD(10, 10, 0);
    INST_TEARDOWN
}

void drc_st_w(v810_instruction *ins, bool access_time) {
    INST_SETUP
    if (arm_reg1 < 4) arm_reg1 = 0;

    if (ins->imm == 0) {
        RELOAD_REG1(0);
    } else if ((short)ins->imm > 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ADD_I(0, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ADD_I(0, arm_reg1, ins->imm & 0xff, 0);
            ADD_I(0, 0, ins->imm >> 8, 24);
        }
    } else if ((short)ins->imm < 0) {
        LOAD_REG1();
        int ctz = __builtin_ctz(-ins->imm) & ~1;
        int clz = __builtin_clz(-ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            SUB_I(0, arm_reg1, (-ins->imm & 0xffff) >> ctz, (32 - ctz) & 31);
        } else {
            SUB_I(0, arm_reg1, -ins->imm & 0xff, 0);
            SUB_I(0, 0, (-ins->imm >> 8) & 0xff, 24);
        }
    }

    if (ins->reg2 == 0)
        MOV_I(1, 0, 0);
    else
        RELOAD_REG2(1);

    LDR_IO(3, 11, offsetof(cpu_state, reloc_table));
    AND_I(2, 0, 7, 8);
    ADD_IS(3, 3, 2, ARM_SHIFT_LSR, 22);
    LDR_IO(3, 3, DRC_RELOC_WWORD*4);

    BLX(ARM_COND_AL, 3);

    // Add cycles returned in r0.
    if (access_time) ADD(10, 10, 0);
    INST_TEARDOWN
}

void drc_bstr(v810_instruction *ins, unsigned *cycles) {
    MOV_I(2, 31, 0);
    if (ins->imm >= 4) {
        // non-search, we have a destination
        // v810 r26 << 5 -> arm r3
        if (!phys_regs[26]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[26]));
        AND(3, phys_regs[26], 2);
        // v810 r27 -> arm r3
        if (!phys_regs[27]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[27]));
        AND(0, phys_regs[27], 2);
        ORR_IS(3, 0, 3, ARM_SHIFT_LSL, 5);

        // cycle count << 10 -> arm r3
        LDR_IO(0, 11, offsetof(cpu_state, cycles_until_event_partial));
        ORR_IS(3, 3, 0, ARM_SHIFT_LSL, 10);
    } else {
        // search, we only have a source
        // v810 r27 -> arm r3 lo
        if (!phys_regs[27]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[27]));
        AND(3, phys_regs[27], 2);
    }

    // mov r2, ~3
    new_data_proc_imm(ARM_COND_AL, ARM_OP_MVN, 0, 0, 2, 0, 3);
    if (ins->imm >= 4) {
        // non-search, clear the bottom two bits
        // v810 r29 & (~3) -> arm r1
        if (!phys_regs[29]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[29]));
        AND(1, phys_regs[29], 2);
    } else {
        // search, leave as-is
        // v810 r29 -> arm r1
        if (!phys_regs[29]) LDR_IO(1, 11, offsetof(cpu_state, P_REG[29]));
        else MOV(1, phys_regs[29]);
    }

    // v810 r30 & (~3) -> arm r0
    if (!phys_regs[30]) LDR_IO(0, 11, offsetof(cpu_state, P_REG[30]));
    AND(0, phys_regs[30], 2);

    // v810 r28 -> arm r2
    if (!phys_regs[28]) LDR_IO(2, 11, offsetof(cpu_state, P_REG[28]));
    else MOV(2, phys_regs[28]);

    // call the function
    PUSH(1<<5);
    LDR_IO(5, 11, offsetof(cpu_state, reloc_table));
    LDR_IO(5, 5, (DRC_RELOC_BSTR+ins->imm)*4);
    BLX(ARM_COND_AL, 5);
    POP(1<<5);

    // reload registers
    for (int j = ins->imm >= 4 ? 26 : 27; j <= 30; j++)
        if (phys_regs[j])
            LDR_IO(phys_regs[j], 11, offsetof(cpu_state, P_REG[j]));
    if (ins->imm < 4) {
        // zero flag for search
        ORRS(0, 0, 0);
    } else {
        // add cycles and check interrupt
        ADD(10, 10, 0);
        drc_handle_interrupts(ins->PC, cycles);
        int len_reg = phys_regs[28];
        if (!len_reg) {
            LDR_IO(0, 11, offsetof(cpu_state, P_REG[28]));
        }
        MRS(1);
        CMP_I(len_reg, 0, 0);
        Boff(ARM_COND_EQ, 3);
        MRS(1);
        B(ARM_COND_AL, 0); // branches to start of instruction
        MRS(1);
    }
}

BEGIN_INSTR(jmp)
    LOAD_REG1();
    STR_IO(arm_reg1, 11, offsetof(cpu_state, PC));
    POP(1 << 15);
END_INSTR()

BEGIN_INSTR(movhi)
    // we need to check if it's 0 to avoid UB with ctz and clz
    if (ins->imm != 0) {
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int neg_ctz = __builtin_ctz(-ins->imm) & ~1;
        int neg_clz = __builtin_clz(-ins->imm << 16);
        int cto = __builtin_ctz(~ins->imm) & ~1;
        int clo = __builtin_clz(~ins->imm << 16);
        int width = (16 - clz) - ctz;
        int neg_width = (16 - neg_clz) - neg_ctz;
        int inv_width = (16 - clo) - cto;
        if (width <= 8) {
            // normal
            if (ins->reg1 != 0) {
                LOAD_REG1();
                ADD_I(arm_reg2, arm_reg1, ins->imm >> ctz, 16 - ctz);
            } else {
                MOV_I(arm_reg2, ins->imm >> ctz, 16 - ctz);
            }
        } else if ((ins->imm & 0x8000) && neg_width <= 8 && ins->reg1 != 0) {
            // negative
            LOAD_REG1();
            SUB_I(arm_reg2, arm_reg1, -(ins->imm >> neg_ctz) & 0xff, 16 - neg_ctz);
        } else {
            // full-size
            if (ins->reg1 != 0) {
                LOAD_REG1();
                ADD_I(arm_reg2, arm_reg1, ins->imm >> 8, 8);
            } else {
                MOV_I(arm_reg2, ins->imm >> 8, 8);
            }
            ADD_I(arm_reg2, arm_reg2, ins->imm & 0xFF, 16);
        }
        reg2_modified = true;
    } else {
        // it's just a mov at this point
        RELOAD_REG1(arm_reg2);
        if (arm_reg1 != arm_reg2) reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(movea)
    // we need to check if it's 0 to avoid UB with ctz and clz
    if (ins->imm != 0) {
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int neg_ctz = __builtin_ctz(-ins->imm) & ~1;
        int neg_clz = __builtin_clz(-ins->imm << 16);
        int cto = __builtin_ctz(~ins->imm) & ~1;
        int clo = __builtin_clz(~ins->imm << 16);
        int width = (16 - clz) - ctz;
        int neg_width = (16 - neg_clz) - neg_ctz;
        int inv_width = (16 - clo) - cto;
        if (!(ins->imm & 0x8000) && width <= 8) {
            // normal
            if (ins->reg1 != 0) {
                LOAD_REG1();
                ADD_I(arm_reg2, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
            } else {
                MOV_I(arm_reg2, ins->imm >> ctz, (32 - ctz) & 31);
            }
        } else if ((ins->imm & 0x8000) && neg_width <= 8 && ins->reg1 != 0) {
            // negative, with alt register
            LOAD_REG1();
            SUB_I(arm_reg2, arm_reg1, (-ins->imm & 0xffff) >> neg_ctz, (32 - neg_ctz) & 31);
        } else if (ins->imm == 0xFFFF || ((ins->imm & 0x8000) && inv_width <= 8)) {
            // inverted
            if (ins->reg1 != 0) {
                LOAD_REG1();
                MVN_I(0, ((~ins->imm) & 0xffff) >> cto, (32 - cto) & 31);
                ADD(arm_reg2, arm_reg1, 0);
            } else {
                MVN_I(arm_reg2, ((~ins->imm & 0xffff) >> cto), (32 - cto) & 31);
            }
        } else if ((ins->imm & 0x8000) && neg_width <= 8 && ins->reg1 == 0) {
            // negative, with zero register
            LOAD_REG1();
            SUB_I(arm_reg2, arm_reg1, (-ins->imm & 0xffff) >> neg_ctz, (32 - neg_ctz) & 31);
        } else {
            if (!(ins->imm & 0x8000)) {
                if (ins->reg1 != 0) {
                    LOAD_REG1();
                    ADD_I(arm_reg2, arm_reg1, ins->imm >> 8, 24);
                } else {
                    MOV_I(arm_reg2, ins->imm >> 8, 24);
                }
                ADD_I(arm_reg2, arm_reg2, ins->imm & 0xFF, 0);
            } else {
                LOAD_REG1();
                SUB_I(arm_reg2, arm_reg1, (-ins->imm & 0xffff) >> 8, 24);
                SUB_I(arm_reg2, arm_reg2, -ins->imm & 0xff, 0);
            }
        }
        reg2_modified = true;
    } else {
        // it's just a mov at this point
        RELOAD_REG1(arm_reg2);
        if (arm_reg1 != arm_reg2) reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(mov)
    RELOAD_REG1(arm_reg2);
    if (arm_reg1 != arm_reg2) reg2_modified = true;
END_INSTR()

BEGIN_INSTR(add)
    LOAD_REG1();
    LOAD_REG2();
    ADDS(arm_reg2, arm_reg2, arm_reg1);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(sub)
    LOAD_REG1();
    LOAD_REG2();
    SUBS(arm_reg2, arm_reg2, arm_reg1);
    INV_CARRY();
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(cmp)
    LOAD_REG2();
    if (ins->reg1 == 0) {
        CMP_I(arm_reg2, 0, 0);
    } else {
        LOAD_REG1();
        if (ins->reg2 == 0)
            RSBS_I(0, arm_reg1, 0, 0);
        else
            CMP(arm_reg2, arm_reg1);
    }
    INV_CARRY();
END_INSTR()

BEGIN_INSTR(shl)
    LOAD_REG1();
    LOAD_REG2();
    MOV(0, arm_reg1);
    AND_I(0, 0, 0x1F, 0);
    LSLS(arm_reg2, arm_reg2, 0);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(shr)
    LOAD_REG1();
    LOAD_REG2();
    MOV(0, arm_reg1);
    AND_I(0, 0, 0x1F, 0);
    LSRS(arm_reg2, arm_reg2, 0);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(sar)
    LOAD_REG1();
    LOAD_REG2();
    MOV(0, arm_reg1);
    AND_I(0, 0, 0x1F, 0);
    ASRS(arm_reg2, arm_reg2, 0);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(mul)
    LOAD_REG1();
    LOAD_REG2();
    SMULLS(arm_reg2, ins->reg2 != 30 ? phys_regs[30] : 0, arm_reg2, arm_reg1);
    // If the 30th register isn't being used in the block, the high
    // word of the multiplication will be in r0 (because
    // phys_regs[30] == 0) and we'll have to save it manually
    if (!phys_regs[30]) {
        STR_IO(0, 11, offsetof(cpu_state, P_REG[30]));
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(mulu)
    LOAD_REG1();
    LOAD_REG2();
    UMULLS(arm_reg2, ins->reg2 != 30 ? phys_regs[30] : 0, arm_reg2, arm_reg1);
    if (!phys_regs[30]) {
        STR_IO(0, 11, offsetof(cpu_state, P_REG[30]));
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(div)
    // reg2/reg1 -> reg2 (r0)
    // reg2%reg1 -> r30 (r1)

    // load function and save flags (interleaved)
    MRS(0);
    LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
    PUSH(1 << 0);
    LDR_IO(2, 2, DRC_RELOC_IDIVMOD*4);

    RELOAD_REG2(0);
    RELOAD_REG1(1);
    BLX(ARM_COND_AL, 2);

    if (ins->reg2 != 30) {
        if (!phys_regs[30])
            STR_IO(1, 11, offsetof(cpu_state, P_REG[30]));
        else
            MOV(phys_regs[30], 1);
    }
    SAVE_REG2(0);

    // restore flags
    POP(1 << 1);
    MSR(1);

    // flags
    ORRS(0, 0, 0);
END_INSTR()

BEGIN_INSTR(divu)
    // reg2/reg1 -> reg2 (r0)
    // reg2%reg1 -> r30 (r1)

    // load function and save flags (interleaved)
    MRS(0);
    LDR_IO(2, 11, offsetof(cpu_state, reloc_table));
    PUSH(1 << 0);
    LDR_IO(2, 2, DRC_RELOC_UIDIVMOD*4);

    RELOAD_REG2(0);
    RELOAD_REG1(1);
    BLX(ARM_COND_AL, 2);

    if (ins->reg2 != 30) {
        if (!phys_regs[30])
            STR_IO(1, 11, offsetof(cpu_state, P_REG[30]));
        else
            MOV(phys_regs[30], 1);
    }
    SAVE_REG2(0);

    // restore flags
    POP(1 << 1);
    MSR(1);

    // flags
    ORRS(0, 0, 0);
END_INSTR()

BEGIN_INSTR(or)
    LOAD_REG1();
    LOAD_REG2();
    ORRS(arm_reg2, arm_reg2, arm_reg1);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(and)
    LOAD_REG1();
    LOAD_REG2();
    ANDS(arm_reg2, arm_reg2, arm_reg1);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(xor)
    LOAD_REG1();
    LOAD_REG2();
    EORS(arm_reg2, arm_reg2, arm_reg1);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(not)
    LOAD_REG1();
    MVNS(arm_reg2, arm_reg1);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(mov_i)
    if (!(ins->imm & 0x10)) {
        MOV_I(arm_reg2, ins->imm, 0);
    } else {
        MVN_I(arm_reg2, ~sign_5(ins->imm), 0);
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(add_i)
    LOAD_REG2();
    if (!(ins->imm & 0x10)) {
        ADDS_I(arm_reg2, arm_reg2, ins->imm, 0);
    } else {
        MOV_I(0, (sign_5(ins->imm) & 0xFF), 8);
        ADDS_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_ASR, 24);
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(cmp_i)
    LOAD_REG2();
    if (!(ins->imm & 0x10)) {
        CMP_I(arm_reg2, ins->imm, 0);
    } else {
        MOV_I(0, (sign_5(ins->imm) & 0xFF), 8);
        CMP_IS(arm_reg2, 0, ARM_SHIFT_ASR, 24);
    }
    INV_CARRY();
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(shl_i)
    LOAD_REG2();
    // lsl reg2, reg2, #imm5
    new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, ins->imm, ARM_SHIFT_LSL, arm_reg2);
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(shr_i)
    // arm doesn't do 0-bit immediate right shifts
    if (ins->imm != 0) {
        LOAD_REG2();
        // lsr reg2, reg2, #imm5
        new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, ins->imm, ARM_SHIFT_LSR, arm_reg2);
        reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(sar_i)
    // arm doesn't do 0-bit immediate right shifts
    if (ins->imm != 0) {
        LOAD_REG2();
        // asr reg2, reg2, #imm5
        new_data_proc_imm_shift(ARM_COND_AL, ARM_OP_MOV, 1, 0, arm_reg2, ins->imm, ARM_SHIFT_ASR, arm_reg2);
        reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(andi)
    if (ins->imm == 0 || ins->reg1 == 0) {
        MOVS_I(arm_reg2, 0, 0);
    } else if (ins->imm == 0xFFFF) {
        LOAD_REG1();
        BIC_I(arm_reg2, arm_reg1, 0xff, 8);
        BICS_I(arm_reg2, arm_reg2, 0xff, 16);
    } else {
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int cto = __builtin_ctz(~ins->imm) & ~1;
        int clo = __builtin_clz(~ins->imm << 16);
        int width = (16 - clz) - ctz;
        int inv_width = (16 - clo) - cto;
        LOAD_REG1();
        if (width <= 8) {
            ANDS_I(arm_reg2, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else if (inv_width <= 8) {
            UXTH(arm_reg2, arm_reg1, 0);
            BICS_I(arm_reg2, arm_reg2, (~ins->imm & 0xFFFF) >> cto, (32 - cto) & 31);
        } else {
            MOV_I(0, ins->imm >> 8, 24);
            ORR_I(0, 0, ins->imm & 0xFF, 0);
            ANDS(arm_reg2, arm_reg1, 0);
        }
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(xori)
    LOAD_REG1();
    if (ins->imm != 0) {
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            EORS_I(arm_reg2, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            EOR_I(arm_reg2, arm_reg1, ins->imm >> 8, 24);
            EORS_I(arm_reg2, arm_reg2, ins->imm & 0xFF, 0);
        }
        reg2_modified = true;
    } else {
        // it's effectively a mov with flags at this point
        MOVS(arm_reg2, arm_reg1);
        if (arm_reg1 != arm_reg2) reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(ori)
LOAD_REG1();
    if (ins->imm != 0) {
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (width <= 8) {
            ORRS_I(arm_reg2, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            ORR_I(arm_reg2, arm_reg1, ins->imm >> 8, 24);
            ORRS_I(arm_reg2, arm_reg2, ins->imm & 0xFF, 0);
        }
        reg2_modified = true;
    } else {
        // it's effectively a mov with flags at this point
        MOVS(arm_reg2, arm_reg1);
        if (arm_reg1 != arm_reg2) reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(addi)
    LOAD_REG1();
    if (ins->imm != 0) {
        int ctz = __builtin_ctz(ins->imm) & ~1;
        int clz = __builtin_clz(ins->imm << 16);
        int width = (16 - clz) - ctz;
        if (clz != 0 && width <= 8) {
            ADDS_I(arm_reg2, arm_reg1, ins->imm >> ctz, (32 - ctz) & 31);
        } else {
            if (clz == 0) {
                if (ins->imm == 0xFFFF) {
                    MVN_I(0, 0, 0);
                } else {
                    int inv_ctz = __builtin_ctz(~ins->imm) & ~1;
                    int inv_clz = __builtin_clz(~ins->imm << 16);
                    int inv_width = (16 - inv_clz) - inv_ctz;
                    if (inv_width <= 8) {
                        MVN_I(0, (~ins->imm & 0xffff) >> inv_ctz, (32 - inv_ctz) & 31);
                    } else {
                        MVN_I(0, ~ins->imm & 0xff, 0);
                        BIC_I(0, 0, ~ins->imm >> 8, 24);
                    }
                }
            } else {
                MOV_I(0, (ins->imm >> 8), 24);
                ORR_I(0, 0, (ins->imm & 0xFF), 0);
            }
            ADDS(arm_reg2, arm_reg1, 0);
        }
        reg2_modified = true;
    } else {
        // it's effectively a mov with flags at this point
        LOAD_REG1();
        MOVS(arm_reg2, arm_reg1);
        if (arm_reg1 != arm_reg2) reg2_modified = true;
    }
END_INSTR()

BEGIN_INSTR(ldsr)
    // Stores reg2 in vb_state->v810_state.S_REG[regID]
    LOAD_REG2();
    STR_IO(arm_reg2, 11, offsetof(cpu_state, S_REG[ins->imm]));
    if (ins->imm == PSW || ins->imm == EIPSW) {
        // load status register
        if (ins->imm == PSW)
            MRS(0);
        else
            LDR_IO(0, 11, offsetof(cpu_state, except_flags));
        // clear out condition flags
        BIC_I(0, 0, 0xf, 4);
        // zero flag
        TST_I(arm_reg2, 1, 0);
        ORRCC_I(ARM_COND_NE, 0, 1, 2);
        // sign flag
        TST_I(arm_reg2, 2, 0);
        ORRCC_I(ARM_COND_NE, 0, 2, 2);
        // overflow flag
        TST_I(arm_reg2, 4, 0);
        ORRCC_I(ARM_COND_NE, 0, 1, 4);
        // carry flag
        TST_I(arm_reg2, 8, 0);
        ORRCC_I(ARM_COND_NE, 0, 2, 4);
        // save status register
        if (ins->imm == PSW)
            MSR(0);
        else
            STR_IO(0, 11, offsetof(cpu_state, except_flags));
    }
END_INSTR()

BEGIN_INSTR(stsr)
    // Loads vb_state->v810_state.S_REG[regID] into reg2
    LOAD_REG2();
    LDR_IO(arm_reg2, 11, offsetof(cpu_state, S_REG[ins->imm]));
    if (ins->imm == PSW || ins->imm == EIPSW) {
        // clear out condition flags
        BIC_I(arm_reg2, arm_reg2, 0xf, 0);
        // load except flags if relevant
        if (ins->imm == EIPSW) {
            MRS(0);
            LDR_IO(1, 11, offsetof(cpu_state, except_flags));
            MSR(1);
        }
        // fill in the actual condition flags
        ORRCC_I(ARM_COND_EQ, arm_reg2, 1, 0);
        ORRCC_I(ARM_COND_MI, arm_reg2, 2, 0);
        ORRCC_I(ARM_COND_VS, arm_reg2, 4, 0);
        ORRCC_I(ARM_COND_CS, arm_reg2, 8, 0);
        // reload original flags
        if (ins->imm == EIPSW)
            MSR(0);
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(sei)
    // Set the 12th bit in vb_state->v810_state.S_REG[PSW]
    LDR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
    ORR_I(0, 0, 1, 20);
    STR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
END_INSTR()

BEGIN_INSTR(cli)
    // Clear the 12th bit in vb_state->v810_state.S_REG[PSW]
    LDR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
    BIC_I(0, 0, 1, 20);
    STR_IO(0, 11, offsetof(cpu_state, S_REG[PSW]));
END_INSTR()

BEGIN_INSTR(setf)
    if ((ins->imm & 0xF) == (V810_OP_BNH & 0xF)) {
        // C or Z
        MOV_I(arm_reg2, 0, 0);
        new_data_proc_imm(ARM_COND_EQ, ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
        new_data_proc_imm(ARM_COND_CS, ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
    } else if ((ins->imm & 0xF) == (V810_OP_BH & 0xF)) {
        // !C and !Z
        MOV_I(arm_reg2, 1, 0);
        new_data_proc_imm(ARM_COND_EQ, ARM_OP_MOV, 0, 0, arm_reg2, 0, 0);
        new_data_proc_imm(ARM_COND_CS, ARM_OP_MOV, 0, 0, arm_reg2, 0, 0);
    } else {
        MOV_I(arm_reg2, 0, 0);
        // mov<cond> reg2, 1
        new_data_proc_imm(cond_map[ins->imm & 0xF], ARM_OP_MOV, 0, 0, arm_reg2, 0, 1);
    }
    reg2_modified = true;
END_INSTR()

BEGIN_INSTR(fpp)
    switch (ins->imm) {
    case V810_OP_CVT_WS:
        LOAD_REG1();
        VMOV_SR(0, arm_reg1);
        VCVT_F32_S32(0, 0);
        VCMP_F32_0(0);
        VMRS();
        INV_CARRY();
        VMOV_RS(arm_reg2, 0);
        reg2_modified = true;
        break;
    case V810_OP_CVT_SW:
        LOAD_REG1();
        VMOV_SR(0, arm_reg1);
        VCVT_S32_F32(0, 0);
        VMOV_RS(arm_reg2, 0);
        ORRS(arm_reg2, arm_reg2, arm_reg2);
        reg2_modified = true;
        break;
    case V810_OP_CMPF_S:
        LOAD_REG1();
        LOAD_REG2();
        VMOV_SR(0, arm_reg1);
        VMOV_SR(1, arm_reg2);
        VCMP_F32(1, 0);
        VMRS();
        INV_CARRY();
        break;
    case V810_OP_ADDF_S:
        LOAD_REG1();
        LOAD_REG2();
        VMOV_SR(0, arm_reg1);
        VMOV_SR(1, arm_reg2);
        VADD_F32(0, 1, 0);
        VCMP_F32_0(0);
        VMRS();
        INV_CARRY();
        VMOV_RS(arm_reg2, 0);
        reg2_modified = true;
        break;
    case V810_OP_SUBF_S:
        LOAD_REG1();
        LOAD_REG2();
        VMOV_SR(0, arm_reg1);
        VMOV_SR(1, arm_reg2);
        VSUB_F32(0, 1, 0);
        VCMP_F32_0(0);
        VMRS();
        INV_CARRY();
        VMOV_RS(arm_reg2, 0);
        reg2_modified = true;
        break;
    case V810_OP_MULF_S:
        LOAD_REG1();
        LOAD_REG2();
        VMOV_SR(0, arm_reg1);
        VMOV_SR(1, arm_reg2);
        VMUL_F32(0, 1, 0);
        VCMP_F32_0(0);
        VMRS();
        INV_CARRY();
        VMOV_RS(arm_reg2, 0);
        reg2_modified = true;
        break;
    case V810_OP_DIVF_S:
        LOAD_REG1();
        LOAD_REG2();
        VMOV_SR(0, arm_reg1);
        VMOV_SR(1, arm_reg2);
        VDIV_F32(0, 1, 0);
        VCMP_F32_0(0);
        VMRS();
        INV_CARRY();
        VMOV_RS(arm_reg2, 0);
        reg2_modified = true;
        break;
    case V810_OP_XB:
        LOAD_REG2();
        REV(0, arm_reg2);
        MOV_IS(arm_reg2, arm_reg2, ARM_SHIFT_LSR, 16);
        MOV_IS(arm_reg2, arm_reg2, ARM_SHIFT_LSL, 16);
        ORR_IS(arm_reg2, arm_reg2, 0, ARM_SHIFT_LSR, 16);
        reg2_modified = true;
        break;
    case V810_OP_XH:
        LOAD_REG2();
        MOV_IS(arm_reg2, arm_reg2, ARM_SHIFT_ROR, 16);
        reg2_modified = true;
        break;
    case V810_OP_REV:
        // RBIT would be great here, but that's only in ARMv6T2, so we'll do it manually.
        LDR_IO(1, 11, offsetof(cpu_state, reloc_table));
        LDR_IO(1, 1, DRC_RELOC_REV*4);
        RELOAD_REG1(0);
        BLX(ARM_COND_AL, 1);
        MOV(arm_reg2, 0);
        reg2_modified = true;
        break;
    case V810_OP_TRNC_SW:
        LOAD_REG1();
        VMOV_SR(0, arm_reg1);
        TRUNC(0, 0);
        VMOV_RS(arm_reg2, 0);
        ORRS(arm_reg2, arm_reg2, arm_reg2);
        reg2_modified = true;
        break;
    case V810_OP_MPYHW:
        LOAD_REG1();
        LOAD_REG2();
        MOV_IS(0, arm_reg1, ARM_SHIFT_LSL, 15);
        MOV_IS(0, 0, ARM_SHIFT_ASR, 15);
        MUL(arm_reg2, 0, arm_reg2);
        reg2_modified = true;
        break;
    default:
        dprintf(0, "[DRC]: Invalid FPU subop 0x%lx\n", ins->imm);
        NOP();
        break;
    }
END_INSTR()

void drc_assemble(translated_inst *dst, ir_inst *ir, v810_instruction *v810) {
    if (ir->needs_branch) {
        WORD v810_dest = v810->PC + v810->branch_offset;
        translated_inst* arm_dest = drc_getEntry(v810_dest, NULL);
        int arm_offset = (int)(arm_dest - (dst) - 2);

        if (arm_dest == cache_start) {
            // Should be fixed, but just in case
            dprintf(0, "WARN:can't jump from %lx to %lx\n", v810->PC, v810_dest);
        }

        ir->b_bl.imm = arm_offset & 0xffffff;
    }

    arm_assemble(dst, ir);
}
