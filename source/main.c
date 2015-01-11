#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "lsplash_bin.h"
#include "rsplash_bin.h"

#include "main.h"
#include "v810_mem.h"
#include "vb_types.h"
#include "v810_cpu.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "rom_db.h"
#include "draw.h"
#include "icon.h"
#include "keybmp.h"

u8 border=0;
u8 is_running=0;
int is_sram = 0; //Flag if writes to sram...

FS_archive sdmcArchive;

static inline void unicodeToChar(char* dst, uint16_t* src, int max) {
    if(!src || !dst) return;
    int n = 0;
    while (*src && n < max - 1) {
        *(dst++) = (*(src++)) & 0xFF;
        n++;
    }
    *dst = 0x00;
}

void toggle3D() {
    tVBOpt.DSPMODE = !tVBOpt.DSPMODE;
    gfxSet3D(tVBOpt.DSPMODE);
}

u32 pos2vbkey(u32 pos){
  switch (pos) {
    case 0: return 13;
    case 1: return 3;
    case 2: return 0;
    case 3: return 2;
    case 4: return 1;
    case 5: return 10;
    case 6: return 11;
    case 7: return 12;
    case 8: return 7;
    case 9: return 4;
    case 10: return 6;
    case 11: return 5;
    case 12: return 8;
    case 13: return 9;
  }
  return 0;
}

u32 ctrkey2pos(u32 key){
  u8 i=0;
   while(!(key&0x1)) {
     key=key>>1;
	 i++;
  }
  return i;
}

void setKeys() {
u32 keys;
u8 * bfb;
u32 x,y,i;
u8 pos=0;
u8 select=0;

char strKeys[32][6] = 
	{"  A  ","  B  "," SEL ","START","RIGHT","LEFT "," UP  ","DOWN ",
	 "  R  ","  L  ","  X  ","  Y  ","     ","     "," ZL  "," ZR  ",
	 "     ","     ","     ","     "," TAP ","     ","     ","     ",
	 "S RGT","S LFT","S UP ","S DWN","P RGT","P LFT","P UP ","P DWN"};
	
    while(aptMainLoop()) {
        hidScanInput();
        keys = hidKeysDown();
        if(keys & KEY_DUP) {
            pos = (pos+13)%14;
        } else if (keys & KEY_DDOWN) {
            pos = (pos+1)%14;
        } else if (keys & KEY_A) {
			select=1;
        } else if (keys & KEY_B) {
            return;
        } 
		bfb=gfxGetFramebuffer(GFX_BOTTOM, GFX_BOTTOM, NULL, NULL);
		for(x=0;x<320;x++)
			for(y=0;y<240;y++){
				bfb[(y+240*x)*3]=keybmp[0x36+(y*320+x)*3];
				bfb[(y+240*x)*3+1]=keybmp[0x36+(y*320+x)*3+1];
				bfb[(y+240*x)*3+2]=keybmp[0x36+(y*320+x)*3+2];
			}
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 0, 320, 38, 0x00002f<<(border*8));
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 37, 320, 2, 0xffffff);
		drawBox(GFX_BOTTOM,GFX_BOTTOM,(pos<7)?5:272, 46 +23 * (pos%7), 43, 21, 0x0000ff<<(border*8));
		paint_word(bfb,"[UP]/[DOWN] Move  [A] Change  [B] Back", 8, 15, 255, 255, 255);

		for (i=0;i<14;i++)
			paint_word(bfb,strKeys[ctrkey2pos(vbkey[pos2vbkey(i)])], (i<7)?8:274, 53 +23 * (i%7), (i==pos)?255:0, (i==pos)?255:0, (i==pos)?255:0);

		if (select){
			paint_word(bfb,"Press the key to map in this position", 8, 220, 0, 0, 0);
			drawBox(GFX_BOTTOM,GFX_BOTTOM,(pos<7)?5:272, 46 +23 * (pos%7), 43, 21, 0x0000ff<<(border*8));
		}
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
		system_checkPolls();
		if (select){
			do { //wait for key pressed
				hidScanInput(); 
				keys = hidKeysDown(); 
				gspWaitForVBlank();
				system_checkPolls();
			} while(!keys);
			vbkey[pos2vbkey(pos)]= 1<<ctrkey2pos(keys); // if more than 1 key pressed, catch only the first
			select=0;
		}
	}
}


