//////////////////////////////////////////////////////////
// Main CPU routines

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "vb_types.h"
#include "v810_ins.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_dsp.h"

#define NEG(n) ((n) >> 31)
#define POS(n) ((~(n)) >> 31)

#ifdef X86_ASM

//   breakpoint: mov dword ptr P_REG[4*flags], 1

/* ASM macro to set the v810 flags for zero and sign. Also pops the x86 flags
 at the end. (Much faster than the C way!) */

#define ASM_FLAG_ZS __asm					   \
{											   \
    __asm jz $+29								\
    __asm js $+11								\
    __asm jmp $+24							   \
    \
    __asm mov flags, PSW_S					   \
    __asm jmp $+12							   \
    \
    __asm mov flags, PSW_Z					   \
    \
    __asm popf								   \
    }

/* Sets all potential flags (CY, OV, S, and Z)
   A bit more code than above, but still many times faster than in C.

   Comments:
   The register ah is loaded with the flags data by "lahf"
   Overflow is not loaded by lahf, so we check that first
   Upon a bit test, if the test is true, the zero flag will be 0 unlike a cmp
    where 0 means the two are equal */

#define ASM_FLAGS_JUMP __asm					\
{											   \
    __asm lahf								   \
    __asm jo $over							   \
    __asm jnae $carry							\
    __asm jz $zero							   \
    __asm js $sign							   \
    __asm jmp $end							   \
    \
    __asm $over:								 \
    __asm or flags, PSW_OV					   \
    __asm test ah, X86F_C						\
    __asm jnz $carry							 \
    __asm test ah, X86F_S						\
    __asm jnz $sign							  \
    __asm test ah, X86F_Z						\
    __asm jnz $zero							  \
    __asm jmp $end							   \
    \
    __asm $carry:								\
    __asm or flags, PSW_CY					   \
    __asm test ah, X86F_S						\
    __asm jnz $sign							  \
    __asm test ah, X86F_Z						\
    __asm jnz $zero							  \
    __asm jmp $end							   \
    \
    __asm $sign:								 \
    __asm or flags, PSW_S						\
    __asm test ah, X86F_Z						\
    __asm jnz $zero							  \
    __asm jmp $end							   \
    \
    __asm $zero:								 \
    __asm or flags, PSW_Z						\
    \
    __asm $end:								  \
    __asm popf								   \
    }
#endif


////////////////////////////////////////////////////////////
// Globals
WORD P_REG[32];  // Main program reg pr0-pr31
WORD S_REG[32];  // System registers sr0-sr31
WORD PC;		 // Program Counter

const BYTE opcycle[0x50] = {
    0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x01,0x0D,0x26,0x0D,0x24,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x01,0x0F,0x0A,0x05,0x00,0x01,0x01,0x03,0x00, //EI, HALT, LDSR, STSR, DI, BSTR -- Unknown clocks
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x03,0x03,0x01,0x01,0x01,0x01,
    0x01,0x01,0x0D,0x01,0x01,0x01,0x00,0x01,0x03,0x03,0x1A,0x05,0x01,0x01,0x00,0x01, //these are based on 16-bit bus!! (should be 32-bit?)
    0x01,0x01,0x01,0x01,0x01,0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01
};

// Reinitialize the defaults in the CPU
void v810_reset() {
    P_REG[0]	  =  0x00000000;
    PC			=  0xFFFFFFF0;
    S_REG[ECR]	=  0x0000FFF0;
    S_REG[PSW]	=  0x00008000;
    //	S_REG[PIR]	=  0x00008300;
    S_REG[PIR]	=  0x00005346;
    S_REG[TKCW]   =  0x000000E0;
    //S_REG[HCCW] =  0x0000FFF0; //V830?
}


