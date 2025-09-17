////////////////////////////////////////////////////////////////
// Defines for the V810 Display Processor

#ifndef V810_DISP_H_
#define V810_DISP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "vb_types.h"

#ifdef __3DS__
#include <citro3d.h>
#endif

#define CONFIG_3D_SLIDERSTATE (*(float*)0x1FF81080)

// VB input defines
#define VB_BATERY_LOW   0x0001 // Batery Low
#define VB_KEY_L        0x0020 // L Trigger
#define VB_KEY_R        0x0010 // R Trigger
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
#define COLTABLE_OFFSET 0x0003DC00
#define OBJ_OFFSET      0x0003E000
#define OBJ_SIZE        0x0008
#define WORLD_OFFSET    0x0003D800
#define WORLD_SIZE      0x0020

typedef enum {
    CPU_WROTE,
    GPU_WROTE,
    GPU_CLEAR,
} DDSPSTATE;

typedef struct {
    u8 min, max;
} SOFTBOUND;

// Struct to encapsulate all the Cache Stuff... I know its not necesary
// But it helps me contain the ever spreading cache
typedef struct {
    bool    BgmPALMod;              // World Pallet Changed
    BYTE    BgmPAL[4][4];           // World Pallet
    bool    ObjPALMod;              // Obj Pallet Changed
    BYTE    ObjPAL[4][4];           // Obj Pallet
    bool    BrtPALMod;              // Britness for Pallet Changed

    bool    ObjDataCacheInvalid;    // Object Cache Is invalid

    bool    ObjCacheInvalid;        // Object Cache Is invalid
    bool    BGCacheInvalid[14];     // Object Cache Is invalid
	bool	CharCacheInvalid;
    bool    CharCacheForceInvalid;
	bool	CharacterCache[2048];	//Character chace
    DDSPSTATE DDSPDataState[2];     // Direct DisplayDraws True
    SOFTBOUND SoftBufWrote[2][64];
    union {
        BYTE u8[2][2][384*256];
        HWORD u16[2][2][384*256/2];
        WORD u32[2][2][384*256/4];
    } OpaquePixels;
    bool    ColumnTableInvalid;     // Column Table is invalid
} VB_DSPCACHE;

////////////////////////////////////////////////////////////////////
// Keybd Fn's. Had to put it somewhere!
// Read the Controller
HWORD V810_RControll(bool reset);

typedef struct {
    u16 head;
    s16 gx, gp, gy, mx, mp, my;
    u16 w, h, param, over;
    u16 _pad[5];
} WORLD;

// DPSTTS / DPCTRL
#define DPRST   0x0001
#define DISP    0x0002
#define L0BSY   0x0004
#define R0BSY   0x0008
#define L1BSY   0x0010
#define R1BSY   0x0020
#define SCANRDY 0x0040
#define FCLK    0x0080
#define RE      0x0100
#define SYNCE   0x0200
#define LOCK    0x0400

#define DPBSY (L0BSY | R0BSY | L1BSY | R1BSY)

// XPSTTS / XPCTRL
#define XPRST    0x0001
#define XPEN     0x0002
#define F0BSY    0x0004
#define F1BSY    0x0008
#define OVERTIME 0x0010
#define SBOUT    0x8000

// VIP interrupts
#define SCANERR     0x0001
#define LFBEND      0x0002
#define RFBEND      0x0004
#define GAMESTART   0x0008
#define FRAMESTART  0x0010
#define SBHIT       0x2000
#define XPEND       0x4000
#define TIMEERR     0x8000

#define XPBSY (F0BSY | F1BSY)

int videoProcessingTime(void);

void video_init(void);
void video_render(int displayed_fb, bool on_time);
void video_flush(bool left_for_both);
void video_quit(void);

void V810_SetPal(int BRTA, int BRTB, int BRTC);

void V810_Dsp_Frame(int left);
void clearCache(void);

extern VB_DSPCACHE tDSPCACHE;


// We have two ways of dealing with the colours:
// 1. multiply base colours by max repeat, and scale down in postprocessing
//  -> lighter darks, less saturated lights
// 2. same but delay up to a factor of 4 until postprocessing using texenv scale
//  -> more saturated lights, barely (if at all) visible darks
// Method 2 would be more accurate if we had gamma correction,
// but I don't know how to do that.
// So for now, we'll use method 1, as it looks better IMO.
// To use method 2, uncomment the following line:
//#define COLTABLESCALE
extern uint8_t maxRepeat;

extern int eye_count;

extern bool tileVisible[2048];
extern int blankTile;

#ifdef __3DS__

// video_hard
extern C3D_Tex screenTexHard[2];
extern C3D_RenderTarget *screenTargetHard[2];
void video_hard_init(void);
void video_hard_render(int drawn_fb);
void update_texture_cache_hard(void);

// video_soft
extern C3D_Tex screenTexSoft[2];
void video_soft_init(void);
void video_soft_to_texture(int displayed_fb);

#endif

void video_soft_render(int drawn_fb);
void update_texture_cache_soft(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
