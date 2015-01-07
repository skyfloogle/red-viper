#include <malloc.h>
#include <string.h>
#include "allegro_compat.h"

void masked_blit(BITMAP *src, BITMAP *dst, int src_x, int src_y, int dst_x, int dst_y, int w, int h) {
    int x, y;

    for (y = 0; y < h; y++) { 
        for (x = 0; x < w; x++) { 
            if (src->line[(src_y+y)%(src->h)][(src_x+x)%(src->w)]) {
                dst->line[(dst_y+y)%(dst->h)][(dst_x+x)%(dst->w)] = src->line[(src_y+y)%(src->h)][(src_x+x)%(src->w)];
            }

        }
    }
}

void masked_stretch_blit(BITMAP *src, BITMAP *dst, int src_x, int src_y, int src_w, int src_h, int dst_x, int dst_y, int dst_w, int dst_h) {
    int x, y, xpos, ypos;
    float xratio = ((float) src_w) / ((float) dst_w);
    float yratio = ((float) src_h) / ((float) dst_h);

    for (y = 0; y < dst_h; y++) { 
        for (x = 0; x < dst_w; x++) { 
            xpos = ((int) ((x + src_x) * xratio)) % src->w;
            ypos = ((int) ((y + src_y) * yratio)) % src->h;
            if (src->line[ypos][xpos]) {
                dst->line[(dst_y+y)%dst->h][(dst_x+x)%dst->w] = src->line[ypos][xpos];
            }
        }
    }
}


BITMAP *create_bitmap(int w, int h) {
// From Allegro - NOP90
   int nr_pointers;
   int padding;
   int i;

   /* We need at least two pointers when drawing, otherwise we get crashes with
    * Electric Fence.  We think some of the assembly code assumes a second line
    * pointer is always available.
    */
   nr_pointers = ((h>2) ? h : 2) ;

   /* padding avoids a crash for assembler code accessing the last pixel, as it
    * read 4 bytes instead of 3.
    */
   padding = 1;

// End of part added from Allegto - NOP90

   BITMAP *bm = (BITMAP*)malloc(sizeof(BITMAP) + (sizeof(char *) * nr_pointers)); // Mod by NOP90 according to Allegro source

    bm->w = w;
    bm->h = h;

    bm->line = malloc(h*sizeof(uint8_t*) + padding);

    bm->dat = malloc(w * h * sizeof(char));
    if (h > 0) {
        bm->line[0] = bm->dat;
        for (i = 1; i < h; i++)
            bm->line[i] = bm->line[i-1] + w;
    }

    return bm;
}

void destroy_bitmap(BITMAP *bitmap) {
    if (bitmap) {
        if (bitmap->dat)
            free(bitmap->dat);
        free(bitmap->line);
        free(bitmap);
    }
}

void clear_to_color(BITMAP *bitmap, int color) {
    if (bitmap) {
        memset(bitmap->dat, color, (bitmap->w)*(bitmap->h));
    }
}
