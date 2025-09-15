#include "v810_cpu.h"
#include "vb_types.h"
#include "vb_dsp.h"
#include "vb_sound.h"
#include "v810_mem.h"

int is_sram = 0;

// Memory read functions
uint64_t mem_rbyte(WORD addr) {
    uint64_t wait;

    switch((addr&0x7000000)) {// switch on address
    case 0x7000000:
        wait = (uint64_t)(2 - (vb_state->tHReg.WCR & 1)) << 32;
        return (WORD)((SBYTE *)(V810_ROM1.off + (addr & V810_ROM1.highaddr)))[0] | wait;
        break;
    case 0:
        addr &= 0x7ffff;
        if(!(addr & 0x40000)) {
            wait = 4LL << 32;
            return (WORD)((SBYTE *)(vb_state->V810_DISPLAY_RAM.off + addr))[0] | wait;
        } else if((addr & 0x7e000) == 0x5e000) {
            wait = 1LL << 32;
            return (WORD)vipcreg_rbyte(addr) | wait;
            // Mirror the Chr ram table to 078000-07FFFF
        } else if(addr >= 0x00078000) {
            wait = 4LL << 32;
            if(addr < 0x0007A000) //CHR 0-511
                return (WORD)((SBYTE *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x00078000 + 0x00006000)))[0] | wait;
            else if(addr < 0x0007C000) //CHR 512-1023
                return (WORD)((SBYTE *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007A000 + 0x0000E000)))[0] | wait;
            else if(addr < 0x0007E000) //CHR 1024-1535
                return (WORD)((SBYTE *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007C000 + 0x00016000)))[0] | wait;
            else //CHR 1536-2047
                return (WORD)((SBYTE *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007E000 + 0x0001E000)))[0] | wait;
        }
        break;
    case 0x1000000:
        wait = 0LL << 32;
        return 0 | wait;
        break;
    case 0x5000000:
        wait = 0LL << 32;
        //~ dtprintf(0,ferr,"\nRead  BYTE  [%08x]:%02x  //VBRam",addr,((BYTE *)(vb_state->V810_VB_RAM.off + (addr & vb_state->V810_VB_RAM.highaddr)))[0]);
        return (WORD)((SBYTE *)(vb_state->V810_VB_RAM.off + (addr & 0x0500ffff)))[0] | wait;
        break;
    case 0x6000000:
        is_sram = 1;
        wait = 0LL << 32;
        //~ dtprintf(0,ferr,"\nRead  BYTE  PC:%08x [%08x]:%02x  //GameRam",PC,addr,((BYTE *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr)))[0]);
        return (WORD)((SBYTE *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr)))[0] | wait;
        break;
    case 0x2000000:
        wait = 0LL << 32;
        return (WORD)hcreg_rbyte(addr & 0x3c) | wait;
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  BYTE  [%08x]:%02x",addr,0);
        return(0);
        break;
    }// End switch on address
    return(0); //Stops a silly compiler error
}

uint64_t mem_rhword(WORD addr) {
    uint64_t wait;

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 16, 0, 0);

    switch((addr&0x7000000)) {
    case 0x7000000:
        wait = (uint64_t)(2 - (vb_state->tHReg.WCR & 1)) << 32;
        return (WORD)((SHWORD *)(V810_ROM1.off + (addr & V810_ROM1.highaddr & ~1)))[0] | wait;
        break;
    case 0:
        addr &= 0x7fffe;
        if(!(addr & 0x40000)) {
            wait = 4LL << 32;
            return (WORD)((SHWORD *)(vb_state->V810_DISPLAY_RAM.off + addr))[0] | wait;
        } else if((addr & 0x7e000) == 0x5e000) {
            wait = 1LL << 32;
            return (WORD)vipcreg_rhword(addr) | wait;
            // Mirror the Chr ram table to 078000-07FFFF
        } else if(addr >= 0x00078000) {
            wait = 4LL << 32;
            if(addr < 0x0007A000) //CHR 0-511
                return (WORD)((SHWORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x00078000 + 0x00006000)))[0] | wait;
            else if(addr < 0x0007C000) //CHR 512-1023
                return (WORD)((SHWORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007A000 + 0x0000E000)))[0] | wait;
            else if(addr < 0x0007E000) //CHR 1024-1535
                return (WORD)((SHWORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007C000 + 0x00016000)))[0] | wait;
            else //CHR 1536-2047
                return (WORD)((SHWORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007E000 + 0x0001E000)))[0] | wait;
        }
        break;
    case 0x1000000:
        wait = 0LL << 32;
        return 0 | wait;
        break;
    case 0x5000000:
        wait = 0LL << 32;
        //~ dtprintf(0,ferr,"\nRead  HWORD [%08x]:%04x  //VBRam",addr,((HWORD *)(vb_state->V810_VB_RAM.off + (addr & vb_state->V810_VB_RAM.highaddr)))[0]);
        return (WORD)((SHWORD *)(vb_state->V810_VB_RAM.off + (addr & 0x0500fffe)))[0] | wait;
        break;
    case 0x6000000:
        is_sram = 1;
        wait = 0LL << 32;
        //~ dtprintf(0,ferr,"\nRead  HWORD PC:%08x [%08x]:%04x  //GameRam",PC,addr,((HWORD *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr)))[0]);
        return (WORD)((SHWORD *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr & ~1)))[0] | wait;
        break;
    case 0x2000000:
        wait = 0LL << 32;
        return (WORD)hcreg_rhword(addr & 0x3c) | wait;
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  HWORD [%08x]:%04x",addr,0);
        return(0);
        break;
    }
    return(0); //Stops a silly compiler error
}

