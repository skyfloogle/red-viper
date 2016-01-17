#include "v810_cpu.h"
#include "vb_types.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "v810_mem.h"

//////////////////////////////////////////////////////////
// Memory Functions

// Memory read functions
BYTE mem_rbyte(WORD addr) {
    addr = addr & 0x07FFFFFF; // map to 24 bit address CFFFFFFF

    switch((addr&0x7000000)) {// switch on address
    case 0x7000000:
        return ((BYTE *)(V810_ROM1.off + (addr & V810_ROM1.highaddr)))[0];
        break;
    case 0:
        if((addr >= V810_DISPLAY_RAM.lowaddr)&&(addr <=V810_DISPLAY_RAM.highaddr)) {
            return ((BYTE *)(V810_DISPLAY_RAM.off + addr))[0];
        } else if((addr >= V810_VIPCREG.lowaddr)&&(addr <=V810_VIPCREG.highaddr)) {
            return (*V810_VIPCREG.rfuncb)(addr);
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if((addr >= 0x00078000)&&(addr <= 0x0007FFFF)) {
            if(addr < 0x0007A000) //CHR 0-511
                return ((BYTE *)(V810_DISPLAY_RAM.off + (addr-0x00078000 + 0x00006000)))[0];
            else if(addr < 0x0007C000) //CHR 512-1023
                return ((BYTE *)(V810_DISPLAY_RAM.off + (addr-0x0007A000 + 0x0000E000)))[0];
            else if(addr < 0x0007E000) //CHR 1024-1535
                return ((BYTE *)(V810_DISPLAY_RAM.off + (addr-0x0007C000 + 0x00016000)))[0];
            else //CHR 1536-2047
                return ((BYTE *)(V810_DISPLAY_RAM.off + (addr-0x0007E000 + 0x0001E000)))[0];
        }
        break;
    case 0x1000000:
        if((addr >= V810_SOUND_RAM.lowaddr)&&(addr <=V810_SOUND_RAM.highaddr)) {
            //~ dtprintf(0,ferr,"\nRead  BYTE  [%08x]:%02x  //SoundRam",addr,((BYTE *)(V810_SOUND_RAM.off + addr))[0]);
            return ((BYTE *)(V810_SOUND_RAM.off + addr))[0];
        }
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nRead  BYTE  [%08x]:%02x  //VBRam",addr,((BYTE *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0]);
        return ((BYTE *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0];
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nRead  BYTE  PC:%08x [%08x]:%02x  //GameRam",PC,addr,((BYTE *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0]);
        return ((BYTE *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0];
        break;
    case 0x2000000:
        if((addr >= V810_HCREG.lowaddr)&&(addr <=V810_HCREG.highaddr)) {
            return (*V810_HCREG.rfuncb)(addr);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  BYTE  [%08x]:%02x",addr,0);
        return(0);
        break;
    }// End switch on address
    return(0); //Stops a silly compiler error
}

HWORD mem_rhword(WORD addr) {
    addr = addr & 0x07FFFFFE; // map to 24 bit address, mask first bit

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 16, 0, 0);

    switch((addr&0x7000000)) {
    case 0x7000000:
        return ((HWORD *)(V810_ROM1.off + (addr & V810_ROM1.highaddr)))[0]; // does this work?
        break;
    case 0:
        if((addr >= V810_DISPLAY_RAM.lowaddr)&&(addr <=V810_DISPLAY_RAM.highaddr)) {
            return ((HWORD *)(V810_DISPLAY_RAM.off + addr))[0];
        } else if((addr >= V810_VIPCREG.lowaddr)&&(addr <=V810_VIPCREG.highaddr)) {
            return (*V810_VIPCREG.rfunch)(addr);
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if((addr >= 0x00078000)&&(addr <= 0x0007FFFF)) {
            if(addr < 0x0007A000) //CHR 0-511
                return ((HWORD *)(V810_DISPLAY_RAM.off + (addr-0x00078000 + 0x00006000)))[0];
            else if(addr < 0x0007C000) //CHR 512-1023
                return ((HWORD *)(V810_DISPLAY_RAM.off + (addr-0x0007A000 + 0x0000E000)))[0];
            else if(addr < 0x0007E000) //CHR 1024-1535
                return ((HWORD *)(V810_DISPLAY_RAM.off + (addr-0x0007C000 + 0x00016000)))[0];
            else //CHR 1536-2047
                return ((HWORD *)(V810_DISPLAY_RAM.off + (addr-0x0007E000 + 0x0001E000)))[0];
        }
        break;
    case 0x1000000:
        if((addr >= V810_SOUND_RAM.lowaddr)&&(addr <=V810_SOUND_RAM.highaddr)) {
            //~ dtprintf(0,ferr,"\nRead  HWORD [%08x]:%04x  //SoundRam",addr,((HWORD *)(V810_SOUND_RAM.off + addr))[0]);
            return ((HWORD *)(V810_SOUND_RAM.off + addr))[0];
        }
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nRead  HWORD [%08x]:%04x  //VBRam",addr,((HWORD *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0]);
        return ((HWORD *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0];
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nRead  HWORD PC:%08x [%08x]:%04x  //GameRam",PC,addr,((HWORD *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0]);
        return ((HWORD *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0];
        break;
    case 0x2000000:
        if((addr >= V810_HCREG.lowaddr)&&(addr <=V810_HCREG.highaddr)) {
            return (*V810_HCREG.rfunch)(addr);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  HWORD [%08x]:%04x",addr,0);
        return(0);
        break;
    }
    return(0); //Stops a silly compiler error
}

WORD mem_rword(WORD addr) {
    addr = addr & 0x07FFFFFC; // map to 24 bit address, mask first 2 bytes to zero

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 32, 0, 0);

    switch((addr&0x7000000)) {
    case 0x7000000:
        return ((WORD *)(V810_ROM1.off + (addr & V810_ROM1.highaddr)))[0];
        break;
    case 0:
        if((addr >= V810_DISPLAY_RAM.lowaddr)&&(addr <=V810_DISPLAY_RAM.highaddr)) {
            return ((WORD *)(V810_DISPLAY_RAM.off + addr))[0];
        } else if((addr >= V810_VIPCREG.lowaddr)&&(addr <=V810_VIPCREG.highaddr)) {
            return (*V810_VIPCREG.rfuncw)(addr);
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if((addr >= 0x00078000)&&(addr <= 0x0007FFFF)) {
            if(addr < 0x0007A000) //CHR 0-511
                return ((WORD *)(V810_DISPLAY_RAM.off + (addr-0x00078000 + 0x00006000)))[0];
            else if(addr < 0x0007C000) //CHR 512-1023
                return ((WORD *)(V810_DISPLAY_RAM.off + (addr-0x0007A000 + 0x0000E000)))[0];
            else if(addr < 0x0007E000) //CHR 1024-1535
                return ((WORD *)(V810_DISPLAY_RAM.off + (addr-0x0007C000 + 0x00016000)))[0];
            else //CHR 1536-2047
                return ((WORD *)(V810_DISPLAY_RAM.off + (addr-0x0007E000 + 0x0001E000)))[0];
        }
        break;
    case 0x1000000:
        if((addr >= V810_SOUND_RAM.lowaddr)&&(addr <=V810_SOUND_RAM.highaddr)) {
            //~ dtprintf(0,ferr,"\nRead  WORD  [%08x]:%08x  //SoundRam",addr,((WORD *)(V810_SOUND_RAM.off + addr))[0]);
            return ((WORD *)(V810_SOUND_RAM.off + addr))[0];
        }
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nRead  WORD  [%08x]:%08x  //VBRam",addr,((WORD *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0]);
        return ((WORD *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0];
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nRead  WORD  PC:%08x [%08x]:%08x  //GameRam",PC,addr,((WORD *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0]);
        return ((WORD *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0];
        break;
    case 0x2000000:
        if((addr >= V810_HCREG.lowaddr)&&(addr <=V810_HCREG.highaddr)) {
            return (*V810_HCREG.rfuncw)(addr);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  WORD  [%08x]:%08x",addr,0);
        return(0);
        break;
    }
    return(0); //Stops a silly compiler error
}

/////////////////////////////////////////////////////////////////////////////
//Memory Write Func
void mem_wbyte(WORD addr, BYTE data) {
    int i =0;
    addr = addr & 0x07FFFFFF; // map to 24 bit address

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 8, 1, data);

    switch((addr&0x7000000)) {
    case 0:
        if((addr >= V810_DISPLAY_RAM.lowaddr)&&(addr <=V810_DISPLAY_RAM.highaddr)) {
            ((BYTE *)(V810_DISPLAY_RAM.off + addr))[0] = data;

            if(addr < BGMAP_OFFSET) {
                //Kill it if writes to Char Table
                if(((addr>=0x00006000)&&(addr<0x00008000)) |
                        ((addr>=0x0000E000)&&(addr<0x00010000)) |
                        ((addr>=0x00016000)&&(addr<0x00018000)) |
                        ((addr>=0x0001E000)&&(addr<0x00020000))) {
                    for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                    tDSPCACHE.ObjDataCacheInvalid=1;
                } else { //Direct Mem Writes, darn thoes fragmented memorys!!!
                    //tDSPCACHE.DDSPDataWrite=1;
                }
            }else if((addr >=OBJ_OFFSET)&&(addr < (OBJ_OFFSET+(OBJ_SIZE*1024)))) { //Writes to Obj Table
                tDSPCACHE.ObjDataCacheInvalid=1;
            } else if((addr >=BGMAP_OFFSET)&&(addr < (BGMAP_OFFSET+(14*BGMAP_SIZE)))) { //Writes to BGMap Table
                tDSPCACHE.BGCacheInvalid[((addr-BGMAP_OFFSET)/BGMAP_SIZE)]=1;
            }
        } else if((addr >= V810_VIPCREG.lowaddr)&&(addr <=V810_VIPCREG.highaddr)) {
            (*V810_VIPCREG.wfuncb)(addr,data);
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if((addr >= 0x00078000)&&(addr <= 0x0007FFFF)) {
            if(addr < 0x0007A000) //CHR 0-511
                ((BYTE *)(V810_DISPLAY_RAM.off + ((addr-0x00078000) + 0x00006000)))[0] = data;
            else if(addr < 0x0007C000) //CHR 512-1023
                ((BYTE *)(V810_DISPLAY_RAM.off + ((addr-0x0007A000) + 0x0000E000)))[0] = data;
            else if(addr < 0x0007E000) //CHR 1024-1535
                ((BYTE *)(V810_DISPLAY_RAM.off + ((addr-0x0007C000) + 0x00016000)))[0] = data;
            else //CHR 1536-2047
                ((BYTE *)(V810_DISPLAY_RAM.off + ((addr-0x0007E000) + 0x0001E000)))[0] = data;
            //Invalidate, writes to Char table
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        break;
    case 0x1000000:
        if((addr >= V810_SOUND_RAM.lowaddr)&&(addr <=V810_SOUND_RAM.highaddr)) {
            //~ dtprintf(0,ferr,"\nWrite BYTE  [%08x]:%02x  //SoundRam",addr,data);
            ((BYTE *)(V810_SOUND_RAM.off + addr))[0] = data;
            sound_update(addr);
        }
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nWrite BYTE  [%08x]:%02x  //VBRam",addr,data);
        ((BYTE *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0] = data;
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nWrite BYTE  PC:%08x [%08x]:%02x  //GameRam",PC,addr,data);
        ((BYTE *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0] = data;
        break;
    case 0x2000000:
        if((addr >= V810_HCREG.lowaddr)&&(addr <=V810_HCREG.highaddr)) {
            (*V810_HCREG.wfuncb)(addr,data);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite BYTE  [%08x]:%02x",addr,data);
        break;
    }
}

void mem_whword(WORD addr, HWORD data) {
    int i = 0;
    addr = addr & 0x07FFFFFE; // map to 24 bit address, mask first bit

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 16, 1, data);

    switch((addr&0x7000000)) {
    case 0:
        if((addr >= V810_DISPLAY_RAM.lowaddr)&&(addr <=V810_DISPLAY_RAM.highaddr)) {
            ((HWORD *)(V810_DISPLAY_RAM.off + addr))[0] = data;
            if(addr < BGMAP_OFFSET) { //Kill it if writes to Char Table
                //Kill it if writes to Char Table
                if(((addr>=0x00006000)&&(addr<0x00008000)) |
                        ((addr>=0x0000E000)&&(addr<0x00010000)) |
                        ((addr>=0x00016000)&&(addr<0x00018000)) |
                        ((addr>=0x0001E000)&&(addr<0x00020000))) {
                    for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                    tDSPCACHE.ObjDataCacheInvalid=1;
                } else { //Direct Mem Writes, darn thoes fragmented memorys!!!
                    //tDSPCACHE.DDSPDataWrite=1;
                }
            }else if((addr >=OBJ_OFFSET)&&(addr < (OBJ_OFFSET+(OBJ_SIZE*1024)))) { //Writes to Obj Table
                tDSPCACHE.ObjDataCacheInvalid=1;
            } else if((addr >=BGMAP_OFFSET)&&(addr < (BGMAP_OFFSET+(14*BGMAP_SIZE)))) { //Writes to BGMap Table
                tDSPCACHE.BGCacheInvalid[((addr-BGMAP_OFFSET)/BGMAP_SIZE)]=1;
            }
        } else if((addr >= V810_VIPCREG.lowaddr)&&(addr <=V810_VIPCREG.highaddr)) {
            (*V810_VIPCREG.wfunch)(addr,data);
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if((addr >= 0x00078000)&&(addr <= 0x0007FFFF)) {
            if(addr < 0x0007A000) //CHR 0-511
                ((HWORD *)(V810_DISPLAY_RAM.off + ((addr-0x00078000) + 0x00006000)))[0] = data;
            else if(addr < 0x0007C000) //CHR 512-1023
                ((HWORD *)(V810_DISPLAY_RAM.off + ((addr-0x0007A000) + 0x0000E000)))[0] = data;
            else if(addr < 0x0007E000) //CHR 1024-1535
                ((HWORD *)(V810_DISPLAY_RAM.off + ((addr-0x0007C000) + 0x00016000)))[0] = data;
            else //CHR 1536-2047
                ((HWORD *)(V810_DISPLAY_RAM.off + ((addr-0x0007E000) + 0x0001E000)))[0] = data;
            //Invalidate, writes to Char table
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        break;
    case 0x1000000:
        if((addr >= V810_SOUND_RAM.lowaddr)&&(addr <=V810_SOUND_RAM.highaddr)) {
            //~ dtprintf(0,ferr,"\nWrite HWORD [%08x]:%04x  //SoundRam",addr,data);
            ((HWORD *)(V810_SOUND_RAM.off + addr))[0] = data;
            sound_update(addr);
        }
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nWrite HWORD [%08x]:%04x  //VBRam",addr,data);
        ((HWORD *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0] = data;
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nWrite HWORD PC:%08x [%08x]:%04x  //GameRam",PC,addr,data);
        ((HWORD *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0] = data;
        break;
    case 0x2000000:
        if((addr >= V810_HCREG.lowaddr)&&(addr <=V810_HCREG.highaddr)) {
            (*V810_HCREG.wfunch)(addr,data);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite HWORD [%08x]:%04x",addr,data);
        break;
    }
}

void mem_wword(WORD addr, WORD data) {
    int i = 0;
    addr = addr & 0x07FFFFFC; // map to 24 bit address, mask first 2 bytes to zero

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 32, 1, data);

    switch((addr&0x7000000)) {
    case 0:
        if((addr >= V810_DISPLAY_RAM.lowaddr)&&(addr <=V810_DISPLAY_RAM.highaddr)) {
            ((WORD *)(V810_DISPLAY_RAM.off + addr))[0] = data;
            if(addr < BGMAP_OFFSET) { //Kill it if writes to Char Table
                //Kill it if writes to Char Table
                if(((addr>=0x00006000)&&(addr<0x00008000)) |
                        ((addr>=0x0000E000)&&(addr<0x00010000)) |
                        ((addr>=0x00016000)&&(addr<0x00018000)) |
                        ((addr>=0x0001E000)&&(addr<0x00020000))) {
                    for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                    tDSPCACHE.ObjDataCacheInvalid=1;
                } else { //Direct Mem Writes, darn thoes fragmented memorys!!!
                    //tDSPCACHE.DDSPDataWrite=1;
                }
            }else if((addr >=OBJ_OFFSET)&&(addr < (OBJ_OFFSET+(OBJ_SIZE*1024)))) { //Writes to Obj Table
                tDSPCACHE.ObjDataCacheInvalid=1;
            } else if((addr >=BGMAP_OFFSET)&&(addr < (BGMAP_OFFSET+(14*BGMAP_SIZE)))) { //Writes to BGMap Table
                tDSPCACHE.BGCacheInvalid[((addr-BGMAP_OFFSET)/BGMAP_SIZE)]=1;
            }
        } else if((addr >= V810_VIPCREG.lowaddr)&&(addr <=V810_VIPCREG.highaddr)) {
            (*V810_VIPCREG.wfuncw)(addr,data);
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if((addr >= 0x00078000)&&(addr <= 0x0007FFFF)) {
            if(addr < 0x0007A000)  //CHR 0-511
                ((WORD *)(V810_DISPLAY_RAM.off + ((addr-0x00078000) + 0x00006000)))[0] = data;
            else if(addr < 0x0007C000) //CHR 512-1023
                ((WORD *)(V810_DISPLAY_RAM.off + ((addr-0x0007A000) + 0x0000E000)))[0] = data;
            else if(addr < 0x0007E000) //CHR 1024-1535
                ((WORD *)(V810_DISPLAY_RAM.off + ((addr-0x0007C000) + 0x00016000)))[0] = data;
            else      //CHR 1536-2047
                ((WORD *)(V810_DISPLAY_RAM.off + ((addr-0x0007E000) + 0x0001E000)))[0] = data;
            //Invalidate, writes to Char table
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        break;
    case 0x1000000:
        if((addr >= V810_SOUND_RAM.lowaddr)&&(addr <=V810_SOUND_RAM.highaddr)) {
            //~ dtprintf(0,ferr,"\nWrite WORD  [%08x]:%08x  //SoundRam",addr,data);
            ((WORD *)(V810_SOUND_RAM.off + addr))[0] = data;
            sound_update(addr);
        }
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nWrite WORD  [%08x]:%08x  //VBRam",addr,data);
        ((WORD *)(V810_VB_RAM.off + (addr & V810_VB_RAM.highaddr)))[0] = data;
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nWrite WORD  PC:%08x [%08x]:%08x  //GameRam",PC,addr,data);
        ((WORD *)(V810_GAME_RAM.off + (addr & V810_GAME_RAM.highaddr)))[0] = data;
        break;
    case 0x2000000:
        if((addr >= V810_HCREG.lowaddr)&&(addr <=V810_HCREG.highaddr)) {
            (*V810_HCREG.wfuncw)(addr,data);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite WORD  [%08x]:%08x",addr,data);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////////
// Hardware Controll Reg....
BYTE hcreg_rbyte(WORD addr) {
    addr = (addr & 0x0200003C);
    switch(addr) {
    case 0x02000000:    //CCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CCR [%08x]",addr);
        return (tHReg.CCR|0x04); //Mask the Write Only Bit...
        break;
    case 0x02000004:    //CCSR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CCSR [%08x]",addr);
        return (tHReg.CCSR|0x04);
        break;
    case 0x02000008:    //CDTR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CDTR [%08x]",addr);
        return tHReg.CDTR;
        break;
    case 0x0200000C:    //CDRR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CDRR [%08x]",addr);
        return tHReg.CDRR;
        break;
    case 0x02000010:    //SLB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG SLB [%08x]",addr);
        return tHReg.SLB;
        break;
    case 0x02000014:    //SHB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG SHB [%08x]",addr);
        return tHReg.SHB;
        break;
    case 0x02000018:    //TLB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG TLB [%08x]",addr);
        return tHReg.TLB;//Should return the current counter???
        break;
    case 0x0200001C:    //THB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG THB [%08x]",addr);
        return tHReg.THB;
        break;
    case 0x02000020:    //TCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG TCR [%08x]",addr);
        return (tHReg.TCR|0x04);
        break;
    case 0x02000024:    //WCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG WCR [%08x]",addr);
        return tHReg.WCR;
        break;
    case 0x02000028:    //SCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG SCR [%08x]",addr);
        //tHReg.SCR=(tHReg.SCR^0x02); //Remove Me!!
        return (tHReg.SCR|0x4C);
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  BYTE HREG error [%08x]",addr);
        return 0;
        break;
    }
}

void hcreg_wbyte(WORD addr, BYTE data) {
    addr = (addr & 0x0200003C);
    switch(addr) {
    case 0x02000000:    //CCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG CCR [%08x]:%02x ",addr,data);
        tHReg.CCR = ((data|0x69)&0xFD);
        break;
    case 0x02000004:    //CCSR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG CCSR [%08x]:%02x ",addr,data);
        tHReg.CCSR = ((data|0x60)&0xFA);
        break;
    case 0x02000008:    //CDTR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG CDTR [%08x]:%02x ",addr,data);
        tHReg.CDTR = data;
        break;
        //case 0x0200000C:    //CDRR  // Read Only!
        //    tHReg.CDRR = data;
        //    break;
        //case 0x02000010:    //SLB
        //    tHReg.SLB = data;
        //    break;
        //case 0x02000014:    //SHB
        //    tHReg.SHB = data;
        //    break;
    case 0x02000018:    //TLB
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG TLB [%08x]:%02x ",addr,data);
        tHReg.TLB = data;
        tHReg.tTHW = (tHReg.TLB | (tHReg.tTHW & 0xFF00)); //Reset internal count
        //tHReg.tTHW = (tHReg.TLB | (tHReg.THB << 8)); //Reset internal count
        break;
    case 0x0200001C:    //THB
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG THB [%08x]:%02x ",addr,data);
        tHReg.THB = data;
        tHReg.tTHW = ((tHReg.THB << 8) | (tHReg.tTHW & 0xFF)); //Reset internal count
        //tHReg.tTHW = (tHReg.TLB | (tHReg.THB << 8)); //Reset internal count
        break;
    case 0x02000020:    //TCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG TCR [%08x]:%02x ",addr,data);
        if ((tHReg.TCR & 1) && ((data & 0x05) == 0x04)) break; //Cannot disable timer and clear ZStat at the same time!

        if (data & 0x01) {
            tHReg.TLB = (tHReg.tTHW&0xFF);
            tHReg.THB = ((tHReg.tTHW>>8)&0xFF);
            tHReg.tCount = tHReg.tTHW;
            if (!(tHReg.TCR & 0x01)) tHReg.tReset = 1;
        }
        if (data & 0x10) { //20us timer
            tHReg.tTRC = 400;
        }
        else { //100us timer
            if (tVBOpt.CRC32 == 0xFA44402D) tHReg.tTRC = 20000; //Space Invaders hack! 1ms
            else tHReg.tTRC = 2000; //100us
        }

        tHReg.TCR = (((data|0xE4)&0xFD)|(tHReg.TCR&0x02));
        if ((data & 0x04) && (!(tHReg.TCR & 0x01))) { //cannot clear ZStat if timer is enabled...
            tHReg.TCR &= 0xFD; // Clear the ZStat Flag...
        }
        break;
    case 0x02000024:    //WCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG WCR [%08x]:%02x ",addr,data);
        tHReg.WCR = (data|0xFC); // Mask
        break;
    case 0x02000028:    //SCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG SCR [%08x]:%02x ",addr,data);
        tHReg.SCR = ((data|0x48)&0xFD);//0x48
        if(data & 0x80) { //Int Clear (What)
            //Clear the Pupy, but why the RETI will clear it for us....
        }
        if(data & 0x04) { //Hardware Read
            if(!(data&0x01)) { //Hardware Read Disabled
                HWORD r = V810_RControll();   //Read the Keybd
                tHReg.SLB =(BYTE)(r&0xFF);    //And stor the results...
                tHReg.SHB =(BYTE)((r>>8)&0xFF);
            }
        } else if(data & 0x20) { //Software Read, same for now....
            HWORD r = V810_RControll();   //Read the Keybd
            tHReg.SLB =(BYTE)(r&0xFF);    //And stor the results...
            tHReg.SHB =(BYTE)((r>>8)&0xFF);
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite  BYTE HREG error [%08x]:%04x ",addr,data);
        break;
    }
}

HWORD hcreg_rhword(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  HWORD HReg [%08x]:%04x !!!!!!",addr,(HWORD)hcreg_rbyte(addr));
    return (HWORD)hcreg_rbyte(addr);
}
void hcreg_whword(WORD addr, HWORD data) {
    //~ dtprintf(0,ferr,"\nWrite  HWORD HReg [%08x]:%04x !!!!!!",addr,data);
    hcreg_wbyte(addr,(BYTE)data); //All read/write is byte...
}
WORD hcreg_rword(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  WORD HReg [%08x]:%08x !!!!!!",addr,(WORD)hcreg_rbyte(addr));
    return (WORD)hcreg_rbyte(addr);
}
void hcreg_wword(WORD addr, WORD data) {
    //~ dtprintf(0,ferr,"\nWrite  WORD HReg [%08x]:%08x !!!!!!",addr,data);
    hcreg_wbyte(addr,(BYTE)data); //All read/write is byte...
}

///////////////////////////////////////////////////////////////////////////////
// Port read functions
BYTE  port_rbyte(WORD addr) { return  mem_rbyte(addr); }
HWORD port_rhword(WORD addr) { return mem_rhword(addr); }
WORD  port_rword(WORD addr) { return mem_rword(addr); }

/////////////////////////////////////////////////////////////////////////////
//Port Write Func
void port_wbyte(WORD addr, BYTE data)   { mem_wbyte(addr,data); }
void port_whword(WORD addr, HWORD data) { mem_whword(addr,data); }
void port_wword(WORD addr, WORD data)   { mem_wword(addr,data); }

BYTE vipcreg_rbyte(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  BYTE VIP [%08x]:%02x **************",addr,0);
    return 0xFF;
}

void vipcreg_wbyte(WORD addr, BYTE data) {
    //~ dtprintf(0,ferr,"\nWrite  BYTE VIP [%08x]:%02x **************",addr,data);
}

HWORD vipcreg_rhword(WORD addr) {
    addr=(addr&0x0005007E); //Bring it into line
    addr=(addr|0x0005F800); //make sure all the right bits are on
    switch(addr) {
    case 0x0005F800:    //INTPND
        //~ dtprintf(4,ferr,"\nRead   HWORD VIP INTPND [%08x]:%04x ",addr,tVIPREG.INTPND);
        return tVIPREG.INTPND;
        break;
    case 0x0005F802:    //INTENB
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP INTENB [%08x]:%04x ",addr,tVIPREG.INTENB);
        return tVIPREG.INTENB;
        break;
    case 0x0005F804:    //INTCLR
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP INTCLR [%08x]:%04x ",addr,tVIPREG.INTCLR);
        return tVIPREG.INTCLR;
        break;
    case 0x0005F820:    //DPSTTS
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP DPSTTS [%08x]:%04x ",addr,tVIPREG.DPSTTS);
        //Handled in vb_dsp.c, not!  what is the right time?
        //if(tVIPREG.DPSTTS == 0x0040) tVIPREG.DPSTTS = 0x003C;
        //else tVIPREG.DPSTTS = 0x0040;

        return tVIPREG.DPSTTS;
        break;
    case 0x0005F822:    //DPCTRL
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP DPCTRL [%08x]:%04x ",addr,tVIPREG.DPCTRL);
        return tVIPREG.DPCTRL;
        break;
    case 0x0005F824:    //BRTA
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BRTA [%08x]:%04x ",addr,tVIPREG.BRTA);
        return tVIPREG.BRTA;
        break;
    case 0x0005F826:    //BRTB
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BRTB [%08x]:%04x ",addr,tVIPREG.BRTB);
        return tVIPREG.BRTB;
        break;
    case 0x0005F828:    //BRTC
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BRTC [%08x]:%04x ",addr,tVIPREG.BRTC);
        return tVIPREG.BRTC;
        break;
    case 0x0005F82A:    //REST
        //~ dtprintf(7,ferr,"\nRead  HWORD VIP REST [%08x]:%04x ",addr,tVIPREG.REST);
        return tVIPREG.REST;
        break;
    case 0x0005F82E:    //FRMCYC
        //~ dtprintf(7,ferr,"\nRead  HWORD VIP FRMCYC [%08x]:%04x ",addr,tVIPREG.FRMCYC);
        return tVIPREG.FRMCYC;
        break;
    case 0x0005F830:    //CTA, ColumTable Address
        //~ dtprintf(8,ferr,"\nRead  HWORD VIP CTA [%08x]:%04x ",addr,tVIPREG.CTA);
        return tVIPREG.CTA;
        break;
    case 0x0005F840:    //XPSTTS
        //~ dtprintf(3,ferr,"\nRead  HWORD VIP XPSTTS [%08x]:%04x ",addr,tVIPREG.XPSTTS);
        //tVIPREG.XPSTTS=(tVIPREG.XPSTTS^0x0C);
        return tVIPREG.XPSTTS;
        break;
    case 0x0005F842:    //XPCTRL
        //~ dtprintf(3,ferr,"\nRead  HWORD VIP XPCTRL [%08x]:%04x ",addr,tVIPREG.XPCTRL);
        //tVIPREG.XPSTTS = 0xfff0;
        return tVIPREG.XPCTRL;
        break;
    case 0x0005F844:    //VER
        //~ dtprintf(8,ferr,"\nRead  HWORD VIP VER [%08x]:%04x ",addr,tVIPREG.VER);
        return tVIPREG.VER;
        break;
    case 0x0005F848:    //SPT0
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT0 [%08x]:%04x ",addr,tVIPREG.SPT[0]);
        return tVIPREG.SPT[0];
        break;
    case 0x0005F84A:    //SPT1
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT1 [%08x]:%04x ",addr,tVIPREG.SPT[1]);
        return tVIPREG.SPT[1];
        break;
    case 0x0005F84C:    //SPT2
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT2 [%08x]:%04x ",addr,tVIPREG.SPT[2]);
        return tVIPREG.SPT[2];
        break;
    case 0x0005F84E:    //SPT3
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT3 [%08x]:%04x ",addr,tVIPREG.SPT[3]);
        return tVIPREG.SPT[3];
        break;
    case 0x0005F860:    //GPLT0
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT0 [%08x]:%04x ",addr,tVIPREG.GPLT[0]);
        return tVIPREG.GPLT[0];
        break;
    case 0x0005F862:    //GPLT1
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT1 [%08x]:%04x ",addr,tVIPREG.GPLT[1]);
        return tVIPREG.GPLT[1];
        break;
    case 0x0005F864:    //GPLT2
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT2 [%08x]:%04x ",addr,tVIPREG.GPLT[2]);
        return tVIPREG.GPLT[2];
        break;
    case 0x0005F866:    //GPLT3
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT3 [%08x]:%04x ",addr,tVIPREG.GPLT[3]);
        return tVIPREG.GPLT[3];
        break;
    case 0x0005F868:    //JPLT0
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT0 [%08x]:%04x ",addr,tVIPREG.JPLT[0]);
        return tVIPREG.JPLT[0];
        break;
    case 0x0005F86A:    //JPLT1
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT1 [%08x]:%04x ",addr,tVIPREG.JPLT[1]);
        return tVIPREG.JPLT[1];
        break;
    case 0x0005F86C:    //JPLT2
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT2 [%08x]:%04x ",addr,tVIPREG.JPLT[2]);
        return tVIPREG.JPLT[2];
        break;
    case 0x0005F86E:    //JPLT3
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT3 [%08x]:%04x ",addr,tVIPREG.JPLT[3]);
        return tVIPREG.JPLT[3];
        break;
    case 0x0005F870:    //BKCOL
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BKCOL [%08x]:%04x ",addr,tVIPREG.BKCOL);
        return tVIPREG.BKCOL;
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  HWORD VIP error [%08x]:%04x ",addr,0xFFFF);
        return 0xFFFF;
        break;
    }
    return(0); //Stops a silly compiler error
}

void vipcreg_whword(WORD addr, HWORD data) {
    int i;
    addr=(addr&0x0005007E); //Bring it into line
    addr=(addr|0x0005F800); //make shure all the right bits are on
    switch(addr) {
    case 0x0005F800:    //INTPND
        //~ dtprintf(4,ferr,"\nWrite  HWORD VIP INTPND [%08x]:%04x ",addr,data);
        //****            tVIPREG.INTPND = data;
        break;
    case 0x0005F802:    //INTENB
        //~ dtprintf(4,ferr,"\nWrite  HWORD VIP INTENB [%08x]:%04x ",addr,data);
        tVIPREG.INTENB = data;
        break;
    case 0x0005F804:    //INTCLR
        //~ dtprintf(4,ferr,"\nWrite  HWORD VIP INTCLR [%08x]:%04x ",addr,data);
        tVIPREG.INTPND &= ~data; //Clear the Bits
        break;
    case 0x0005F820:    //DPSTTS
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP DPSTTS [%08x]:%04x ",addr,data);
        //****            tVIPREG.DPSTTS = data;
        break;
    case 0x0005F822:    //DPCTRL
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP DPCTRL [%08x]:%04x ",addr,data);
        tVIPREG.DPCTRL = data & 0x0703;
        tVIPREG.DPSTTS = (data & 0x0702) | (tVIPREG.DPSTTS & 0x00FC);
        if(data & 0x0001) {
            tVIPREG.INTPND &= 0x6000;
            tVIPREG.INTENB &= 0x6000;
        }
        break;
    case 0x0005F824:    //BRTA
        //if (debuglog) dbg_addtrc("\nWrite  HWORD VIP BRTA [%08x]:%04x ",addr,data);
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BRTA [%08x]:%04x ",addr,data);
        tDSPCACHE.BrtPALMod = 1;  //Invalidate Brigtness Pallet Cache
        tVIPREG.BRTA = ((data & 0xFF) | 0x0100);
        break;
    case 0x0005F826:    //BRTB
        //if (debuglog) dbg_addtrc("\nWrite  HWORD VIP BRTB [%08x]:%04x ",addr,data);
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BRTB [%08x]:%04x ",addr,data);
        tDSPCACHE.BrtPALMod = 1;  //Invalidate Brigtness Pallet Cache
        tVIPREG.BRTB = ((data & 0xFF) | 0x0100);
        break;
    case 0x0005F828:    //BRTC
        //if (debuglog) dbg_addtrc("\nWrite  HWORD VIP BRTC [%08x]:%04x ",addr,data);
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BRTC [%08x]:%04x ",addr,data);
        tDSPCACHE.BrtPALMod = 1;  //Invalidate Brigtness Pallet Cache
        tVIPREG.BRTC = ((data & 0xFF) | 0x0100);
        break;
    case 0x0005F82A:    //REST
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP REST [%08x]:%04x ",addr,data);
        tVIPREG.REST = data;
        break;
    case 0x0005F82E:    //FRMCYC
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP FRMCYC [%08x]:%04x ",addr,data);
        tVIPREG.FRMCYC = data;
        break;
    case 0x0005F830:    //CTA
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP CTA [%08x]:%04x ",addr,data);
        tVIPREG.CTA = data;
        break;
    case 0x0005F840:    //XPSTTS
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP XPSTTS [%08x]:%04x ",addr,data);
        break;
    case 0x0005F842:    //XPCTRL
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP XPCTRL [%08x]:%04x ",addr,data);
        tVIPREG.XPCTRL = data & 0x1F03;
        tVIPREG.XPSTTS  = (tVIPREG.XPSTTS & 0x9F1C) | (data & 0x0002);
        if(data & 0x0001) {
            tVIPREG.INTPND &= 0x001F;
            tVIPREG.INTENB &= 0x001F;
        }
        break;
    case 0x0005F844:    //VER
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP VER [%08x]:%04x ",addr,data);
        tVIPREG.VER = data;
        break;
    case 0x0005F848:    //SPT0   // Pointers to the 4 OBJ groupes in OBJ Mem
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT0 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        tVIPREG.SPT[0] = data;
        break;
    case 0x0005F84A:    //SPT1
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT1 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        tVIPREG.SPT[1] = data;
        break;
    case 0x0005F84C:    //SPT2
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT2 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        tVIPREG.SPT[2] = data;
        break;
    case 0x0005F84E:    //SPT3
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT3 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        tVIPREG.SPT[3] = data;
        break;
    case 0x0005F860:    //GPLT0  //Set the current color palet for the BGMap's
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT0 [%08x]:%04x ",addr,data);
        tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        tVIPREG.GPLT[0] = data;
        break;
    case 0x0005F862:    //GPLT1
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT1 [%08x]:%04x ",addr,data);
        tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        tVIPREG.GPLT[1] = data;
        break;
    case 0x0005F864:    //GPLT2
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT2 [%08x]:%04x ",addr,data);
        tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        tVIPREG.GPLT[2] = data;
        break;
    case 0x0005F866:    //GPLT3
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT3 [%08x]:%04x ",addr,data);
        tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        tVIPREG.GPLT[3] = data;
        break;
    case 0x0005F868:    //JPLT0  //Set the current color palet for the OBJ's
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT0 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
        tDSPCACHE.ObjDataCacheInvalid=1;
        tVIPREG.JPLT[0] = data;
        break;
    case 0x0005F86A:    //JPLT1
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT1 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
        tDSPCACHE.ObjDataCacheInvalid=1;
        tVIPREG.JPLT[1] = data;
        break;
    case 0x0005F86C:    //JPLT2
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT2 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
        tDSPCACHE.ObjDataCacheInvalid=1;
        tVIPREG.JPLT[2] = data;
        break;
    case 0x0005F86E:    //JPLT3
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT3 [%08x]:%04x ",addr,data);
        tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
        tDSPCACHE.ObjDataCacheInvalid=1;
        tVIPREG.JPLT[3] = data;
        break;
    case 0x0005F870:    //BKCOL
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BKCOL [%08x]:%04x ",addr,data);
        tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
        tDSPCACHE.ObjPALMod = 1;
        tVIPREG.BKCOL = data;
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite  HWORD VIP error [%08x]:%04x ",addr,data);
        break;
    }
}

WORD vipcreg_rword(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  WORD VIP [%08x]:%08x **************",addr,0);
    return vipcreg_rhword(addr); //More???
}

void vipcreg_wword(WORD addr, WORD data) {
    //~ dtprintf(0,ferr,"\nWrite  WORD VIP [%08x]:%08x **************",addr,data);
    vipcreg_whword(addr,(HWORD)data);
    vipcreg_whword(addr+2,(HWORD)(data>>16));
}
