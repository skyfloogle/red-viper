#include "v810_mem.h"
#include "v810_opt.h"
#include "vb_types.h"
#include "drc_core.h"
#include "interpreter.h"

int interpreter_run(void) {
    // keep PC and cycles in local variables for extra speed
    // can't do this with PSW because interrupts modify it
    WORD PC = vb_state->v810_state.PC;
    WORD last_PC = PC;
    WORD cycles = vb_state->v810_state.cycles;
    BYTE last_opcode = 0;
    WORD target = cycles;
    do {
        if ((SWORD)(target - cycles) <= 0) {
            vb_state->v810_state.PC = PC;
            if (serviceInt(cycles, PC) && (PC != vb_state->v810_state.PC || vb_state->v810_state.ret)) {
                // interrupt triggered, so we exit
                // PC may have been modified so don't reset it
                vb_state->v810_state.cycles = cycles;
                return 0;
            }
            target = cycles + vb_state->v810_state.cycles_until_event_partial;
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
            if (!(opcode & 0x10) && reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
            switch (opcode) {
                case V810_OP_MOV:
                    interpret_mov(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_ADD:
                    interpret_add(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_SUB:
                    interpret_sub(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_CMP:
                    interpret_cmp(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_SHL:
                    interpret_shl(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_SHR:
                    interpret_shr(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_JMP:
                    PC = reg1_val;
                    break;
                case V810_OP_SAR:
                    interpret_sar(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_MUL:
                    interpret_mul(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_DIV:
                    interpret_div(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_MULU:
                    interpret_mulu(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_DIVU:
                    interpret_divu(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_OR:
                    interpret_or(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_AND:
                    interpret_and(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_XOR:
                    interpret_xor(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_NOT:
                    interpret_not(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_MOV_I:
                    interpret_mov_i(&vb_state->v810_state, reg2, reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1);
                    break;
                case V810_OP_ADD_I:
                    interpret_add_i(&vb_state->v810_state, reg2, reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1);
                    break;
                case V810_OP_SETF:
                    interpret_setf(&vb_state->v810_state, reg2, reg1);
                    break;
                case V810_OP_CMP_I:
                    interpret_cmp_i(&vb_state->v810_state, reg2, reg1 & 0x10 ? reg1 | 0xfffffff0 : reg1);
                    break;
                case V810_OP_SHL_I:
                    interpret_shl_i(&vb_state->v810_state, reg2, reg1);
                    break;
                case V810_OP_SHR_I:
                    interpret_shr_i(&vb_state->v810_state, reg2, reg1);
                    break;
                case V810_OP_CLI:
                    interpret_cli(&vb_state->v810_state);
                    break;
                case V810_OP_SAR_I:
                    interpret_sar_i(&vb_state->v810_state, reg2, reg1);
                    break;
                // case V810_OP_TRAP:
                case V810_OP_RETI:
                    if (vb_state->v810_state.S_REG[PSW] & PSW_NP) {
                        PC = vb_state->v810_state.S_REG[FEPC];
                        vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[FEPSW];
                    } else {
                        PC = vb_state->v810_state.S_REG[EIPC];
                        vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[EIPSW];
                    }
                    break;
                case V810_OP_HALT: {
                    cycles = target;
                    vb_state->v810_state.PC = PC;
                    do {
                        cycles += vb_state->v810_state.cycles_until_event_partial;
                        vb_state->v810_state.cycles_until_event_partial = vb_state->v810_state.cycles_until_event_full = 0;
                        vb_state->v810_state.cycles = cycles;
                        serviceInt(cycles, PC);
                    } while (!vb_state->v810_state.ret && vb_state->v810_state.PC == PC);
                    if (vb_state->v810_state.PC == PC) {
                        // no interrupt triggered, so repeat the halt
                        vb_state->v810_state.PC = last_PC;
                    }
                    // PC was modified so don't reset it
                    return 0;
                }
                case V810_OP_LDSR:
                    interpret_ldsr(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_STSR:
                    interpret_stsr(&vb_state->v810_state, reg1, reg2);
                    break;
                case V810_OP_SEI:
                    interpret_sei(&vb_state->v810_state);
                    break;
                case V810_OP_BSTR: {
                    typedef bool (*bstr_func)(WORD,WORD,WORD,WORD);
                    bstr_func func = (bstr_func)bssuboptable[reg1].func;
                    WORD lastarg = reg1 < 4 ? vb_state->v810_state.P_REG[27] & 31 : ((vb_state->v810_state.P_REG[27] & 31)) | ((vb_state->v810_state.P_REG[26] & 31) << 5) | ((target - cycles) << 10);
                    WORD res = func(vb_state->v810_state.P_REG[30], vb_state->v810_state.P_REG[29], vb_state->v810_state.P_REG[28], lastarg);
                    if (reg1 < 4) {
                        vb_state->v810_state.S_REG[PSW] = (vb_state->v810_state.S_REG[PSW] & ~1) | !res;
                    } else {
                        vb_state->v810_state.cycles += res;
                        if (vb_state->v810_state.P_REG[28]) {
                            PC = last_PC;
                        }
                    }
                    break;
                }
                default: {
                    vb_state->v810_state.PC = last_PC;
                    return DRC_ERR_BAD_INST;
                }
            }
        } else if (opcode < 0x28) {
            // branch
            if (interpreter_get_cond(instr >> 9, vb_state->v810_state.S_REG[PSW])) {
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
                case V810_OP_MOVEA:
                    interpret_movea(&vb_state->v810_state, reg1, reg2, (SHWORD)instr2);
                    break;
                case V810_OP_ADDI:
                    interpret_addi(&vb_state->v810_state, reg1, reg2, (SHWORD)instr2);
                    break;
                case V810_OP_JAL:
                    vb_state->v810_state.P_REG[31] = PC;
                    // fallthrough
                case V810_OP_JR: {
                    SWORD disp = instr2 | ((SWORD)instr << 16);
                    if (disp & 0x02000000) disp |= 0xfc000000;
                    else disp &= ~(0xfc000000);
                    PC += disp - 4;
                    break;
                }
                case V810_OP_ORI:
                    interpret_ori(&vb_state->v810_state, reg1, reg2, (SHWORD)instr2);
                    break;
                case V810_OP_ANDI:
                    interpret_andi(&vb_state->v810_state, reg1, reg2, (SHWORD)instr2);
                    break;
                case V810_OP_XORI:
                    interpret_xori(&vb_state->v810_state, reg1, reg2, (SHWORD)instr2);
                    break;
                case V810_OP_MOVHI:
                    interpret_movhi(&vb_state->v810_state, reg1, reg2, (SHWORD)instr2);
                    break;
                case V810_OP_LD_B: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    vb_state->v810_state.P_REG[reg2] = (SBYTE)mem_rbyte(reg1_val + (SHWORD)instr2);
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
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    vb_state->v810_state.P_REG[reg2] = (SHWORD)mem_rhword(reg1_val + (SHWORD)instr2);
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
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    vb_state->v810_state.P_REG[reg2] = mem_rword(reg1_val + (SHWORD)instr2);
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
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    vb_state->v810_state.P_REG[reg2] = (BYTE)mem_rbyte(reg1_val + (SHWORD)instr2);
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
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    vb_state->v810_state.P_REG[reg2] = (HWORD)mem_rhword(reg1_val + (SHWORD)instr2);
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
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    vb_state->v810_state.P_REG[reg2] = (WORD)mem_rword(reg1_val + (SHWORD)instr2);
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
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    BYTE reg2_val = 0;
                    if (reg2) reg2_val = vb_state->v810_state.P_REG[reg2];
                    mem_wbyte(reg1_val + (SHWORD)instr2, reg2_val);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                case V810_OP_ST_H: case V810_OP_OUT_H: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    HWORD reg2_val = 0;
                    if (reg2) reg2_val = vb_state->v810_state.P_REG[reg2];
                    mem_whword(reg1_val + (SHWORD)instr2, reg2_val);
                    if ((last_opcode & 0x34) == 0x34 && (last_opcode & 3) != 2) {
                        // with two consecutive stores, the second takes 2 cycles instead of 1
                        cycles += 1;
                    }
                    break;
                }
                case V810_OP_ST_W: case V810_OP_OUT_W: {
                    WORD reg1_val = 0;
                    if (reg1) reg1_val = vb_state->v810_state.P_REG[reg1];
                    WORD reg2_val = 0;
                    if (reg2) reg2_val = vb_state->v810_state.P_REG[reg2];
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
                    switch (instr2 >> 10) {
                        case V810_OP_CVT_WS:
                            interpret_cvt_ws(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_CVT_SW:
                            interpret_cvt_sw(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_TRNC_SW:
                            interpret_trnc_sw(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_ADDF_S:
                            interpret_addf_s(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_SUBF_S:
                            interpret_subf_s(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_CMPF_S:
                            interpret_cmpf_s(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_MULF_S:
                            interpret_mulf_s(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_DIVF_S:
                            interpret_divf_s(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_MPYHW:
                            interpret_mpyhw(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_REV:
                            interpret_rev(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_XB:
                            interpret_xb(&vb_state->v810_state, reg1, reg2);
                            break;
                        case V810_OP_XH:
                            interpret_xh(&vb_state->v810_state, reg1, reg2);
                            break;
                        default:
                            return DRC_ERR_BAD_INST;
                    }
                    break;
                }
                default: {
                    vb_state->v810_state.PC = last_PC;
                    return DRC_ERR_BAD_INST;
                }
            }
        }
        last_opcode = opcode;
        if ((PC & 0x07000000) < 0x05000000) {
            vb_state->v810_state.PC = last_PC;
            return DRC_ERR_BAD_PC;
        }
        last_PC = PC;
    } while (!vb_state->v810_state.ret && (!DRC_AVAILABLE || (PC & 0x07000000) != 0x07000000));
    vb_state->v810_state.PC = PC;
    vb_state->v810_state.cycles = cycles;
    return 0;
}