uint64_t mem_rword(WORD addr) {
    uint64_t wait;

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 32, 0, 0);

    switch((addr&0x7000000)) {
    case 0x7000000:
        wait = (uint64_t)(2LL - (vb_state->tHReg.WCR & 1)) << 33;
        return ((WORD *)(V810_ROM1.off + (addr & V810_ROM1.highaddr & ~3)))[0] | wait;
        break;
    case 0:
        addr &= 0x7fffc;
        if(!(addr & 0x40000)) {
            wait = 4LL << 33;
            return ((WORD *)(vb_state->V810_DISPLAY_RAM.off + addr))[0] | wait;
        } else if((addr & 0x7e000) == 0x5e000) {
            wait = 1LL << 33;
            return vipcreg_rword(addr) | wait;
            // Mirror the Chr ram table to 078000-07FFFF
        } else  if(addr >= 0x00078000) {
            wait = 4LL << 33;
            if(addr < 0x0007A000) //CHR 0-511
                return ((WORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x00078000 + 0x00006000)))[0] | wait;
            else if(addr < 0x0007C000) //CHR 512-1023
                return ((WORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007A000 + 0x0000E000)))[0] | wait;
            else if(addr < 0x0007E000) //CHR 1024-1535
                return ((WORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007C000 + 0x00016000)))[0] | wait;
            else //CHR 1536-2047
                return ((WORD *)(vb_state->V810_DISPLAY_RAM.off + (addr-0x0007E000 + 0x0001E000)))[0] | wait;
        }
        break;
    case 0x1000000:
        wait = 0LL << 33;
        return 0 | wait;
        break;
    case 0x5000000:
        wait = 0LL << 33;
        //~ dtprintf(0,ferr,"\nRead  WORD  [%08x]:%08x  //VBRam",addr,((WORD *)(vb_state->V810_VB_RAM.off + (addr & vb_state->V810_VB_RAM.highaddr)))[0]);
        return ((WORD *)(vb_state->V810_VB_RAM.off + (addr & 0x0500fffc)))[0] | wait;
        break;
    case 0x6000000:
        is_sram = 1;
        wait = 0LL << 33;
        //~ dtprintf(0,ferr,"\nRead  WORD  PC:%08x [%08x]:%08x  //GameRam",PC,addr,((WORD *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr)))[0]);
        return ((WORD *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr & ~3)))[0] | wait;
        break;
    case 0x2000000:
        wait = 0LL << 33;
        return hcreg_rword(addr & 0x3c) | wait;
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
WORD mem_wbyte(WORD addr, WORD data) {
    int i =0;

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 8, 1, data);

    switch((addr&0x7000000)) {
    case 0:
        addr &= 0x7ffff;
        if(!(addr & 0x40000)) {
            ((BYTE *)(vb_state->V810_DISPLAY_RAM.off + addr))[0] = data;

            if (emulating_self) {
                if(addr < BGMAP_OFFSET) {
                    //Kill it if writes to Char Table
                    if((addr & 0x6000) == 0x6000) {
                        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                        tDSPCACHE.ObjDataCacheInvalid=1;
                        tDSPCACHE.CharCacheInvalid=1;
                        tDSPCACHE.CharacterCache[((addr & 0x1fff) | ((addr & 0x18000) >> 2)) >> 4] = true;
                    } else { //Direct Mem Writes, darn thoes fragmented memorys!!!
                        tDSPCACHE.DDSPDataState[(addr>>15)&1] = CPU_WROTE;
                        SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[(addr>>15)&1][(addr>>9)&63];
                        int y = (addr>>1) & 31;
                        if (y < column->min) column->min = y;
                        if (y > column->max) column->max = y;
                    }
                } else if (addr >= COLTABLE_OFFSET && addr < OBJ_OFFSET) {
                    tDSPCACHE.ColumnTableInvalid=1;
                }else if((addr >=OBJ_OFFSET)&&(addr < (OBJ_OFFSET+(OBJ_SIZE*1024)))) { //Writes to Obj Table
                    tDSPCACHE.ObjDataCacheInvalid=1;
                } else if((addr >=BGMAP_OFFSET)&&(addr < (BGMAP_OFFSET+(14*BGMAP_SIZE)))) { //Writes to BGMap Table
                    tDSPCACHE.BGCacheInvalid[((addr-BGMAP_OFFSET)/BGMAP_SIZE)]=1;
                }
            }
        } else if((addr & 0x7e000) == 0x5e000) {
            vipcreg_wbyte(addr, data);
            // Mirror the Chr ram table to 078000-07FFFF
        } else if(addr >= 0x00078000) {
            if(addr < 0x0007A000) //CHR 0-511
                ((BYTE *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x00078000) + 0x00006000)))[0] = data;
            else if(addr < 0x0007C000) //CHR 512-1023
                ((BYTE *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007A000) + 0x0000E000)))[0] = data;
            else if(addr < 0x0007E000) //CHR 1024-1535
                ((BYTE *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007C000) + 0x00016000)))[0] = data;
            else //CHR 1536-2047
                ((BYTE *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007E000) + 0x0001E000)))[0] = data;
            if (emulating_self) {
                //Invalidate, writes to Char table
                for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                tDSPCACHE.ObjDataCacheInvalid=1;
                tDSPCACHE.CharCacheInvalid=1;
                tDSPCACHE.CharacterCache[(addr - 0x78000) >> 4] = true;
            }
        }
        return 1;
        break;
    case 0x1000000:
        sound_write(addr & 0x010007ff, data & 0xff);
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nWrite BYTE  [%08x]:%02x  //VBRam",addr,data);
        ((BYTE *)(vb_state->V810_VB_RAM.off + (addr & 0x0500ffff)))[0] = data;
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nWrite BYTE  PC:%08x [%08x]:%02x  //GameRam",PC,addr,data);
        ((BYTE *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr)))[0] = data;
        break;
    case 0x2000000:
        return hcreg_wbyte(addr & 0x3c, data);
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite BYTE  [%08x]:%02x",addr,data);
        break;
    }
    return 0;
}

