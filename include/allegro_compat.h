#ifndef ALLEGRO_COMPAT_H_
#define ALLEGRO_COMPAT_H_

#include <stdint.h>
#include <stdbool.h>

// A reimplementation of some allegro functions and structures
// Hopefully we won't need it in the future

#define D_DISABLED 0
#define D_OK 0
#define D_EXIT 1

#define PLAYMODE_PLAY       0
#define PLAYMODE_LOOP       1
#define PLAYMODE_FORWARD    0
#define PLAYMODE_BACKWARD   2
#define PLAYMODE_BIDIR      4

#define DIGI_AUTODETECT     -1 // For passing to install_sound()
#define DIGI_NONE           0

#define MIDI_AUTODETECT     -1
#define MIDI_NONE           0

// A bitmap structure
typedef struct {
   int w, h;    // Width and height in pixels
   void *dat;   // The memory we allocated for the bitmap
   unsigned char **line;
} BITMAP;

typedef struct {
    unsigned char r, g, b;
} RGB;

typedef struct {
    int bits;                   // 8 or 16
    int stereo;                 // Sample type flag
    int freq;                   // Sample frequency
    int priority;               // 0-255
    unsigned long len;          // Length (in samples)
    unsigned long loop_start;   // Loop start position
    unsigned long loop_end;     // Loop finish position
    unsigned long param;        // For internal use by the driver
    void* data;                 // Raw sample data
} SAMPLE;

void masked_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height);
void masked_stretch_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int source_w, int source_h, int dest_x, int dest_y, int dest_w, int dest_h);

BITMAP *create_bitmap(int w, int h);
void destroy_bitmap(BITMAP *bitmap);
void clear_to_color(BITMAP *bitmap, int color);

int install_sound(int digi, int midi, const char *config_path);
void remove_sound();
SAMPLE* create_sample(int bits, int stereo, int freq, int len);
void destroy_sample(SAMPLE* spl);
int allocate_voice(SAMPLE* spl);
void voice_set_playmode(int voice, int playmode);
void voice_stop(int voice);
void deallocate_voice(int voice);
void voice_sweep_frequency(int voice, int time, int endfreq);
void voice_set_position(int voice, int position);
void voice_start(int voice);
void voice_set_volume(int voice, int volume);
int voice_get_volume(int voice);
void voice_set_pan(int voice, int pan);
void voice_ramp_volume(int voice, int time, int endvol);
void voice_set_frequency(int voice, int frequency);
int voice_get_frequency(int voice);

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
    const char* title;            // The title of the menu
    struct _menu_s* parent; // The previous menu
    int numitems;           // The number of items in the menu
    struct _menu_item_s* items;
} menu_t;

int openMenu(menu_t* menu);
// Similar to file_select_ex in allegro
int fileSelect(const char* message, char* path, const char* ext);

#endif
