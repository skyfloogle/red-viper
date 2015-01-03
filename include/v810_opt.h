///////////////////////////////////////////////////////////////
// File:  v810_dis.h
//
// Author:  Bob VanderClay (vandercl@umich.edu)
//
// Description:  Defines used in v810_dis.cpp(not)
//
//
// History:
// --------
// 8 Apr 1998 - Created
// 9 Apr 1998 - Version 1.0 completed
// Many mods byb David Tucker....
//

#ifndef V810_OPT_H_
#define V810_OPT_H_

#include "v810_ins.h"
#include "vb_types.h"

///////////////////////////////////////////////////////////////////
// Define Modes
#define AM_I    0x01
#define AM_II   0x02
#define AM_III  0x03
#define AM_IV   0x04
#define AM_V    0x05
#define AM_VIa  0x06    // Mode6 form1
#define AM_VIb  0x0A    // Mode6 form2
#define AM_VII  0x07
#define AM_VIII 0x08
#define AM_IX   0x09
#define AM_BSTR 0x0B  // Bit String Instructions
#define AM_FPP  0x0C  // Floating Point Instructions
#define AM_UDEF 0x0D  // Unknown/Undefined Instructions


///////////////////////////////////////////////////////////////////
// Table of Instructions and Address Modes

typedef struct {
    int addr_mode;               // Addressing mode
    char * opname;               // Optcode name (string)
    void (*func)(int, int, int); //pointer to handler func =)
} operation;