WORD mem_whword(WORD addr, WORD data) {
    int i = 0;
    addr = addr & 0x07FFFFFE; // map to 24 bit address, mask first bit

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 16, 1, data);

    switch((addr&0x7000000)) {
    case 0:
        addr &= 0x7fffe;
        if(!(addr & 0x40000)) {
            ((HWORD *)(vb_state->V810_DISPLAY_RAM.off + addr))[0] = data;
            if (emulating_self) {
                if(addr < BGMAP_OFFSET) { //Kill it if writes to Char Table
                    //Kill it if writes to Char Table
                    if((addr & 0x6000) == 0x6000) {
                        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                        tDSPCACHE.ObjDataCacheInvalid=1;
                        tDSPCACHE.CharCacheInvalid=1;
                        tDSPCACHE.CharacterCache[((addr & 0x1fff) | ((addr & 0x18000) >> 2)) >> 4] = true;
                    } else { //Direct Mem Writes, darn thoes fragmented memorys!!!
                        tDSPCACHE.DDSPDataState[(addr>>15)&1] = CPU_WROTE;
                        SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[(addr>>15)&1][(addr>>9)&63];
                        int y = (addr>>1) & 31;
                        if (y < column->min) column->min = y;
                        if (y > column->max) column->max = y;
                    }
                } else if (addr >= COLTABLE_OFFSET && addr < OBJ_OFFSET) {
                    tDSPCACHE.ColumnTableInvalid=1;
                }else if((addr >=OBJ_OFFSET)&&(addr < (OBJ_OFFSET+(OBJ_SIZE*1024)))) { //Writes to Obj Table
                    tDSPCACHE.ObjDataCacheInvalid=1;
                } else if((addr >=BGMAP_OFFSET)&&(addr < (BGMAP_OFFSET+(14*BGMAP_SIZE)))) { //Writes to BGMap Table
                    tDSPCACHE.BGCacheInvalid[((addr-BGMAP_OFFSET)/BGMAP_SIZE)]=1;
                }
            }
        } else if((addr & 0x7e000) == 0x5e000) {
            vipcreg_whword(addr, data);
            // Mirror the Chr ram table to 078000-07FFFF
        } else if(addr >= 0x00078000) {
            if(addr < 0x0007A000) //CHR 0-511
                ((HWORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x00078000) + 0x00006000)))[0] = data;
            else if(addr < 0x0007C000) //CHR 512-1023
                ((HWORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007A000) + 0x0000E000)))[0] = data;
            else if(addr < 0x0007E000) //CHR 1024-1535
                ((HWORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007C000) + 0x00016000)))[0] = data;
            else //CHR 1536-2047
                ((HWORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007E000) + 0x0001E000)))[0] = data;
            if (emulating_self) {
                //Invalidate, writes to Char table
                for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                tDSPCACHE.ObjDataCacheInvalid=1;
                tDSPCACHE.CharCacheInvalid=1;
                tDSPCACHE.CharacterCache[(addr - 0x78000) >> 4] = true;
            }
        }
        return 1;
        break;
    case 0x1000000:
        sound_write(addr & 0x010007fe, data & 0xff);
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nWrite HWORD [%08x]:%04x  //VBRam",addr,data);
        ((HWORD *)(vb_state->V810_VB_RAM.off + (addr & 0x0500fffe)))[0] = data;
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nWrite HWORD PC:%08x [%08x]:%04x  //GameRam",PC,addr,data);
        ((HWORD *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr & ~1)))[0] = data;
        break;
    case 0x2000000:
        return hcreg_whword(addr & 0x3c, data);
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite HWORD [%08x]:%04x",addr,data);
        break;
    }
    return 0;
}

