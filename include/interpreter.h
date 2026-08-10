#include <math.h>
#include "v810_cpu.h"
#include "v810_opt.h"

int interpreter_run(void);

static inline bool interpreter_get_cond(BYTE code, WORD psw) {
    bool cond = false;
    switch (0x40 | (code & 7)) {
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

#define BEGIN_NOARG(name) \
    static inline void interpret_##name(cpu_state *v810_state) { \

#define BEGIN_REG12(name) \
    static inline void interpret_##name(cpu_state *v810_state, int reg1, int reg2) { \
        const WORD reg1_val = reg1 ? v810_state->P_REG[reg1] : 0; \
        const WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;

#define BEGIN_REG1F2(name) \
    static inline void interpret_##name(cpu_state *v810_state, int reg1, int reg2) { \
        const float reg1_val = reg1 ? *(float*)&v810_state->P_REG[reg1] : 0; \

#define BEGIN_REG1F2F(name) \
    static inline void interpret_##name(cpu_state *v810_state, int reg1, int reg2) { \
        const float reg1_val = reg1 ? *(float*)&v810_state->P_REG[reg1] : 0; \
        const float reg2_val = reg2 ? *(float*)&v810_state->P_REG[reg2] : 0;

#define BEGIN_REG2_IMM(name) \
    static inline void interpret_##name(cpu_state *v810_state, int reg2, int imm) { \
        const WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;

#define BEGIN_REG12_IMM(name) \
    static inline void interpret_##name(cpu_state *v810_state, int reg1, int reg2, SHWORD imm) { \
        const WORD reg1_val = reg1 ? v810_state->P_REG[reg1] : 0; \
        const WORD reg2_val = reg2 ? v810_state->P_REG[reg2] : 0;

#define END_INSTR() \
    }

BEGIN_REG12(mov)
    v810_state->P_REG[reg2] = reg1_val;
END_INSTR()

BEGIN_REG12(add)
    WORD res = reg2_val + reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = (SWORD)(~(reg2_val ^ reg1_val) & (reg2_val ^ res)) < 0;
    bool cy = (unsigned)res < (unsigned)reg2_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(sub)
    WORD res = reg2_val - reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = (SWORD)((reg2_val ^ reg1_val) & (reg2_val ^ res)) < 0;
    bool cy = (unsigned)reg2_val < (unsigned)reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(cmp)
    WORD res = reg2_val - reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = (SWORD)((reg2_val ^ reg1_val) & (reg2_val ^ res)) < 0;
    bool cy = (unsigned)reg2_val < (unsigned)reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
END_INSTR()

BEGIN_REG12(shl)
    WORD shift = reg1_val & 31;
    WORD res = reg2_val << reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = false;
    bool cy = reg1_val != 0 ? (reg2_val >> (32 - reg1_val)) & 1 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(shr)
    WORD shift = reg1_val & 31;
    WORD res = reg2_val >> reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = false;
    bool cy = reg1_val != 0 ? (reg2_val >> (reg1_val - 1)) & 1 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(sar)
    WORD shift = reg1_val & 31;
    WORD res = (SWORD)reg2_val >> reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = false;
    bool cy = reg1_val != 0 ? (reg2_val >> (reg1_val - 1)) & 1 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(mul)
    int64_t res = (int64_t)(SWORD)reg1_val * (int64_t)(SWORD)reg2_val;
    bool ov = res != (int64_t)(int32_t)res;
    bool z = res == 0;
    bool s = res < 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2);
    v810_state->P_REG[30] = (WORD)(res >> 32);
    v810_state->P_REG[reg2] = (WORD)res;
END_INSTR()

BEGIN_REG12(div)
    if (reg2_val == 0x80000000 && (SWORD)reg1_val == -1) {
        v810_state->P_REG[30] = 0;
        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | 6;
    } else {
        v810_state->P_REG[30] = (SWORD)reg2_val % (SWORD)reg1_val;
        SWORD res = (SWORD)reg2_val / (SWORD)reg1_val;
        bool z = res == 0;
        bool s = res < 0;
        v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | z | (s << 1);
        v810_state->P_REG[reg2] = res;
    }
END_INSTR()

BEGIN_REG12(mulu)
    uint64_t res = (uint64_t)reg1_val * (uint64_t)reg2_val;
    bool ov = res != (uint64_t)(uint32_t)res;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2);
    v810_state->P_REG[30] = (WORD)(res >> 32);
    v810_state->P_REG[reg2] = (WORD)res;
END_INSTR()