static operation optable[80] = {
    { AM_I,       "mov  ", /*&(ins_mov)  */ &(ins_err) },           // 0x00
    { AM_I,       "add  ", /*&(ins_add)  */ &(ins_err) },           // 0x01
    { AM_I,       "sub  ", /*&(ins_sub)  */ &(ins_err) },           // 0x02
    { AM_I,       "cmp  ", /*&(ins_cmp)  */ &(ins_err) },           // 0x03
    { AM_I,       "shl  ", /*&(ins_shl)  */ &(ins_err) },           // 0x04
    { AM_I,       "shr  ", /*&(ins_shr)  */ &(ins_err) },           // 0x05
    { AM_I,       "jmp  ", /*&(ins_jmp)  */ &(ins_err) },           // 0x06
    { AM_I,       "sar  ", /*&(ins_sar)  */ &(ins_err) },           // 0x07
    { AM_I,       "mul  ", /*&(ins_mul)  */ &(ins_err) },           // 0x08
    { AM_I,       "div  ", /*&(ins_div)  */ &(ins_err) },           // 0x09
    { AM_I,       "mulu ", /*&(ins_mulu) */ &(ins_err) },           // 0x0A
    { AM_I,       "divu ", /*&(ins_divu) */ &(ins_err) },           // 0x0B
    { AM_I,       "or   ", /*&(ins_or)   */ &(ins_err) },           // 0x0C
    { AM_I,       "and  ", /*&(ins_and)  */ &(ins_err) },           // 0x0D
    { AM_I,       "xor  ", /*&(ins_xor)  */ &(ins_err) },           // 0x0E
    { AM_I,       "not  ", /*&(ins_not)  */ &(ins_err) },           // 0x0F

    { AM_II,      "mov  ", /*&(ins_mov_i)*/ &(ins_err) },           // 0x10  // Imediate
    { AM_II,      "add  ", /*&(ins_add_i)*/ &(ins_err) },           // 0x11
    { AM_II,      "setf ", /*&(ins_setf) */ &(ins_err) },           // 0x12
    { AM_II,      "cmp  ", /*&(ins_cmp_i)*/ &(ins_err) },           // 0x13
    { AM_II,      "shl  ", /*&(ins_shl_i)*/ &(ins_err) },           // 0x14
    { AM_II,      "shr  ", /*&(ins_shr_i)*/ &(ins_err) },           // 0x15
    { AM_II,      "cli  ", /*&(ins_ei)   */ &(ins_err) },           // 0x16
    { AM_II,      "sar  ", /*&(ins_sar_i)*/ &(ins_err) },           // 0x17
    { AM_II,      "trap ", /*&(ins_trap) */ &(ins_err) },           // 0x18

    { AM_IX,      "reti ", /*&(ins_reti) */ &(ins_err) },           // 0x19  //BRKRETI
    { AM_IX,      "halt ", /*&(ins_halt) */ &(ins_err) },           // 0x1A  //STBY

    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x1B  // Unknown
    { AM_II,      "ldsr ", /*&(ins_ldsr) */ &(ins_err) },           // 0x1C
    { AM_II,      "stsr ", /*&(ins_stsr) */ &(ins_err) },           // 0x1D
    { AM_II,      "sei  ", /*&(ins_di)   */ &(ins_err) },           // 0x1E
    {AM_BSTR,     "BSTR ", /*&(ins_bstr) */ &(ins_err) },           // 0x1F  // Special Bit String Instructions

    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x20  // Unknown   // This is a fudg on our part
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x21  // Unknown   // We have 6 and 7 bit instructions
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x22  // Unknown   // this is filled in by the Conditional Branch Instructions
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x23  // Unknown
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x24  // Unknown
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x25  // Unknown
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x26  // Unknown
    {AM_UDEF,     "???  ", /*&(ins_err)  */ &(ins_err) },           // 0x27  // Unknown

    { AM_V,       "movea", /*&(ins_movea)*/ &(ins_err) },           // 0x28
    { AM_V,       "addi ", /*&(ins_addi) */ &(ins_err) },           // 0x29
    { AM_IV,      "jr   ", /*&(ins_jr)   */ &(ins_err) },           // 0x2A
    { AM_IV,      "jal  ", /*&(ins_jal)  */ &(ins_err) },           // 0x2B
    { AM_V,       "ori  ", /*&(ins_ori)  */ &(ins_err) },           // 0x2C
    { AM_V,       "andi ", /*&(ins_andi) */ &(ins_err) },           // 0x2D
    { AM_V,       "xori ", /*&(ins_xori) */ &(ins_err) },           // 0x2E
    { AM_V,       "movhi", /*&(ins_movhi)*/ &(ins_err) },           // 0x2F

    { AM_VIa,     "ld.b ", /*&(ins_ld_b) */ &(ins_err) },           // 0x30
    { AM_VIa,     "ld.h ", /*&(ins_ld_h) */ &(ins_err) },           // 0x31
    {AM_UDEF,     "muli ", /*&(ins_muli) */ &(ins_err) },           // 0x32  // Unknown
    { AM_VIa,     "ld.w ", /*&(ins_ld_w) */ &(ins_err) },           // 0x33
    { AM_VIb,     "st.b ", /*&(ins_st_b) */ &(ins_err) },           // 0x34
    { AM_VIb,     "st.h ", /*&(ins_st_h) */ &(ins_err) },           // 0x35
    {AM_UDEF,     "maci ", /*&(ins_maci) */ &(ins_err) },           // 0x36  // Unknown
    { AM_VIb,     "st.w ", /*&(ins_st_w) */ &(ins_err) },           // 0x37
    { AM_VIa,     "in.b ", /*&(ins_in_b) */ &(ins_err) },           // 0x38
    { AM_VIa,     "in.h ", /*&(ins_in_h) */ &(ins_err) },           // 0x39
    { AM_VIa,     "caxi ", /*&(ins_caxi) */ &(ins_err) },           // 0x3A
    { AM_VIa,     "in.w ", /*&(ins_in_w) */ &(ins_err) },           // 0x3B
    { AM_VIb,     "out.b", /*&(ins_out_b)*/ &(ins_err) },           // 0x3C
    { AM_VIb,     "out.h", /*&(ins_out_h)*/ &(ins_err) },           // 0x3D
    { AM_FPP,     "FPP  ", /*&(ins_fpp)  */ &(ins_err) },           // 0x3E  //Floating Point Instruction, Special Case
    { AM_VIb,     "out.w", /*&(ins_out_w)*/ &(ins_err) },           // 0x3F

    { AM_III,     "bv   ", /*&(ins_bv)   */ &(ins_err) },           // 0x40
    { AM_III,     "bl   ", /*&(ins_bl)   */ &(ins_err) },           // 0x41  //BC  0x41
    { AM_III,     "be   ", /*&(ins_be)   */ &(ins_err) },           // 0x42  //BZ  0x42
    { AM_III,     "bnh  ", /*&(ins_bnh)  */ &(ins_err) },           // 0x43
    { AM_III,     "bn   ", /*&(ins_bn)   */ &(ins_err) },           // 0x44
    { AM_III,     "br   ", /*&(ins_br)   */ &(ins_err) },           // 0x45
    { AM_III,     "blt  ", /*&(ins_blt)  */ &(ins_err) },           // 0x46
    { AM_III,     "ble  ", /*&(ins_ble)  */ &(ins_err) },           // 0x47
    { AM_III,     "bnv  ", /*&(ins_bnv)  */ &(ins_err) },           // 0x48
    { AM_III,     "bnl  ", /*&(ins_bnl)  */ &(ins_err) },           // 0x49 //BNC 0x49
    { AM_III,     "bne  ", /*&(ins_bne)  */ &(ins_err) },           // 0x4A //BNZ 0x4A
    { AM_III,     "bh   ", /*&(ins_bh)   */ &(ins_err) },           // 0x4B
    { AM_III,     "bp   ", /*&(ins_bp)   */ &(ins_err) },           // 0x4C
    { AM_III,     "nop  ", /*&(ins_nop)  */ &(ins_err) },           // 0x4D
    { AM_III,     "bge  ", /*&(ins_bge)  */ &(ins_err) },           // 0x4E
    { AM_III,     "bgt  ", /*&(ins_bgt)  */ &(ins_err) },           // 0x4F
};
// All instructions greater than 0x50 are undefined (this should not be possible of course)


