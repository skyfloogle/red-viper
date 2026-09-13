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

static BYTE am_size_table[] = {
    0, // Undefined
    2, // AM_I
    2, // AM_II
    2, // AM_III
    4, // AM_IV
    4, // AM_V
    4, // AM_VIa
    4, // AM_VII
    4, // AM_VIII
    2, // AM_IX
    4, // AM_VIb
    2, // AM_BSTR
    4, // AM_FPP
    2, // AM_UDEF
};


///////////////////////////////////////////////////////////////////
// Table of Instructions and Address Modes

typedef struct {
    int addr_mode;               // Addressing mode
    const char * opname;         // Optcode name (string)
} operation;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
static operation optable[80] = {
    { AM_I,       "mov  " },           // 0x00
    { AM_I,       "add  " },           // 0x01
    { AM_I,       "sub  " },           // 0x02
    { AM_I,       "cmp  " },           // 0x03
    { AM_I,       "shl  " },           // 0x04
    { AM_I,       "shr  " },           // 0x05
    { AM_I,       "jmp  " },           // 0x06
    { AM_I,       "sar  " },           // 0x07
    { AM_I,       "mul  " },           // 0x08
    { AM_I,       "div  " },           // 0x09
    { AM_I,       "mulu " },           // 0x0A
    { AM_I,       "divu " },           // 0x0B
    { AM_I,       "or   " },           // 0x0C
    { AM_I,       "and  " },           // 0x0D
    { AM_I,       "xor  " },           // 0x0E
    { AM_I,       "not  " },           // 0x0F

    { AM_II,      "mov  " },           // 0x10  // Imediate
    { AM_II,      "add  " },           // 0x11
    { AM_II,      "setf " },           // 0x12
    { AM_II,      "cmp  " },           // 0x13
    { AM_II,      "shl  " },           // 0x14
    { AM_II,      "shr  " },           // 0x15
    { AM_II,      "cli  " },           // 0x16
    { AM_II,      "sar  " },           // 0x17
    { AM_II,      "trap " },           // 0x18

    { AM_IX,      "reti " },           // 0x19  //BRKRETI
    { AM_IX,      "halt " },           // 0x1A  //STBY

    {AM_UDEF,     "???  " },           // 0x1B  // Unknown
    { AM_II,      "ldsr " },           // 0x1C
    { AM_II,      "stsr " },           // 0x1D
    { AM_II,      "sei  " },           // 0x1E
    {AM_BSTR,     "BSTR " },           // 0x1F  // Special Bit String Instructions

    {AM_UDEF,     "???  " },           // 0x20  // Unknown   // This is a fudg on our part
    {AM_UDEF,     "???  " },           // 0x21  // Unknown   // We have 6 and 7 bit instructions
    {AM_UDEF,     "???  " },           // 0x22  // Unknown   // this is filled in by the Conditional Branch Instructions
    {AM_UDEF,     "???  " },           // 0x23  // Unknown
    {AM_UDEF,     "???  " },           // 0x24  // Unknown
    {AM_UDEF,     "???  " },           // 0x25  // Unknown
    {AM_UDEF,     "???  " },           // 0x26  // Unknown
    {AM_UDEF,     "???  " },           // 0x27  // Unknown

    { AM_V,       "movea" },           // 0x28
    { AM_V,       "addi " },           // 0x29
    { AM_IV,      "jr   " },           // 0x2A
    { AM_IV,      "jal  " },           // 0x2B
    { AM_V,       "ori  " },           // 0x2C
    { AM_V,       "andi " },           // 0x2D
    { AM_V,       "xori " },           // 0x2E
    { AM_V,       "movhi" },           // 0x2F

    { AM_VIa,     "ld.b " },           // 0x30
    { AM_VIa,     "ld.h " },           // 0x31
    {AM_UDEF,     "muli " },           // 0x32  // Unknown
    { AM_VIa,     "ld.w " },           // 0x33
    { AM_VIb,     "st.b " },           // 0x34
    { AM_VIb,     "st.h " },           // 0x35
    {AM_UDEF,     "maci " },           // 0x36  // Unknown
    { AM_VIb,     "st.w " },           // 0x37
    { AM_VIa,     "in.b " },           // 0x38
    { AM_VIa,     "in.h " },           // 0x39
    { AM_VIa,     "caxi " },           // 0x3A
    { AM_VIa,     "in.w " },           // 0x3B
    { AM_VIb,     "out.b" },           // 0x3C
    { AM_VIb,     "out.h" },           // 0x3D
    { AM_FPP,     "FPP  " },           // 0x3E  //Floating Point Instruction, Special Case
    { AM_VIb,     "out.w" },           // 0x3F

    { AM_III,     "bv   " },           // 0x40
    { AM_III,     "bl   " },           // 0x41  //BC  0x41
    { AM_III,     "be   " },           // 0x42  //BZ  0x42
    { AM_III,     "bnh  " },           // 0x43
    { AM_III,     "bn   " },           // 0x44
    { AM_III,     "br   " },           // 0x45
    { AM_III,     "blt  " },           // 0x46
    { AM_III,     "ble  " },           // 0x47
    { AM_III,     "bnv  " },           // 0x48
    { AM_III,     "bnl  " },           // 0x49 //BNC 0x49
    { AM_III,     "bne  " },           // 0x4A //BNZ 0x4A
    { AM_III,     "bh   " },           // 0x4B
    { AM_III,     "bp   " },           // 0x4C
    { AM_III,     "nop  " },           // 0x4D
    { AM_III,     "bge  " },           // 0x4E
    { AM_III,     "bgt  " },           // 0x4F
};
// All instructions greater than 0x50 are undefined (this should not be possible of course)