int romSelect(char* path) {
    u8 pos = 1;
	u8 slot = 1;
    u32 keys;
    char romv[50][100];
    u8* romicons;
    int romc = 0;
    int i,x,y;
	u8* bfb;

    Handle dirHandle;
    uint32_t entries_read = 1;
    FSUSER_OpenDirectory(NULL, &dirHandle, sdmcArchive, FS_makePath(PATH_CHAR, "/vb/"));
    static FS_dirent entry;
    romicons=malloc(50*32*32*3);

    for(i = 0; i < 29 && entries_read; i++) {
        memset(&entry, 0, sizeof(FS_dirent));
        FSDIR_Read(dirHandle, &entries_read, 1, &entry);
        if(entries_read && !entry.isDirectory) {
//            if(!strncmp("VB", (char*) entry.shortExt, 2)) { // NOP90 - 3dmoo doesn't get entry.shortExt
            unicodeToChar(romv[romc], entry.name, 100);
			u8 sl = strlen(romv[romc]);  
			if((romv[romc][sl-2]=='V' || romv[romc][sl-2]=='v') && (romv[romc][sl-1]=='B' || romv[romc][sl-1]=='b')) {
				char full_path[256] = "/vb/";
				strcat(full_path, romv[romc]);
				strcat(full_path, ".bmp");
				if (!LoadBitmap(full_path,32, 32, romicons+(romc*32*32*3), 0xFF, 0, 64, 0x1)) {
					memcpy(romicons+(romc*32*32*3),icon+0x36,32*32*3);
				}
                romc++;
            }
        }
    }

	u8 fileSelected =0;	
    while(aptMainLoop() && ! fileSelected) {
        hidScanInput();
        keys = hidKeysDown();
        if((keys & KEY_DUP)&&(pos>1)) {
            pos--;
			if(slot>1) slot--;
        } else if ((keys & KEY_DDOWN)&&(pos<romc)) {
            pos++;
			if (slot<5) slot++;
        } else if ((keys & KEY_START) || (keys & KEY_A)) {
            fileSelected=1;
        } else if (keys & KEY_B) {
			free(romicons);
            return 0;
        } else if ((CONFIG_3D_SLIDERSTATE > 0.0f) && !tVBOpt.DSPMODE) {
            toggle3D();
        } else if ((CONFIG_3D_SLIDERSTATE == 0.0f) && tVBOpt.DSPMODE) {
            toggle3D();
        }

		bfb=gfxGetFramebuffer(GFX_BOTTOM, GFX_BOTTOM, NULL, NULL);
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 0, 320, 240, 0x00002f<<(border*8));

		paint_word(bfb,"[A] Select Rom   [B] Back", 60, 15, 255, 255, 255);
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 38, 320, 2, 0xffffff);
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0,40*slot, 320, 40, 0x0000ff<<(border*8));
		for(i=0;(i<5)&&(i<romc);i++ ) {
			for(x=0;x<32;x++)
				for(y=0;y<32;y++){
					bfb[(165-40*i+y+240*(x+4))*3]=romicons[((pos-slot+i)*32*32+(y*32)+x)*3];
					bfb[(165-40*i+y+240*(x+4))*3+1]=romicons[((pos-slot+i)*32*32+(y*32)+x)*3+1];
					bfb[(165-40*i+y+240*(x+4))*3+2]=romicons[((pos-slot+i)*32*32+(y*32)+x)*3+2];
				}
				paint_word(bfb,romv[pos-slot+i], 40, 55+40*i, 255, 255, 255);
			}

		gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
		system_checkPolls();
    }
	strcpy(path, romv[pos-1]);
	free(romicons);
    return 1;
}