WORD mem_wword(WORD addr, WORD data) {
    int i = 0;
    uint64_t wait;

    //~ if(dbg_watchpt_en)
    //~ dbg_watchpt(addr, 32, 1, data);

    switch((addr&0x7000000)) {
    case 0:
        addr &= 0x7fffc;
        if(!(addr & 0x40000)) {
            ((WORD *)(vb_state->V810_DISPLAY_RAM.off + addr))[0] = data;
            if (emulating_self) {
                if(addr < BGMAP_OFFSET) { //Kill it if writes to Char Table
                    //Kill it if writes to Char Table
                    if((addr & 0x6000) == 0x6000) {
                        for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                        tDSPCACHE.ObjDataCacheInvalid=1;
                        tDSPCACHE.CharCacheInvalid=1;
                        tDSPCACHE.CharacterCache[((addr & 0x1fff) | ((addr & 0x18000) >> 2)) >> 4] = true;
                    } else { //Direct Mem Writes, darn thoes fragmented memorys!!!
                        tDSPCACHE.DDSPDataState[(addr>>15)&1] = CPU_WROTE;
                        SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[(addr>>15)&1][(addr>>9)&63];
                        int y = (addr>>1) & 31;
                        if (y < column->min) column->min = y;
                        y++; // 32-bit write covers two tiles, so add 1 for the max calc
                        if (y > column->max) column->max = y;
                    }
                } else if (addr >= COLTABLE_OFFSET && addr < OBJ_OFFSET) {
                    tDSPCACHE.ColumnTableInvalid=1;
                }else if((addr >=OBJ_OFFSET)&&(addr < (OBJ_OFFSET+(OBJ_SIZE*1024)))) { //Writes to Obj Table
                    tDSPCACHE.ObjDataCacheInvalid=1;
                } else if((addr >=BGMAP_OFFSET)&&(addr < (BGMAP_OFFSET+(14*BGMAP_SIZE)))) { //Writes to BGMap Table
                    tDSPCACHE.BGCacheInvalid[((addr-BGMAP_OFFSET)/BGMAP_SIZE)]=1;
                }
            }
        } else if((addr & 0x7e000) == 0x5e000) {
            vipcreg_wword(addr, data);
            // Mirror the Chr ram table to 078000-07FFFF
        } else if(addr >= 0x00078000) {
            if(addr < 0x0007A000)  //CHR 0-511
                ((WORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x00078000) + 0x00006000)))[0] = data;
            else if(addr < 0x0007C000) //CHR 512-1023
                ((WORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007A000) + 0x0000E000)))[0] = data;
            else if(addr < 0x0007E000) //CHR 1024-1535
                ((WORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007C000) + 0x00016000)))[0] = data;
            else      //CHR 1536-2047
                ((WORD *)(vb_state->V810_DISPLAY_RAM.off + ((addr-0x0007E000) + 0x0001E000)))[0] = data;
            if (emulating_self) {
                //Invalidate, writes to Char table
                for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
                tDSPCACHE.ObjDataCacheInvalid=1;
                tDSPCACHE.CharCacheInvalid=1;
                tDSPCACHE.CharacterCache[(addr - 0x78000) >> 4] = true;
            }
        }
        return 2;
        break;
    case 0x1000000:
        sound_write(addr & 0x010007fc, data & 0xff);
        break;
    case 0x5000000:
        //~ dtprintf(0,ferr,"\nWrite WORD  [%08x]:%08x  //VBRam",addr,data);
        ((WORD *)(vb_state->V810_VB_RAM.off + (addr & 0x0500fffc)))[0] = data;
        break;
    case 0x6000000:
        is_sram = 1;
        //~ dtprintf(0,ferr,"\nWrite WORD  PC:%08x [%08x]:%08x  //GameRam",PC,addr,data);
        ((WORD *)(vb_state->V810_GAME_RAM.off + (addr & vb_state->V810_GAME_RAM.highaddr & ~3)))[0] = data;
        break;
    case 0x2000000:
        return hcreg_wword(addr & 0x3c, data) * 2;
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite WORD  [%08x]:%08x",addr,data);
        break;
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Hardware Controll Reg....
SBYTE hcreg_rbyte(WORD addr) {
    addr = addr | 0x02000000;
    switch(addr) {
    case 0x02000000:    //CCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CCR [%08x]",addr);
        return (vb_state->tHReg.CCR|0x04|((vb_state->tHReg.CCR&0x04)>>1)); //Mask the Write Only Bit...
        break;
    case 0x02000004:    //CCSR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CCSR [%08x]",addr);
        return vb_state->tHReg.CCSR | (!is_multiplayer || ((vb_players[0].tHReg.CCSR & 2) && (vb_players[1].tHReg.CCSR & 2)));
        break;
    case 0x02000008:    //CDTR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CDTR [%08x]",addr);
        return vb_state->tHReg.CDTR;
        break;
    case 0x0200000C:    //CDRR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG CDRR [%08x]",addr);
        return vb_state->tHReg.CDRR;
        break;
    case 0x02000010:    //SLB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG SLB [%08x]",addr);
        return vb_state->tHReg.SLB;
        break;
    case 0x02000014:    //SHB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG SHB [%08x]",addr);
        return vb_state->tHReg.SHB;
        break;
    case 0x02000018:    //TLB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG TLB [%08x]",addr);
        return vb_state->tHReg.TLB;//Should return the current counter???
        break;
    case 0x0200001C:    //THB
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG THB [%08x]",addr);
        return vb_state->tHReg.THB;
        break;
    case 0x02000020:    //TCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG TCR [%08x]",addr);
        return (vb_state->tHReg.TCR|0x04);
        break;
    case 0x02000024:    //WCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG WCR [%08x]",addr);
        return vb_state->tHReg.WCR;
        break;
    case 0x02000028:    //SCR
        //~ dtprintf(3,ferr,"\nRead  BYTE HREG SCR [%08x]",addr);
        //vb_state->tHReg.SCR=(vb_state->tHReg.SCR^0x02); //Remove Me!!
        return (vb_state->tHReg.SCR|0x4C);
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  BYTE HREG error [%08x]",addr);
        return 0;
        break;
    }
}