// Structure for holding the SubOpcodes, Same as above, without the InsType.
typedef struct {
    char * opname;               // Optcode name (string)
    void (*func)(int, int, int); //pointer to handler func =)
} suboperation;


// Bit String Subopcodes
static suboperation bssuboptable[16] = {
    { "sch0bsu", &(ins_sch0bsu)  },           // 0x00
    { "sch0bsd", &(ins_sch0bsd)  },           // 0x01
    { "sch1bsu", &(ins_sch1bsu)  },           // 0x02
    { "sch1bsd", &(ins_sch1bsd)  },           // 0x03
    { "BError4", &(ins_err)      },           // 0x04  // Unknown
    { "BError5", &(ins_err)      },           // 0x05  // Unknown
    { "BError6", &(ins_err)      },           // 0x06  // Unknown
    { "BError7", &(ins_err)      },           // 0x07  // Unknown
    { "orbsu  ", &(ins_orbsu)    },           // 0x08
    { "andbsu ", &(ins_andbsu)   },           // 0x09
    { "xorbsu ", &(ins_xorbsu)   },           // 0x0A
    { "movbsu ", &(ins_movbsu)   },           // 0x0B
    { "ornbsu ", &(ins_ornbsu)   },           // 0x0C
    { "andnbsu", &(ins_andnbsu)  },           // 0x0D
    { "xornbsu", &(ins_xornbsu)  },           // 0x0E
    { "notbsu ", &(ins_notbsu)   },           // 0x0F
};

// Floating Point Subopcodes
static suboperation fpsuboptable[16] = {
    { "cmpf.s ", &(ins_cmpf_s ) },           // 0x00
    { "FError1", &(ins_err )    },           // 0x01  // Unknown
    { "cvt.ws ", &(ins_cvt_ws ) },           // 0x02
    { "cvt.sw ", &(ins_cvt_sw ) },           // 0x03
    { "addf.s ", &(ins_addf_s ) },           // 0x04
    { "subf.s ", &(ins_subf_s ) },           // 0x05
    { "mulf.s ", &(ins_mulf_s ) },           // 0x06
    { "divf.s ", &(ins_divf_s ) },           // 0x07
    { "xb     ", &(ins_xb )     },           // 0x08  // Undocumented opcode XB -- Special case, NOT an FPU opcode
    { "xh     ", &(ins_xh )     },           // 0x09  // Undocumented opcode XH -- Special case, NOT an FPU opcode
    { "rev    ", &(ins_rev )    },           // 0x0A  // Undocumented opcode XH -- Special case, NOT an FPU opcode
    { "trnc.sw", &(ins_trnc_sw )},           // 0x0B
    { "mpyhw  ", &(ins_mpyhw )  },           // 0x0C  // Undocumented opcode MPYHW -- Special case, NOT an FPU opcode
    { "FErrorD", &(ins_err )    },           // 0x0D  // Unknown
    { "FErrorE", &(ins_err )    },           // 0x0E  // Unknown
    { "FErrorF", &(ins_err )    },           // 0x0F  // Unknown
};

