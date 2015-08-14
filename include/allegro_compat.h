#ifndef ALLEGRO_COMPAT_H_
#define ALLEGRO_COMPAT_H_

#include <stdint.h>

// A reimplementation of some allegro functions and structures
// Hopefully we won't need it in the future

#define D_DISABLED 0
#define D_OK 0
#define D_EXIT 1

typedef struct BITMAP {          // A bitmap structure
   int w, h;                     // Width and height in pixels
   void *dat;                    // The memory we allocated for the bitmap
   unsigned char **line;
} BITMAP;

typedef struct {
    unsigned char r, g, b;
} RGB;

void masked_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height);
void masked_stretch_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int source_w, int source_h, int dest_x, int dest_y, int dest_w, int dest_h);

BITMAP *create_bitmap(int w, int h);
void destroy_bitmap(BITMAP *bitmap);
void clear_to_color(BITMAP *bitmap, int color);

// Not exactly allegro stuff, but kinda compatible.
// menu_item_t is exactly the same as a MENU struct in allegro, but it's used
// differently.
typedef struct _menu_item_s {
    char* text;             // The text to display for the menu item
    int (*proc)(void);      // Called when the menu item is clicked
    struct _menu_s* child;  // Nested child menu
    int flags;              // Unused for now
    void* dp;               // Unused for now
} menu_item_t;

typedef struct _menu_s {
    char* title;            // The title of the menu
    struct _menu_s* parent; // The previous menu
    int numitems;           // The number of items in the menu
    struct _menu_item_s* items;
} menu_t;

int openMenu(menu_t* menu);
// Similar to file_select_ex in allegro
int fileSelect(const char* message, char* path, const char* ext);

#endif
