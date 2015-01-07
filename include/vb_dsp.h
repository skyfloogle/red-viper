////////////////////////////////////////////////////////////////
// Defines for the V810 Display Processor

#ifndef V810_DISP_H_
#define V810_DISP_H_

#include "vb_types.h"
#include "allegro_compat.h"

#define CONFIG_3D_SLIDERSTATE (*(float*)0x1FF81080)

// VB input defines
#define VB_BATERY_LOW   0x0001 // Batery Low
#define VB_KEY_L        0x0010 // L Trigger
#define VB_KEY_R        0x0020 // R Trigger
#define VB_KEY_SELECT   0x2000 // Select Button
#define VB_KEY_START    0x1000 // Start Button
#define VB_KEY_B        0x0008 // B Button
#define VB_KEY_A        0x0004 // A Button
#define VB_RPAD_R       0x0080 // Right Pad, Right
#define VB_RPAD_L       0x4000 // Right Pad, Left
#define VB_RPAD_D       0x8000 // Right Pad, Down
#define VB_RPAD_U       0x0040 // Right Pad, Up
#define VB_LPAD_R       0x0100 // Left Pad, Right
#define VB_LPAD_L       0x0200 // Left Pad, Left
#define VB_LPAD_D       0x0400 // Left Pad, Down
#define VB_LPAD_U       0x0800 // Left Pad, Up

////////////////////////////////////////////////////////////////////
// Defines into memory
#define CHR_OFFSET      0x00078000  // Not!
#define CHR_SIZE        0x0010      // The size of one character, in bytes
#define BGMAP_OFFSET    0x00020000
#define BGMAP_SIZE      0x2000
#define OBJ_OFFSET      0x0003E000
#define OBJ_SIZE        0x0008
#define WORLD_OFFSET    0x0003D800
#define WORLD_SIZE      0x0020

#define MAXBRIGHT 64 // For brighter or darker screen...

#define PAL_SIZE 256

typedef RGB PALETTE[PAL_SIZE];

typedef struct {
    char            BPLTS;  // Pallete to be used (0-3)
    bool            HFLP;   // Horizontal flip
    bool            VFLP;   // Vertical flip
	bool			UNDEF;
    unsigned short  BCA;    // Chr# to be displayed 0-2047
} VB_BGMAP;

// Structure defining one Obj from the Obj Table
// There are 1024 Obj in the obj table...

typedef struct {
    int             JX;     // Horizontal Offset (-7-383)
    bool            JLON;   // Left Screen On
    bool            JRON;   // Right Screen On
    int             JP;     // Paralax (-256-255)
    int             JY;     // Vertical Offset (-7-223)
    bool            JHFLP;  // Horizontal Flip
    bool            JVFLP;  // Vertical Flip
	bool			UNDEF;
    unsigned short  JCA;    // Chr# to be displayd 0-2047
    char            JPLTS;  // Palet to be Used (0-3)
} VB_OBJ;

// Structure defining one world from the world table
// there are 32 worlds in the world table
typedef struct {
    bool    LON;        // Apears on left screen
    bool    RON;        // Apears on right screen
    BYTE    BGM;        // World Type, Normal, H-bias, Affine, OBJ (0-3)
    BYTE    SCX;        // H-Size of BG Map(0-3)
    BYTE    SCY;        // V-Size of BG Map(0-3)
    bool    OVER;       // Whatever???
    bool    END;        // End of Worlds (Dont bother going any further...)
	bool	Unknown1;	//Whatever???
	bool	Unknown2;	//Whatever???
    BYTE    BGMAP_BASE; // Determins the segment that SCX and SCY are based in (0-15)

    int     GX;         // H-Ofset Screen (0-383)
    int     GP;         // Paralax Screen (-256-255)
    int     GY;         // V-Ofset Screen (0-223)
    int     MX;         // H-Ofset BGMAP (0-383)
    int     MP;         // Paralax BGMAP (-256-255)
    int     MY;         // V-Ofset BGMAP (0-223)
    HWORD   W;          // Width of BG to be cut out(From wear???)
    HWORD   H;          // Height of BG to be cut out
    HWORD   PARAM_BASE; // base of paramater table
    HWORD   OVERP_CHR;  // Whatever???
    HWORD	Dont_Write[5]; // Unused 5 HWORDS of data
} VB_WORLD;

