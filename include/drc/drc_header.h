#ifndef DRC_HEADER_H
#define DRC_HEADER_H

#include "drc_core.h"

static inline void drc_bake_add_cycles(unsigned int *cycles);
static inline void drc_bake_subtract_cycles_runtime(int cycles);
static inline void drc_bake_halt(WORD next_PC, unsigned *cycles);
static inline void drc_bake_busywait(v810_instruction *ins, unsigned *cycles);
static inline void drc_bake_handle_interrupts(WORD ret_PC, unsigned *cycles);
static inline void drc_bake_jump_short(v810_instruction *ins); // goes to instruction's branch offset
static inline void drc_bake_jump_long(v810_instruction *ins); // goes to instruction's branch offset
static inline void drc_bake_branch(v810_instruction *ins);
static inline void drc_bake_link_reg(v810_instruction *ins);

static inline void drc_bake_golf_hack(void);
static inline void drc_bake_ballsort(void);
static inline void drc_bake_ballscale_start(void);
static inline void drc_bake_ballscale_end(void);
static inline void drc_bake_vertical_force_hack(void);
static inline void drc_bake_bowling_nikochan_hack(v810_instruction *ins, unsigned cycles);

static inline void drc_bake_reti(void);
static inline void drc_bake_nop(void);
static inline void drc_bake_end_block(void);
static inline void drc_bake_ld_b(v810_instruction *ins, bool access_time);
static inline void drc_bake_ld_h(v810_instruction *ins, bool access_time);
static inline void drc_bake_ld_w(v810_instruction *ins, bool access_time);
static inline void drc_bake_st_b(v810_instruction *ins, bool access_time);
static inline void drc_bake_st_h(v810_instruction *ins, bool access_time);
static inline void drc_bake_st_w(v810_instruction *ins, bool access_time);
static inline void drc_bake_bstr(v810_instruction *ins, unsigned *cycles);

static inline void drc_bake_jmp(v810_instruction *ins);
static inline void drc_bake_movhi(v810_instruction *ins);
static inline void drc_bake_movea(v810_instruction *ins);
static inline void drc_bake_mov(v810_instruction *ins);
static inline void drc_bake_add(v810_instruction *ins);
static inline void drc_bake_sub(v810_instruction *ins);
static inline void drc_bake_cmp(v810_instruction *ins);
static inline void drc_bake_shl(v810_instruction *ins);
static inline void drc_bake_shr(v810_instruction *ins);
static inline void drc_bake_sar(v810_instruction *ins);
static inline void drc_bake_mul(v810_instruction *ins);
static inline void drc_bake_mulu(v810_instruction *ins);
static inline void drc_bake_div(v810_instruction *ins);
static inline void drc_bake_divu(v810_instruction *ins);
static inline void drc_bake_or(v810_instruction *ins);
static inline void drc_bake_and(v810_instruction *ins);
static inline void drc_bake_xor(v810_instruction *ins);
static inline void drc_bake_not(v810_instruction *ins);
static inline void drc_bake_mov_i(v810_instruction *ins);
static inline void drc_bake_add_i(v810_instruction *ins);
static inline void drc_bake_cmp_i(v810_instruction *ins);
static inline void drc_bake_shl_i(v810_instruction *ins);
static inline void drc_bake_shr_i(v810_instruction *ins);
static inline void drc_bake_sar_i(v810_instruction *ins);
static inline void drc_bake_andi(v810_instruction *ins);
static inline void drc_bake_xori(v810_instruction *ins);
static inline void drc_bake_ori(v810_instruction *ins);
static inline void drc_bake_addi(v810_instruction *ins);
static inline void drc_bake_ldsr(v810_instruction *ins);
static inline void drc_bake_stsr(v810_instruction *ins);
static inline void drc_bake_sei(v810_instruction *ins);
static inline void drc_bake_cli(v810_instruction *ins);
static inline void drc_bake_setf(v810_instruction *ins);
static inline void drc_bake_fpp(v810_instruction *ins);

#endif // DRC_HEADER_H
