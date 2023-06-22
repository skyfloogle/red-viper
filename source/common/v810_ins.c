////////////////////////////////////////////////////////////
//Instruction handler routines for
//the V810 processor

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "vb_types.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"  //Remove Me!!!
#include "v810_ins.h"

//The Instructions
void ins_err(int arg1, int arg2) { //Mode1/2
    //dtprintf(6,ferr,"\nInvalid code! err");
}

//Bitstring routines, wrapper functions for bitstring instructions!
void get_bitstr(WORD *str, WORD src, WORD srcoff, WORD len) {
    WORD i=0,tword,tmp;

    memset(str,0,(((len>>5)+1)<<2)); //clear bitstring data

    tmp = ((i+srcoff)>>5);
    tword = mem_rword(src+(tmp<<2));
    while (i < len) {
        if (((i+srcoff)>>5) != tmp) {
            tmp = ((i+srcoff)>>5);
            tword = mem_rword(src+(tmp<<2));
        }
        str[i>>5] |= (((tword >> ((srcoff+i)&0x1F)) & 1) << (i&0x1F));
        i++;
    }
}

void set_bitstr(WORD *str, WORD dst, WORD dstoff, WORD len) {
    WORD i=0,tword,tmp;

    tmp = ((i+dstoff)>>5);
    tword = mem_rword(dst+(tmp<<2));
    while (i < len) {
        if (((i+dstoff)>>5) != tmp) {
            tmp = ((i+dstoff)>>5);
            tword = mem_rword(dst+(tmp<<2));
        }
        tword &= (~(1<<((dstoff+i)&0x1F)));
        tword |= (((str[i>>5]>>(i&0x1F))&1)<<((dstoff+i)&0x1F));
        i++;
        if (!((i+dstoff)&0x1F)) mem_wword(dst+(tmp<<2),tword);
    }
    mem_wword(dst+(tmp<<2),tword);
}

//Bitstring SubOpcodes
void ins_sch0bsu (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);
}

void ins_sch0bsd (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);
}

void ins_sch1bsu (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);
}

void ins_sch1bsd (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);
}

void ins_orbsu   (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] |= tmp2[i];
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_andbsu  (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] &= tmp2[i];
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_xorbsu  (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] ^= tmp2[i];
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_movbsu  (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD tmp[8192];

    get_bitstr(tmp,src,srcoff,len);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_ornbsu  (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = (~tmp[i] | tmp2[i]);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_andnbsu (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = (~tmp[i] & tmp2[i]);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_xornbsu (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = (~tmp[i] ^ tmp2[i]);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_notbsu  (int arg1, int arg2) {
    WORD dstoff = (v810_state->P_REG[26] & 0x1F);
    WORD srcoff = (v810_state->P_REG[27] & 0x1F);
    WORD len =     v810_state->P_REG[28];
    WORD dst =    (v810_state->P_REG[29] & 0xFFFFFFFC);
    WORD src =    (v810_state->P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192];

    get_bitstr(tmp,src,srcoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = ~tmp[i];
    set_bitstr(tmp,dst,dstoff,len);
}

//FPU SubOpcodes  
float ins_cmpf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    float fTemp = reg2 - reg1;
    if (fTemp == 0.0F) flags = flags | PSW_Z;
    if (fTemp < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=7;
    return reg2;
}

float ins_cvt_ws(int reg1, float reg2) {   //Int to Float
    int flags = 0; // Set Flags, OV set to Zero
    reg2 = (float)reg1;
    if (reg2 == 0) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=5; //5 to 16
    return reg2;
}

int ins_cvt_sw(float reg1, int reg2) {  //Float To Int
    int flags = 0; // Set Flags, CY unchanged, OV set to Zero
    reg2 = lroundf(reg1);
    if (reg2 == 0) flags = flags | PSW_Z;
    if (reg2 & 0x80000000)  flags = flags | PSW_S;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF8)|flags;
    //	clocks+=9; //9 to 14
    return reg2;
}

float ins_addf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 += reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=9; //9 to 28
    return reg2;
}

float ins_subf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 -= reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=12; //12 to 28
    return reg2;
}

float ins_mulf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 *= reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=8; //8 to 30
    return reg2;
}

float ins_divf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 /= reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=44; //always 44
    return reg2;
}

int ins_trnc_sw(float reg1, int reg2) {
    dprintf(0, "[FP]: %s - %f %f\n", __func__, (float)reg1, (float)reg2);
    int flags = 0; // Set Flags, CY unchanged, OV set to Zero
    reg2 = (int)trunc(reg1);
    if (reg2 == 0) flags = flags | PSW_Z;
    if (reg2 & 0x80000000)  flags = flags | PSW_S;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF8)|flags;
    //	clocks+=8; //8 to 14
    return reg2;
}

int ins_xb(int arg1, int arg2) {
    return ((arg2&0xFFFF0000) | (((arg2<<8)&0xFF00) | ((arg2>>8)&0xFF)));
    //	clocks+=1; //just a guess
}

int ins_xh(int arg1, unsigned arg2) {
    return (arg2<<16)|(arg2>>16);
    //	clocks+=1; //just a guess
}

int ins_rev(int arg1, int arg2) {
    WORD temp = 0;
    int i;
    for (i = 0; i < 32; i++) temp = ((temp << 1) | ((arg1 >> i) & 1));
    return temp;
}

int ins_mpyhw(short arg1, short arg2) {
    return (int)arg1 * (int)arg2; //signed multiplication
    //	clocks+=9; //always 9
}