WORD hcreg_wbyte(WORD addr, BYTE data) {
    addr = addr | 0x02000000;
    switch(addr) {
    case 0x02000000:    //CCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG CCR [%08x]:%02x ",addr,data);
        data |= vb_state->tHReg.CCR & 0x04;
        if (data & 0x04) {
            if (
                // begin communication
                !(vb_state->tHReg.CCR & 0x04) ||
                // remote -> host
                ((vb_state->tHReg.CCR & 0x10) && !(data & 0x10))
            ) {
                vb_state->tHReg.nextcomm = vb_state->v810_state.cycles + vb_state->v810_state.cycles_until_event_full - vb_state->v810_state.cycles_until_event_partial + 3200;
            }
        }
        vb_state->tHReg.CCR = ((data|0x69)&0xFD);
        if (data & 0x80) {
            // acknowledge interrupt
            vb_state->tHReg.cInt = false;
        }
        break;
    case 0x02000004:    //CCSR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG CCSR [%08x]:%02x ",addr,data);
        vb_state->tHReg.CCSR = ((data|0x60)&0xFA) | (vb_state->tHReg.CCSR & 0x04);
        if (data & 0x80) {
            // acknowledge interrupt
            vb_state->tHReg.ccInt = false;
        }
        break;
    case 0x02000008:    //CDTR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG CDTR [%08x]:%02x ",addr,data);
        vb_state->tHReg.CDTR = data;
        break;
        //case 0x0200000C:    //CDRR  // Read Only!
        //    vb_state->tHReg.CDRR = data;
        //    break;
        //case 0x02000010:    //SLB
        //    vb_state->tHReg.SLB = data;
        //    break;
        //case 0x02000014:    //SHB
        //    vb_state->tHReg.SHB = data;
        //    break;
    case 0x02000018:    //TLB
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG TLB [%08x]:%02x ",addr,data);
        vb_state->tHReg.TLB = data;
        vb_state->tHReg.tTHW = (vb_state->tHReg.TLB | (vb_state->tHReg.tTHW & 0xFF00)); //Reset internal count
        vb_state->tHReg.tCount = vb_state->tHReg.tTHW;
        if (vb_state->tHReg.TCR & 0x01) predictEvent(true);
        //vb_state->tHReg.tTHW = (vb_state->tHReg.TLB | (vb_state->tHReg.THB << 8)); //Reset internal count
        break;
    case 0x0200001C:    //THB
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG THB [%08x]:%02x ",addr,data);
        vb_state->tHReg.THB = data;
        vb_state->tHReg.tTHW = ((vb_state->tHReg.THB << 8) | (vb_state->tHReg.tTHW & 0xFF)); //Reset internal count
        vb_state->tHReg.tCount = vb_state->tHReg.tTHW;
        if (vb_state->tHReg.TCR & 0x01) predictEvent(true);
        //vb_state->tHReg.tTHW = (vb_state->tHReg.TLB | (vb_state->tHReg.THB << 8)); //Reset internal count
        break;
    case 0x02000020: {  //TCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG TCR [%08x]:%02x ",addr,data);
        BYTE zstat = vb_state->tHReg.TCR & 0x02;
        if ((data & 0x04) // Z-Stat-Clr
            && (!(vb_state->tHReg.TCR & 1) || // if timer is disabled, we can always clear
                ((data & 1) // if timer is enabled, we can't be disabling it
                && vb_state->tHReg.tCount != 0)) // if timer is enabled, timer must be nonzero
        ) zstat = 0; // Clear the ZStat Flag...
        if (!zstat || !(data & 0x08)) vb_state->tHReg.tInt = false;

        int old_res = vb_state->tHReg.TCR & 0x10;

        vb_state->tHReg.TCR = (((data|0xE4)&0xFD)|zstat);

        if (!old_res && (data & 0x10)) {
            // When switching from 100us to 20us mode in any 20us tick other than the one
            // where the 100us timer ticks, the timer is immediately decremented.
            if (vb_state->tHReg.ticks != 0) {
                vb_state->tHReg.tCount--;
                vb_state->tHReg.TLB = (vb_state->tHReg.tCount&0xFF);
                vb_state->tHReg.THB = ((vb_state->tHReg.tCount>>8)&0xFF);
                if (vb_state->tHReg.tCount == 0) {
                    vb_state->tHReg.TCR |= 0x02;
                    if ((data & 0x09) == 0x09) vb_state->tHReg.tInt = true;
                } else if (vb_state->tHReg.tCount < 0) {
                    vb_state->tHReg.tCount += vb_state->tHReg.tTHW + 1; // reset counter
                }
            }
        }

        predictEvent(true);

        break;
    }
    case 0x02000024:    //WCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG WCR [%08x]:%02x ",addr,data);
        vb_state->tHReg.WCR = (data|0xFC); // Mask
        break;
    case 0x02000028:    //SCR
        //~ dtprintf(3,ferr,"\nWrite  BYTE HCREG SCR [%08x]:%02x ",addr,data);
        vb_state->tHReg.SCR = ((data|0x48)&0xFD);//0x48
        if(data & 0x80) { //Int Clear (What)
            //Clear the Pupy, but why the RETI will clear it for us....
        }
        if(data & 0x04) { //Hardware Read
            if(!(data&0x01)) { //Hardware Read Disabled
                vb_state->tHReg.SCR |= 2;
                vb_state->tHReg.hwRead = 10240;
                vb_state->tHReg.lastinput = vb_state->v810_state.cycles + vb_state->v810_state.cycles_until_event_full - vb_state->v810_state.cycles_until_event_partial;
                predictEvent(true);
            }
        } else if(data & 0x20) { //Software Read, same for now....
        }
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite  BYTE HREG error [%08x]:%04x ",addr,data);
        break;
    }
    return 0;
}