BEGIN_REG12(divu)
    v810_state->P_REG[30] = reg2_val % reg1_val;
    WORD res = reg2_val / reg1_val;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | z | (s << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(or)
    WORD res = (reg2 ? v810_state->P_REG[reg2] : 0) | reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(and)
    WORD res = (reg2 ? v810_state->P_REG[reg2] : 0) & reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(xor)
    WORD res = (reg2 ? v810_state->P_REG[reg2] : 0) ^ reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12(not)
    WORD res = ~reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG2_IMM(mov_i)
    v810_state->P_REG[reg2] = imm;
END_INSTR()

BEGIN_REG2_IMM(add_i)
    WORD res = reg2_val + imm;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = (SWORD)(~(reg2_val ^ imm) & (reg2_val ^ res)) < 0;
    bool cy = (unsigned)res < (unsigned)reg2_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG2_IMM(setf)
    v810_state->P_REG[reg2] = interpreter_get_cond(imm, v810_state->S_REG[PSW]);
END_INSTR()

BEGIN_REG2_IMM(cmp_i)
    WORD res = reg2_val - imm;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = (SWORD)((reg2_val ^ imm) & (reg2_val ^ res)) < 0;
    bool cy = (unsigned)reg2_val < (unsigned)imm;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
END_INSTR()

BEGIN_REG2_IMM(shl_i)
    imm &= 31;
    WORD res = reg2_val << imm;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = false;
    bool cy = imm != 0 ? (reg2_val >> (32 - imm)) & 1 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG2_IMM(shr_i)
    imm &= 31;
    WORD res = reg2_val >> imm;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = false;
    bool cy = imm != 0 ? (reg2_val >> (imm - 1)) & 1 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_NOARG(cli)
    v810_state->S_REG[PSW] &= ~(1 << 12);
END_INSTR()

BEGIN_REG2_IMM(sar_i)
    imm &= 31;
    WORD res = (SWORD)reg2_val >> imm;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = false;
    bool cy = imm != 0 ? (reg2_val >> (imm - 1)) & 1 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG2_IMM(ldsr)
    v810_state->S_REG[imm & 31] = reg2_val;
END_INSTR()

BEGIN_REG2_IMM(stsr)
    v810_state->P_REG[reg2] = v810_state->S_REG[imm & 31];
END_INSTR()

BEGIN_NOARG(sei)
    v810_state->S_REG[PSW] |= 1 << 12;
END_INSTR()

BEGIN_REG12_IMM(movea)
    v810_state->P_REG[reg2] = reg1_val + imm;
END_INSTR()

BEGIN_REG12_IMM(addi)
    WORD res = reg1_val + imm;
    bool z = res == 0;
    bool s = (SWORD)res < 0;
    bool ov = (SWORD)(~(reg1_val ^ imm) & (reg1_val ^ res)) < 0;
    bool cy = (unsigned)res < (unsigned)reg1_val;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | (s << 1) | (ov << 2) | (cy << 3);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12_IMM(ori)
    WORD res = reg1_val | (HWORD)imm;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12_IMM(andi)
    WORD res = reg1_val & (HWORD)imm;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12_IMM(xori)
    WORD res = reg1_val ^ (HWORD)imm;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0x7) | (res == 0) | (((SWORD)res < 0) << 1);
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG12_IMM(movhi)
    v810_state->P_REG[reg2] = reg1_val + ((WORD)imm << 16);
END_INSTR()

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
BEGIN_REG12(cvt_ws)
    float res = (float)(SWORD)reg1_val;
    bool z = res == 0;
    int scy = res < 0 ? 0xa : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    *(float*)&v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2(cvt_sw)
    SWORD res = round(reg1_val);
    bool z = res == 0;
    int scy = res < 0 ? 2 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2(trnc_sw)
    SWORD res = (SWORD)(reg1_val);
    bool z = res == 0;
    int scy = res < 0 ? 2 : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2F(addf_s)
    float res = reg2_val + reg1_val;
    bool z = res == 0;
    int scy = res < 0 ? 0xa : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    *(float*)&v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2F(subf_s)
    float res = reg2_val - reg1_val;
    bool z = res == 0;
    int scy = res < 0 ? 0xa : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    *(float*)&v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2F(mulf_s)
    float res = reg2_val * reg1_val;
    bool z = res == 0;
    int scy = res < 0 ? 0xa : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    *(float*)&v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2F(divf_s)
    float res = reg2_val / reg1_val;
    bool z = res == 0;
    int scy = res < 0 ? 0xa : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
    *(float*)&v810_state->P_REG[reg2] = res;
END_INSTR()

BEGIN_REG1F2F(cmpf_s)
    float res = reg2_val - reg1_val;
    bool z = res == 0;
    int scy = res < 0 ? 0xa : 0;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & ~0xf) | z | scy;
END_INSTR()

#pragma GCC diagnostic pop

BEGIN_REG12(mpyhw)
    v810_state->P_REG[reg2] *= (SWORD)(reg1_val << 15) >> 15;
END_INSTR()

BEGIN_REG12(rev)
    v810_state->P_REG[reg2] = reg1 ? ins_rev(v810_state->P_REG[reg1]) : 0;
END_INSTR()

BEGIN_REG12(xb)
    v810_state->P_REG[reg2] = (reg2_val & 0xFFFF0000) | ((reg2_val << 8) & 0xFF00) | ((reg2_val >> 8) & 0xFF);
END_INSTR()

BEGIN_REG12(xh)
    v810_state->P_REG[reg2] = (reg2_val << 16) | (reg2_val >> 16);
END_INSTR()

#undef BEGIN_NOARG
#undef BEGIN_REG12
#undef BEGIN_REG2_IMM
#undef END_INSTR
