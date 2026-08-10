#ifndef DRC_CORE_H
#define DRC_CORE_H

#include <stdalign.h>
#include <assert.h>
#include "vb_types.h"
#include "v810_mem.h"

typedef struct interpret_inst_t interpret_inst;
typedef union {
    ssize_t full;
    struct {
        BYTE reg1, reg2;
        SHWORD imm;
    };
    WORD target_PC;
    interpret_inst *target_instr;
} interpret_arg;
struct interpret_inst_t {
    interpret_inst *(*func)(cpu_state*, interpret_inst*, interpret_arg);
    interpret_arg arg;
};
typedef struct {
    interpret_inst *(*func)(cpu_state*, interpret_inst*, interpret_arg);
    interpret_arg arg;
    bool needs_branch;
} interpret_ir_inst;

#if (__ARM_ARCH >= 6 && __arm__)
#include "arm_types.h"
#define DRC_AVAILABLE true
#define ARM_DRC true
typedef arm_inst ir_inst;
typedef WORD translated_inst;
typedef WORD drc_unit;
#else
#define DRC_AVAILABLE true
#define ARM_DRC false
typedef interpret_ir_inst ir_inst;
typedef interpret_inst translated_inst;
typedef size_t drc_unit;
#endif

extern ir_inst *inst_ptr;

static_assert(alignof(drc_unit) >= alignof(translated_inst));

#define BLOCK_MAP_COUNT (MAX_ROM_SIZE / 2 / 2)
#define CACHE_SIZE  0x200000
#define MAX_V810_INST 8192
#define MAX_ARM_INST  32768
#define ARM_CACHE_REG_START 4
#define ARM_NUM_CACHE_REGS 6
#define MAX_NUM_BLOCKS 4096

#if MAX_ARM_INST >= 65536
#error "MAX_ARM_INST can't be more than 64K"
#endif

enum {
    DRC_ERR_BAD_ENTRY   = 1,
    DRC_ERR_BAD_PC      = 2,
    DRC_ERR_NO_DYNAREC  = 3,
    DRC_ERR_NO_BLOCKS   = 4,
    DRC_ERR_CACHE_FULL  = 5,
    DRC_ERR_BAD_INST    = 6,
};

enum {
    DRC_RELOC_RBYTE     = 0,
    DRC_RELOC_RHWORD    = 8,
    DRC_RELOC_RWORD     = 16,
    DRC_RELOC_WBYTE     = 24,
    DRC_RELOC_WHWORD    = 32,
    DRC_RELOC_WWORD     = 40,
    DRC_RELOC_BSTR      = 48,
    DRC_RELOC_IDIVMOD   = 64,
    DRC_RELOC_UIDIVMOD  = 65,
    DRC_RELOC_REV       = 66,
    DRC_RELOC_GOLFHACK  = 67,
    DRC_RELOC_BALLSCALE = 68,
    DRC_RELOC_BALLSORT  = 69,
};

#define END_BLOCK 0xFF
typedef struct {
    drc_unit *phys_offset;
    WORD size;
    // We can use ARM_NUM_CACHE_REGS registers at a time, r4-r10, and r11 will
    // have the address of v810_state
    // reg_map & 0x1F would have the VB register that is mapped to r4
    WORD reg_map;
    bool free;
    WORD start_pc;
    WORD pc_range; // start_pc + pc_range = the address of the last instruction in the block
} exec_block;

typedef struct {
    WORD PC;
    WORD imm;
    BYTE opcode;
    BYTE reg1, reg2;
    HWORD start_pos;
    int branch_offset;
    BYTE trans_size;
    bool save_flags;
    bool busywait;
    bool is_branch_target;
} v810_instruction;

extern drc_unit* cache_start;
extern drc_unit* cache_pos;

extern BYTE reg_usage[32];

int __divsi3(int a, int b);
int __modsi3(int a, int b);
unsigned int __udivsi3(unsigned int a, unsigned int b);
unsigned int __umodsi3(unsigned int a, unsigned int b);

void drc_executeBlock(drc_unit* entrypoint, exec_block* block);
int drc_handleInterrupts(WORD cpsr, WORD* PC);
void drc_relocTable(void);
void drc_clearCache(void);