int serviceint(unsigned long long cycles) {
    static unsigned long long lasttime=0,lastfb=0;
    static int rowcount,tmp1,frames=0;
    int gamestart;


    //manually generate interrupts
    //if (key[KEY_1]) v810_int(0);
    //if (key[KEY_2]) v810_int(1);
    //if (key[KEY_3]) v810_int(2);
    //if (key[KEY_4]) v810_int(3);
    //if (key[KEY_5]) v810_int(4);

    /*
    OK, this is a strange muck of code... basically it attempts to hit interrupts and
    handle the VIP regs at the correct time. The timing needs a LOT of work. Right now,
    the count values I'm using are the best values from my old clock cycle table. In
    other words, the values are so far off. PBBT!  FIXME
    */

    //Controller Int
    if ((!(tHReg.SCR & 0x80)) && (V810_RControll()&0xFFFC)) {
        v810_int(0);
    }
    //*
    //new way
    if (tHReg.TCR & 0x01) { // Timer Enabled
        if ((cycles-lasttime) > tHReg.tTRC) {
            if (tHReg.tCount) tHReg.tCount--;
            tHReg.TLB = (tHReg.tCount&0xFF);
            tHReg.THB = ((tHReg.tCount>>8)&0xFF);
            lasttime=cycles;
            if (tHReg.tCount == 0) {
                tHReg.tCount = tHReg.tTHW; //reset counter
                tHReg.TCR |= 0x02; //Zero Status
                if (tHReg.TCR & 0x08) {
                    v810_int(1);
                    //tVIPREG.INTPND |= 0x8000; //(tVIPREG.INTENB&0x8000);
                }
            }
        }
    }

    //Handle DPSTTS, XPSTTS, and Frame interrupts
    //Old version, seems to work better!
    if (rowcount < 0x1C) {
        if ((rowcount == 0) && ((cycles-lastfb) > 0x0210) && (!tmp1)) {
            tmp1=1;
            tVIPREG.XPSTTS &= 0x000F;
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0xC0);
            if (++frames > tVIPREG.FRMCYC) {
                frames = 0;
                gamestart = 0x0008;
            }
            else gamestart = 0;
            if (tVIPREG.INTENB&(0x0010|gamestart)) v810_int(4); //FRAMESTART | GAMESTART
            tVIPREG.INTPND |= (0x0010|gamestart); //(tVIPREG.INTENB&0x0018);
            //break; //time to display screen
#ifndef FBHACK
            return 1;
#endif //FBHACK
        }
        if (((cycles-lastfb) > 0x0500) && (!(tVIPREG.XPSTTS&0x8000))) tVIPREG.XPSTTS |= 0x8000;
        if ((cycles-lastfb) > 0x0A00) {
            tVIPREG.XPSTTS = ((tVIPREG.XPSTTS&0xE0)|(rowcount<<8)|(tVIPREG.XPCTRL & 0x02));
            rowcount++;
            lastfb=cycles;
#ifdef FBHACK
            if (rowcount == 1) {
                if (tVIPREG.XPCTRL & 0x0002) tDSPCACHE.DDSPDataWrite = 1;
                return 1; //here? or earlier?
            }
#endif //FBHACK
        }
        if ((rowcount == 0x12) && ((cycles-lastfb) > 0x670)) tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|(tVIPREG.tFrame&1?0xD0:0xC4));
    }
    else {
        if ((rowcount == 0x1C) && ((cycles-lastfb) > 0x10000)) { //0x100000
            tVIPREG.XPSTTS = (0x1B00|(tVIPREG.XPCTRL & 0x02));
            //tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|(tVIPREG.tFrame&1?0xD0:0xC4));

            //Vertical Force hack!
            if ((tVBOpt.CRC32 == 0x9E9B8B92) || //(J) Good
                    (tVBOpt.CRC32 == 0x05D06377) || //(J) Bad (1)
                    (tVBOpt.CRC32 == 0x066288FF) || //(J) Bad (2)
                    (tVBOpt.CRC32 == 0x4C32BA5E))   //(U) Good
                v810_int(4);

            else if (tVIPREG.INTENB&0x4000) v810_int(4); //XPEND
            tVIPREG.INTPND |= 0x4000; //(tVIPREG.INTENB&0x4000);
            rowcount++;
        }
        else if ((rowcount == 0x1D) && ((cycles-lastfb) > 0x18000)) { //0xE690
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0xC0);
            if (tVIPREG.INTENB&0x0002) v810_int(4); //LFBEND
            tVIPREG.INTPND |= 0x0002; //(tVIPREG.INTENB&0x0002);
            rowcount++;
        }
        else if ((rowcount == 0x1E) && ((cycles-lastfb) > 0x20000)) { //0x15E70
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0x40);
            if (tVIPREG.INTENB&0x0004) v810_int(4); //RFBEND
            tVIPREG.INTPND |= 0x0004; //(tVIPREG.INTENB&0x0004);
            rowcount++;
        }
        else if ((rowcount == 0x1F) && ((cycles-lastfb) > 0x28000)) { //0x1FAD8
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|(tVIPREG.tFrame&1?0x48:0x60));
            if (tVIPREG.INTENB&0x2000) v810_int(4); //SBHIT
            tVIPREG.INTPND |= 0x2000;
            rowcount++;
        }
        else if ((rowcount == 0x20) && ((cycles-lastfb) > 0x38000)) { //0x33FD8
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0x40);
            rowcount++;
        }
        else if ((rowcount == 0x21) && ((cycles-lastfb) > 0x42000)) {
            tmp1=0;
            rowcount=0;
#ifdef FBHACK
            if (tVIPREG.XPCTRL & 0x0002) tVIPREG.tFrame++;
#else
            tVIPREG.tFrame++;
#endif //FBHACK
            if ((tVIPREG.tFrame < 1) || (tVIPREG.tFrame > 2)) tVIPREG.tFrame = 1;
            //dtprintf(10,ferr, "\ntVIPREG.tFrame = %d",tVIPREG.tFrame);
            tVIPREG.XPSTTS = (0x1B00|(tVIPREG.tFrame<<2)|(tVIPREG.XPCTRL & 0x02));
            lastfb=cycles;
        }
    }

    return 0;
}