typedef struct {
	float pb_y;
	int paralax;
	float pd_y;
	float pa;
	float pc;
	int u1;
	int u2;
	int u3;
} AFFINE_MAP;

//grab one entry from the affine param table based ont the
//current y offset
void getAffine(int y, int pBase,AFFINE_MAP AFN_MP);


// Struct to encapsulate all the Cache Stuff... I know its not necesary
// But it helps me contain the ever spreading cache
typedef struct {
    bool    BgmPALMod;              // World Pallet Changed
    BYTE    BgmPAL[4][4];           // World Pallet
    bool    ObjPALMod;              // Obj Pallet Changed
    BYTE    ObjPAL[4][4];           // Obj Pallet
    bool    BrtPALMod;              // Britness for Pallet Changed

    bool    ObjDataCacheInvalid;    // Object Cache Is invalid
    VB_OBJ  ObjDataCache[0x400];    // Cache the Obj Data

    bool    ObjCacheInvalid;        // Object Cache Is invalid
    BITMAP  *ObjCacheBMP[4];        // Obj Cache Bitmaps
    bool    BGCacheInvalid[14];     // Object Cache Is invalid
    BITMAP  *BGCacheBMP[14];        // BGMap Cache Bitmaps
	bool		CharCacheInvalid;
	BITMAP	*CharacterCache;		//Character chace
    bool    DDSPDataWrite;          // Direct DisplayDraws True
} VB_DSPCACHE;

////////////////////////////////////////////////////////////////////
// Keybd Fn's. Had to put it somewhere!
// Read the Controller
HWORD V810_RControll(void);

void screen_blit(BITMAP *bitmap, int src_x, int src_y, int screen);

// Blit a bgmap to the screen buffer, wraping around if we take an immage past the edge of the source bmp..
void dt_blit(BITMAP *source[], BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height, int source_width, int source_height);

////////////////////////////////////////////////////////////////////
// Retreaves a character(Sprite) from the character table, old!
void getChr(HWORD num, HWORD chr[]);

// Translates a chr to a sprite, old only for displayRom()!!
void chr2sprite(HWORD chr[],BITMAP *sprt);

// Translates a chr to a sprite faster (dont pass in a sprite...
void fchr2sprite(HWORD num, BITMAP *sprt, bool hflp, bool vflp,BYTE pal[]);

////////////////////////////////////////////////////////////////////
// Returns a BGMap Buffer HWORD BGMap_Buff[4096]
void getBGmap(HWORD num, VB_BGMAP BGMap_Buff[]);

// Converts a BG Map Buffer to a World Picture, With Chrs in place.
void BGMap2World(HWORD num, BITMAP *wPlane);

////////////////////////////////////////////////////////////////////
// Returns a OBJ_buf Buffer VB_OBJ OBJ_Buff[1024]
void getObj(HWORD num, VB_OBJ OBJ_Buff[]);

// Converts a OBJ_buf Buffer to a World Picture, With Chrs in place.
void Obj2World(VB_OBJ OBJ_Buff[], BITMAP *wPlane, int spt_num, int img_n);

////////////////////////////////////////////////////////////////////
// Returns a WORLD_buf Buffer VB_WORLD WORLD_Buff[32]
// Now directly acesses the video ram (Scary)
void getWorld(HWORD num, VB_WORLD WORLD_Buff[]);

void World2Display(int wNum, VB_WORLD WORLD_Buff[], BITMAP *wPlane, int img_n);

////////////////////////////////////////////////////////////////////
bool V810_DSP_Init();
void V810_DSP_Quit();

void V810_SetPal(int BRTA, int BRTB, int BRTC);

void V810_Dsp_Frame(int left);
void clearCache();

extern VB_DSPCACHE tDSPCACHE;
extern BITMAP *dsp_bmp;

#endif