// Structure for holding the SubOpcodes, Same as above, without the InsType.
typedef struct {
    const char * opname;                   // Optcode name (string)
    int (*func)(WORD, WORD, WORD, SWORD);  //pointer to handler func =)
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
    { "cmpf.s ", &(ins_err) },           // 0x00
    { "FError1", &(ins_err) },           // 0x01  // Unknown
    { "cvt.ws ", &(ins_err) },           // 0x02
    { "cvt.sw ", &(ins_err) },           // 0x03
    { "addf.s ", &(ins_err) },           // 0x04
    { "subf.s ", &(ins_err) },           // 0x05
    { "mulf.s ", &(ins_err) },           // 0x06
    { "divf.s ", &(ins_err) },           // 0x07
    { "xb     ", &(ins_err) },           // 0x08  // Undocumented opcode XB -- Special case, NOT an FPU opcode
    { "xh     ", &(ins_err) },           // 0x09  // Undocumented opcode XH -- Special case, NOT an FPU opcode
    { "rev    ", &(ins_err) },           // 0x0A  // Undocumented opcode XH -- Special case, NOT an FPU opcode
    { "trnc.sw", &(ins_err) },           // 0x0B
    { "mpyhw  ", &(ins_err) },           // 0x0C  // Undocumented opcode MPYHW -- Special case, NOT an FPU opcode
    { "FErrorD", &(ins_err) },           // 0x0D  // Unknown
    { "FErrorE", &(ins_err) },           // 0x0E  // Unknown
    { "FErrorF", &(ins_err) },           // 0x0F  // Unknown
};
#pragma GCC diagnostic pop
// All instructions greater than 0x50 are undefined (this should not be possible of course)

///////////////////////////////////////////////////////////////////
// Opcodes for V810 Instruction set
#define V810_OP_MOV     0x00
#define V810_OP_ADD     0x01
#define V810_OP_SUB     0x02
#define V810_OP_CMP     0x03
#define V810_OP_SHL     0x04
#define V810_OP_SHR     0x05
#define V810_OP_JMP     0x06
#define V810_OP_SAR     0x07
#define V810_OP_MUL     0x08
#define V810_OP_DIV     0x09
#define V810_OP_MULU    0x0A
#define V810_OP_DIVU    0x0B
#define V810_OP_OR      0x0C
#define V810_OP_AND     0x0D
#define V810_OP_XOR     0x0E
#define V810_OP_NOT     0x0F
#define V810_OP_MOV_I   0x10
#define V810_OP_ADD_I   0x11
#define V810_OP_SETF    0x12
#define V810_OP_CMP_I   0x13
#define V810_OP_SHL_I   0x14
#define V810_OP_SHR_I   0x15
#define V810_OP_CLI     0x16
#define V810_OP_SAR_I   0x17
#define V810_OP_TRAP    0x18
#define V810_OP_RETI    0x19
#define V810_OP_HALT    0x1A
      //0x1B
