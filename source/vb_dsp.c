#include <3ds.h>

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <ctype.h>
#include <string.h>

#include "vb_types.h"
#include "v810_ins.h" // Needed for sign_16()
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_dsp.h"
#include "allegro_compat.h"

// Globals
int pCnt = 0;
int isDsp = 0;
int CurObj = 3;
int MaxBrt = 30;
PALETTE pallete;  // keep a global pall, so we dont have to clear the whole thing....
int exit_flag = 0;
// Instead we Blit the first n Bitmaps, instead of a masked blit...

VB_DSPCACHE tDSPCACHE; // Array of Display Cache info...

BITMAP *world_bmp;
BITMAP *world_bmp2;
BITMAP *dsp_bmp;

//Offset into Chr ram for the given chr#
WORD ChrOff[4] = {0x00006000, 0x0000E000, 0x00016000, 0x0001E000};

// Keybd Fn's. Had to put it somewhere!

// Read the Controller, Fix Me....
HWORD V810_RControll() {
    int ret_keys = 0;

    hidScanInput();
    int key = hidKeysHeld();

    if (key & vbkey[14])        ret_keys |= VB_BATERY_LOW;  // Batery Low
    if (key & vbkey[13])        ret_keys |= VB_KEY_L;       // L Trigger
    if (key & vbkey[12])        ret_keys |= VB_KEY_R;       // R Trigger
    if (key & vbkey[11])        ret_keys |= VB_KEY_SELECT;  // Select Button
    if (key & vbkey[10])        ret_keys |= VB_KEY_START;   // Start Button
    if (key & vbkey[9])         ret_keys |= VB_KEY_B;       // B Button
    if (key & vbkey[8])         ret_keys |= VB_KEY_A;       // A Button
    if (key & vbkey[7])         ret_keys |= VB_RPAD_R;      // Right Pad, Right
    else if (key & vbkey[6])    ret_keys |= VB_RPAD_L;      // Right Pad, Left
    if (key & vbkey[5])         ret_keys |= VB_RPAD_D;      // Right Pad, Down
    else if (key & vbkey[4])    ret_keys |= VB_RPAD_U;      // Right Pad, Up
    if (key & vbkey[3])         ret_keys |= VB_LPAD_R;      // Left Pad, Right
    else if (key & vbkey[2])    ret_keys |= VB_LPAD_L;      // Left Pad, Left
    if (key & vbkey[1])         ret_keys |= VB_LPAD_D;      // Left Pad, Down
    else if (key & vbkey[0])    ret_keys |= VB_LPAD_U;      // Left Pad, Up
    ret_keys = ret_keys|0x0002; // Always set bit1, ctrl ID

    return ret_keys;
}

void screen_blit(BITMAP *bitmap, int src_x, int src_y, int screen) {
    int x, y;
    uint8_t* fb = gfxGetFramebuffer(GFX_TOP, screen, NULL, NULL);

    for (y = src_y; y < 224+src_y; y++) {
        for (x = src_x; x < 384+src_x; x++) {
            uint32_t v = ((224+src_y+8 - y - 1) + x * 240) * 3;
            fb[v]     = pallete[bitmap->line[y][x]].b << 2;
            fb[v + 1] = pallete[bitmap->line[y][x]].g << 2;
            fb[v + 2] = pallete[bitmap->line[y][x]].r << 2;
        }
    }
}

////////////////////////////////////////////////////////////////////
// Blit a bgmap to the screen buffer, wraping around if we take an image
// past the edge of the source bmp.. Also handle sources in the negative...
// TODO: Remove additional overhead by passing a pointer to the WORLD_Buff struct, rather than all of it's contents!
void dt_blit(BITMAP *source[], BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height, int source_width, int source_height) {
    int SX;
    int SY;
    int tWidth;
    int tHeight;
    int startOff;
    int neg_x=0, neg_y=0;
    BITMAP *src_array[4];

    //Handle rotations over the end of the screen, wrap to the begining. //mod by this...
    SX = (1<<source_width)<<9;
    SY = (1<<source_height)<<9;

    if (source_x < 0)
        source_x = (((unsigned int)source_x & (SX-1)) | ~(SX-1));
    else
        source_x &= (SX-1);
    if (source_y < 0)
        source_y = (((unsigned int)source_y & (SY-1)) | ~(SY-1));
    else
        source_y &= (SY-1);


    if ((dest_x > 512) || (dest_x < -512))
        dest_x &= 0x01FF;
    if ((dest_y > 512) || (dest_y < -512))
        dest_y &= 0x01FF;

    //Funky code to support negative sources!
    if (source_x < 0) {
        dest_x += (neg_x = -source_x);
        source_x = 0;
        tWidth = SX;
    } else {
        tWidth = (SX-source_x);
    }

    if (source_y < 0) {
        dest_y += (neg_y = -source_y);
        source_y = 0;
        tHeight = SY;
    } else {
        tHeight = (SY-source_y);
    }

    if (tWidth > width)
        tWidth = width;
    if (tHeight > height)
        tHeight = height;

    startOff = (source_x>>9)+((source_y>>9)<<source_width);
    src_array[0] = source[startOff];
    src_array[1] = source[startOff];//Next right or wrap to first
    src_array[2] = source[startOff];//Next down or wrap to top
    src_array[3] = source[startOff];//next right and down, or wrap to top


    masked_blit(src_array[0], dest, source_x&511, source_y&511, dest_x, dest_y, tWidth, tHeight);

    // TODO: Add support for overplane
    // [character(s) drawn outside of BGMap if overplane is enabled, else wrap BGMap around world]
    if (neg_x)
        masked_blit(src_array[1], dest, 512-neg_x, 0, dest_x-neg_x, dest_y, neg_x, tHeight);
    if (neg_y)
        masked_blit(src_array[2], dest, 0, 512-neg_y, dest_x, 7, tWidth, neg_y);
    if ((neg_x) && (neg_y))
        masked_blit(src_array[3], dest, 512-neg_x, 512-neg_y, dest_x-neg_x, 7, neg_x, neg_y);

    if ((source_x+width) > SX)
        masked_blit(src_array[1], dest, 0, source_y&511, dest_x+tWidth, dest_y, (width - tWidth), tHeight);
    if ((source_y+height) > SY)
        masked_blit(src_array[2], dest, source_x&511, 0, dest_x, (dest_y+tHeight), tWidth, (height - tHeight));
    if (((source_x+width) > SX) && ((source_y+height) > SY))
        masked_blit(src_array[3], dest, 0, 0, dest_x+tWidth, (dest_y+tHeight), (width - tWidth), (height - tHeight));
}


