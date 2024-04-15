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
        if (opcode < 0x20) {
            // small instr
            WORD reg1_val = 0;
            if (!(opcode & 0x10) && reg1) reg1_val = v810_state->P_REG[reg1];
            switch (opcode) {
                case V810_OP_MOV:
                    v810_state->P_REG[reg2] = reg1_val;
                    break;
                // case V810_OP_ADD:
                // case V810_OP_SUB:
                // case V810_OP_CMP:
                // case V810_OP_SHL:
                // case V810_OP_SHR:
                case V810_OP_JMP:
                    PC = reg1_val;
                    break;
                // case V810_OP_SAR:
                // case V810_OP_MUL:
                // case V810_OP_DIV:
                // case V810_OP_MULU:
                // case V810_OP_DIVU:
                case V810_OP_OR: {
                    WORD res = v810_state->P_REG[reg2] | reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_AND: {
                    WORD res = v810_state->P_REG[reg2] & reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_XOR: {
                    WORD res = v810_state->P_REG[reg2] ^ reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_NOT: {
                    WORD res = ~reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_MOV_I: {
                    WORD imm = reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1;
                    v810_state->P_REG[reg2] = imm;
                    break;
                }
                // case V810_OP_ADD_I:
                // case V810_OP_SETF:
                // case V810_OP_CMP_I:
                // case V810_OP_SHL_I:
                // case V810_OP_SHR_I:
                case V810_OP_CLI:
                    v810_state->S_REG[PSW] &= ~(1 << 12);
                    break;
                // case V810_OP_SAR_I:
                // case V810_OP_TRAP:
                // case V810_OP_RETI:
                // case V810_OP_HALT:
                case V810_OP_LDSR:
                    v810_state->S_REG[reg1] = v810_state->P_REG[reg2];
                    break;
                case V810_OP_STSR:
                    v810_state->P_REG[reg2] = v810_state->S_REG[reg1];
                    break;
                case V810_OP_SEI:
                    v810_state->S_REG[PSW] |= 1 << 12;
                    break;
                // case V810_OP_BSTR:
                default: {
                    return DRC_ERR_BAD_INST;
                }
            }
        } else if (opcode < 0x28) {
            // branch
            return DRC_ERR_BAD_INST;
        } else {
            // long instr
            HWORD instr2 = mem_rhword(PC);
            PC += 2;
            switch (opcode) {
                case V810_OP_MOVEA: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = reg1_val + (SHWORD)instr2;
                    break;
                }
                case V810_OP_JAL:
                    v810_state->P_REG[31] = PC;
                    // fallthrough
                case V810_OP_JR: {
                    WORD disp = instr2 | (instr << 16);
                    if (disp & 0x04000000) disp |= 0xfc000000;
                    else disp &= ~(0xfc000000);
                    PC += disp - 4;
                    break;
                }
                case V810_OP_ORI: {
                    WORD res = instr2;
                    if (reg1) res |= v810_state->P_REG[reg1];
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_ANDI: {
                    WORD res = 0;
                    if (reg1) res = v810_state->P_REG[reg1] & instr2;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_XORI: {
                    WORD res = instr2;
                    if (reg1) res ^= v810_state->P_REG[reg1];
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_MOVHI: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = reg1_val + ((WORD)instr2 << 16);
                    break;
                }
                // case V810_OP_LD_B:
                // case V810_OP_LD_H:
                // case V810_OP_LD_W:
                // case V810_OP_IN_B:
                // case V810_OP_IN_H:
                // case V810_OP_IN_W:
                case V810_OP_ST_B: case V810_OP_OUT_B: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    mem_wbyte(reg1_val + instr2, reg2);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                case V810_OP_ST_H: case V810_OP_OUT_H: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    mem_whword(reg1_val + instr2, reg2);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                case V810_OP_ST_W: case V810_OP_OUT_W: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    mem_wword(reg1_val + instr2, reg2);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 4 cycles instead of 1
                        cycles += 3;
                    }
                    break;
                }
                // case V810_OP_CAXI:
                // case V810_OP_FPP:
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