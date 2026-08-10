#include "drc_core.h"
#include "interpreter.h"
#include "v810_cpu.h"
#include "v810_ins.h"

void drc_prepare(exec_block *block) {
    // clear all IR instructions since we don't always update needs_branch
    memset(inst_ptr, 0, MAX_ARM_INST * sizeof(ir_inst));
}
void drc_flags_to_native() {}
void drc_flags_to_v810() {}

void drc_assemble(translated_inst *dst, ir_inst *ir, v810_instruction *v810) {
    if (ir->needs_branch) {
        WORD v810_dest = v810->PC + v810->branch_offset;
        drc_unit* translated_dest = drc_getEntry(v810_dest, NULL);
        if (translated_dest == cache_start) {
            // Should be fixed, but just in case
            dprintf(0, "WARN:can't jump from %lx to %lx\n", v810->PC, v810_dest);
        }
        ir->arg.target_instr = (translated_inst*)translated_dest;
    }
    dst->func = ir->func;
    dst->arg.full = ir->arg.full;
}

void drc_executeBlock(drc_unit *entrypoint, exec_block *block) {
    cpu_state *v810_state = &vb_state->v810_state;
    translated_inst *inst = (translated_inst*)entrypoint;
    do {
        inst = inst->func(v810_state, inst, inst->arg);
    } while (inst != NULL);
    v810_state->cycles += v810_state->cycles_until_event_full - v810_state->cycles_until_event_partial;
    v810_state->cycles_until_event_full = v810_state->cycles_until_event_partial;
}

#define ADD_CYCLES_RUNTIME(cycles) v810_state->cycles_until_event_partial -= cycles;

#define DECLARE_INSTR(name) \
    interpret_inst *drc_interpret_##name(cpu_state *v810_state, interpret_inst *inst, interpret_arg arg)

#define BEGIN_INSTR(name) \
    DECLARE_INSTR(name) { \
        interpret_inst *next_inst = inst + 1;

#define END_INSTR() \
        return next_inst; \
    }

#define INSTR_NOARG(name) \
    BEGIN_INSTR(name) \
        interpret_##name(v810_state); \
    END_INSTR() \
    void drc_bake_##name(v810_instruction *ins) { \
        inst_ptr->func = drc_interpret_##name; \
        inst_ptr++; \
    }

#define RUN_REG12(name) \
    BEGIN_INSTR(name) \
        interpret_##name(v810_state, arg.reg1, arg.reg2); \
    END_INSTR() \

#define INSTR_REG12(name) \
    RUN_REG12(name) \
    void drc_bake_##name(v810_instruction *ins) { \
        inst_ptr->func = drc_interpret_##name; \
        inst_ptr->arg.reg1 = ins->reg1; \
        inst_ptr->arg.reg2 = ins->reg2; \
        inst_ptr++; \
    }

#define INSTR_REG2_IMM(name) \
    BEGIN_INSTR(name) \
        interpret_##name(v810_state, arg.reg2, arg.imm); \
    END_INSTR() \
    void drc_bake_##name(v810_instruction *ins) { \
        inst_ptr->func = drc_interpret_##name; \
        inst_ptr->arg.reg2 = ins->reg2; \
        inst_ptr->arg.imm = sign_5(ins->imm); \
        inst_ptr++; \
    }

#define INSTR_REG12_IMM(name) \
    BEGIN_INSTR(name) \
        interpret_##name(v810_state, arg.reg1, arg.reg2, arg.imm); \
    END_INSTR() \
    void drc_bake_##name(v810_instruction *ins) { \
        inst_ptr->func = drc_interpret_##name; \
        inst_ptr->arg.reg1 = ins->reg1; \
        inst_ptr->arg.reg2 = ins->reg2; \
        inst_ptr->arg.imm = (HWORD)ins->imm; \
        inst_ptr++; \
    }

BEGIN_INSTR(nop)
END_INSTR()
void drc_bake_nop() {
    inst_ptr++->func = drc_interpret_nop;
}

BEGIN_INSTR(end_block)
    next_inst = NULL;
END_INSTR()
void drc_bake_end_block() {
    inst_ptr++->func = drc_interpret_end_block;
}

BEGIN_INSTR(add_cycles)
    v810_state->cycles_until_event_partial -= arg.full;
