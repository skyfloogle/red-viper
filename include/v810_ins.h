//Defenition file of Instruction handler routines for
//the V810 processor

#ifndef V810_INST_H_
#define V810_INST_H_

#include "vb_types.h"

#define sign_32(num) (((num) & 0x80000000) ? (INT64)((num)|0xFFFFFFFF00000000LL) : (num))
#define sign_26(num) (((num) & 0x02000000) ? (WORD)((num)|0xFC000000) : (num))
#define sign_16(num) (((num) & 0x8000) ? (WORD)((num)|0xFFFF0000) : (num))
#define sign_14(num) (((num) & 0x2000) ? (WORD)((num)|0xFFFFC000) : (num))
#define sign_12(num) (((num) & 0x0800) ? (WORD)((num)|0xFFFFF000) : (num))
#define sign_9(num) (((num) & 0x0100) ? (WORD)((num)|0xFFFFFE00) : (num))
#define sign_8(num) (((num) & 0x0080) ? (WORD)((num)|0xFFFFFF00) : (num))
#define sign_5(num) (((num) & 0x0010) ? (WORD)((num)|0xFFFFFFE0) : (num))


extern void ins_err   (int arg1, int arg2); //Special handler?

//Bitstring SubOpcodes
extern bool ins_sch0bsu (WORD src, WORD dst, WORD len, WORD offs);
extern bool ins_sch0bsd (WORD src, WORD dst, WORD len, WORD offs);
extern bool ins_sch1bsu (WORD src, WORD dst, WORD len, WORD offs);
extern bool ins_sch1bsd (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_orbsu   (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_andbsu  (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_xorbsu  (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_movbsu  (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_ornbsu  (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_andnbsu (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_xornbsu (WORD src, WORD dst, WORD len, WORD offs);
extern void ins_notbsu  (WORD src, WORD dst, WORD len, WORD offs);

//FPU SubOpcodes
extern float ins_cmpf_s  (float reg1, float reg2);
extern float ins_cvt_ws  (int   reg1, float reg2);
extern int   ins_cvt_sw  (float reg1, int   reg2);
extern float ins_addf_s  (float reg1, float reg2);
extern float ins_subf_s  (float reg1, float reg2);
extern float ins_mulf_s  (float reg1, float reg2);
extern float ins_divf_s  (float reg1, float reg2);
extern int   ins_trnc_sw (float reg1, int   reg2);
extern int   ins_xb      (int   arg1, int    arg2);   //Undocumented opcode XB (non-FPU)
extern int   ins_xh      (int   arg1, unsigned arg2);   //Undocumented opcode XH (non-FPU)
extern int   ins_rev     (int   arg1, int    arg2);   //Undocumented opcode REV (non-FPU)
extern int   ins_mpyhw   (short arg1, short  arg2); //Undocumented opcode MPYHW (non-FPU)

#endif