#define V810_OP_LDSR    0x1C
#define V810_OP_STSR    0x1D
#define V810_OP_SEI     0x1E
#define V810_OP_BSTR    0x1F  //Special Bit String Inst
      //0x20 - 0x27  // Lost to Branch Instructions
#define V810_OP_MOVEA   0x28
#define V810_OP_ADDI    0x29
#define V810_OP_JR      0x2A
#define V810_OP_JAL     0x2B
#define V810_OP_ORI     0x2C
#define V810_OP_ANDI    0x2D
#define V810_OP_XORI    0x2E
#define V810_OP_MOVHI   0x2F
#define V810_OP_LD_B    0x30
#define V810_OP_LD_H    0x31
#define V810_OP_MULI    0x32  // Unknown
#define V810_OP_LD_W    0x33
#define V810_OP_ST_B    0x34
#define V810_OP_ST_H    0x35
#define V810_OP_MACI    0x36
#define V810_OP_ST_W    0x37
#define V810_OP_IN_B    0x38
#define V810_OP_IN_H    0x39
#define V810_OP_CAXI    0x3A
#define V810_OP_IN_W    0x3B
#define V810_OP_OUT_B   0x3C
#define V810_OP_OUT_H   0x3D
#define V810_OP_FPP     0x3E  //Special Float Inst
#define V810_OP_OUT_W   0x3F

//      Branch Instructions ( Extended opcode only for Branch command)
//  Common instrcutions commented out

#define V810_OP_BV      0x40
#define V810_OP_BL      0x41
#define V810_OP_BE      0x42
#define V810_OP_BNH     0x43
#define V810_OP_BN      0x44
#define V810_OP_BR      0x45
#define V810_OP_BLT     0x46
#define V810_OP_BLE     0x47
#define V810_OP_BNV     0x48
#define V810_OP_BNL     0x49
#define V810_OP_BNE     0x4A
#define V810_OP_BH      0x4B
#define V810_OP_BP      0x4C
#define V810_OP_NOP     0x4D
#define V810_OP_BGE     0x4E
#define V810_OP_BGT     0x4F

//#define V810_OP_BC      0x41
//#define V810_OP_BZ      0x42
//#define V810_OP_BNC     0x49
//#define V810_OP_BNZ     0x4A

//  Bit String Subopcodes
#define V810_OP_SCH0BSU 0x00
#define V810_OP_SCH0BSD 0x01
#define V810_OP_SCH1BSU 0x02
#define V810_OP_SCH1BSD 0x03

#define V810_OP_ORBSU   0x08
#define V810_OP_ANDBSU  0x09
#define V810_OP_XORBSU  0x0A
#define V810_OP_MOVBSU  0x0B
#define V810_OP_ORNBSU  0x0C
#define V810_OP_ANDNBSU 0x0D
#define V810_OP_XORNBSU 0x0E
#define V810_OP_NOTBSU  0x0F

//  Floating Point Subopcodes
#define V810_OP_CMPF_S  0x00

#define V810_OP_CVT_WS  0x02
#define V810_OP_CVT_SW  0x03
#define V810_OP_ADDF_S  0x04
#define V810_OP_SUBF_S  0x05
#define V810_OP_MULF_S  0x06
#define V810_OP_DIVF_S  0x07
#define V810_OP_XB      0x08
#define V810_OP_XH      0x09
#define V810_OP_REV     0x0A
#define V810_OP_TRNC_SW 0x0B
#define V810_OP_MPYHW   0x0C

#endif // V810_OPT_H_