END_INSTR()
void drc_bake_add_cycles(unsigned int *cycles) {
    if (*cycles != 0) {
        inst_ptr->func = drc_interpret_add_cycles;
        inst_ptr->arg.full = *cycles;
        inst_ptr++;
    }
    *cycles = 0;
}

BEGIN_INSTR(add_cycles_runtime)
    ADD_CYCLES_RUNTIME(arg.full);
END_INSTR()
void drc_bake_subtract_cycles_runtime(int cycles) {
    inst_ptr->func = drc_interpret_add_cycles_runtime;
    inst_ptr->arg.full = -cycles;
    inst_ptr++;
}

BEGIN_INSTR(jmp)
    WORD reg1_val = 0;
    if (arg.reg1) reg1_val = v810_state->P_REG[arg.reg1];
    v810_state->PC = reg1_val;
    next_inst = NULL;
END_INSTR()
void drc_bake_jmp(v810_instruction *ins) {
    inst_ptr->func = drc_interpret_jmp;
    inst_ptr->arg.reg1 = ins->reg1;
    inst_ptr++;
}

BEGIN_INSTR(jump_short)
    next_inst = arg.target_instr;
END_INSTR()
void drc_bake_jump_short(v810_instruction *ins) {
    inst_ptr->func = drc_interpret_jump_short;
    inst_ptr->needs_branch = true;
    inst_ptr++;
}

BEGIN_INSTR(jump_long)
    v810_state->PC = arg.target_PC;
    next_inst = NULL;
END_INSTR()
void drc_bake_jump_long(v810_instruction *ins) {
    inst_ptr->func = drc_interpret_jump_long;
    inst_ptr->arg.target_PC = ins->PC + ins->branch_offset;
    inst_ptr++;
}