// Similar to dt_blit, but edited for scaling (and soon rotation)
// works on one scanline at a time  (requires negative source patches -- see dt_blit())
// new function -- includes SOME rotation support.
void affine_blit(BITMAP *source[], BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int source_width, int source_height, float scale, int skew) {
    int SX,SY;
    int tWidth,sWidth;
    int startOff;
    int sign=1,width_x;
    int i;

    // Handle rotations over the end of the screen, wrap to the begining. //mod by this...
    SX = (1<<source_width)<<9;
    SY = (1<<source_height)<<9;

    source_x &= (SX-1);
    source_y &= (SY-1);

    // AHHHH! That -512 thing was giving me a headache! Wish I had my flash programmer right about now! :)  - Parasyte
    if ((dest_x > 512) || (dest_x < -512))
        dest_x &= 0x01FF;
    if ((dest_y > 512) || (dest_y < -512))
        dest_y &= 0x01FF;


    if ((width_x=(dest_x+width)) > 383) width_x=383;

    if (skew) { // This is for rotation
        if (skew >> 15) {
            skew = -skew; // Negate skew if negative
            sign = -1;
        }
        if (skew <= 512) {
            sWidth = (512/skew);
            tWidth = (int)(sWidth/scale);
            // All this should be replaced with a nice, fast function to grab the correct
            // pixels, and send them to the dest bitmap!! (Allegro's stretch functions do
            // not support negative widths!! >_<
            for (i = 0; i < skew; i++) {
                source_x &= 0x01FF;
                source_y &= 0x01FF;
                startOff = (source_x>>9)+((source_y>>9)<<source_width);
                masked_stretch_blit(source[startOff], dest, source_x, source_y, tWidth, 1, dest_x, dest_y, sWidth, 1);
                source_x += sWidth;
                source_y += sign;
                dest_x += sWidth;
                if (dest_x >= width_x)
                    break;
            }
        } else {
            startOff = (source_x>>9)+((source_y>>9)<<source_width);
            masked_blit(source[startOff], dest, source_x, source_y, dest_x, dest_y, width, 1);
        }
    } else {
        tWidth = (int)(width/scale);

        startOff = (source_x>>9)+((source_y>>9)<<source_width);

        masked_stretch_blit(source[startOff], dest, (source_x & 0x01FF), (source_y & 0x01FF), tWidth, 1, dest_x, dest_y, width, 1);
    }
}

////////////////////////////////////////////////////////////////////
// Retreaves a character(Sprite) from the character table, Only used for DisplayRom()
// Now directly acesses the video ram (Scary)
void getChr(HWORD num, HWORD chr[]) {
    // Strip the first 2 bits to decode what chr table to use, use the remaning bits to index into the table...
    WORD offset =ChrOff[(num>>9)&0x03] + (CHR_SIZE * (num & 0x01FF));
    if((offset < V810_DISPLAY_RAM.lowaddr)||(offset+CHR_SIZE > V810_DISPLAY_RAM.highaddr))
        offset = ChrOff[0]; // Set to a known value (Chr0)
    offset += V810_DISPLAY_RAM.off; // Offset in Phisical Ram

    ((WORD *)chr)[0] = ((WORD *)(offset))[0];
    ((WORD *)chr)[1] = ((WORD *)(offset))[1];
    ((WORD *)chr)[2] = ((WORD *)(offset))[2];
    ((WORD *)chr)[3] = ((WORD *)(offset))[3];
}

// Translates a chr to a sprite, Only used for DisplayRom()
void chr2sprite(HWORD chr[],BITMAP *sprt) {
    int i;
    for (i = 0; i < 8; i++) {// We want words not bytes
        sprt->line[i][0] = ((chr[i] >>  0) & 3)+1;
        sprt->line[i][1] = ((chr[i] >>  2) & 3)+1;
        sprt->line[i][2] = ((chr[i] >>  4) & 3)+1;
        sprt->line[i][3] = ((chr[i] >>  6) & 3)+1;
        sprt->line[i][4] = ((chr[i] >>  8) & 3)+1;
        sprt->line[i][5] = ((chr[i] >> 10) & 3)+1;
        sprt->line[i][6] = ((chr[i] >> 12) & 3)+1;
        sprt->line[i][7] = ((chr[i] >> 14) & 3)+1;
    }
}