int showMenu(char* path) {
	int i;
    u8 pos = 1;
    u32 keys;
	u8 fileSelected = 0;	
	u8 resumeRom = 0;
	u8* tlfb;
	u8* trfb;
	u8* bfb;
	char strBuf[16];
	char strMenu[6][12]=
		{"Load Rom   ","Scr Color  ","Frame Color","Frameskip  ","Map Keys   ","Exit       "};
	
	// Draw splash screen
    while(aptMainLoop() && !fileSelected && !resumeRom) {
		tlfb=gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		trfb=gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
		bfb=gfxGetFramebuffer(GFX_BOTTOM, GFX_BOTTOM, NULL, NULL);
		
		if (tVBOpt.PALMODE == PAL_RED){ 
			memset(tlfb, 0, 400*240*3);
			memset(trfb, 0,400*240*3);
			for(i=2;i<400*240*3;i+=3) {
				tlfb[i]= lsplash_bin[i];
				trfb[i]= rsplash_bin[i];
			}
		} else {
			memcpy(tlfb, lsplash_bin, lsplash_bin_size);
			memcpy(trfb, rsplash_bin, rsplash_bin_size);
		}

        hidScanInput();
        keys = hidKeysDown();
        if((keys & KEY_DUP)&&(pos>1)) {
            pos--;
        } else if ((keys & KEY_DDOWN)&&(pos<6)) {
            pos++;
        } else if ((keys & KEY_START) || (keys & KEY_A)) {
            if (pos==1)
				fileSelected=romSelect(path);
            else if (pos==2) {
				if (tVBOpt.PALMODE == PAL_NORMAL) 
					tVBOpt.PALMODE = PAL_RED;
				else 
					tVBOpt.PALMODE = PAL_NORMAL;
			} else if (pos==3) 
				border = (border+1)&0x3; // %4
			else if (pos==4) 
				tVBOpt.FRMSKIP  = (tVBOpt.FRMSKIP +1)%10;
			else if (pos==5) 
				setKeys(); // Keymap settings
			else if (pos==6) 
				return 0;
        } else if ((keys & KEY_B)&&is_running) {
			resumeRom=1;
        } else if (keys & KEY_R) {
            // The splash screen changes to red
            tVBOpt.PALMODE = PAL_RED;
        } else if (keys & KEY_L) {
            tVBOpt.PALMODE = PAL_NORMAL;
        } else if ((CONFIG_3D_SLIDERSTATE > 0.0f) && !tVBOpt.DSPMODE) {
            toggle3D();
        } else if ((CONFIG_3D_SLIDERSTATE == 0.0f) && tVBOpt.DSPMODE) {
            toggle3D();
        }

		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 0, 320, 240, 0x00002f<<(border*8));
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 37, 320, 2, 0xffffff);
		paint_word(bfb,"[UP]/[DOWN] Move   [A] Select/Change", 16, 15, 255, 255, 255);
		for (i=1;i<=6;i++) {
			drawBox(GFX_BOTTOM,GFX_BOTTOM,5, 30*i+25, 90, 20, (i==pos)?0x0000ff<<(border*8):0x888888);
			paint_word(bfb,strMenu[i-1], 6, 30*i+31, 255, 255, 255);
		}
		if (is_running)	paint_word(bfb,"[B] Back to running rom", 110, 61, 255, 255, 255);
		if (tVBOpt.PALMODE == PAL_NORMAL) 
			paint_word(bfb,">> WHITE", 110, 91, 255, 255, 255);
		else 
			paint_word(bfb,">> RED", 110, 91, 255, 255, 255);

		switch (border){
			case 0:
				paint_word(bfb,">> BLUE", 110, 121, 255, 255, 255);
				break;
			case 1:
				paint_word(bfb,">> GREEN", 110, 121, 255, 255, 255);
				break;
			case 2:
				paint_word(bfb,">> RED", 110, 121, 255, 255, 255);
				break;
			case 3:
				paint_word(bfb,">> BLACK", 110, 121, 255, 255, 255);
				break;
		}
		
		itoa(tVBOpt.FRMSKIP,strBuf);
		paint_word(bfb,strBuf, 110, 151, 255, 255, 255);

		gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
		system_checkPolls();
	}
	// Clean both fb groups
	for(i=0;i<2;i++) {
		bfb=gfxGetFramebuffer(GFX_BOTTOM, GFX_BOTTOM, NULL, NULL);
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 0, 320, 240, 0x00002f<<(border*8));
		drawBox(GFX_TOP,GFX_LEFT,0, 0, 400, 240, 0x00002f<<(border*8));
		drawBox(GFX_TOP,GFX_RIGHT,0, 0, 400, 240, 0x00002f<<(border*8));
		drawBox(GFX_BOTTOM,GFX_BOTTOM,0, 37, 320, 2, 0xffffff);
		paint_word(bfb,"Tap touchscreen to pause/change settings", 0, 15, 255, 255, 255);
		paint_word(bfb,"FPS  :", 10, 50, 255, 255, 255);
		paint_word(bfb,"Frame:", 10, 58, 255, 255, 255);
		paint_word(bfb,"PC:", 10, 66, 255, 255, 255);
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	if(resumeRom) return 2;
	return 1;
}