#define RUN_BRANCH(lo, hi) \
    BEGIN_INSTR(lo) \
        if (interpreter_get_cond(V810_OP_##hi, v810_state->S_REG[PSW])) { \
            next_inst = arg.target_instr; \
        } \
    END_INSTR()

RUN_BRANCH(bv, BV)
RUN_BRANCH(bl, BL)
RUN_BRANCH(be, BE)
RUN_BRANCH(bnh, BNH)
RUN_BRANCH(bn, BN)
RUN_BRANCH(blt, BLT)
RUN_BRANCH(ble, BLE)
RUN_BRANCH(bnv, BNV)
RUN_BRANCH(bnl, BNL)
RUN_BRANCH(bne, BNE)
RUN_BRANCH(bh, BH)
RUN_BRANCH(bp, BP)
RUN_BRANCH(bge, BGE)
RUN_BRANCH(bgt, BGT)

void drc_bake_branch(v810_instruction *ins) {
    switch (ins->opcode) {
        case V810_OP_BV:  inst_ptr->func = drc_interpret_bv; break;
        case V810_OP_BL:  inst_ptr->func = drc_interpret_bl; break;
        case V810_OP_BE:  inst_ptr->func = drc_interpret_be; break;
        case V810_OP_BNH: inst_ptr->func = drc_interpret_bnh; break;
        case V810_OP_BN:  inst_ptr->func = drc_interpret_bn; break;
        case V810_OP_BR:  inst_ptr->func = drc_interpret_jump_short; break;
        case V810_OP_BLT: inst_ptr->func = drc_interpret_blt; break;
        case V810_OP_BLE: inst_ptr->func = drc_interpret_ble; break;
        case V810_OP_BNV:  inst_ptr->func = drc_interpret_bnv; break;
        case V810_OP_BNL:  inst_ptr->func = drc_interpret_bnl; break;
        case V810_OP_BNE:  inst_ptr->func = drc_interpret_bne; break;
        case V810_OP_BH: inst_ptr->func = drc_interpret_bh; break;
        case V810_OP_BP:  inst_ptr->func = drc_interpret_bp; break;
        case V810_OP_NOP:  inst_ptr->func = drc_interpret_nop; break;
        case V810_OP_BGE: inst_ptr->func = drc_interpret_bge; break;
        case V810_OP_BGT: inst_ptr->func = drc_interpret_bgt; break;
    }
    inst_ptr->needs_branch = true;
    inst_ptr++;
}

BEGIN_INSTR(reti)
    if (v810_state->S_REG[PSW] & PSW_NP) {
        v810_state->PC = v810_state->S_REG[FEPC];
        v810_state->S_REG[PSW] = v810_state->S_REG[FEPSW];
    } else {
        v810_state->PC = v810_state->S_REG[EIPC];
        v810_state->S_REG[PSW] = v810_state->S_REG[EIPSW];
    }
    next_inst = NULL;
END_INSTR()
void drc_bake_reti() {
    inst_ptr++->func = drc_interpret_reti;
}

BEGIN_INSTR(link_reg)
    v810_state->P_REG[31] = arg.target_PC;
END_INSTR()
void drc_bake_link_reg(v810_instruction *ins) {
    inst_ptr->func = drc_interpret_link_reg;
    inst_ptr->arg.target_PC = ins->PC + 4;
    inst_ptr++;
}

static bool handle_interrupts(cpu_state *v810_state, WORD target_PC) {
    v810_state->PC = target_PC;
    v810_state->cycles += v810_state->cycles_until_event_full - v810_state->cycles_until_event_partial;
    v810_state->cycles_until_event_full = v810_state->cycles_until_event_partial;
    return serviceInt(v810_state->cycles, target_PC);
}

BEGIN_INSTR(handle_interrupts)
    if (v810_state->cycles_until_event_partial <= 0) {
        if (handle_interrupts(v810_state, arg.target_PC)) {
            next_inst = NULL;
        }
    }
END_INSTR()
void drc_bake_handle_interrupts(uint32_t ret_PC, unsigned int *cycles) {
    inst_ptr->func = drc_interpret_add_cycles_runtime;
    inst_ptr->arg.full = *cycles;
    inst_ptr++;
    inst_ptr->func = drc_interpret_handle_interrupts;
    inst_ptr->arg.target_PC = ret_PC;
    inst_ptr++;
    *cycles = 0;
}

BEGIN_INSTR(halt)
    do {
        v810_state->cycles_until_event_partial = 0;
    } while (!handle_interrupts(v810_state, arg.target_PC));
    next_inst = NULL;
END_INSTR()
void drc_bake_halt(uint32_t next_PC, unsigned int *cycles) {
    inst_ptr->func = drc_interpret_halt;
    inst_ptr->arg.target_PC = next_PC;
    inst_ptr++;
    *cycles = 0;
}

void drc_bake_busywait(v810_instruction *ins, unsigned int *cycles) {
    // flip the branch and point it to the following instruction
    int branch_offset = ins->branch_offset;
    ins->opcode ^= 8;
    ins->branch_offset = 2;
    drc_bake_branch(ins);
    drc_bake_halt(ins->PC + branch_offset, cycles);
}

void drc_clearScreenForGolf(void);
BEGIN_INSTR(golf_hack)
    drc_clearScreenForGolf();
END_INSTR()
void drc_bake_golf_hack() {
    inst_ptr++->func = drc_interpret_golf_hack;
}

void baseball2_sort(void);
BEGIN_INSTR(ballsort)
    baseball2_sort();
END_INSTR()
void drc_bake_ballsort() {
    inst_ptr++->func = drc_interpret_ballsort;
}

void baseball2_scaling(WORD in_img, WORD out_img, WORD scale_fixed);
BEGIN_INSTR(ballscale)
    WORD reg17 = v810_state->P_REG[17];
    WORD reg18 = v810_state->P_REG[18];
    WORD reg19 = v810_state->P_REG[19];
    if ((reg17 >> 20) == 0x70 && (reg18 >> 16) == 5) {
        baseball2_scaling(reg17, reg18, reg19);
        next_inst = inst + arg.full;
    }
END_INSTR()
static ir_inst *ballscale_start_inst;
void drc_bake_ballscale_start() {
    ballscale_start_inst = inst_ptr;
    inst_ptr++->func = drc_interpret_ballscale;
}
void drc_bake_ballscale_end() {
    ballscale_start_inst->arg.full = inst_ptr - ballscale_start_inst;
}

void drc_bake_vertical_force_hack() {
    inst_ptr->func = drc_interpret_add_cycles_runtime;
    inst_ptr->arg.full = 256;
    inst_ptr++;
}

BEGIN_INSTR(bowling_nikochan_hack)
    if (handle_interrupts(v810_state, arg.target_PC)) {
        next_inst = NULL;
    }
END_INSTR()
void drc_bake_bowling_nikochan_hack(v810_instruction *ins, unsigned int cycles) {
    inst_ptr->func = drc_interpret_add_cycles_runtime;
    inst_ptr->arg.full = cycles;
    inst_ptr++;
    inst_ptr->func = drc_interpret_bowling_nikochan_hack;
    inst_ptr->arg.target_PC = ins[1].PC;
    inst_ptr++;
}

#define RUN_LOAD(instr, type_lo, type_hi, access_time) \
    BEGIN_INSTR(instr) \
        WORD reg1_val = 0; \
        if (arg.reg1) reg1_val = v810_state->P_REG[arg.reg1]; \
        uint64_t res = mem_r ## type_lo(reg1_val + arg.imm); \
        v810_state->P_REG[arg.reg2] = (type_hi)res; \
        if (access_time) ADD_CYCLES_RUNTIME(res >> 32); \
    END_INSTR()

RUN_LOAD(ld_b_fast, byte,  SBYTE,  false)
RUN_LOAD(ld_h_fast, hword, SHWORD, false)
RUN_LOAD(ld_w_fast, word,  SWORD,  false)
RUN_LOAD(in_b_fast, byte,  BYTE,   false)
RUN_LOAD(in_h_fast, hword, HWORD,  false)
RUN_LOAD(in_w_fast, word,  WORD,   false)
RUN_LOAD(ld_b_slow, byte,  SBYTE,  true)
RUN_LOAD(ld_h_slow, hword, SHWORD, true)
RUN_LOAD(ld_w_slow, word,  SWORD,  true)
RUN_LOAD(in_b_slow, byte,  BYTE,   true)
RUN_LOAD(in_h_slow, hword, HWORD,  true)
RUN_LOAD(in_w_slow, word,  WORD,   true)

#define BAKE_LOAD(lo, hi) \
    void drc_bake_ld_##lo(v810_instruction *ins, bool access_time) { \
        inst_ptr->func = ( \
            access_time \
                ? (ins->opcode == V810_OP_LD_##hi \
                    ? drc_interpret_ld_##lo##_slow \
                    : drc_interpret_in_##lo##_slow \
                ) : (ins->opcode == V810_OP_LD_##hi \
                    ? drc_interpret_ld_##lo##_fast \
                    : drc_interpret_in_##lo##_fast \
                ) \
        ); \
        inst_ptr->arg.reg1 = ins->reg1; \
        inst_ptr->arg.reg2 = ins->reg2; \
        inst_ptr->arg.imm = ins->imm; \
        inst_ptr++; \
    }
