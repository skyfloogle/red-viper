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
extern int  ins_orbsu   (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_andbsu  (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_xorbsu  (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_movbsu  (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_ornbsu  (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_andnbsu (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_xornbsu (WORD src, WORD dst, WORD len, SWORD offs);
extern int  ins_notbsu  (WORD src, WORD dst, WORD len, SWORD offs);

//FPU SubOpcodes
extern WORD ins_rev(WORD n);   //Undocumented opcode REV (non-FPU)

#endif