// Replaces the two Fn's Above (Faster?)
void fchr2sprite(HWORD num, BITMAP *sprt, bool hflp, bool vflp,BYTE pal[]) {
    int i;

    // Strip the first 2 bits to decode what chr table to use, use the remaning bits to index into the table...
    WORD offset = ChrOff[(num>>9)&0x03] + (CHR_SIZE * (num & 0x01FF)) + V810_DISPLAY_RAM.off;

    if (!hflp && !vflp){
        for (i = 0; i < 8; i++) {// We want words not bytes
            sprt->line[i][0] = pal[((((HWORD *)(offset))[i] >>  0) & 3)]; //replace "<<4) & 0x3f);" with cPal[#]
            sprt->line[i][1] = pal[((((HWORD *)(offset))[i] >>  2) & 3)];
            sprt->line[i][2] = pal[((((HWORD *)(offset))[i] >>  4) & 3)];
            sprt->line[i][3] = pal[((((HWORD *)(offset))[i] >>  6) & 3)];
            sprt->line[i][4] = pal[((((HWORD *)(offset))[i] >>  8) & 3)];
            sprt->line[i][5] = pal[((((HWORD *)(offset))[i] >> 10) & 3)];
            sprt->line[i][6] = pal[((((HWORD *)(offset))[i] >> 12) & 3)];
            sprt->line[i][7] = pal[((((HWORD *)(offset))[i] >> 14) & 3)];
        }
    } else if (hflp && vflp){
        for (i = 0; i < 8; i++) {// We want words not bytes
            sprt->line[7-i][7] = pal[((((HWORD *)(offset))[i] >>  0) & 3)];
            sprt->line[7-i][6] = pal[((((HWORD *)(offset))[i] >>  2) & 3)];
            sprt->line[7-i][5] = pal[((((HWORD *)(offset))[i] >>  4) & 3)];
            sprt->line[7-i][4] = pal[((((HWORD *)(offset))[i] >>  6) & 3)];
            sprt->line[7-i][3] = pal[((((HWORD *)(offset))[i] >>  8) & 3)];
            sprt->line[7-i][2] = pal[((((HWORD *)(offset))[i] >> 10) & 3)];
            sprt->line[7-i][1] = pal[((((HWORD *)(offset))[i] >> 12) & 3)];
            sprt->line[7-i][0] = pal[((((HWORD *)(offset))[i] >> 14) & 3)];
        }
    } else if(hflp) {
        for(i = 0; i < 8; i++) {// We want words not bytes
            sprt->line[i][7] = pal[((((HWORD *)(offset))[i] >>  0) & 3)];
            sprt->line[i][6] = pal[((((HWORD *)(offset))[i] >>  2) & 3)];
            sprt->line[i][5] = pal[((((HWORD *)(offset))[i] >>  4) & 3)];
            sprt->line[i][4] = pal[((((HWORD *)(offset))[i] >>  6) & 3)];
            sprt->line[i][3] = pal[((((HWORD *)(offset))[i] >>  8) & 3)];
            sprt->line[i][2] = pal[((((HWORD *)(offset))[i] >> 10) & 3)];
            sprt->line[i][1] = pal[((((HWORD *)(offset))[i] >> 12) & 3)];
            sprt->line[i][0] = pal[((((HWORD *)(offset))[i] >> 14) & 3)];
        }
    } else if (vflp) {
        for (i = 0; i < 8; i++) {// We want words not bytes
            sprt->line[7-i][0] = pal[((((HWORD *)(offset))[i] >>  0) & 3)];
            sprt->line[7-i][1] = pal[((((HWORD *)(offset))[i] >>  2) & 3)];
            sprt->line[7-i][2] = pal[((((HWORD *)(offset))[i] >>  4) & 3)];
            sprt->line[7-i][3] = pal[((((HWORD *)(offset))[i] >>  6) & 3)];
            sprt->line[7-i][4] = pal[((((HWORD *)(offset))[i] >>  8) & 3)];
            sprt->line[7-i][5] = pal[((((HWORD *)(offset))[i] >> 10) & 3)];
            sprt->line[7-i][6] = pal[((((HWORD *)(offset))[i] >> 12) & 3)];
            sprt->line[7-i][7] = pal[((((HWORD *)(offset))[i] >> 14) & 3)];
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////
// vRenderCharacter
//
// Jason -
// Renders a given character number into the byte buffer provided at
// The starting X and Y locations.  Note that p_wBitmapWidth is the width of the
// buffer into which we are rendering.
// Non allegro version of fchr2sprite for sub bitmaps
// Review-- Why do we need the Palette Pointer?
//
void vRenderCharacter(HWORD p_hwCharacterNumber,// The number of the character to display
                      BYTE *p_pbSpriteData, // The raw byte buffer to render the sprite into.
                      WORD p_wStartingX,// Starting X offset within the byte buffer
                      WORD p_wStartingY,// Starting Y offset in the byte buffer
                      WORD p_wBitmapWidth, // How wide the bitmap we're rendering on is.
                      bool p_fFlipHorizontally,
                      bool p_fFlipVertically,
                      BYTE p_rgbPalette[])
{// vRenderCharacter
    int l_nRowCounter;

    WORD l_wDataOffset = ChrOff[(p_hwCharacterNumber>>9)&0x03] + (CHR_SIZE * (p_hwCharacterNumber & 0x01FF)) + V810_DISPLAY_RAM.off;
    //WORD offset = ChrOff[(num>>9)&0x03] + (CHR_SIZE * (num & 0x01FF)) + V810_DISPLAY_RAM.off;
    HWORD * l_phwLineData = ((HWORD *)(l_wDataOffset));
    HWORD l_hwCurrentData; // We use this to store the current data.

    p_pbSpriteData += ((p_wStartingX) + (p_wStartingY*p_wBitmapWidth));

    l_nRowCounter = 0;
    if (!p_fFlipHorizontally && !p_fFlipVertically) {// Normal Character
        for(l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) {// For Each Line
            l_hwCurrentData = *l_phwLineData;
            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            p_pbSpriteData+=(p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData++;
        }// End for Each Line
    }// End Normal Character
    else if (p_fFlipHorizontally && p_fFlipVertically) { // Flip both
        l_phwLineData+=7;
        for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) { // For Each Line
            l_hwCurrentData = *l_phwLineData;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  14) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  12) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  10) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  8) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  6) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  4) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  2) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  0) & 3)];
            p_pbSpriteData++;
            p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData--;
        } // End For Each Line
    } // End Flip both
    else if (p_fFlipHorizontally) { // Flip Horizontal
        for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) { // For Each Line
            l_hwCurrentData = *l_phwLineData;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  14) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  12) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  10) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  8) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  6) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  4) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  2) & 3)];
            p_pbSpriteData++;
            *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  0) & 3)];
            p_pbSpriteData++;
            p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData++;
        } // End For Each Line
    } // End Flip Horizontal
    else if (p_fFlipVertically) { // Vertically flipped
            l_phwLineData+=7;
            for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) { // For Each Line
                l_hwCurrentData = *l_phwLineData;
                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                l_hwCurrentData>>=2;

                *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
                p_pbSpriteData++;
                p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
                l_phwLineData--;
            } // End for Each Line
        } // End vertical flip
} // End vRenderCharacter

