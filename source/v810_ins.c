////////////////////////////////////////////////////////////
//Instruction handler routines for
//the V810 processor

#include <stdio.h>
#include <string.h>

#include "vb_types.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"  //Remove Me!!!
#include "v810_ins.h"

//http://www.rt66.com/~brennan/djgpp/djgpp_asm.html
//info gcc "C Extensions" "Extended Asm"

//The Instructions
void ins_err(int arg1, int arg2, int tos3) { //Mode1/2
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
void ins_sch0bsu (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);
}

void ins_sch0bsd (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);
}

void ins_sch1bsu (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);
}

void ins_sch1bsd (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);
}

void ins_orbsu   (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] |= tmp2[i];
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_andbsu  (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] &= tmp2[i];
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_xorbsu  (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] ^= tmp2[i];
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_movbsu  (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD tmp[8192];

    get_bitstr(tmp,src,srcoff,len);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_ornbsu  (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = (~tmp[i] | tmp2[i]);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_andnbsu (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = (~tmp[i] & tmp2[i]);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_xornbsu (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192],tmp2[8192];

    get_bitstr(tmp,src,srcoff,len);
    get_bitstr(tmp2,dst,dstoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = (~tmp[i] ^ tmp2[i]);
    set_bitstr(tmp,dst,dstoff,len);
}

void ins_notbsu  (int arg1, int arg2, int arg3) {
    WORD dstoff = (P_REG[26] & 0x1F);
    WORD srcoff = (P_REG[27] & 0x1F);
    WORD len =     P_REG[28];
    WORD dst =    (P_REG[29] & 0xFFFFFFFC);
    WORD src =    (P_REG[30] & 0xFFFFFFFC);

    WORD i,tmp[8192];

    get_bitstr(tmp,src,srcoff,len);
    for (i = 0; i < ((len>>5)+1); i++) tmp[i] = ~tmp[i];
    set_bitstr(tmp,dst,dstoff,len);
}

//FPU SubOpcodes
//How do we convert from INT to Float without changing the Number (BitWise)?
void ins_cmpf_s  (int arg1, int arg2, int arg3) {
    int flags = 0; // Set Flags, OV set to Zero
    double temp = (double)(*((float *)&P_REG[arg1])) - (double)(*((float *)&P_REG[arg2]));

    if (temp == 0.0F) flags = flags | PSW_Z;
    if (temp < 0.0F)  flags = flags | PSW_S;
    if (temp > ((float)temp)) flags = flags | PSW_CY; //How???
    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
}

void ins_cvt_ws  (int arg1, int arg2, int arg3) {   //Int to Float
    int flags = 0; // Set Flags, OV set to Zero
    float temp = (float)((long)P_REG[arg2]);

    if (temp == 0) flags = flags | PSW_Z;
    if (temp < 0.0F)  flags = flags | PSW_S;
    if (P_REG[arg2] != temp) flags = flags | PSW_CY; //How???
    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;

    P_REG[arg1] = *((WORD *)&temp);
}

void ins_cvt_sw  (int arg1, int arg2, int arg3) {  //Float To Int
    int flags = 0; // Set Flags, CY unchanged, OV set to Zero
    P_REG[arg1] = (long)(*((float *)&P_REG[arg2])+0.5F);

    if (P_REG[arg1] == 0) flags = flags | PSW_Z;
    if (P_REG[arg1] & 0x80000000)  flags = flags | PSW_S;
    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF7)|flags;
}

void ins_addf_s  (int arg1, int arg2, int arg3) {
    int flags = 0; // Set Flags, OV set to Zero
    float temp2;
    double temp = (double)(*((float *)&P_REG[arg1])) + (double)(*((float *)&P_REG[arg2]));

    if (temp == 0.0F) flags = flags | (PSW_Z | PSW_CY);  //changed by frostgiant based on NEC docs
    if (temp < 0.0F)  flags = flags | PSW_S;

    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;

    temp2 = ((float)temp);
    P_REG[arg1] = *((WORD *)&temp2);
}

void ins_subf_s  (int arg1, int arg2, int arg3) {
    int flags = 0; // Set Flags, OV set to Zero
    float temp2;
    double temp = (double)(*((float *)&P_REG[arg1])) - (double)(*((float *)&P_REG[arg2]));

    if (temp == 0.0F) flags = flags | (PSW_Z | PSW_CY);  //changed by frostgiant based on NEC docs
    if (temp < 0.0F)  flags = flags | PSW_S;

    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;

    temp2 = ((float)temp);
    P_REG[arg1] = *((WORD *)&temp2);
}

void ins_mulf_s  (int arg1, int arg2, int arg3) {
    int flags = 0; // Set Flags, OV set to Zero
    float temp2;
    double temp = (double)(*((float *)&P_REG[arg1])) * (double)(*((float *)&P_REG[arg2]));

    if (temp == 0.0F) flags = flags | (PSW_Z | PSW_CY);  //changed by frostgiant based on NEC docs
    if (temp < 0.0F)  flags = flags | PSW_S;
    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;

    temp2 = ((float)temp);
    P_REG[arg1] = *((WORD *)&temp2);
}

void ins_divf_s  (int arg1, int arg2, int arg3) {
    int flags = 0; // Set Flags, OV set to Zero
    float temp2;
    double temp = (double)(*((float *)&P_REG[arg1])) / (double)(*((float *)&P_REG[arg2]));

    if (temp == 0.0F) flags = flags | (PSW_Z | PSW_CY);  //changed by frostgiant based on NEC docs
    if (temp < 0.0F)  flags = flags | PSW_S;

    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;

    temp2 = ((float)temp);
    P_REG[arg1] = *((WORD *)&temp2);
}

void ins_trnc_sw (int arg1, int arg2, int arg3) {
    int flags = 0; // Set Flags, CY unchanged, OV set to Zero
    P_REG[arg1] = (WORD)(*((float *)&P_REG[arg2])+0.5F);

    if (!P_REG[arg1]) flags = flags | PSW_Z;
    if (P_REG[arg1] & 0x80000000)  flags = flags | PSW_S;
    S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF7)|flags;
}

void ins_xb       (int arg1, int arg2, int arg3) {
    P_REG[arg1] = ((P_REG[arg1]&0xFFFF0000) | (((P_REG[arg1]<<8)&0xFF00) | ((P_REG[arg1]>>8)&0xFF)));
}

void ins_xh       (int arg1, int arg2, int arg3) {
    P_REG[arg1] = (P_REG[arg1]<<16)|(P_REG[arg1]>>16);
}

void ins_rev      (int arg1, int arg2, int arg3) {
    WORD temp = 0;
    int i;

    for (i = 0; i < 32; i++) temp = ((temp << 1) | ((P_REG[arg2] >> i) & 1));
    P_REG[arg1] = temp;
}

void ins_mpyhw    (int arg1, int arg2, int arg3) {
    P_REG[arg1] = P_REG[arg1] * P_REG[arg2];
}
