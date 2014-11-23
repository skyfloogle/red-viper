#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "vb_types.h"
#include "v810_ins.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"

//Options...
#include "vb_set.h"
#include "vb_dsp.h"

#define NEG(n) ((n) >> 31)
#define POS(n) ((~(n)) >> 31)

//could be done a lot better, later maby!
int v810_trc() {
    int lowB, highB, lowB2, highB2;             // up to 4 bytes for instruction (either 16 or 32 bits)
    static int opcode;
    int arg1 = 0;
    int arg2 = 0;
    int arg3 = 0;
    int i;
    int flags = 0;
    INT64 temp = 0;
    INT64U tempu = 0;
    int val = 0;
    WORD msb = 0;
    static unsigned long long clocks;
    static int lastop,lastclock;

    while (1) {
        if (serviceint(clocks)) return 0; //serviceint() returns with 1 when the screen needs to be redrawn

        PC = (PC&0x07FFFFFE);

        if ((PC>>24) == 0x05) { // RAM
            PC     = (PC & V810_VB_RAM.highaddr);
            lowB   = ((BYTE *)(V810_VB_RAM.off + PC))[0];
            highB  = ((BYTE *)(V810_VB_RAM.off + PC))[1];
            lowB2  = ((BYTE *)(V810_VB_RAM.off + PC))[2];
            highB2 = ((BYTE *)(V810_VB_RAM.off + PC))[3];
        }
        else if ((PC>>24) >= 0x07) { // ROM
            PC     = (PC & V810_ROM1.highaddr);
            lowB   = ((BYTE *)(V810_ROM1.off + PC))[0];
            highB  = ((BYTE *)(V810_ROM1.off + PC))[1];
            lowB2  = ((BYTE *)(V810_ROM1.off + PC))[2];
            highB2 = ((BYTE *)(V810_ROM1.off + PC))[3];
        }
        else {
            return 1;
        }

        // Remove Me!!!
        P_REG[0]=0; // Zero the Zero Reg!!!

        if ((opcode >0) && (opcode < 0x50)) { //hooray for instruction cache! (cache only if last opcode was not bad!)
            lastop = opcode;
            lastclock = opcycle[opcode];
        }
        opcode = highB >> 2;
        if((highB & 0xE0) == 0x80)	// Special opcode format for
            opcode = (highB >> 1);	// type III instructions.

        if((opcode > 0x4F) || (opcode < 0)) {
            return 1;
        }

        clocks += opcycle[opcode];

        switch(optable[opcode].addr_mode) {
        case AM_I: // Do the same Ither way =)
        case AM_II:
            arg1 = (lowB & 0x1F);
            arg2 = (lowB >> 5) + ((highB & 0x3) << 3);
            PC += 2; // 16 bit instruction
            break;
        case AM_III:
            arg1 = ((highB & 0x1) << 8) + (lowB & 0xFE);
            break;
        case AM_IV:
            arg1 = ((highB & 0x3) << 24) + (lowB << 16) + (highB2 << 8) + lowB2;
            break;
        case AM_V:
            arg3 = (lowB >> 5) + ((highB & 0x3) << 3);
            arg2 = (lowB & 0x1F);
            arg1 = (highB2 << 8) + lowB2;
            PC += 4; // 32 bit instruction
            break;
        case AM_VIa: // Mode6 form1
            arg1 = (highB2 << 8) + lowB2;
            arg2 = (lowB & 0x1F);
            arg3 = (lowB >> 5) + ((highB & 0x3) << 3);
            PC += 4; // 32 bit instruction
            break;
        case AM_VIb: // Mode6 form2
            arg1 = (lowB >> 5) + ((highB & 0x3) << 3);
            arg2 = (highB2 << 8) + lowB2; // Whats the order??? 2,3,1 or 1,3,2
            arg3 = (lowB & 0x1F);
            PC += 4; // 32 bit instruction
            break;
        case AM_VII: // Unhandled
            PC +=4; // 32 bit instruction
            break;
        case AM_VIII: // Unhandled
            PC += 4; // 32 bit instruction
            break;
        case AM_IX:
            arg1 = (lowB & 0x1); // Mode ID, Ignore for now
            PC += 2; // 16 bit instruction
            break;
        case AM_BSTR: // Bit String Subopcodes
            arg1 = (lowB >> 5) + ((highB & 0x3) << 3);
            arg2 = (lowB & 0x1F);
            PC += 2; // 16 bit instruction
            break;
        case AM_FPP: // Floating Point Subcode
            arg1 = (lowB >> 5) + ((highB & 0x3) << 3);
            arg2 = (lowB & 0x1F);
            arg3 = ((highB2 >> 2)&0x3F);
            PC += 4; // 32 bit instruction
            break;
        case AM_UDEF: // Invalid opcode.
        default: // Invalid opcode.
            PC += 2;
            break;
        }


        switch(opcode) {
        case LD_H:
            P_REG[arg3] = sign_16(mem_rhword((sign_16(arg1)+P_REG[arg2]) & 0xFFFFFFFE));
            if (lastclock < 6) {
                if ((lastop == LD_B) || (lastop == LD_H) || (lastop == LD_W)) clocks += 1;
                else clocks += 2;
            }
            break;

        case CMP:
            //bug in CY flag generation
            flags = 0;
            //temp = P_REG[arg2]-P_REG[arg1];
            temp = (INT64)((INT64U)(P_REG[arg2])-(INT64U)(P_REG[arg1]));

            // Set Flags
            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            if(((P_REG[arg2]^P_REG[arg1])&(P_REG[arg2]^temp))&0x80000000) flags = flags | PSW_OV;
            //if(temp != ((long)temp)) flags = flags | PSW_CY;
            //if (P_REG[arg2] < P_REG[arg1]) flags = flags | PSW_CY; //FIXES WARIO! Breaks other games
            if ((INT64U)(temp) >> 32) flags = flags | PSW_CY;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;

            break;

        case BE:
            if(S_REG[PSW]&PSW_Z) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case MOVHI:
            P_REG[arg3] = (arg1 << 16) + P_REG[arg2];
            break;

        case ADD:
            flags = 0;
            temp = P_REG[arg2] + P_REG[arg1];
            // Set Flags
            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            //if(temp != ((long)temp)) flags = flags | PSW_CY; // Old
            if (temp < P_REG[arg2]) flags = flags | PSW_CY;
            if(((P_REG[arg2]^(~P_REG[arg1]))&(P_REG[arg2]^temp))&0x80000000) flags = flags | PSW_OV;

            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            P_REG[arg2] = (long)temp;
            break;

        case BNE:
            if((S_REG[PSW]&PSW_Z) == PSW_Z) PC += 2;
            else {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            break;

        case LD_B:
            P_REG[arg3] = sign_8(mem_rbyte(sign_16(arg1)+P_REG[arg2]));
            // Should be 3 clocks when executed alone, 2 when precedes another LD, or 1
            // when precedes an instruction with many clocks (I'm guessing FP, MUL, DIV, etc)
            if (lastclock < 6) {
                if ((lastop == LD_B) || (lastop == LD_H) || (lastop == LD_W)) clocks += 1;
                else clocks += 2;
            }
            break;

        case LD_W:
            P_REG[arg3] = mem_rword((sign_16(arg1)+P_REG[arg2]) & 0xFFFFFFFC);
            if (lastclock < 6) {
                if ((lastop == LD_B) || (lastop == LD_H) || (lastop == LD_W)) clocks += 3;
                else clocks += 4;
            }
            break;

        case MOV:
            P_REG[arg2] = P_REG[arg1];
            break;

        case SUB:
            flags = 0;
            temp = (INT64)((INT64U)(P_REG[arg2])-(INT64U)(P_REG[arg1]));

            // Set Flags
            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            if(((P_REG[arg2]^P_REG[arg1])&(P_REG[arg2]^temp))&0x80000000) flags = flags | PSW_OV;

            if ((INT64U)(temp) >> 32) flags = flags | PSW_CY;

            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            P_REG[arg2] = (long)temp;
            break;



        case SHL:
            flags = 0;
            val = P_REG[arg1] & 0x1F;
            // Set CY before we destroy the regisrer info....
            if((val != 0)&&(P_REG[arg2] >> (32 - val))&0x01) flags = flags | PSW_CY;
            P_REG[arg2] = P_REG[arg2] << val;
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case SHR:
            flags = 0;
            val = P_REG[arg1] & 0x1F;
            // Set CY before we destroy the regisrer info....
            if ((val) && ((P_REG[arg2] >> (val-1))&0x01)) flags = flags | PSW_CY;
            P_REG[arg2] = P_REG[arg2] >> val;
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case JMP:
            PC = (P_REG[arg1] & 0xFFFFFFFE);
            break;

        case SAR:
            // Tweaked by frostgiant
            // Further tweaked by parasyte :)
            flags = 0;
            val = P_REG[arg1] & 0x1F;	// Lower 5 bits
            msb = P_REG[arg2] & 0x80000000; // Grab the MSB

            // Carry is last bit shifted out
            if( (val) && ((P_REG[arg2]>>(val-1))&0x01) )
                flags = flags | PSW_CY;

            for(i = 0; i < val; i++) P_REG[arg2] = (P_REG[arg2] >> 1)|msb; // Append the MSB to the end
            if (P_REG[arg2] & 0x80000000) flags = flags | PSW_S;

            if (!P_REG[arg2]) flags = flags | PSW_Z;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case MUL:
            flags=0;
            temp = (INT64)P_REG[arg1] * (INT64)P_REG[arg2];
            P_REG[30]   = temp >> 32;
            P_REG[arg2] = (long)temp;
            // Set Flags
            if (temp != sign_32((long)temp)) flags = flags | PSW_OV;
            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case DIV:
            flags = 0;
            if ((long)P_REG[arg1] == 0) { // Div by zero error
                // Generate exception!
                v810_exp(8, 0xFF80);
            }
            else {
                if ((P_REG[arg2] == 0x80000000) && (P_REG[arg1] == 0xFFFFFFFF)) {
                    flags = flags |PSW_OV;
                    P_REG[30]   = 0;
                    P_REG[arg2] = 0x80000000;
                }
                else {
                    //P_REG[30]   = (long)P_REG[arg2] % (long)P_REG[arg1];
                    temp        = (long)P_REG[arg2] % (long)P_REG[arg1];
                    P_REG[arg2] = (long)P_REG[arg2] / (long)P_REG[arg1];
                    if (arg2 != 30) P_REG[30] = temp;
                }
                // Set Flags
                if (P_REG[arg2] == 0) flags = flags | PSW_Z;
                if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
                S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            }
            break;

        case MULU:
            flags = 0;
            tempu = (INT64U)P_REG[arg1] * (INT64U)P_REG[arg2];
            P_REG[30]   = tempu >> 32;
            P_REG[arg2] = (WORD)tempu;
            // Set Flags
            if (tempu != (long)tempu) flags = flags | PSW_OV;
            if ((long)tempu == 0) flags = flags | PSW_Z;
            if ((long)tempu & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case DIVU:
            flags = 0;
            if((WORD)P_REG[arg1] == 0) { // Div by zero error
                // Generate exception!
                v810_exp(8, 0xFF80);
            }
            else {
                temp        = (WORD)P_REG[arg2] % (WORD)P_REG[arg1];
                P_REG[arg2] = (WORD)P_REG[arg2] / (WORD)P_REG[arg1];
                if (arg2 != 30) P_REG[30] = temp;
                // Set Flags
                if (P_REG[arg2] == 0) flags = flags | PSW_Z;
                if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
                S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            }
            break;

        case OR:
            flags = 0;
            P_REG[arg2] = P_REG[arg1] | P_REG[arg2];
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case AND:
            flags = 0;
            P_REG[arg2] = P_REG[arg1] & P_REG[arg2];
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case XOR:
            flags = 0;
            P_REG[arg2] = P_REG[arg1] ^ P_REG[arg2];
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case NOT:
            flags = 0;
            P_REG[arg2] = ~P_REG[arg1];
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case MOV_I:
            P_REG[arg2] = sign_5(arg1);
            break;

        case ADD_I:
            flags = 0;
            temp = P_REG[arg2] + sign_5(arg1);
            // Set Flags
            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            if(((P_REG[arg2]^(~sign_5(arg1)))&(P_REG[arg2]^temp))&0x80000000) flags = flags | PSW_OV;
            if (temp < P_REG[arg2]) flags = flags | PSW_CY;

            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            P_REG[arg2] = (WORD)temp;
            break;



        case CMP_I:
            flags = 0;
            temp = (INT64)((INT64U)(P_REG[arg2])-(INT64U)(sign_5(arg1)));

            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            if(((P_REG[arg2]^(sign_5(arg1)))&(P_REG[arg2]^temp))&0x80000000) flags = flags | PSW_OV;
            if ((INT64U)(temp) >> 32) flags = flags | PSW_CY;

            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case SHL_I:
            flags = 0;
            if((arg1)&&(P_REG[arg2] >> (32 - arg1))&0x01) flags = flags | PSW_CY;
            // Set CY before we destroy the register info....
            P_REG[arg2] = P_REG[arg2] << arg1;
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case SHR_I:
            flags = 0;
            if ((arg1) && ((P_REG[arg2] >> (arg1-1))&0x01)) flags = flags | PSW_CY;
            // Set CY before we destroy the register info....
            P_REG[arg2] = P_REG[arg2] >> arg1;
            // Set Flags
            if (P_REG[arg2] == 0) flags = flags | PSW_Z;
            if (P_REG[arg2] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case EI:
            S_REG[PSW] = S_REG[PSW] & (0xFFFFFFFF - PSW_ID);
            break;

        case SAR_I:
            // Tweaked by frostgiant
            // Further tweaks by parasyte :)
            flags = 0;
            msb = P_REG[arg2] & 0x80000000; // Grab the MSB

            // Carry is last bit shifted out
            if( (arg1) && ((P_REG[arg2]>>(arg1-1))&0x01) )
                flags = flags | PSW_CY;

            for(i = 0; i < arg1; i++) P_REG[arg2] = (P_REG[arg2] >> 1) | msb;
            if (P_REG[arg2] & 0x80000000) flags = flags | PSW_S;

            if (!P_REG[arg2]) flags = flags | PSW_Z;
            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            break;

        case LDSR:
            S_REG[(arg1 & 0x1F)] = P_REG[(arg2 & 0x1F)];
            break;

        case STSR:
            P_REG[(arg2 & 0x1F)] = S_REG[(arg1 & 0x1F)];
            break;

        case DI:
            S_REG[PSW] = S_REG[PSW] | PSW_ID;
            break;

        case BV:
            if(S_REG[PSW]&PSW_OV) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BL:
            if(S_REG[PSW]&PSW_CY) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;



        case BNH:
            if((S_REG[PSW]&PSW_Z)||(S_REG[PSW]&PSW_CY)) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BN:
            if(S_REG[PSW]&PSW_S) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BR:
            PC += (sign_9(arg1) & 0xFFFFFFFE);
            clocks += 2;
            break;

        case BLT:
            if((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV))) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BLE:
            if(((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV)))||(S_REG[PSW]&PSW_Z)) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BNV:
            if(!(S_REG[PSW]&PSW_OV)) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BNL:
            if(!(S_REG[PSW]&PSW_CY)) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BH:
            if(!((S_REG[PSW]&PSW_Z)||(S_REG[PSW]&PSW_CY))) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BP:
            if(!(S_REG[PSW] & PSW_S)) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case NOP:
            //Its a NOP do nothing =)
            PC += 2;
            break;

        case BGE:
            if(!((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV)))) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC += 2;
            break;

        case BGT:
            if(!(((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV)))||(S_REG[PSW]&PSW_Z))) {
                PC += (sign_9(arg1) & 0xFFFFFFFE);
                clocks += 2;
            }
            else PC +=2;
            break;

        case JR:
            //if(arg1 & 0x02000000) arg1 +=0xFC000000;
            PC += (sign_26(arg1) & 0xFFFFFFFE);
            break;

        case JAL:
            P_REG[31]=PC+4;
            PC += (sign_26(arg1) & 0xFFFFFFFE);
            break;

        case MOVEA:
            P_REG[arg3] = P_REG[arg2] + sign_16(arg1);
            break;

        case ADDI:
            flags = 0;
            temp = P_REG[arg2] + sign_16(arg1);
            // Set Flags
            if ((long)temp == 0) flags = flags | PSW_Z;
            if ((long)temp & 0x80000000)  flags = flags | PSW_S;
            if (((P_REG[arg2]^(~sign_16(arg1)))&(P_REG[arg2]^temp))&0x80000000) flags = flags | PSW_OV;
            //if (temp != ((long)temp)) flags = flags | PSW_CY;
            if (temp < P_REG[arg2]) flags = flags | PSW_CY;

            S_REG[PSW] = (S_REG[PSW] & 0xFFFFFFF0)|flags;
            P_REG[arg3] = (long)temp;
            break;

        case ORI:
            flags = 0;
            P_REG[arg3] = arg1 | P_REG[arg2];
            // Set Flags
            if (P_REG[arg3] == 0) flags = flags | PSW_Z;
            if (P_REG[arg3] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case ANDI:
            flags = 0;
            P_REG[arg3] = (arg1 & P_REG[arg2]);
            // Set Flags
            if (P_REG[arg3] == 0) flags = (flags | PSW_Z);
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case XORI:
            flags = 0;
            P_REG[arg3] = arg1 ^ P_REG[arg2];
            // Set Flags
            if (P_REG[arg3] == 0) flags = flags | PSW_Z;
            if (P_REG[arg3] & 0x80000000)  flags = flags | PSW_S;
            S_REG[PSW] = (S_REG[PSW] & (0xFFFFFFF0|PSW_CY))|flags;
            break;

        case RETI:
            //Return from Trap/Interupt
            if(S_REG[PSW] & PSW_NP) { // Read the FE Reg
                PC = S_REG[FEPC];
                S_REG[PSW] = S_REG[FEPSW];
            }
            else { 	//Read the EI Reg Interupt
                PC = S_REG[EIPC];
                S_REG[PSW] = S_REG[EIPSW];
            }
            break;

        case MULI:
            temp = (INT64)P_REG[arg1] * (INT64)sign_16(arg3);
            P_REG[arg2] = (long)temp;
            // Set Flags
            if ((long)temp != temp) {
                S_REG[PSW] = S_REG[PSW] | PSW_SAT;
                if (temp < 0) P_REG[arg2] = 0x7FFFFFFF;
                else P_REG[arg2] = 0x80000000;
            }
            break;

        case ST_B:
            mem_wbyte(sign_16(arg2)+P_REG[arg3],P_REG[arg1]&0xFF);
            // Clocks should be 2 clocks when follows another ST
            if (lastop == ST_B) clocks += 1;
            break;

        case ST_H:
            mem_whword((sign_16(arg2)+P_REG[arg3])&0xFFFFFFFE,P_REG[arg1]&0xFFFF);
            if (lastop == ST_H) clocks += 1;
            break;

        case ST_W:
            mem_wword((sign_16(arg2)+P_REG[arg3])&0xFFFFFFFC,P_REG[arg1]);
            if (lastop == ST_W) clocks += 3;
            break;

        case IN_B:
            P_REG[arg3] = port_rbyte(sign_16(arg1)+P_REG[arg2]);
            break;

        case IN_H:
            P_REG[arg3] = port_rhword((sign_16(arg1)+P_REG[arg2]) & 0xFFFFFFFE);
            break;

        case IN_W:
            P_REG[arg3] = port_rword((sign_16(arg1)+P_REG[arg2]) & 0xFFFFFFFC);
            break;

        case OUT_B:
            port_wbyte(sign_16(arg2)+P_REG[arg3],P_REG[arg1]&0xFF);
            //clocks should be 2 when follows another OUT
            if (lastop == OUT_B) clocks += 1;
            break;

        case OUT_H:
            port_whword((sign_16(arg2)+P_REG[arg3])&0xFFFFFFFE,P_REG[arg1]&0xFFFF);
            if (lastop == OUT_H) clocks += 1;
            break;

        case OUT_W:
            port_wword((sign_16(arg2)+P_REG[arg3])&0xFFFFFFFC,P_REG[arg1]);
            if (lastop == OUT_W) clocks += 3;
            break;

        case FPP:
            if(arg3 > 15) {
            } else {
               (*fpsuboptable[arg3].func)(arg1, arg2, 0);
            }
            break;

        case BSTR:
            if(arg2 > 15) {
            } else {
                //dtprintf(6,ferr,"\n%08lx\t%s, $%d", PC, bssuboptable[arg2].opname,arg1);
                (*bssuboptable[arg2].func)(arg1, 0, 0);
            }
            break;

            //The never-to-rarely used instructions

        case SETF:
            //SETF may contain bugs
            P_REG[arg2] = 0;
            switch (arg1 & 0x0F) {
            case COND_V:
                if (S_REG[PSW] & PSW_OV) P_REG[arg2] = 1;
                break;
            case COND_C:
                if (S_REG[PSW] & PSW_CY) P_REG[arg2] = 1;
                break;
            case COND_Z:
                if (S_REG[PSW] & PSW_Z) P_REG[arg2] = 1;
                break;
            case COND_NH:
                if (S_REG[PSW] & (PSW_CY|PSW_Z)) P_REG[arg2] = 1;
                break;
            case COND_S:
                if (S_REG[PSW] & PSW_S) P_REG[arg2] = 1;
                break;
            case COND_T:
                P_REG[arg2] = 1;
                break;
            case COND_LT:
                //FIXME
                if ((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV))) P_REG[arg2] = 1;
                break;
            case COND_LE:
                //FIXME
                if (((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV)))|(S_REG[PSW]&PSW_Z)) P_REG[arg2] = 1;
                break;
            case COND_NV:
                if (!(S_REG[PSW] & PSW_OV)) P_REG[arg2] = 1;
                break;
            case COND_NC:
                if (!(S_REG[PSW] & PSW_CY)) P_REG[arg2] = 1;
                break;
            case COND_NZ:
                if (!(S_REG[PSW] & PSW_Z)) P_REG[arg2] = 1;
                break;
            case COND_H:
                if (!(S_REG[PSW] & (PSW_CY|PSW_Z))) P_REG[arg2] = 1;
                break;
            case COND_NS:
                if (!(S_REG[PSW] & PSW_S)) P_REG[arg2] = 1;
                break;
            case COND_F:
                //always false! do nothing more
                break;
            case COND_GE:
                //FIXME
                if (!((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV)))) P_REG[arg2] = 1;
                break;
            case COND_GT:
                //FIXME
                if (!(((!!(S_REG[PSW]&PSW_S))^(!!(S_REG[PSW]&PSW_OV)))|(S_REG[PSW]&PSW_Z))) P_REG[arg2] = 1;
                break;
            }
            break;

        case HALT:
            break;

        case CAXI:
            break;

        case MACI:
            break;

        case TRAP:
            break;

        default:
            break;
        }
    }

    return 0;
}