////////////////////////////////////////////////////////////////////////////////////////
// vRenderCharacterTransparent
//
// Jason -
// Renders a given character number into the byte buffer provided at
// The starting X and Y locations.  Note that p_wBitmapWidth is the width of the
// buffer into which we are rendering.
// Non allegro version of fchr2sprite for sub bitmaps
// This version supports transparencies.  This is a major slowdown, so
// we'll keep two versions.
// Still Review-- Why do we need the Palette Pointer?
//
void vRenderCharacterTransparent(HWORD p_hwCharacterNumber,// The number of the character to display
                                 BYTE *p_pbSpriteData, // The raw byte buffer to render the sprite into.
                                 WORD p_wStartingX,// Starting X offset within the byte buffer
                                 WORD p_wStartingY,// Starting Y offset in the byte buffer
                                 WORD p_wBitmapWidth, // How wide the bitmap we're rendering on is.
                                 bool p_fFlipHorizontally,
                                 bool p_fFlipVertically,
                                 BYTE p_rgbPalette[])
{ // vRenderCharacterTransparent
    int l_nRowCounter;

    WORD l_wDataOffset = ChrOff[(p_hwCharacterNumber>>9)&0x03] + (CHR_SIZE * (p_hwCharacterNumber & 0x01FF)) + V810_DISPLAY_RAM.off;
    //WORD offset = ChrOff[(num>>9)&0x03] + (CHR_SIZE * (num & 0x01FF)) + V810_DISPLAY_RAM.off;
    HWORD * l_phwLineData = ((HWORD *)(l_wDataOffset));
    HWORD l_hwCurrentData; // We use this to store the current data.

    p_pbSpriteData += ((p_wStartingX) + (p_wStartingY*p_wBitmapWidth));

    l_nRowCounter = 0;

    if (!p_fFlipHorizontally && !p_fFlipVertically) { // Normal Character
        for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) { // For Each Line
            l_hwCurrentData = *l_phwLineData;
            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData &3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData++;
        }// End for Each Line
    } else if (p_fFlipHorizontally && p_fFlipVertically) {// Flip both
        l_phwLineData += 7;
        for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) {// For Each Line
            l_hwCurrentData = *l_phwLineData;
            if (p_rgbPalette[((l_hwCurrentData >>  14) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  14) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>  12) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  12) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>  10) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  10) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   8) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  8) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   6) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  6) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   4) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  4) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   2) & 3)])  *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  2) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   0) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  0) & 3)];
            p_pbSpriteData++;
            p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData--;
        }// End For Each Line
    } else if (p_fFlipHorizontally) {// Flip Horizontal
        for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) {// For Each Line
            l_hwCurrentData = *l_phwLineData;
            if (p_rgbPalette[((l_hwCurrentData >>  14) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  14) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>  12) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  12) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>  10) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  10) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   8) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  8) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   6) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  6) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   4) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  4) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>  2) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  2) & 3)];
            p_pbSpriteData++;

            if (p_rgbPalette[((l_hwCurrentData >>   0) & 3)]) *p_pbSpriteData= p_rgbPalette[((l_hwCurrentData >>  0) & 3)];
            p_pbSpriteData++;
            p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData++;
        }// End For Each Line
    } else if (p_fFlipVertically) {// Vertically flipped
        l_phwLineData+=7;
        for (l_nRowCounter = 0; l_nRowCounter < 8; l_nRowCounter++) {// For Each Line
            l_hwCurrentData = *l_phwLineData;
            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            l_hwCurrentData>>=2;

            if (p_rgbPalette[(l_hwCurrentData & 3)]) *p_pbSpriteData= p_rgbPalette[(l_hwCurrentData & 3)];
            p_pbSpriteData++;
            p_pbSpriteData += (p_wBitmapWidth-8); // Skip to start of next.
            l_phwLineData--;
        } // End for Each Line
    } // End vertical flip
} // End vRenderCharacterTransparent

////////////////////////////////////////////////////////////////////
// Returns a BGMap Buffer VB_BGMAP BGMap_Buff[4096]
// Now directly acesses the video ram (Scary)
void getBGmap(HWORD num, VB_BGMAP BGMap_Buff[]) {
    int i;
    HWORD thword;

    WORD offset = BGMAP_OFFSET + (BGMAP_SIZE*(num & 0xF));// Only 14 posible bg's, this is 16 but whos counting?
    offset += V810_DISPLAY_RAM.off; // Offset in Phisical Ram

    for(i = 0; i < (BGMAP_SIZE>>1); i++) {
        // Make shure we only grab num's from 0-4095...
        thword = ((HWORD *)(offset))[i];
        BGMap_Buff[i].BCA   = thword & 0x7FF;
        BGMap_Buff[i].VFLP  = (thword >> 12) & 0x1;
        BGMap_Buff[i].HFLP  = (thword >> 13) & 0x1;
        BGMap_Buff[i].BPLTS = (thword >> 14) & 0x3;
    }
}