drc_unit* drc_getEntry(WORD loc, exec_block **p_block);
void drc_setEntry(WORD loc, drc_unit *entry, exec_block *block);
exec_block* drc_getNextBlockStruct(void);

void drc_init(void);
void drc_reset(void);
void drc_exit(void);
int drc_run(void);
void drc_loadSavedCache(void);
void drc_dumpCache(char* filename);
void drc_dumpDebugInfo(int code);

void drc_prepare(exec_block *block);
void drc_assemble(translated_inst *dst, ir_inst *ir, v810_instruction *v810);
void drc_flags_to_native(void);
void drc_flags_to_v810(void);

void drc_bake_add_cycles(unsigned int *cycles);
void drc_bake_subtract_cycles_runtime(int cycles);
void drc_bake_halt(WORD next_PC, unsigned *cycles);
void drc_bake_busywait(v810_instruction *ins, unsigned *cycles);
void drc_bake_handle_interrupts(WORD ret_PC, unsigned *cycles);
void drc_bake_jump_short(v810_instruction *ins); // goes to instruction's branch offset
void drc_bake_jump_long(v810_instruction *ins); // goes to instruction's branch offset
void drc_bake_branch(v810_instruction *ins);
void drc_bake_link_reg(v810_instruction *ins);

void drc_bake_golf_hack(void);
void drc_bake_ballsort(void);
void drc_bake_ballscale_start(void);
void drc_bake_ballscale_end(void);
void drc_bake_vertical_force_hack(void);
void drc_bake_bowling_nikochan_hack(v810_instruction *ins, unsigned cycles);

void drc_bake_reti(void);
void drc_bake_nop(void);
void drc_bake_end_block(void);
void drc_bake_ld_b(v810_instruction *ins, bool access_time);
void drc_bake_ld_h(v810_instruction *ins, bool access_time);
void drc_bake_ld_w(v810_instruction *ins, bool access_time);
void drc_bake_st_b(v810_instruction *ins, bool access_time);
void drc_bake_st_h(v810_instruction *ins, bool access_time);
void drc_bake_st_w(v810_instruction *ins, bool access_time);
void drc_bake_bstr(v810_instruction *ins, unsigned *cycles);

void drc_bake_jmp(v810_instruction *ins);
void drc_bake_movhi(v810_instruction *ins);
void drc_bake_movea(v810_instruction *ins);
void drc_bake_mov(v810_instruction *ins);
void drc_bake_add(v810_instruction *ins);
void drc_bake_sub(v810_instruction *ins);
void drc_bake_cmp(v810_instruction *ins);
void drc_bake_shl(v810_instruction *ins);
void drc_bake_shr(v810_instruction *ins);
void drc_bake_sar(v810_instruction *ins);
void drc_bake_mul(v810_instruction *ins);
void drc_bake_mulu(v810_instruction *ins);
void drc_bake_div(v810_instruction *ins);
void drc_bake_divu(v810_instruction *ins);
void drc_bake_or(v810_instruction *ins);
void drc_bake_and(v810_instruction *ins);
void drc_bake_xor(v810_instruction *ins);
void drc_bake_not(v810_instruction *ins);
void drc_bake_mov_i(v810_instruction *ins);
void drc_bake_add_i(v810_instruction *ins);
void drc_bake_cmp_i(v810_instruction *ins);
void drc_bake_shl_i(v810_instruction *ins);
void drc_bake_shr_i(v810_instruction *ins);
void drc_bake_sar_i(v810_instruction *ins);
void drc_bake_andi(v810_instruction *ins);
void drc_bake_xori(v810_instruction *ins);
void drc_bake_ori(v810_instruction *ins);
void drc_bake_addi(v810_instruction *ins);
void drc_bake_ldsr(v810_instruction *ins);
void drc_bake_stsr(v810_instruction *ins);
void drc_bake_sei(v810_instruction *ins);
void drc_bake_cli(v810_instruction *ins);
void drc_bake_setf(v810_instruction *ins);
void drc_bake_fpp(v810_instruction *ins);


#endif //DRC_CORE_H
