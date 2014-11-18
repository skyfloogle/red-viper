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


extern void ins_err   (int arg1, int arg2, int tos3); //Special handler?

//Bitstring SubOpcodes
extern void ins_sch0bsu (int arg1, int arg2, int arg3);
extern void ins_sch0bsd (int arg1, int arg2, int arg3);
extern void ins_sch1bsu (int arg1, int arg2, int arg3);
extern void ins_sch1bsd (int arg1, int arg2, int arg3);
extern void ins_orbsu   (int arg1, int arg2, int arg3);
extern void ins_andbsu  (int arg1, int arg2, int arg3);
extern void ins_xorbsu  (int arg1, int arg2, int arg3);
extern void ins_movbsu  (int arg1, int arg2, int arg3);
extern void ins_ornbsu  (int arg1, int arg2, int arg3);
extern void ins_andnbsu (int arg1, int arg2, int arg3);
extern void ins_xornbsu (int arg1, int arg2, int arg3);
extern void ins_notbsu  (int arg1, int arg2, int arg3);

//FPU SubOpcodes
extern void ins_cmpf_s  (int arg1, int arg2, int arg3);
extern void ins_cvt_ws  (int arg1, int arg2, int arg3);
extern void ins_cvt_sw  (int arg1, int arg2, int arg3);
extern void ins_addf_s  (int arg1, int arg2, int arg3);
extern void ins_subf_s  (int arg1, int arg2, int arg3);
extern void ins_mulf_s  (int arg1, int arg2, int arg3);
extern void ins_divf_s  (int arg1, int arg2, int arg3);
extern void ins_trnc_sw (int arg1, int arg2, int arg3);
extern void ins_xb      (int arg1, int arg2, int arg3); //Undocumented opcode XB (non-FPU)
extern void ins_xh      (int arg1, int arg2, int arg3); //Undocumented opcode XH (non-FPU)
extern void ins_rev     (int arg1, int arg2, int arg3); //Undocumented opcode REV (non-FPU)
extern void ins_mpyhw   (int arg1, int arg2, int arg3); //Undocumented opcode MPYHW (non-FPU)

#endif