// Converts a BG Map Buffer to a World Picture, With Chrs in place.
void BGMap2World(HWORD num, BITMAP *wPlane) {
    int i;
    VB_BGMAP BGMap_Buff[4096];
    HWORD thword;
    WORD offset = BGMAP_OFFSET + (BGMAP_SIZE*(num & 0xF))+V810_DISPLAY_RAM.off;// only 14 posible bg's, this is 16 but whos counting?

    // Grab the BGMap info...
    for (i = 0; i < (BGMAP_SIZE>>1); i++) {
        // Make shure we only grab num's from 0-4095...
        thword = ((HWORD *)(offset))[i];
        BGMap_Buff[i].BCA   = thword & 0x7FF;
        BGMap_Buff[i].VFLP  = (thword >> 12) & 0x1;
        BGMap_Buff[i].HFLP  = (thword >> 13) & 0x1;
        BGMap_Buff[i].BPLTS = (thword >> 14) & 0x3;
    }


    if (isDsp){ // If were in the Display and not the debug code...
        if (tDSPCACHE.BgmPALMod>0) { // If cache is invalid
            for (i = 0; i < 4; i++) {
                tDSPCACHE.BgmPAL[i][0]=((tVIPREG.GPLT[i]   )&3)+1; // First collor is transparent, offset by 1
                tDSPCACHE.BgmPAL[i][1]=((tVIPREG.GPLT[i]>>2)&3)+1;
                tDSPCACHE.BgmPAL[i][2]=((tVIPREG.GPLT[i]>>4)&3)+1;
                tDSPCACHE.BgmPAL[i][3]=((tVIPREG.GPLT[i]>>6)&3)+1;
                tDSPCACHE.BgmPAL[i][0]=0; // Fill in the transparent char
            }
            tDSPCACHE.BgmPALMod=0;
        }
    } else { // If in debug land (make this better!)
        for (i = 0; i < 4; i++) {
            tDSPCACHE.BgmPAL[i][0]=1;
            tDSPCACHE.BgmPAL[i][1]=2;
            tDSPCACHE.BgmPAL[i][2]=3;
            tDSPCACHE.BgmPAL[i][3]=4;
        }
    }

    for(i = 0; i < (BGMAP_SIZE >> 1); i++) {// For each character in the map
        vRenderCharacter(BGMap_Buff[i].BCA, *wPlane->line, ((i&63)<<3), ((i>>6)<<3), wPlane->w, BGMap_Buff[i].HFLP, BGMap_Buff[i].VFLP, tDSPCACHE.BgmPAL[(BGMap_Buff[i].BPLTS&0x3)]);
    }// End for each character in the map

}

////////////////////////////////////////////////////////////////////
// Returns a OBJ_buf Buffer VB_OBJ OBJ_Buff[0x400]
// Now directly acesses the video ram (Scary)
void getObj(HWORD num, VB_OBJ OBJ_Buff[]) {
    WORD tword;
    WORD offset = OBJ_OFFSET + (OBJ_SIZE*(num & 0x03FF)); // 1024 posible obj...
    offset += V810_DISPLAY_RAM.off; //Offset in Phisical Ram

    OBJ_Buff[num].JX = (int)sign_16(((HWORD *)(offset))[0]);
    tword = ((HWORD *)(offset))[1];
    OBJ_Buff[num].JP = (int)sign_14(tword & 0x3FFF);
    OBJ_Buff[num].JRON = (tword >> 14) & 0x1;
    OBJ_Buff[num].JLON = (tword >> 15) & 0x1;
    OBJ_Buff[num].JY = (int)sign_16(((HWORD *)(offset))[2]);
    tword = ((HWORD *)(offset))[3];
    OBJ_Buff[num].JCA = tword & 0x7FF;
    OBJ_Buff[num].JVFLP = (tword >> 12) & 0x1;
    OBJ_Buff[num].JHFLP = (tword >> 13) & 0x1;
    OBJ_Buff[num].JPLTS = (tword >> 14) & 0x3;
}

////////////////////////////////////////////////////////////////////
// vGetAllObjects
// Returns all objects
//
void vGetAllObjects(VB_OBJ OBJ_Buff[]) {
    WORD tword;
    int num;
    WORD offset = OBJ_OFFSET + V810_DISPLAY_RAM.off;
    for (num = 0; num < 0x400; num++) {
        OBJ_Buff[num].JX = (int)sign_16(((HWORD *)(offset))[0]);
        tword = ((HWORD *)(offset))[1];
        OBJ_Buff[num].JP = (int)sign_14(tword & 0x3FFF);
        OBJ_Buff[num].JRON = (tword >> 14) & 0x1;
        OBJ_Buff[num].JLON = (tword >> 15) & 0x1;
        OBJ_Buff[num].JY = (int)sign_16(((HWORD *)(offset))[2]);
        tword = ((HWORD *)(offset))[3];
        OBJ_Buff[num].JCA = tword & 0x7FF;
        OBJ_Buff[num].JVFLP = (tword >> 12) & 0x1;
        OBJ_Buff[num].JHFLP = (tword >> 13) & 0x1;
        OBJ_Buff[num].JPLTS = (tword >> 14) & 0x3;
        offset += OBJ_SIZE;//Increment the pointer
    }
}

