#ifndef ALLEGRO_COMPAT_H_
#define ALLEGRO_COMPAT_H_

#include <stdint.h>

// A reimplementation of some allegro functions and structures
// Hopefully we won't need it in the future

typedef struct BITMAP {          /* a bitmap structure */
   int w, h;                     /* width and height in pixels */
   void *dat;                    /* the memory we allocated for the bitmap */
   unsigned char **line;
} BITMAP;

typedef struct {
    unsigned char r, g, b;
} RGB;

void masked_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height);
void masked_stretch_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int source_w, int source_h, int dest_x, int dest_y, int dest_w, int dest_h);

BITMAP *create_bitmap(int w, int h);
void destroy_bitmap(BITMAP *bitmap);
BITMAP *create_sub_bitmap(BITMAP *parent, int x, int y, int width, int height);
void clear_to_color(BITMAP *bitmap, int color);

#endif