int v810_loadRom(char * rom_name) {
    char ram_name[32];
    unsigned int rom_size = 0;
    unsigned int ram_size = 0;

// Open VB Rom
    char full_path[46] = "/vb/";
    strcat(full_path, rom_name);

    FILE* f = fopen(full_path, "r");
    if (f) {
        fseek(f , 0 , SEEK_END);
        rom_size = ftell(f);
        rewind(f);

		if(V810_ROM1.pmemory) free(V810_ROM1.pmemory);
        V810_ROM1.pmemory = malloc(rom_size);
        fread(V810_ROM1.pmemory, 1, rom_size, f);

        fclose(f);
    } else {
        return 0;
    }

    // CRC32 Calculations
    gen_table();
    tVBOpt.CRC32 = get_crc(rom_size);

    // Initialize our rom tables.... (USA)
    V810_ROM1.highaddr = 0x07000000 + rom_size - 1;
    V810_ROM1.lowaddr  = 0x07000000;
    V810_ROM1.off = (unsigned)V810_ROM1.pmemory - V810_ROM1.lowaddr;
    // Offset + Lowaddr = pmemory

    // Try to load up the saveRam file...
    // First, copy the rom path and concatenate .ram to it
//    strcpy(ram_name, rom_name);
//    strcat(ram_name, ".ram");

//    V810_GAME_RAM.pmemory = readFile(ram_name, (uint64_t*)&ram_size);

    if (!ram_size) {
        is_sram = 0;
    } else {
        is_sram = 1;
    }

    // Initialize our GameRam tables.... (Cartrige Ram)
    V810_GAME_RAM.lowaddr  = 0x06000000;
    V810_GAME_RAM.highaddr = 0x06003FFF; //0x06007FFF; //(8K, not 64k!)
    // Alocate space for it in memory
    if(!is_sram) {
		if(V810_GAME_RAM.pmemory) free(V810_GAME_RAM.pmemory);
		V810_GAME_RAM.pmemory = (unsigned char *)calloc(((V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr), sizeof(BYTE));
    }
    // Offset + Lowaddr = pmemory
    V810_GAME_RAM.off = (unsigned)V810_GAME_RAM.pmemory - V810_GAME_RAM.lowaddr;

    if(ram_size > (V810_GAME_RAM.highaddr+1) - V810_GAME_RAM.lowaddr) {
        ram_size = (V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr;
    }
	
	return 1;
}


int v810_init() {

    // Initialize our ram1 tables....
    V810_DISPLAY_RAM.lowaddr  = 0x00000000;
    V810_DISPLAY_RAM.highaddr = 0x0003FFFF; //0x0005FFFF; //97FFF
    // Alocate space for it in memory
    V810_DISPLAY_RAM.pmemory = (unsigned char *)malloc(((V810_DISPLAY_RAM.highaddr +1) - V810_DISPLAY_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_DISPLAY_RAM.off = (unsigned)V810_DISPLAY_RAM.pmemory - V810_DISPLAY_RAM.lowaddr;

    // Initialize our VIPC Reg tables....
    V810_VIPCREG.lowaddr  = 0x00040000; //0x0005F800
    V810_VIPCREG.highaddr = 0x0005FFFF; //0x0005F870
    // Point to the handler funcs...
    V810_VIPCREG.rfuncb = &(vipcreg_rbyte);
    V810_VIPCREG.wfuncb = &(vipcreg_wbyte);
    V810_VIPCREG.rfunch = &(vipcreg_rhword);
    V810_VIPCREG.wfunch = &(vipcreg_whword);
    V810_VIPCREG.rfuncw = &(vipcreg_rword);
    V810_VIPCREG.wfuncw = &(vipcreg_wword);

    // Initialize our SoundRam tables....
    V810_SOUND_RAM.lowaddr  = 0x01000000;
    V810_SOUND_RAM.highaddr = 0x010005FF; //0x010002FF
    // Alocate space for it in memory
    V810_SOUND_RAM.pmemory = (unsigned char *)malloc(((V810_SOUND_RAM.highaddr +1) - V810_SOUND_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_SOUND_RAM.off = (unsigned)V810_SOUND_RAM.pmemory - V810_SOUND_RAM.lowaddr;

    // Initialize our VBRam tables....
    V810_VB_RAM.lowaddr  = 0x05000000;
    V810_VB_RAM.highaddr = 0x0500FFFF;
    // Alocate space for it in memory
    V810_VB_RAM.pmemory = (unsigned char *)malloc(((V810_VB_RAM.highaddr +1) - V810_VB_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_VB_RAM.off = (unsigned)V810_VB_RAM.pmemory - V810_VB_RAM.lowaddr;

    // Initialize our HCREG tables.... // realy reg01
    V810_HCREG.lowaddr  = 0x02000000;
    V810_HCREG.highaddr = 0x02FFFFFF; // Realy just 0200002C but its mirrored...
    // Point to the handler funcs...
    V810_HCREG.rfuncb = &(hcreg_rbyte);
    V810_HCREG.wfuncb = &(hcreg_wbyte);
    V810_HCREG.rfunch = &(hcreg_rhword);
    V810_HCREG.wfunch = &(hcreg_whword);
    V810_HCREG.rfuncw = &(hcreg_rword);
    V810_HCREG.wfuncw = &(hcreg_wword);

    mem_whword(0x0005F840, 0x0004); //XPSTTS

    tHReg.SCR	= 0x4C;
    tHReg.WCR	= 0xFC;
    tHReg.TCR	= 0xE4;
    tHReg.THB	= 0xFF;
    tHReg.TLB	= 0xFF;
    tHReg.SHB	= 0x00;
    tHReg.SLB	= 0x00;
    tHReg.CDRR	= 0x00;
    tHReg.CDTR	= 0x00;
    tHReg.CCSR	= 0xFF;
    tHReg.CCR	= 0x6D;

    tHReg.tTRC = 2000;
    tHReg.tCount = 0xFFFF;
    tHReg.tReset = 0;

    return 1;
}

int main() {
    int qwe;
    int frame = 0;
    int err = 0;
    static int Left = 0;
    int skip = 0;
	u8* bfb;
    touchPosition touchpos;
	char buf[16];
	
    srvInit();
    aptInit();
    hidInit(NULL);
    gfxInit();
    fsInit();
    sdmcInit();

    sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (uint8_t*)"/"}};
    FSUSER_OpenArchive(NULL, &sdmcArchive);

    setDefaults();
    V810_DSP_Init();

    if (tVBOpt.DSPMODE == DM_3D) {
        gfxSet3D(true);
    } else {
        gfxSet3D(false);
    }

    if (!v810_init()) {
        goto exit;
    }

    char path[64] = "";
    if (!showMenu(path)) {
        goto exit;
    }
    if (!v810_loadRom(path)) {
        goto exit;
    }
	v810_reset();
    v810_trc();
    clearCache();
	is_running=1;

    while(aptMainLoop()) {
        uint64_t startTime = osGetTime();

        hidScanInput();
		hidTouchRead(&touchpos);
        int keys = hidKeysHeld();

        if ((keys & KEY_START) && (keys & KEY_SELECT))
            break;
        if ((CONFIG_3D_SLIDERSTATE > 0.0f) && !tVBOpt.DSPMODE) {
            toggle3D();
        } else if ((CONFIG_3D_SLIDERSTATE == 0.0f) && tVBOpt.DSPMODE) {
            toggle3D();
        }

		if (touchpos.px>0) {
			int res = showMenu(path);
			switch (res) {
			case 0: 
				goto exit;
				break;
			case 1: 
				if (!v810_loadRom(path)) 
					goto exit;
					v810_reset();
					v810_trc();
					clearCache();
				break;
			case 2: ;
			}
    
        }
 
		for (qwe = 0; qwe <= tVBOpt.FRMSKIP; qwe++) {
            // Trace
            err = v810_trc();
            if (err)
                break;

            // Display a frame, only after the right number of 'skips'
            if((tVIPREG.FRMCYC & 0x00FF) < skip) {
                skip = 0;
                Left ^= 1;
            }

            // Increment skip
            skip++;
            frame++;
        }

        // Display
        if (tVIPREG.DPCTRL & 0x0002) {
            V810_Dsp_Frame(Left); //Temporary...
        }

		bfb=gfxGetFramebuffer(GFX_BOTTOM, GFX_BOTTOM, NULL, NULL);
		drawBox(GFX_BOTTOM,GFX_BOTTOM,66, 50, 88, 23, 0x00002f<<(border*8));
		ftoa((tVBOpt.FRMSKIP+1)*(1000./(osGetTime() - startTime)),buf,3);
		paint_word(bfb,buf, 66, 50, 255, 255, 255);
		itoa(frame,buf);
		paint_word(bfb,buf, 66, 58, 255, 255, 255);
		itoa(PC,buf);
		paint_word(bfb,buf, 66, 66, 255, 255, 255);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
		system_checkPolls();
    }

exit:
    V810_DSP_Quit();

    sdmcExit();
    fsExit();
    hidExit();
    gfxExit();
    aptExit();
    srvExit();
    return 0;
}

void system_checkPolls() {
    APP_STATUS status;

	while((status=aptGetStatus()) != APP_RUNNING) {

        if(status == APP_SUSPENDING)
        {
            aptReturnToMenu();
        }
        else if(status == APP_PREPARE_SLEEPMODE)
        {
	    aptSignalReadyForSleep();
            aptWaitStatusEvent();
        }
        else if (status == APP_SLEEPMODE) {
        }
        else if (status == APP_EXITING) {
			V810_DSP_Quit();
			sdmcExit();
			fsExit();
			hidExit();
			gfxExit();
			aptExit();
			srvExit();
        }

    }
}

int LoadBitmap(char* path, u32 width, u32 height, void* buf, u32 alpha, u32 startx, u32 stride, u32 flags)
{
	Handle file;
	FS_path filePath;
	filePath.type = PATH_CHAR;
	filePath.size = strlen(path) + 1;
	filePath.data = (u8*)path;
	
	Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, filePath, FS_OPEN_READ, FS_ATTRIBUTE_NONE);
	if (res) 
		return 0;
		
	u32 bytesread;
	u32 temp;
	
	// magic
	FSFILE_Read(file, &bytesread, 0, (u32*)&temp, 2);
	if ((u16)temp != 0x4D42)
	{
		FSFILE_Close(file);
		return 0;
	}
	
	// width
	FSFILE_Read(file, &bytesread, 0x12, (u32*)&temp, 4);
	if (temp != width)
	{
		FSFILE_Close(file);
		return false;
	}
	
	// height
	FSFILE_Read(file, &bytesread, 0x16, (u32*)&temp, 4);
	if (temp != height)
	{
		FSFILE_Close(file);
		return false;
	}
	
	// bitplanes
	FSFILE_Read(file, &bytesread, 0x1A, (u32*)&temp, 2);
	if ((u16)temp != 1)
	{
		FSFILE_Close(file);
		return 0;
	}
	
	// bit depth
	FSFILE_Read(file, &bytesread, 0x1C, (u32*)&temp, 2);
	if ((u16)temp != 24)
	{
		FSFILE_Close(file);
		return 0;
	}
	
	
	u32 bufsize = width*height*3;
	
	FSFILE_Read(file, &bytesread, 0x36, buf, bufsize);
	FSFILE_Close(file);
	
	return 1;
}