// Converts a OBJ_buf Buffer to a World Picture, With Chrs in place.
// Pass in int spt0-3 for the world num....
// Note modified tSprt for Blits to screen, enabling Transparency.
void Obj2World(VB_OBJ OBJ_Buff[], BITMAP *wPlane, int spt_num, int img_n) {
    int i;
    int end = 0;
    BITMAP* tSprt = create_bitmap(8,8);

    if (spt_num > 0) {
        if ((tVIPREG.SPT[spt_num]&0x3FF) >= (tVIPREG.SPT[spt_num-1]&0x3FF)) {
            end = (tVIPREG.SPT[spt_num-1]&0x3FF);
        }
    }

    if (isDsp) { // If were in the Display and not the debug code...
        if (tDSPCACHE.ObjPALMod > 0) { // If cache is invalid
            for (i = 0; i < 4; i++) {
                tDSPCACHE.ObjPAL[i][0]=((tVIPREG.JPLT[i]   )&3)+1; // First collor is transparent, offset by 1
                tDSPCACHE.ObjPAL[i][1]=((tVIPREG.JPLT[i]>>2)&3)+1;
                tDSPCACHE.ObjPAL[i][2]=((tVIPREG.JPLT[i]>>4)&3)+1;
                tDSPCACHE.ObjPAL[i][3]=((tVIPREG.JPLT[i]>>6)&3)+1;
                tDSPCACHE.ObjPAL[i][0]=0; // Fill in the transparent char
            }
            tDSPCACHE.ObjPALMod=0;
        }
    } else { // If in debug land (make this better!)
        for (i = 0; i < 4; i++) {
            tDSPCACHE.ObjPAL[i][0]=0;
            tDSPCACHE.ObjPAL[i][1]=1;
            tDSPCACHE.ObjPAL[i][2]=2;
            tDSPCACHE.ObjPAL[i][3]=3;
        }
    }

    for (i = tVIPREG.SPT[spt_num]&0x3FF; i >= end; i--) { // No!!!
        if ((img_n == 0) && (OBJ_Buff[i].JLON)) { // Default, no paralax
            fchr2sprite(OBJ_Buff[i].JCA, tSprt,OBJ_Buff[i].JHFLP,OBJ_Buff[i].JVFLP,tDSPCACHE.ObjPAL[(OBJ_Buff[i].JPLTS&0x3)]); //Pass in the palet...
            masked_blit(tSprt, wPlane, 0, 0, (OBJ_Buff[i].JX+7), (OBJ_Buff[i].JY+7), 8, 8);
        } else if ((img_n == 1) && (OBJ_Buff[i].JLON)) { // Left Image
            fchr2sprite(OBJ_Buff[i].JCA, tSprt,OBJ_Buff[i].JHFLP,OBJ_Buff[i].JVFLP,tDSPCACHE.ObjPAL[(OBJ_Buff[i].JPLTS&0x3)]); //Pass in the palet...
            masked_blit(tSprt, wPlane, 0, 0, (OBJ_Buff[i].JX+7)-OBJ_Buff[i].JP, (OBJ_Buff[i].JY+7), 8, 8);
        } else if ((img_n == 2) && (OBJ_Buff[i].JRON)) { // Right Immage
            fchr2sprite(OBJ_Buff[i].JCA, tSprt,OBJ_Buff[i].JHFLP,OBJ_Buff[i].JVFLP,tDSPCACHE.ObjPAL[(OBJ_Buff[i].JPLTS&0x3)]); //Pass in the palet...
            masked_blit(tSprt, wPlane, 0, 0, (OBJ_Buff[i].JX+7)+OBJ_Buff[i].JP, (OBJ_Buff[i].JY+7), 8, 8);
        }
    }
    destroy_bitmap(tSprt);
}

// Display the direct screen draws
// Pass in the mem buffer (0-3) and the world bitmap to draw on
// This clears the mem, as it should keep that in mind
// 0-1 left dsp, 2-3 right dsp
void DSP2World(int num, BITMAP *wPlane) {
    HWORD chr;
    int y, x;
    int chr_x = 384+7;
    int chr_y = 256+7;
    WORD offset = (0x00008000*num); // Select Bitmap 0-3, Fix Me
    offset += V810_DISPLAY_RAM.off; // Offset in Phisical Ram

    for (x = 7; x < chr_x; x++) {
        for (y = 7; y < chr_y; y += 8) {
            chr = ((HWORD *)(offset))[0];
            // Display it
            if ((chr >> 0)  & 3) wPlane->line[y+0][x] = ((chr >>  0) & 3)+1;
            if ((chr >> 2)  & 3) wPlane->line[y+1][x] = ((chr >>  2) & 3)+1;
            if ((chr >> 4)  & 3) wPlane->line[y+2][x] = ((chr >>  4) & 3)+1;
            if ((chr >> 6)  & 3) wPlane->line[y+3][x] = ((chr >>  6) & 3)+1;
            if ((chr >> 8)  & 3) wPlane->line[y+4][x] = ((chr >>  8) & 3)+1;
            if ((chr >> 10) & 3) wPlane->line[y+5][x] = ((chr >> 10) & 3)+1;
            if ((chr >> 12) & 3) wPlane->line[y+6][x] = ((chr >> 12) & 3)+1;
            if ((chr >> 14) & 3) wPlane->line[y+7][x] = ((chr >> 14) & 3)+1;

#ifdef FBHACK
            if (tDSPCACHE.DDSPDataWrite) ((HWORD *)(offset))[0] = 0;
#else
            // Clear it, if needed
            if (tVIPREG.XPCTRL&2) ((HWORD *)(offset))[0] = 0;
#endif //FBHACK
            offset+=2;
        }
    }
#ifdef FBHACK
    tDSPCACHE.DDSPDataWrite = 0;
#endif //FBHACK
}

// Returns a WORLD_buf Buffer VB_WORLD WORLD_Buff[32]
// Now directly acesses the video ram (Scary)
void getWorld(HWORD num, VB_WORLD WORLD_Buff[]) {
    WORD tword;

    WORD offset = WORLD_OFFSET + (WORLD_SIZE*(num & 0x01F)); // only 32 posible worlds...
    offset += V810_DISPLAY_RAM.off; //Offset in Phisical Ram

    tword = ((HWORD *)(offset))[0];
    WORLD_Buff[num].LON = ((tword >> 15) & 0x01);
    WORLD_Buff[num].RON = ((tword >> 14) & 0x01);
    WORLD_Buff[num].BGM = ((tword >> 12) & 0x03);
    WORLD_Buff[num].SCX = ((tword >> 10) & 0x03);
    WORLD_Buff[num].SCY = ((tword >>  8) & 0x03);
    WORLD_Buff[num].OVER= ((tword >>  7) & 0x01);
    WORLD_Buff[num].END = ((tword >>  6) & 0x01);
    WORLD_Buff[num].BGMAP_BASE =  (tword & 0x0F);

    //Are negative values allowed in displaying objects????
    //YES!
    WORLD_Buff[num].GX = (int)sign_16(((HWORD *)(offset))[1]);
    WORLD_Buff[num].GP = (int)sign_16(((HWORD *)(offset))[2]);
    WORLD_Buff[num].GY = (int)sign_16(((HWORD *)(offset))[3]);

    WORLD_Buff[num].MX = (int)sign_16(((HWORD *)(offset))[4]);
    WORLD_Buff[num].MP = (int)sign_16(((HWORD *)(offset))[5]);
    WORLD_Buff[num].MY = (int)sign_16(((HWORD *)(offset))[6]);

    WORLD_Buff[num].PARAM_BASE = (((HWORD *)(offset))[9]);
    WORLD_Buff[num].OVERP_CHR = ((HWORD *)(offset))[10];

    WORLD_Buff[num].W = (((HWORD *)(offset))[7]+1);
    WORLD_Buff[num].H = (((HWORD *)(offset))[8]+1);
}