HWORD hcreg_rhword(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  HWORD HReg [%08x]:%04x !!!!!!",addr,(HWORD)hcreg_rbyte(addr));
    return (HWORD)hcreg_rbyte(addr);
}
WORD hcreg_whword(WORD addr, HWORD data) {
    //~ dtprintf(0,ferr,"\nWrite  HWORD HReg [%08x]:%04x !!!!!!",addr,data);
    return hcreg_wbyte(addr,(BYTE)data); //All read/write is byte...
}
WORD hcreg_rword(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  WORD HReg [%08x]:%08x !!!!!!",addr,(WORD)hcreg_rbyte(addr));
    return (WORD)hcreg_rbyte(addr);
}
WORD hcreg_wword(WORD addr, WORD data) {
    //~ dtprintf(0,ferr,"\nWrite  WORD HReg [%08x]:%08x !!!!!!",addr,data);
    return hcreg_wbyte(addr,(BYTE)data); //All read/write is byte...
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

SBYTE vipcreg_rbyte(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  BYTE VIP [%08x]:%02x **************",addr,0);
    return 0xFF;
}

WORD vipcreg_wbyte(WORD addr, BYTE data) {
    //~ dtprintf(0,ferr,"\nWrite  BYTE VIP [%08x]:%02x **************",addr,data);
    return 0;
}

HWORD vipcreg_rhword(WORD addr) {
    addr=(addr&0x0005007E); //Bring it into line
    addr=(addr|0x0005F800); //make sure all the right bits are on
    switch(addr) {
    case 0x0005F800:    //INTPND
        //~ dtprintf(4,ferr,"\nRead   HWORD VIP INTPND [%08x]:%04x ",addr,vb_state->tVIPREG.INTPND);
        return vb_state->tVIPREG.INTPND;
        break;
    case 0x0005F802:    //INTENB
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP INTENB [%08x]:%04x ",addr,vb_state->tVIPREG.INTENB);
        return vb_state->tVIPREG.INTENB;
        break;
    case 0x0005F804:    //INTCLR
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP INTCLR [%08x]:%04x ",addr,vb_state->tVIPREG.INTCLR);
        return vb_state->tVIPREG.INTCLR;
        break;
    case 0x0005F820:    //DPSTTS
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP DPSTTS [%08x]:%04x ",addr,vb_state->tVIPREG.DPSTTS);
        //Handled in vb_dsp.c, not!  what is the right time?
        //if(vb_state->tVIPREG.DPSTTS == 0x0040) vb_state->tVIPREG.DPSTTS = 0x003C;
        //else vb_state->tVIPREG.DPSTTS = 0x0040;

        return vb_state->tVIPREG.DPSTTS;
        break;
    case 0x0005F822:    //DPCTRL
        //~ dtprintf(4,ferr,"\nRead  HWORD VIP DPCTRL [%08x]:%04x ",addr,vb_state->tVIPREG.DPCTRL);
        return vb_state->tVIPREG.DPCTRL;
        break;
    case 0x0005F824:    //BRTA
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BRTA [%08x]:%04x ",addr,vb_state->tVIPREG.BRTA);
        return vb_state->tVIPREG.BRTA;
        break;
    case 0x0005F826:    //BRTB
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BRTB [%08x]:%04x ",addr,vb_state->tVIPREG.BRTB);
        return vb_state->tVIPREG.BRTB;
        break;
    case 0x0005F828:    //BRTC
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BRTC [%08x]:%04x ",addr,vb_state->tVIPREG.BRTC);
        return vb_state->tVIPREG.BRTC;
        break;
    case 0x0005F82A:    //REST
        //~ dtprintf(7,ferr,"\nRead  HWORD VIP REST [%08x]:%04x ",addr,vb_state->tVIPREG.REST);
        return vb_state->tVIPREG.REST;
        break;
    case 0x0005F82E:    //FRMCYC
        //~ dtprintf(7,ferr,"\nRead  HWORD VIP FRMCYC [%08x]:%04x ",addr,vb_state->tVIPREG.FRMCYC);
        return vb_state->tVIPREG.FRMCYC;
        break;
    case 0x0005F830:    //CTA, ColumTable Address
        //~ dtprintf(8,ferr,"\nRead  HWORD VIP CTA [%08x]:%04x ",addr,vb_state->tVIPREG.CTA);
        return 0xffff;
        break;
    case 0x0005F840:    //XPSTTS
        //~ dtprintf(3,ferr,"\nRead  HWORD VIP XPSTTS [%08x]:%04x ",addr,vb_state->tVIPREG.XPSTTS);
        //vb_state->tVIPREG.XPSTTS=(vb_state->tVIPREG.XPSTTS^0x0C);
        return vb_state->tVIPREG.XPSTTS;
        break;
    case 0x0005F842:    //XPCTRL
        //~ dtprintf(3,ferr,"\nRead  HWORD VIP XPCTRL [%08x]:%04x ",addr,vb_state->tVIPREG.XPCTRL);
        //vb_state->tVIPREG.XPSTTS = 0xfff0;
        return vb_state->tVIPREG.XPCTRL;
        break;
    case 0x0005F844:    //VER
        //~ dtprintf(8,ferr,"\nRead  HWORD VIP VER [%08x]:%04x ",addr,vb_state->tVIPREG.VER);
        return 2;
        break;
    case 0x0005F848:    //SPT0
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT0 [%08x]:%04x ",addr,vb_state->tVIPREG.SPT[0]);
        return vb_state->tVIPREG.SPT[0];
        break;
    case 0x0005F84A:    //SPT1
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT1 [%08x]:%04x ",addr,vb_state->tVIPREG.SPT[1]);
        return vb_state->tVIPREG.SPT[1];
        break;
    case 0x0005F84C:    //SPT2
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT2 [%08x]:%04x ",addr,vb_state->tVIPREG.SPT[2]);
        return vb_state->tVIPREG.SPT[2];
        break;
    case 0x0005F84E:    //SPT3
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP SPT3 [%08x]:%04x ",addr,vb_state->tVIPREG.SPT[3]);
        return vb_state->tVIPREG.SPT[3];
        break;
    case 0x0005F860:    //GPLT0
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT0 [%08x]:%04x ",addr,vb_state->tVIPREG.GPLT[0]);
        return vb_state->tVIPREG.GPLT[0];
        break;
    case 0x0005F862:    //GPLT1
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT1 [%08x]:%04x ",addr,vb_state->tVIPREG.GPLT[1]);
        return vb_state->tVIPREG.GPLT[1];
        break;
    case 0x0005F864:    //GPLT2
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT2 [%08x]:%04x ",addr,vb_state->tVIPREG.GPLT[2]);
        return vb_state->tVIPREG.GPLT[2];
        break;
    case 0x0005F866:    //GPLT3
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP GPLT3 [%08x]:%04x ",addr,vb_state->tVIPREG.GPLT[3]);
        return vb_state->tVIPREG.GPLT[3];
        break;
    case 0x0005F868:    //JPLT0
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT0 [%08x]:%04x ",addr,vb_state->tVIPREG.JPLT[0]);
        return vb_state->tVIPREG.JPLT[0];
        break;
    case 0x0005F86A:    //JPLT1
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT1 [%08x]:%04x ",addr,vb_state->tVIPREG.JPLT[1]);
        return vb_state->tVIPREG.JPLT[1];
        break;
    case 0x0005F86C:    //JPLT2
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT2 [%08x]:%04x ",addr,vb_state->tVIPREG.JPLT[2]);
        return vb_state->tVIPREG.JPLT[2];
        break;
    case 0x0005F86E:    //JPLT3
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP JPLT3 [%08x]:%04x ",addr,vb_state->tVIPREG.JPLT[3]);
        return vb_state->tVIPREG.JPLT[3];
        break;
    case 0x0005F870:    //BKCOL
        //~ dtprintf(1,ferr,"\nRead  HWORD VIP BKCOL [%08x]:%04x ",addr,vb_state->tVIPREG.BKCOL);
        return vb_state->tVIPREG.BKCOL;
        break;
    default:
        //~ dtprintf(0,ferr,"\nRead  HWORD VIP error [%08x]:%04x ",addr,0xFFFF);
        return 0xFFFF;
        break;
    }
    return 0;
}

WORD vipcreg_whword(WORD addr, HWORD data) {
    int i;
    addr=(addr&0x0005007E); //Bring it into line
    addr=(addr|0x0005F800); //make shure all the right bits are on
    switch(addr) {
    case 0x0005F800:    //INTPND
        //~ dtprintf(4,ferr,"\nWrite  HWORD VIP INTPND [%08x]:%04x ",addr,data);
        //****            vb_state->tVIPREG.INTPND = data;
        break;
    case 0x0005F802:    //INTENB
        //~ dtprintf(4,ferr,"\nWrite  HWORD VIP INTENB [%08x]:%04x ",addr,data);
        vb_state->tVIPREG.INTENB = data;
        break;
    case 0x0005F804:    //INTCLR
        //~ dtprintf(4,ferr,"\nWrite  HWORD VIP INTCLR [%08x]:%04x ",addr,data);
        vb_state->tVIPREG.INTPND &= ~data; //Clear the Bits
        break;
    case 0x0005F820:    //DPSTTS
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP DPSTTS [%08x]:%04x ",addr,data);
        //****            vb_state->tVIPREG.DPSTTS = data;
        break;
    case 0x0005F822:    //DPCTRL
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP DPCTRL [%08x]:%04x ",addr,data);
        vb_state->tVIPREG.DPCTRL = data & 0x0703;
        vb_state->tVIPREG.DPSTTS = (data & 0x0702) | (vb_state->tVIPREG.DPSTTS & 0x00FC);
        if(data & 0x0001) {
            vb_state->tVIPREG.INTPND &= 0x6000;
            vb_state->tVIPREG.INTENB &= 0x6000;
        }
        break;
    case 0x0005F824:    //BRTA
        //if (debuglog) dbg_addtrc("\nWrite  HWORD VIP BRTA [%08x]:%04x ",addr,data);
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BRTA [%08x]:%04x ",addr,data);
        if ((data & 0xFF) != vb_state->tVIPREG.BRTA) {
            if (emulating_self) tDSPCACHE.BrtPALMod = 1;  //Invalidate Brigtness Pallet Cache
            vb_state->tVIPREG.BRTA = data & 0xFF;
        }
        break;
    case 0x0005F826:    //BRTB
        //if (debuglog) dbg_addtrc("\nWrite  HWORD VIP BRTB [%08x]:%04x ",addr,data);
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BRTB [%08x]:%04x ",addr,data);
        if ((data & 0xFF) != vb_state->tVIPREG.BRTB) {
            if (emulating_self) tDSPCACHE.BrtPALMod = 1;  //Invalidate Brigtness Pallet Cache
            vb_state->tVIPREG.BRTB = data & 0xFF;
        }
        break;
    case 0x0005F828:    //BRTC
        //if (debuglog) dbg_addtrc("\nWrite  HWORD VIP BRTC [%08x]:%04x ",addr,data);
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BRTC [%08x]:%04x ",addr,data);
        if ((data & 0xFF) != vb_state->tVIPREG.BRTC) {
            if (emulating_self) tDSPCACHE.BrtPALMod = 1;  //Invalidate Brigtness Pallet Cache
            vb_state->tVIPREG.BRTC = data & 0xFF;
        }
        break;
    case 0x0005F82A:    //REST
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP REST [%08x]:%04x ",addr,data);
        vb_state->tVIPREG.REST = data;
        break;
    case 0x0005F82E:    //FRMCYC
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP FRMCYC [%08x]:%04x ",addr,data);
        vb_state->tVIPREG.FRMCYC = data & 0xf;
        break;
    case 0x0005F830:    //CTA
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP CTA [%08x]:%04x ",addr,data);
        break;
    case 0x0005F840:    //XPSTTS
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP XPSTTS [%08x]:%04x ",addr,data);
        break;
    case 0x0005F842:    //XPCTRL
        //~ dtprintf(6,ferr,"\nWrite  HWORD VIP XPCTRL [%08x]:%04x ",addr,data);
        if(data & 0x0001) {
            vb_state->tVIPREG.INTPND &= 0x001F;
            vb_state->tVIPREG.INTENB &= 0x001F;
            vb_state->tVIPREG.XPCTRL &= ~0x0002;
            vb_state->tVIPREG.XPSTTS &= ~0x0002;
        }
        vb_state->tVIPREG.XPCTRL = data & 0x1F03;
        vb_state->tVIPREG.XPSTTS = (vb_state->tVIPREG.XPSTTS & 0x9F1C) | (data & 0x0002);
        break;
    case 0x0005F844:    //VER
        //~ dtprintf(8,ferr,"\nWrite  HWORD VIP VER [%08x]:%04x ",addr,data);
        break;
    case 0x0005F848:    //SPT0   // Pointers to the 4 OBJ groupes in OBJ Mem
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT0 [%08x]:%04x ",addr,data);
        if (emulating_self) tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        vb_state->tVIPREG.SPT[0] = data;
        break;
    case 0x0005F84A:    //SPT1
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT1 [%08x]:%04x ",addr,data);
        if (emulating_self) tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        vb_state->tVIPREG.SPT[1] = data;
        break;
    case 0x0005F84C:    //SPT2
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT2 [%08x]:%04x ",addr,data);
        if (emulating_self) tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        vb_state->tVIPREG.SPT[2] = data;
        break;
    case 0x0005F84E:    //SPT3
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP SPT3 [%08x]:%04x ",addr,data);
        if (emulating_self) tDSPCACHE.ObjDataCacheInvalid=1;  //Invalidate Char Cache
        vb_state->tVIPREG.SPT[3] = data;
        break;
    case 0x0005F860:    //GPLT0  //Set the current color palet for the BGMap's
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT0 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        }
        vb_state->tVIPREG.GPLT[0] = data;
        break;
    case 0x0005F862:    //GPLT1
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT1 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        }
        vb_state->tVIPREG.GPLT[1] = data;
        break;
    case 0x0005F864:    //GPLT2
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT2 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        }
        vb_state->tVIPREG.GPLT[2] = data;
        break;
    case 0x0005F866:    //GPLT3
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP GPLT3 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
            for(i=0;i<14;i++) tDSPCACHE.BGCacheInvalid[i]=1;
        }
        vb_state->tVIPREG.GPLT[3] = data;
        break;
    case 0x0005F868:    //JPLT0  //Set the current color palet for the OBJ's
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT0 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        vb_state->tVIPREG.JPLT[0] = data;
        break;
    case 0x0005F86A:    //JPLT1
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT1 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        vb_state->tVIPREG.JPLT[1] = data;
        break;
    case 0x0005F86C:    //JPLT2
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT2 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        vb_state->tVIPREG.JPLT[2] = data;
        break;
    case 0x0005F86E:    //JPLT3
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP JPLT3 [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.ObjPALMod = 1;  //Invalidate Pallet Cache
            tDSPCACHE.ObjDataCacheInvalid=1;
        }
        vb_state->tVIPREG.JPLT[3] = data;
        break;
    case 0x0005F870:    //BKCOL
        //~ dtprintf(1,ferr,"\nWrite  HWORD VIP BKCOL [%08x]:%04x ",addr,data);
        if (emulating_self) {
            tDSPCACHE.BgmPALMod = 1;  //Invalidate Pallet Cache
            tDSPCACHE.ObjPALMod = 1;
        }
        vb_state->tVIPREG.BKCOL = data & 3;
        break;
    default:
        //~ dtprintf(0,ferr,"\nWrite  HWORD VIP error [%08x]:%04x ",addr,data);
        break;
    }
    return 0;
}

WORD vipcreg_rword(WORD addr) {
    //~ dtprintf(0,ferr,"\nRead  WORD VIP [%08x]:%08x **************",addr,0);
    return vipcreg_rhword(addr); //More???
}

WORD vipcreg_wword(WORD addr, WORD data) {
    //~ dtprintf(0,ferr,"\nWrite  WORD VIP [%08x]:%08x **************",addr,data);
    vipcreg_whword(addr,(HWORD)data);
    vipcreg_whword(addr+2,(HWORD)(data>>16));
    return 0;
}
