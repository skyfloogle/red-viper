#include <math.h>
#include "v810_cpu.h"
#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_types.h"
#include "drc_core.h"

static bool get_cond(BYTE code, WORD psw) {
    bool cond = false;
    switch (0x40 | (code & ~8)) {
        case V810_OP_BV: cond = psw & 4; break;
        case V810_OP_BL: cond = psw & 8; break;
        case V810_OP_BE: cond = psw & 1; break;
        case V810_OP_BNH: cond = psw & 9; break;
        case V810_OP_BN: cond = psw & 2; break;
        case V810_OP_BR: cond = true; break;
        case V810_OP_BLT: cond = !!(psw & 4) != !!(psw & 2); break;
        case V810_OP_BLE: cond = (psw & 1) || !!(psw & 4) != !!(psw & 2); break;
    }
    if (code & 8) cond = !cond;
    return cond;
}

int interpreter_run(void) {
    // keep PC and cycles in local variables for extra speed
    // can't do this with PSW because interrupts modify it
    WORD PC = v810_state->PC;
    WORD last_PC = PC;
    WORD cycles = v810_state->cycles;
    BYTE last_opcode = 0;
    WORD target = 0;
    do {
        if (cycles >= target) {
            v810_state->PC = PC;
            if ((serviceInt(cycles, PC) || serviceDisplayInt(cycles, PC)) && PC != v810_state->PC) {
                // interrupt triggered, so we exit
                // PC was modified so don't reset it
                v810_state->cycles = cycles;
                return 0;
            }
            target = cycles + v810_state->cycles_until_event_partial;
        }
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
                case V810_OP_ADD: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = reg2_val + reg1_val;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = (SWORD)(~(reg2_val ^ reg1_val) & (reg2_val ^ res)) < 0;
                    bool cy = (unsigned)res < (unsigned)reg2_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_SUB: case V810_OP_CMP: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = reg2_val - reg1_val;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = (SWORD)((reg2_val ^ reg1_val) & (reg2_val ^ res)) < 0;
                    bool cy = (unsigned)reg2_val < (unsigned)reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    if (opcode == V810_OP_SUB) v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_SHL: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    reg1_val &= 31;
                    WORD res = reg2_val << reg1_val;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = false;
                    bool cy = reg1_val != 0 ? (reg2_val >> (32 - reg1_val)) & 1 : 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_SHR: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    reg1_val &= 31;
                    WORD res = reg2_val >> reg1_val;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = false;
                    bool cy = reg1_val != 0 ? (reg2_val >> (reg1_val - 1)) & 1 : 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_JMP:
                    PC = reg1_val;
                    break;
                case V810_OP_SAR: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    reg1_val &= 31;
                    WORD res = (SWORD)reg2_val >> reg1_val;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = false;
                    bool cy = reg1_val != 0 ? (reg2_val >> (reg1_val - 1)) & 1 : 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_MUL: {
                    SWORD reg2_val = (SWORD)v810_state->P_REG[reg2];
                    SWORD res;
                    bool ov = __builtin_mul_overflow((SWORD)reg1_val, reg2_val, &res);
                    bool z = res == 0;
                    bool s = res < 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_DIV: {
                    SWORD reg2_val = (SWORD)v810_state->P_REG[reg2];
                    if (reg2_val == 0x80000000 && (SWORD)reg1_val == -1) {
                        v810_state->P_REG[30] = 0;
                        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | 6;
                    } else {
                        v810_state->P_REG[30] = reg2_val % (SWORD)reg1_val;
                        SWORD res = reg2_val / (SWORD)reg1_val;
                        bool z = res == 0;
                        bool s = res < 0;
                        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | z | (s << 1);
                        v810_state->P_REG[reg2] = res;
                    }
                    break;
                }
                case V810_OP_MULU: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res;
                    bool ov = __builtin_mul_overflow((SWORD)reg1_val, reg2_val, &res);
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_DIVU: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    v810_state->P_REG[30] = reg2_val % reg1_val;
                    WORD res = reg2_val / reg1_val;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | z | (s << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_OR: {
                    WORD res = (reg2 ? v810_state->P_REG[reg2] : 0) | reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_AND: {
                    WORD res = (reg2 ? v810_state->P_REG[reg2] : 0) & reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_XOR: {
                    WORD res = (reg2 ? v810_state->P_REG[reg2] : 0) ^ reg1_val;
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
                case V810_OP_ADD_I: {
                    WORD imm = reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1;
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = reg2_val + imm;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = (SWORD)(~(reg2_val ^ imm) & (reg2_val ^ res)) < 0;
                    bool cy = (unsigned)res < (unsigned)reg2_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_SETF: {
                    v810_state->P_REG[reg2] = get_cond(reg1, v810_state->S_REG[PSW]);
                    break;
                }
                case V810_OP_CMP_I: {
                    WORD imm = reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1;
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = reg2_val - imm;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = (SWORD)((reg2_val ^ imm) & (reg2_val ^ res)) < 0;
                    bool cy = (unsigned)reg2_val < (unsigned)imm;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    if (opcode == V810_OP_SUB) v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_SHL_I: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = reg2_val << reg1;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = false;
                    bool cy = reg1 != 0 ? (reg2_val >> (32 - reg1)) & 1 : 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_SHR_I: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = reg2_val >> reg1;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = false;
                    bool cy = reg1 != 0 ? (reg2_val >> (reg1 - 1)) & 1 : 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_CLI:
                    v810_state->S_REG[PSW] &= ~(1 << 12);
                    break;
                case V810_OP_SAR_I: {
                    WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;
                    WORD res = (SWORD)reg2_val >> reg1;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = false;
                    bool cy = reg1 != 0 ? (reg2_val >> (reg1 - 1)) & 1 : 0;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                // case V810_OP_TRAP:
                case V810_OP_RETI:
                    if (v810_state->S_REG[PSW] & PSW_NP) {
                        PC = v810_state->S_REG[FEPC];
                        v810_state->S_REG[PSW] = v810_state->S_REG[FEPSW];
                    } else {
                        PC = v810_state->S_REG[EIPC];
                        v810_state->S_REG[PSW] = v810_state->S_REG[EIPSW];
                    }
                    break;
                case V810_OP_HALT: {
                    cycles = target;
                    v810_state->PC = PC;
                    do {
                        cycles += v810_state->cycles_until_event_partial;
                        v810_state->cycles_until_event_partial = v810_state->cycles_until_event_full = 0;
                        v810_state->cycles = cycles;
                        serviceDisplayInt(cycles, PC);
                        serviceInt(cycles, PC);

                    } while (!v810_state->ret && v810_state->PC == PC);
                    break;
                }
                case V810_OP_LDSR:
                    v810_state->S_REG[reg1] = (reg2 ? v810_state->P_REG[reg2] : 0);
                    break;
                case V810_OP_STSR:
                    v810_state->P_REG[reg2] = v810_state->S_REG[reg1];
                    break;
                case V810_OP_SEI:
                    v810_state->S_REG[PSW] |= 1 << 12;
                    break;
                case V810_OP_BSTR: {
                    typedef bool (*bstr_func)(WORD,WORD,WORD,WORD);
                    bstr_func func = (bstr_func)bssuboptable[reg1].func;
                    WORD lastarg = reg1 < 4 ? v810_state->P_REG[27] & 31 : ((v810_state->P_REG[27] & 31)) | ((v810_state->P_REG[26] & 31) << 16);
                    bool res = func(v810_state->P_REG[30], v810_state->P_REG[29], v810_state->P_REG[28], lastarg);
                    if (reg1 < 4) {
                        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~1) | !res;
                    }
                    break;
                }
                default: {
                    v810_state->PC = last_PC;
                    return DRC_ERR_BAD_INST;
                }
            }
        } else if (opcode < 0x28) {
            // branch
            if (get_cond(instr >> 9, v810_state->S_REG[PSW])) {
                SHWORD disp = instr & (1 << 8) ? (instr | 0xfe00) : (instr & ~0xfe00);
                PC += disp - 2;
            } else {
                // branch not taken, so it only took 1 cycle
                cycles -= 2;
            }
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
                case V810_OP_ADDI: {
                    WORD reg1_val = v810_state->P_REG[reg1];
                    WORD imm = (SHWORD)instr2;
                    WORD res = reg1_val + imm;
                    bool z = res == 0;
                    bool s = (SWORD)res < 0;
                    bool ov = (SWORD)(~(reg1_val ^ imm) & (reg1_val ^ res)) < 0;
                    bool cy = (unsigned)res < (unsigned)reg1_val;
                    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
                    v810_state->P_REG[reg2] = res;
                    break;
                }
                case V810_OP_JAL:
                    v810_state->P_REG[31] = PC;
                    // fallthrough
                case V810_OP_JR: {
                    SWORD disp = instr2 | ((SWORD)instr << 16);
                    if (disp & 0x02000000) disp |= 0xfc000000;
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
                case V810_OP_LD_B: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = (SBYTE)mem_rbyte(reg1_val + (SHWORD)instr2);
                    if ((last_opcode & 0x34) == 0x30 && (last_opcode & 3) != 2) {
                    // load immediately following another load takes 2 cycles instead of 3
                        cycles -= 1;
                    } else if (opcycle[last_opcode] > 4) {
                        // load following instruction taking "many" cycles only takes 1 cycles
                        // guessing "many" is 4 for now
                        cycles -= 2;
                    }
                    break;
                }
                case V810_OP_LD_H: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = (SHWORD)mem_rhword(reg1_val + (SHWORD)instr2);
                    if ((last_opcode & 0x34) == 0x30 && (last_opcode & 3) != 2) {
                    // load immediately following another load takes 2 cycles instead of 3
                        cycles -= 1;
                    } else if (opcycle[last_opcode] > 4) {
                        // load following instruction taking "many" cycles only takes 1 cycles
                        // guessing "many" is 4 for now
                        cycles -= 2;
                    }
                    break;
                }
                case V810_OP_LD_W: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = mem_rword(reg1_val + (SHWORD)instr2);
                    if ((last_opcode & 0x34) == 0x30 && (last_opcode & 3) != 2) {
                    // load immediately following another load takes 4 cycles instead of 5
                        cycles -= 1;
                    } else if (opcycle[last_opcode] > 4) {
                        // load following instruction taking "many" cycles only takes 1 cycles
                        // guessing "many" is 4 for now
                        cycles -= 4;
                    }
                    break;
                }
                case V810_OP_IN_B: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = (BYTE)mem_rbyte(reg1_val + (SHWORD)instr2);
                    if ((last_opcode & 0x34) == 0x30 && (last_opcode & 3) != 2) {
                    // load immediately following another load takes 2 cycles instead of 3
                        cycles -= 1;
                    } else if (opcycle[last_opcode] > 4) {
                        // load following instruction taking "many" cycles only takes 1 cycles
                        // guessing "many" is 4 for now
                        cycles -= 2;
                    }
                    break;
                }
                case V810_OP_IN_H: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = (HWORD)mem_rhword(reg1_val + (SHWORD)instr2);
                    if ((last_opcode & 0x34) == 0x30 && (last_opcode & 3) != 2) {
                    // load immediately following another load takes 2 cycles instead of 3
                        cycles -= 1;
                    } else if (opcycle[last_opcode] > 4) {
                        // load following instruction taking "many" cycles only takes 1 cycles
                        // guessing "many" is 4 for now
                        cycles -= 2;
                    }
                    break;
                }
                case V810_OP_IN_W: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    v810_state->P_REG[reg2] = (WORD)mem_rword(reg1_val + (SHWORD)instr2);
                    if ((last_opcode & 0x34) == 0x30 && (last_opcode & 3) != 2) {
                    // load immediately following another load takes 4 cycles instead of 5
                        cycles -= 1;
                    } else if (opcycle[last_opcode] > 4) {
                        // load following instruction taking "many" cycles only takes 1 cycles
                        // guessing "many" is 4 for now
                        cycles -= 4;
                    }
                    break;
                }
                case V810_OP_ST_B: case V810_OP_OUT_B: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    BYTE reg2_val = 0;
                    if (reg2) reg2_val = v810_state->P_REG[reg2];
                    mem_wbyte(reg1_val + (SHWORD)instr2, reg2_val);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                case V810_OP_ST_H: case V810_OP_OUT_H: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    HWORD reg2_val = 0;
                    if (reg2) reg2_val = v810_state->P_REG[reg2];
                    mem_whword(reg1_val + (SHWORD)instr2, reg2_val);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                case V810_OP_ST_W: case V810_OP_OUT_W: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = v810_state->P_REG[reg1];
                    WORD reg2_val = 0;
                    if (reg2) reg2_val = v810_state->P_REG[reg2];
                    mem_wword(reg1_val + (SHWORD)instr2, reg2_val);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 4 cycles instead of 1
                        cycles += 3;
                    }
                    break;
                }
                // case V810_OP_CAXI:
                case V810_OP_FPP: {
                    int subop = instr2 >> 10;
                    #pragma GCC diagnostic push
                    #pragma GCC diagnostic ignored "-Wstrict-aliasing"
                    if (subop == V810_OP_CVT_WS) {
                        float res = (float)(SWORD)v810_state->P_REG[reg1];
                        bool z = res == 0;
                        int scy = res < 0 ? 0xa : 0;
                        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
                        *(float*)&v810_state->P_REG[reg2] = res;
                    } else if (!(subop & 8) || subop == V810_OP_TRNC_SW) {
                        // float
                        float reg1_val = *(float*)&v810_state->P_REG[reg1];
                        if (subop == V810_OP_CVT_SW) {
                            SWORD res = round(reg1_val);
                            bool z = res == 0;
                            int scy = res < 0 ? 2 : 0;
                            v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
                            v810_state->P_REG[reg2] = res;
                        } else if (subop == V810_OP_TRNC_SW) {
                            SWORD res = (SWORD)(reg1_val);
                            bool z = res == 0;
                            int scy = res < 0 ? 2 : 0;
                            v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
                            v810_state->P_REG[reg2] = res;
                        } else {
                            float reg2_val = *(float*)&v810_state->P_REG[reg2];
                            float res;
                            switch (subop) {
                                case V810_OP_ADDF_S:
                                    res = reg2_val + reg1_val;
                                    break;
                                case V810_OP_CMPF_S:
                                case V810_OP_SUBF_S:
                                    res = reg2_val - reg1_val;
                                    break;
                                case V810_OP_MULF_S:
                                    res = reg2_val * reg1_val;
                                    break;
                                case V810_OP_DIVF_S:
                                    res = reg2_val / reg1_val;
                                    break;
                                default:
                                    return DRC_ERR_BAD_INST;
                            }
                            bool z = res == 0;
                            int scy = res < 0 ? 0xa : 0;
                            v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
                            if (subop != V810_OP_CMPF_S) *(float*)&v810_state->P_REG[reg2] = res;
                        }
                    } else {
                        // extended
                        switch (subop) {
                            case V810_OP_MPYHW:
                                v810_state->P_REG[reg2] *= v810_state->P_REG[reg1];
                                break;
                            case V810_OP_REV:
                                v810_state->P_REG[reg2] = ins_rev(v810_state->P_REG[reg1], 0);
                                break;
                            case V810_OP_XB:
                                v810_state->P_REG[reg2] = ins_xb(0, v810_state->P_REG[reg2]);
                                break;
                            case V810_OP_XH:
                                v810_state->P_REG[reg2] = ins_xh(0, v810_state->P_REG[reg2]);
                                break;
                            default:
                                return DRC_ERR_BAD_INST;
                        }
                    }
                    #pragma GCC diagnostic pop
                    break;
                }
                default: {
                    v810_state->PC = last_PC;
                    return DRC_ERR_BAD_INST;
                }
            }
        }
        last_opcode = opcode;
        if ((PC & 0x07000000) < 0x05000000) {
            v810_state->PC = last_PC;
            return DRC_ERR_BAD_PC;
        }
        last_PC = PC;
    } while (!v810_state->ret && (!DRC_AVAILABLE || (PC & 0x07000000) != 0x07000000));
    v810_state->PC = PC;
    v810_state->cycles = cycles;
    return 0;
}