// All instructions greater than 0x50 are undefined (this should not be posible of cource)


///////////////////////////////////////////////////////////////////
// Opcodes for V810 Instruction set
#define         MOV                             0x00
#define         ADD                             0x01
#define         SUB                             0x02
#define         CMP                             0x03
#define         SHL                             0x04
#define         SHR                             0x05
#define         JMP                             0x06
#define         SAR                             0x07
#define         MUL                             0x08
#define         DIV                             0x09
#define         MULU                            0x0A
#define         DIVU                            0x0B
#define         OR                              0x0C
#define         AND                             0x0D
#define         XOR                             0x0E
#define         NOT                             0x0F
#define         MOV_I                           0x10
#define         ADD_I                           0x11
#define         SETF                            0x12
#define         CMP_I                           0x13
#define         SHL_I                           0x14
#define         SHR_I                           0x15
#define         CLI                             0x16
#define         SAR_I                           0x17
#define         TRAP                            0x18
#define         RETI                            0x19
#define         HALT                            0x1A
                                              //0x1B
#define         LDSR                            0x1C
#define         STSR                            0x1D
#define         SEI                             0x1E
#define         BSTR                            0x1F  //Special Bit String Inst
                                              //0x20 - 0x27  // Lost to Branch Instructions
#define         MOVEA                           0x28
#define         ADDI                            0x29
#define         JR                              0x2A
#define         JAL                             0x2B
#define         ORI                             0x2C
#define         ANDI                            0x2D
#define         XORI                            0x2E
#define         MOVHI                           0x2F
#define         LD_B                            0x30
#define         LD_H                            0x31
#define         MULI                            0x32  // Unknown
#define         LD_W                            0x33
#define         ST_B                            0x34
#define         ST_H                            0x35
#define         MACI                            0x36
#define         ST_W                            0x37
#define         IN_B                            0x38
#define         IN_H                            0x39
#define         CAXI                            0x3A
#define         IN_W                            0x3B
#define         OUT_B                           0x3C
#define         OUT_H                           0x3D
#define         FPP                             0x3E  //Special Float Inst
#define         OUT_W                           0x3F


//      Branch Instructions ( Extended opcode only for Branch command)
//  Common instrcutions commented out

#define         BV                              0x40
#define         BL                              0x41
#define         BE                              0x42
#define         BNH                             0x43
#define         BN                              0x44
#define         BR                              0x45
#define         BLT                             0x46
#define         BLE                             0x47
#define         BNV                             0x48
#define         BNL                             0x49
#define         BNE                             0x4A
#define         BH                              0x4B
#define         BP                              0x4C
#define         NOP                             0x4D
#define         BGE                             0x4E
#define         BGT                             0x4F

//#define       BC                              0x41
//#define       BZ                              0x42
//#define       BNC                             0x49
//#define       BNZ                             0x4A

//  Bit String Subopcodes
#define         SCH0BSU                         0x00
#define         SCH0BSD                         0x01
#define         SCH1BSU                         0x02
#define         SCH1BSD                         0x03

#define         ORBSU                           0x08
#define         ANDBSU                          0x09
#define         XORBSU                          0x0A
#define         MOVBSU                          0x0B
#define         ORNBSU                          0x0C
#define         ANDNBSU                         0x0D
#define         XORNBSU                         0x0E
#define         NOTBSU                          0x0F


//  Floating Point Subopcodes
#define         CMPF_S                          0x00

#define         CVT_WS                          0x02
#define         CVT_SW                          0x03
#define         ADDF_S                          0x04
#define         SUBF_S                          0x05
#define         MULF_S                          0x06
#define         DIVF_S                          0x07
#define         XB                              0x08
#define         XH                              0x09
#define         REV                             0x0A
#define         TRNC_SW                         0x0B
#define         MPYHW                           0x0C

#endif //DEFINE_H