////////////////////////////////////////////////////////////////////
// Render a Display Screen on a Screen Bitmap, pass in an array
// of type World Obj.
void World2Display(int wNum, VB_WORLD WORLD_Buff[], BITMAP *wPlane, int img_n) {
    int t;
    int bgm;
    int curscr;
    int ty, tx, ny, nx;
    WORD offset;
    int GPX = 0; // Global Paralax setings...
    int MPX = 0;
    int XSrc, Prlx, YSrc, YSkw; // for affine
    float XScl;                 // for affine

    // Kill it if were trying to display the wrong screen type...
    if (((!img_n)||(img_n == 1))&&(!WORLD_Buff[wNum].LON))
        return;
    if ((img_n == 2)&&(!WORLD_Buff[wNum].RON))
        return;

    if (img_n == 1) {
        GPX = -WORLD_Buff[wNum].GP; // Global Paralax setings...
        MPX = -WORLD_Buff[wNum].MP;
    } else if (img_n == 2) {
        GPX = WORLD_Buff[wNum].GP; // Global Paralax setings...
        MPX = WORLD_Buff[wNum].MP;
    }

    if (WORLD_Buff[wNum].BGM == 3) {  // Obj
        if (tDSPCACHE.ObjDataCacheInvalid==1) { // Cash the Obj Info...
            vGetAllObjects(tDSPCACHE.ObjDataCache);
            tDSPCACHE.ObjDataCacheInvalid = 0;
        }
        // Dont mess around with sub bitmaps, just blast it to the world plane
        Obj2World(tDSPCACHE.ObjDataCache, wPlane,CurObj,img_n);
        CurObj = (CurObj-1)&3; // (CurObj-1)%4;
    } else {
        // Grab the BGMaps, we can have several so grab them all...
        bgm = WORLD_Buff[wNum].BGMAP_BASE;
        nx = (1<<WORLD_Buff[wNum].SCX);
        ny = (1<<WORLD_Buff[wNum].SCY);
        for (ty = 0; ty < ny; ty++) { // 1,2,4,8 bgmaps in the y dir
            for (tx = 0; tx < nx; tx++) { //1,2,4,8 bgmaps in the x dir
                curscr = tx + (ty*nx);
                if (tDSPCACHE.BGCacheInvalid[bgm+curscr] == 1) {
                    BGMap2World(bgm+curscr, tDSPCACHE.BGCacheBMP[bgm+curscr]);
                    tDSPCACHE.BGCacheInvalid[bgm+curscr] = 0;
                }
            }
        }

        if (WORLD_Buff[wNum].BGM == 2) {  // Affine BGMap (Incomplete!)
            offset = ((WORLD_Buff[wNum].PARAM_BASE*2)+BGMAP_OFFSET )&0xFFFFFFFE; //BGMAP_OFFSET
            for (t = 0; t < WORLD_Buff[wNum].H; t++) { //Factor in the Shift Index....
                if ((XScl = (int)sign_16((((HWORD *)(offset + 6 + V810_DISPLAY_RAM.off))[0])&0xFFFF)) != 0) {
                    XSrc = (int)(sign_16((((HWORD *)(offset + V810_DISPLAY_RAM.off))[0])&0xFFFF)>>3);
                    Prlx = (int)sign_16((((HWORD *)(offset + 2 + V810_DISPLAY_RAM.off))[0])&0xFFFF);
                    YSrc = (int)(sign_16((((HWORD *)(offset + 4 + V810_DISPLAY_RAM.off))[0])&0xFFFF)>>3);
                    YSkw = (int)sign_16((((HWORD *)(offset + 8 + V810_DISPLAY_RAM.off))[0])&0xFFFF);
                    XScl = (512.0 / XScl);

                    // FIXME! parallax is not 100%

                    //XSrc = (int)(XSrc / XScl);
                    //YSrc = (int)(YSrc / XScl);
                    //Prlx = (int)(Prlx / XScl); //almost works

                    if (img_n == 2) Prlx = -Prlx; //fix parallax for right screen

                    affine_blit(&tDSPCACHE.BGCacheBMP[bgm], wPlane, XSrc/*+Prlx*/, YSrc, 7+WORLD_Buff[wNum].GX+GPX+Prlx, 7+WORLD_Buff[wNum].GY+t, WORLD_Buff[wNum].W, WORLD_Buff[wNum].SCX, WORLD_Buff[wNum].SCY, XScl, YSkw);
                    offset += 16;
                }
            }


        } else if (WORLD_Buff[wNum].BGM == 1) {  // H-Biased BGMap
            offset = (WORLD_Buff[wNum].PARAM_BASE*2)+BGMAP_OFFSET + V810_DISPLAY_RAM.off;
            if (img_n==2)
                offset += 2; // Shift by 2 if right screen (Read the right offset table, instead of left)

            for (t = 0; t < WORLD_Buff[wNum].H; t ++) { // Factor in the Shift Index....
                masked_blit(tDSPCACHE.BGCacheBMP[bgm], wPlane, WORLD_Buff[wNum].MX+MPX+(((short *)(offset))[(t<<1)]), WORLD_Buff[wNum].MY+t, 7+WORLD_Buff[wNum].GX+GPX, 7+WORLD_Buff[wNum].GY+t, WORLD_Buff[wNum].W, 1);
            }
        } else {  // Normal BGMap
            dt_blit(&tDSPCACHE.BGCacheBMP[bgm], wPlane, WORLD_Buff[wNum].MX+MPX, WORLD_Buff[wNum].MY, 7+WORLD_Buff[wNum].GX+GPX, 7+WORLD_Buff[wNum].GY, WORLD_Buff[wNum].W, WORLD_Buff[wNum].H,WORLD_Buff[wNum].SCX,WORLD_Buff[wNum].SCY);
        }
    }
}

