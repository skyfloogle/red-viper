#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_types.h"
#include "drc_core.h"

int interpreter_run() {
    // keep PC and cycles in local variables for extra speed
    // can't do this with PSW because interrupts modify it
    WORD PC = v810_state->PC;
    WORD cycles = v810_state->cycles;
    BYTE last_opcode = 0;
    do {
        HWORD instr = mem_rhword(PC);
        PC += 2;
        BYTE opcode = instr >> 10;
        BYTE reg1 = instr & 31;
        BYTE reg2 = (instr >> 5) & 31;
        cycles += opcycle[opcode];
        if (!(opcode & 0x20)) {
            // small instr
            WORD reg1_val = 0;
            if (!(opcode & 0x10) && reg1) reg1_val = v810_state->P_REG[reg1];
            switch (opcode) {
                case V810_OP_JMP:
                    PC = reg1_val;
                    break;
                case V810_OP_MOV_I: {
                    WORD imm = reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1;
                    v810_state->P_REG[reg2] = imm;
                    break;
                }
                default: {
                    return DRC_ERR_BAD_INST;
                }
            }
        } else if (!(opcode & 0x18)) {
            // branch
            return DRC_ERR_BAD_INST;
        } else {
            // long instr
            HWORD instr2 = mem_rhword(PC);
            PC += 2;
            switch (opcode) {
                case V810_OP_ST_B: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    mem_wbyte(reg1_val + instr2, reg2);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                default: {
                    return DRC_ERR_BAD_INST;
                }
            }
        }
        if (serviceDisplayInt(cycles, PC) == 0 || serviceDisplayInt(cycles, PC) == 0) {
            // interrupt triggered, so we exit
            // PC was modified so don't reset it
            v810_state->cycles = cycles;
            return 0;
        }
        last_opcode = opcode;
    } while (!v810_state->ret && (PC & 0x07000000) != 0x07000000);
    v810_state->PC = PC;
    v810_state->cycles = cycles;
    return 0;
}