// Generate Interupt #n
void v810_int(WORD iNum) {
    //~ dtprintf(4,ferr,"\nInt atempt %x",iNum);

    if (iNum > 0x0F) return;  // Invalid Interupt number...
    if((S_REG[PSW] & PSW_NP)) return;
    if((S_REG[PSW] & PSW_EP)) return; // Exception pending?
    if((S_REG[PSW] & PSW_ID)) return; // Interupt disabled
    if(iNum < ((S_REG[PSW] & PSW_IA)>>16)) return; // Interupt to low on the chain

    //dtprintf(6,ferr,"\nInt %x",iNum);
    //~ if (debuglog) dbg_intlog(iNum);

    //Ready to Generate the Interupts
    S_REG[EIPC] = PC;
    S_REG[EIPSW] = S_REG[PSW];

    PC = 0xFFFFFE00 | (iNum << 4);

    S_REG[ECR] = 0xFE00 | (iNum << 4);
    S_REG[PSW] = S_REG[PSW] | PSW_EP;
    S_REG[PSW] = S_REG[PSW] | PSW_ID;
    if((iNum+=1) > 0x0F) (iNum = 0x0F);
    S_REG[PSW] = S_REG[PSW] | (iNum << 16); //Set the Interupt

    //tVIPREG.INTPND = tVIPREG.INTENB; //Need to decode the actual Int some day... (how?)
}

// Generate exception #n
//Exceptions are Div by zero, trap and Invalid Opcode, we can live without...
void v810_exp(WORD iNum, WORD eCode) {
    if (iNum > 0x0F) return;  // Invalid Exception number...

    //if(!S_REG[PSW]&PSW_ID) return;
    //if(iNum < ((S_REG[PSW] & PSW_IA)>>16)) return; // Interupt to low on the mask level....
    if ((S_REG[PSW] & PSW_IA)>>16) return; //Interrupt Pending

    eCode &= 0xFFFF;

    if(S_REG[PSW]&PSW_EP) { //Double Exception
        S_REG[FEPC] = PC;
        S_REG[FEPSW] = S_REG[PSW];
        S_REG[ECR] = (eCode << 16); //Exception Code, dont get it???
        S_REG[PSW] = S_REG[PSW] | PSW_NP;
        S_REG[PSW] = S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        PC = 0xFFFFFFD0;
        return;
    } else {		// Regular Exception
        S_REG[EIPC] = PC;
        S_REG[EIPSW] = S_REG[PSW];
        S_REG[ECR] = eCode; //Exception Code, dont get it???
        S_REG[PSW] = S_REG[PSW] | PSW_EP;
        S_REG[PSW] = S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        PC = 0xFFFFFF00 | (iNum << 4);
        return;
    }
}