BAKE_LOAD(b,B)
BAKE_LOAD(h,H)
BAKE_LOAD(w,W)

#define RUN_STORE(instr, type_lo, type_hi, access_time) \
    BEGIN_INSTR(instr) \
        WORD reg1_val = 0; \
        if (arg.reg1) reg1_val = v810_state->P_REG[arg.reg1]; \
        type_hi reg2_val = 0; \
        if (arg.reg2) reg2_val = v810_state->P_REG[arg.reg2]; \
        WORD new_cycles = mem_w##type_lo(reg1_val + arg.imm, reg2_val); \
        if (access_time) ADD_CYCLES_RUNTIME(new_cycles); \
    END_INSTR()

RUN_STORE(st_b_fast, byte,  BYTE,  false)
RUN_STORE(st_h_fast, hword, HWORD, false)
RUN_STORE(st_w_fast, word,  WORD,  false)
RUN_STORE(st_b_slow, byte,  BYTE,  true)
RUN_STORE(st_h_slow, hword, HWORD, true)
RUN_STORE(st_w_slow, word,  WORD,  true)

#define BAKE_STORE(lo, hi) \
    void drc_bake_st_##lo(v810_instruction *ins, bool access_time) { \
        inst_ptr->func = (access_time \
            ? drc_interpret_st_##lo##_slow \
            : drc_interpret_st_##lo##_fast \
        ); \
        inst_ptr->arg.reg1 = ins->reg1; \
        inst_ptr->arg.reg2 = ins->reg2; \
        inst_ptr->arg.imm = ins->imm; \
        inst_ptr++; \
    }

BAKE_STORE(b,B)
BAKE_STORE(h,H)
BAKE_STORE(w,W)