////////////////////////////////////////////////////////////////////
//Initialize the display, and get the video mode (from wherever)
bool V810_DSP_Init() {
    int i;

    for(i = 0; i < 14; i++) {
        tDSPCACHE.BGCacheBMP[i] = create_bitmap(512, 512); // Create our temp Bitmap...
    }
    world_bmp = create_bitmap(512+8,512+8); // Make them a bit bigger for the Obj's
    world_bmp2 = create_bitmap(384+8, 224+8);
    dsp_bmp = create_bitmap(384*2, 224*2);
    for(i = 0; i < 4; i++) {
        tDSPCACHE.ObjCacheBMP[i] = create_bitmap(512,512); // Create our temp Bitmap...
    }

    clear_to_color(world_bmp,(tVIPREG.BKCOL & 0x3) + 1);    // zero the memory bitmap
    clear_to_color(world_bmp2,(tVIPREG.BKCOL & 0x3) + 1);   // zero the memory bitmap
    clear_to_color(dsp_bmp,(tVIPREG.BKCOL & 0x3) + 1);      // zero the memory bitmap

    return true;
}

void V810_SetPal(int BRTA, int BRTB, int BRTC) {
    int i;
    int tPal[] = {0, 0, 21, 42, 63};

    if (!tVBOpt.FIXPAL) {
        tPal[2] = BRTA;
        tPal[3] = BRTB;
        tPal[4] = BRTC;
        if (tPal[4] > 63) tPal[4]=63;
        if (tPal[3] > 63) tPal[3]=63;
        if (tPal[2] > 63) tPal[2]=63;
    }

    if (tVBOpt.PALMODE == PAL_RED) { // Red Pallet
        for(i = 0; i < 5; i++) {
            pallete[i].r = tPal[i];
            pallete[i].g = 0;
            pallete[i].b = 0;
        }
    } else { // Standard Pallet
        for (i = 0; i < 5; i++) {
            pallete[i].r = tPal[i];
            pallete[i].g = tPal[i];
            pallete[i].b = tPal[i];
        }
    }

    // Standard text color
    pallete[252].r = 63;
    pallete[252].g = 63;
    pallete[252].b = 63;
}


void V810_DSP_Quit() {
    int i = 0;

    for (i = 0; i < 14; i++) {
        destroy_bitmap(tDSPCACHE.BGCacheBMP[i]);
    }
    destroy_bitmap(world_bmp);
    destroy_bitmap(world_bmp2);
    destroy_bitmap(dsp_bmp);
    for (i = 0; i < 4; i++) {
        destroy_bitmap(tDSPCACHE.ObjCacheBMP[i]);
    }
}

// Display one frame of graphics...
void V810_Dsp_Frame(int dNum) {
    VB_WORLD WORLD_Buff[32];
    int i;
    int T_X,T_Y;
    int tObj = 0;

    T_X = tVBOpt.SCR_X;
    T_Y = tVBOpt.SCR_Y;
    if (T_X > 384)
        T_X = 384;
    if (T_Y > 224)
        T_Y = 224;
    isDsp = 1; // Secret flag...
    CurObj = 3;

#ifdef FBHACK
    //FIX ME!
    //dNum = (tVIPREG.tFrame & 1);
    dNum = (tVIPREG.tFrame - 1);
#endif // FBHACK

    // Normalize the Pallete, Is this to slow??? (tVIPREG.BRTA*64)/MaxBrt
    if (tDSPCACHE.BrtPALMod > 0) { //If pallet changed
        V810_SetPal((tVIPREG.BRTA&0xFF)/2, (tVIPREG.BRTB&0xFF)/2, ((tVIPREG.BRTA&0xFF)+(tVIPREG.BRTB&0xFF)+(tVIPREG.BRTC&0xFF))/2);
        tDSPCACHE.BrtPALMod = 0;
    }

    if (tVBOpt.DSPMODE == DM_NORMAL) {  // Normal
        clear_to_color(world_bmp,(tVIPREG.BKCOL&0x3)+1); // Zero the memory bitmap
        for (i = 31; i >= 0; i--) {
            getWorld(i,WORLD_Buff);
            if (WORLD_Buff[i].END)
                break; // Here? or farther down...
            World2Display(i, WORLD_Buff, world_bmp,0);
        }

        DSP2World(dNum, world_bmp);
    } else { // 3D Mode...
        clear_to_color(world_bmp,(tVIPREG.BKCOL&0x3)+1);  // zero the memory bitmap
        clear_to_color(world_bmp2,(tVIPREG.BKCOL&0x3)+1); // zero the memory bitmap
        for (i = 31; i >= 0; i--) {
            getWorld(i,WORLD_Buff);
            if (WORLD_Buff[i].END)
                break; // Here? or farther down...
            tObj = CurObj; // Save Curent Obj
            World2Display(i, WORLD_Buff, world_bmp,1+tVBOpt.DSPSWAP);
            CurObj = tObj; // Reset it
            World2Display(i, WORLD_Buff, world_bmp2,2-tVBOpt.DSPSWAP);
        }

        DSP2World((dNum&1), world_bmp);
        DSP2World((dNum&1)+2, world_bmp2);

        screen_blit(world_bmp2, 7, 7, GFX_RIGHT);
    }
    screen_blit(world_bmp, 7, 7, GFX_LEFT);

    isDsp = 0; // Secret flag...
}

void clearCache() {
    int i;
    tDSPCACHE.BgmPALMod = 1;                // World Pallet Changed
    tDSPCACHE.ObjPALMod = 1;                // Obj Pallet Changed
    tDSPCACHE.BrtPALMod = 1;                // Britness for Pallet Changed
    tDSPCACHE.ObjDataCacheInvalid = 1;      // Object Cache Is invalid
    tDSPCACHE.ObjCacheInvalid = 1;          // Object Cache Is invalid
    for(i = 0; i < 14; i++)
        tDSPCACHE.BGCacheInvalid[i] = 1;    // Object Cache Is invalid
    tDSPCACHE.DDSPDataWrite = 1;            // Direct Screen Draw changed
}