BEGIN_INSTR(bstr)
    typedef bool (*bstr_func)(WORD,WORD,WORD,WORD);
    bstr_func func = (bstr_func)bssuboptable[arg.full].func;
    WORD lastarg = arg.full < 4 ? v810_state->P_REG[27] & 31 : ((v810_state->P_REG[27] & 31)) | ((v810_state->P_REG[26] & 31) << 5) | ((v810_state->cycles_until_event_partial) << 10);
    WORD res = func(v810_state->P_REG[30], v810_state->P_REG[29], v810_state->P_REG[28], lastarg);
    if (arg.full < 4) {
        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~1) | !res;
    } else {
        ADD_CYCLES_RUNTIME(res);
    }
END_INSTR()
BEGIN_INSTR(bstr_check_length)
    if (v810_state->P_REG[28] != 0) {
        next_inst = arg.target_instr;
    }
END_INSTR()
void drc_bake_bstr(v810_instruction *ins, unsigned int *cycles) {
    inst_ptr->func = drc_interpret_bstr;
    inst_ptr->arg.full = ins->imm;
    inst_ptr++;
    if (ins->imm >= 4) {
        drc_bake_handle_interrupts(ins->PC, cycles);
        inst_ptr->func = drc_interpret_bstr_check_length;
        inst_ptr->arg.target_PC = ins->PC;
        inst_ptr->needs_branch = true;
        inst_ptr++;
    }
}

INSTR_REG12(mov)
INSTR_REG12(add)
INSTR_REG12(sub)
INSTR_REG12(cmp)
INSTR_REG12(shl)
INSTR_REG12(shr)
INSTR_REG12(sar)
INSTR_REG12(mul)
INSTR_REG12(div)
INSTR_REG12(mulu)
INSTR_REG12(divu)
INSTR_REG12(or)
INSTR_REG12(and)
INSTR_REG12(xor)
INSTR_REG12(not)
INSTR_REG2_IMM(mov_i)
INSTR_REG2_IMM(add_i)
INSTR_REG2_IMM(setf)
INSTR_REG2_IMM(cmp_i)
INSTR_REG2_IMM(shl_i)
INSTR_REG2_IMM(shr_i)
INSTR_NOARG(cli)
INSTR_REG2_IMM(sar_i)
INSTR_REG2_IMM(ldsr)
INSTR_REG2_IMM(stsr)
INSTR_NOARG(sei)
INSTR_REG12_IMM(movea)
INSTR_REG12_IMM(addi)
INSTR_REG12_IMM(ori)
INSTR_REG12_IMM(andi)
INSTR_REG12_IMM(xori)
INSTR_REG12_IMM(movhi)

RUN_REG12(cvt_ws)
RUN_REG12(cvt_sw)
RUN_REG12(trnc_sw)
RUN_REG12(addf_s)
RUN_REG12(subf_s)
RUN_REG12(mulf_s)
RUN_REG12(divf_s)
RUN_REG12(cmpf_s)
RUN_REG12(mpyhw)
RUN_REG12(rev)
RUN_REG12(xb)
RUN_REG12(xh)
void drc_bake_fpp(v810_instruction *ins) {
    switch (ins->imm) {
        case V810_OP_CVT_WS: inst_ptr->func = drc_interpret_cvt_ws; break;
        case V810_OP_CVT_SW: inst_ptr->func = drc_interpret_cvt_sw; break;
        case V810_OP_TRNC_SW: inst_ptr->func = drc_interpret_trnc_sw; break;
        case V810_OP_ADDF_S: inst_ptr->func = drc_interpret_addf_s; break;
        case V810_OP_SUBF_S: inst_ptr->func = drc_interpret_subf_s; break;
        case V810_OP_MULF_S: inst_ptr->func = drc_interpret_mulf_s; break;
        case V810_OP_DIVF_S: inst_ptr->func = drc_interpret_divf_s; break;
        case V810_OP_CMPF_S: inst_ptr->func = drc_interpret_cmpf_s; break;
        case V810_OP_MPYHW: inst_ptr->func = drc_interpret_mpyhw; break;
        case V810_OP_REV: inst_ptr->func = drc_interpret_rev; break;
        case V810_OP_XB: inst_ptr->func = drc_interpret_xb; break;
        case V810_OP_XH: inst_ptr->func = drc_interpret_xh; break;
        default:
            dprintf(0, "[DRC]: Invalid FPU subop 0x%lx\n", ins->imm);
            inst_ptr->func = drc_interpret_nop;
            break;
    }
    inst_ptr->arg.reg1 = ins->reg1;
    inst_ptr->arg.reg2 = ins->reg2;
    inst_ptr++;
}
