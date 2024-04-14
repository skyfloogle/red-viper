#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <citro2d.h>
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_gui.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_types.h"
#include "replay.h"
#include "main.h"
#include "utils.h"
#include "periodic.h"
#include "sprites_t3x.h"
#include "sprites.h"

#define TINT_R ( (tVBOpt.TINT & 0x000000FF) )
#define TINT_G ( (tVBOpt.TINT & 0x0000FF00) >> 8 )
#define TINT_B ( (tVBOpt.TINT & 0x00FF0000) >> 16 )

static bool buttons_on_screen = false;
void setTouchControls(bool button);
bool guiShouldSwitch();
void drawTouchControls(int inputs);

static C3D_RenderTarget *screen;

static C2D_TextBuf static_textbuf;
static C2D_TextBuf dynamic_textbuf;

static C2D_Text text_A, text_B, text_btn_A, text_btn_B, text_btn_X, text_btn_L, text_btn_R,
                text_switch, text_saving, text_on, text_off, text_toggle, text_hold, text_3ds,
                text_vbipd, text_left, text_right, text_sound_error, text_anykeyexit, text_about,
                text_debug_filenames, text_loading, text_loaderr, text_unloaded, text_yes, text_no,
                text_areyousure_reset, text_areyousure_exit;

static C2D_SpriteSheet sprite_sheet;
static C2D_Sprite colour_wheel_sprite, logo_sprite;

// helpers
#define dis(X1,Y1,X2,Y2) ( (((X2)-(X1)) * ((X2)-(X1))) + (((Y2)-(Y1)) * ((Y2)-(Y1))) )

static inline int sqr(int i) {
    return i * i;
}

static inline int u16len(const u16 *s) {
    const u16 *e = s;
    while (*e) e++;
    return e - s;
}

typedef struct Button_t Button;
struct Button_t {
    char *str;
    float x, y, w, h;
    bool show_toggle, toggle, hidden;
    C2D_Text *toggle_text_on;
    C2D_Text *toggle_text_off;
    C2D_Text text;
    void (*custom_draw)(Button*);
};

static Button* selectedButton = NULL;
static bool buttonLock = false;

static inline int handle_buttons(Button buttons[], int count);
#define HANDLE_BUTTONS(buttons) handle_buttons(buttons, sizeof(buttons) / sizeof(buttons[0]))

#define STATIC_TEXT(var, str) {\
    C2D_TextParse(var, static_textbuf, str); \
    C2D_TextOptimize(var); \
}

#define SETUP_BUTTONS(arr) \
    for (int i = 0; i < sizeof(arr) / sizeof(arr[0]); i++) { \
        if (arr[i].str) { \
            STATIC_TEXT(&arr[i].text, arr[i].str ? arr[i].str : ""); \
        } \
    }

#define LOOP_BEGIN(buttons, initial_button) \
    if (initial_button >= 0) selectedButton = &buttons[initial_button]; \
    int button = -1; \
    bool loop = true; \
    while (loop && aptMainLoop()) { \
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW); \
        C2D_TargetClear(screen, 0); \
        video_flush(true); \
        C2D_SceneBegin(screen); \
        C2D_Prepare(); \
        hidScanInput(); \

#define DEFAULT_RETURN

#define LOOP_END(buttons) \
        button = HANDLE_BUTTONS(buttons); \
        if (button >= 0) loop = false; \
        C2D_Flush(); \
        C3D_FrameEnd(0); \
    } \
    if (loop) { \
        /* home menu exit */ \
        guiop = GUIEXIT; \
        return DEFAULT_RETURN; \
    }

static void first_menu(int initial_button);
static Button first_menu_buttons[] = {
    {.str="Load ROM", .x=16, .y=16, .w=288, .h=144},
    {.str="Controls", .x=0, .y=176, .w=80, .h=64},
    {.str="Options", .x=240, .y=176, .w=80, .h=64},
    {.str="Quit", .x=112, .y=192, .w=96, .h=48},
};

static void game_menu(int initial_button);
static Button game_menu_buttons[] = {
    #define MAIN_MENU_LOAD_ROM 0
    {.str="Load ROM", .x=224 - 48, .y=64, .w=80 + 48, .h=80},
    #define MAIN_MENU_CONTROLS 1
    {.str="Controls", .x=0, .y=176, .w=80, .h=64},
    #define MAIN_MENU_OPTIONS 2
    {.str="Options", .x=240, .y=176, .w=80, .h=64},
    #define MAIN_MENU_QUIT 3
    {.str="Quit", .x=112, .y=192, .w=96, .h=48},
    #define MAIN_MENU_RESUME 4
    {.str="Resume", .x=0, .y=0, .w=320, .h=48},
    #define MAIN_MENU_RESET 5
    {.str="Reset", .x=16, .y=64, .w=80 + 48, .h=80},
};

static void rom_loader();
static Button rom_loader_buttons[] = {
    #define ROM_LOADER_UP 0
    {.str="Up", .x=0, .y=0, .w=32, .h=32},
    #define ROM_LOADER_BACK 1
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void draw_abxy(Button*);
static void draw_shoulders(Button*);
static void controls(int initial_button);
static Button controls_buttons[] = {
    #define CONTROLS_FACE 0
    // y is set later
    {.x=160-64, .w=128, .h=80, .custom_draw=draw_abxy},
    #define CONTROLS_SHOULDER 1
    {.x=160-64, .y=0, .w=128, .h=40, .custom_draw=draw_shoulders},
    #define CONTROLS_TOUCHSCREEN 2
    {.str="Touchscreen\nsettings", .x=96, .y=144, .w=128, .h=64},
    #define CONTROLS_BACK 3
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void touchscreen_settings();
static Button touchscreen_settings_buttons[] = {
    #define TOUCHSCREEN_BACK 0
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define TOUCHSCREEN_RESET 1
    {.str="Reset", .x=0, .y=0, .w=48, .h=32},
};

static void options(int initial_button);
static Button options_buttons[] = {
    #define OPTIONS_VIDEO 0
    {.str="Video settings", .x=16, .y=16, .w=288, .h=48},
    #define OPTIONS_FF 1
    {.str="Fast forward", .x=16, .y=80, .w=128, .h=48, .show_toggle=true, .toggle_text_on=&text_toggle, .toggle_text_off=&text_hold},
    #define OPTIONS_SOUND 2
    {.str="Sound", .x=176, .y=80, .w=128, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define OPTIONS_DEV 3
    {.str="Dev settings", .x=16, .y=144, .w=128, .h=48},
    #define OPTIONS_ABOUT 4
    {.str="About", .x=176, .y=144, .w=128, .h=48},
    #define OPTIONS_BACK 5
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define OPTIONS_DEBUG 6
    {.str="Save debug info", .x=170, .y=208, .w=150, .h=32},
};

static void video_settings(int initial_button);
static Button video_settings_buttons[] = {
    #define VIDEO_COLOUR 0
    {.str="Color mode", .x=16, .y=16, .w=288, .h=48},
    #define VIDEO_SLIDER 1
    {.str="Slider mode", .x=16, .y=80, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_vbipd, .toggle_text_off=&text_3ds},
    #define VIDEO_DEFAULT_EYE 2
    {.str="Default eye", .x=16, .y=144, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_right, .toggle_text_off=&text_left},
    #define VIDEO_BACK 3
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void colour_filter();
static Button colour_filter_buttons[] = {
    #define COLOUR_BACK 0
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define COLOUR_RED 1
    {.str="Red", .x=16, .y=64, .w=48, .h=32},
    #define COLOUR_GRAY 2
    {.str="Gray", .x=16, .y=128, .w=48, .h=32},
};

static void dev_options(int initial_button);
static Button dev_options_buttons[] = {
    #define DEV_PERF 0
    {.str="Perf. info", .x=16, .y=16, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define DEV_N3DS 1
    {.str="N3DS speedup", .x=16, .y=80, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define DEV_BACK 2
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static bool areyousure(C2D_Text *message);
static Button areyousure_buttons[] = {
    #define AREYOUSURE_YES 0
    {"Yes", .x=160-48-32, .y=180, .w=64, .h=48},
    #define AREYOUSURE_NO 1
    {"No", .x=160+32, .y=180, .w=64, .h=48},
};

static void sound_error();
static Button sound_error_buttons[] = {
    {.str="Continue without sound", .x=48, .y=130, .w=320-48*2, .h=32},
};

static void about();
static Button about_buttons[] = {
    {"Back", .x=160-48, .y=180, .w=48*2, .h=48},
};

static void load_rom();
static Button load_rom_buttons[] = {
    {"Unload & cancel", .x=160-80, .y=180, .w=80*2, .h=48},
};

#define SETUP_ALL_BUTTONS \
    SETUP_BUTTONS(first_menu_buttons); \
    SETUP_BUTTONS(game_menu_buttons); \
    SETUP_BUTTONS(rom_loader_buttons); \
    SETUP_BUTTONS(controls_buttons); \
    SETUP_BUTTONS(options_buttons); \
    SETUP_BUTTONS(video_settings_buttons); \
    SETUP_BUTTONS(colour_filter_buttons); \
    SETUP_BUTTONS(dev_options_buttons); \
    SETUP_BUTTONS(sound_error_buttons); \
    SETUP_BUTTONS(touchscreen_settings_buttons); \
    SETUP_BUTTONS(about_buttons); \
    SETUP_BUTTONS(load_rom_buttons); \
    SETUP_BUTTONS(areyousure_buttons);

static void draw_logo() {
    C2D_SceneBegin(screenTarget);
    C2D_ViewScale(1, -1);
    C2D_ViewTranslate(0, -512);
    C2D_SpriteSetPos(&logo_sprite, 384 / 2 - 2, 224 / 2);
    C2D_DrawSprite(&logo_sprite);
    C2D_SpriteSetPos(&logo_sprite, 384 / 2 + 2, 224 / 2 + 256);
    C2D_DrawSprite(&logo_sprite);
    C2D_ViewReset();
    C2D_SceneBegin(screen);
}

static void first_menu(int initial_button) {
    LOOP_BEGIN(first_menu_buttons, initial_button);
        draw_logo();
    LOOP_END(first_menu_buttons);
    guiop = 0;
    switch (button) {
        case MAIN_MENU_LOAD_ROM:
            return rom_loader();
        case MAIN_MENU_CONTROLS:
            return controls(0);
        case MAIN_MENU_OPTIONS:
            return options(0);
        case MAIN_MENU_QUIT: // Quit
            if (areyousure(&text_areyousure_exit)) {
            guiop = GUIEXIT;
            return;
            } else return first_menu(MAIN_MENU_QUIT);
    }
}

static void game_menu(int initial_button) {
    LOOP_BEGIN(game_menu_buttons, initial_button);
    LOOP_END(game_menu_buttons);
    switch (button) {
        case MAIN_MENU_LOAD_ROM: // Load ROM
            guiop = AKILL | VBRESET;
            return rom_loader();
        case MAIN_MENU_CONTROLS: // Controls
            return controls(0);
        case MAIN_MENU_OPTIONS: // Options
            return options(0);
        case MAIN_MENU_QUIT: // Quit
            if (areyousure(&text_areyousure_exit)) {
                guiop = AKILL | GUIEXIT;
                return;
            } else return game_menu(MAIN_MENU_QUIT);
        case MAIN_MENU_RESUME: // Resume
            guiop = 0;
            return;
        case MAIN_MENU_RESET: // Reset
            if (areyousure(&text_areyousure_reset)) {
                guiop = AKILL | VBRESET;
                return;
            } else return game_menu(MAIN_MENU_RESET);
    }
}

static void main_menu(int initial_button) {
    if (game_running) game_menu(initial_button);
    else first_menu(initial_button);
}

int strptrcmp(const void *s1, const void *s2) {
    return strcasecmp(*(const char**)s1, *(const char**)s2);
}

static void rom_loader() {
    static char *path = NULL;
    static int path_cap = 128;
    if (!path) {
        if (tVBOpt.ROM_PATH) {
            int path_len = strlen(tVBOpt.ROM_PATH);
            while (path_cap <= path_len) path_cap *= 2;
            path = malloc(path_cap);
            strcpy(path, tVBOpt.ROM_PATH);
        } else {
            path = calloc(path_cap, 1);
        }
    }

    // in case we broke it somehow
    if (strlen(path) < 6) {
        strcpy(path, "sdmc:/");
    }

    DIR *dirHandle;
    // cut filename from path if we reload mid-game
    // also go up a directory if we can't load this directory
    do {
        strrchr(path, '/')[1] = 0;
        dirHandle = opendir(path);
        if (!dirHandle) path[strlen(path) - 1] = 0;
    } while (!dirHandle && strlen(path) > 6);

    if (!dirHandle) {
        // TODO error
        guiop = 0;
        return;
    }

    char **dirs = malloc(8 * sizeof(char*));
    char **files = malloc(8 * sizeof(char*));
    int dirCount = 0, fileCount = 0;
    int dirCap = 8, fileCap = 8;

    struct dirent *dp;

    // read .vb files and directories
    while ((dp = readdir(dirHandle))) {
        archive_dir_t* dirSt = (archive_dir_t*)dirHandle->dirData->dirStruct;
        FS_DirectoryEntry *thisEntry = &dirSt->entry_data[dirSt->index];
        if ((thisEntry->attributes & FS_ATTRIBUTE_HIDDEN) || dp->d_name[0] == '.')
            continue;
        if (thisEntry->attributes & FS_ATTRIBUTE_DIRECTORY) {
            if (dirCount == dirCap) {
                dirCap *= 2;
                dirs = realloc(dirs, dirCap * sizeof(char*));
            }
            int len = strlen(dp->d_name) + 1;
            dirs[dirCount] = malloc(len);
            memcpy(dirs[dirCount++], dp->d_name, len);
        } else {
            // check the file extension
            char *dot = strrchr(dp->d_name, '.');
            if (dot && (strcasecmp(dot, ".vb") == 0 || strcasecmp(dot, ".zip") == 0)) {
                if (fileCount == fileCap) {
                    fileCap *= 2;
                    files = realloc(files, fileCap * sizeof(char*));
                }
                int len = strlen(dp->d_name) + 1;
                files[fileCount] = malloc(len);
                memcpy(files[fileCount++], dp->d_name, len);
            }
        }
    }

    closedir(dirHandle);

    qsort(files, fileCount, sizeof(char*), strptrcmp);
    qsort(dirs, dirCount, sizeof(char*), strptrcmp);

    int entry_count = dirCount + fileCount;
    const float entry_height = 32;
    float scroll_top = -entry_height;
    float scroll_bottom = entry_count * entry_height - 240;
    if (scroll_bottom < scroll_top) scroll_bottom = scroll_top;
    float scroll_pos = scroll_top;
    float scroll_speed = 0;
    int cursor = 0;

    if (tVBOpt.ROM_PATH && strstr(tVBOpt.ROM_PATH, path) == tVBOpt.ROM_PATH) {
        char *filename = strrchr(tVBOpt.ROM_PATH, '/');
        // null check but also skip the slash
        if (filename++) {
            for (int i = 0; i < fileCount; i++) {
                if (strcmp(files[i], filename) == 0) {
                    int button_y = i * entry_height;
                    scroll_pos = C2D_Clamp(button_y - (240 / 2), scroll_top, scroll_bottom);
                    cursor = i;
                }
            }
        }
    }

    buttonLock = true;
    int last_py = 0;
    int clicked_entry = -1;
    bool dragging = false;
    // Delayed Auto Shift
    int das_start = 20;
    int xdas_length = 0, xdas_time = 0, xdas_count = 0;
    int ydas_length = 0, ydas_time = 0, ydas_count = 0;

    LOOP_BEGIN(rom_loader_buttons, -1);
        // process rom list
        touchPosition touch_pos;
        hidTouchRead(&touch_pos);
        if ((hidKeysDown() & KEY_TOUCH) && touch_pos.px >= 48) {
            last_py = touch_pos.py;
            clicked_entry = floorf((touch_pos.py + scroll_pos) / entry_height);
            if (clicked_entry < 0 || clicked_entry >= entry_count)
                clicked_entry = -1;
            else
                cursor = clicked_entry;
            dragging = false;
            scroll_speed = 0;
        } else if (clicked_entry >= 0 && (hidKeysHeld() & KEY_TOUCH)) {
            if (!dragging && abs(touch_pos.py - last_py) >= 3) {
                clicked_entry = -1;
                dragging = true;
            }
        }
        if (dragging) {
            if (!(hidKeysHeld() & KEY_TOUCH)) {
                dragging = false;
            } else {
                // negated
                scroll_speed = -(touch_pos.py - last_py);
                last_py = touch_pos.py;
            }
        } else if (clicked_entry >= 0) {
            if (!(hidKeysHeld() & KEY_TOUCH)) {
                bool clicked_dir = clicked_entry < dirCount;
                const char *new_entry = clicked_dir ? dirs[clicked_entry] : files[clicked_entry - dirCount];
                int new_path_len = strlen(path) + strlen(new_entry) + clicked_dir;
                if (new_path_len + 2 > path_cap) {
                    while (new_path_len + 2 > path_cap)
                        path_cap *= 2;
                    path = realloc(path, path_cap);
                }
                strcat(path, new_entry);
                if (clicked_entry < dirCount) {
                    // clicked on directory, so add a slash
                    path[new_path_len - 1] = '/';
                    path[new_path_len] = 0;
                }
                loop = false;
            }
        } else if (scroll_speed != 0) {
            scroll_speed += scroll_speed > 0 ? -1 : 1;
            if (abs(scroll_speed) < 1) scroll_speed = 0;
        }
        // inertia
        int top_pos = scroll_top;
        if (dragging) {
            top_pos -= 8;
        } else if (scroll_pos > scroll_top) {
            top_pos -= 8;
        } else {
            if (scroll_speed < 0) scroll_speed = 0;
            top_pos = scroll_pos < scroll_top ? scroll_pos + 0.25 : scroll_top;
        }
        int bottom_pos = scroll_bottom;
        if (dragging) {
            bottom_pos += 8;
        } else if (scroll_pos < scroll_bottom) {
            bottom_pos += 8;
        } else {
            if (scroll_speed > 0) scroll_speed = 0;
            bottom_pos = scroll_pos > scroll_bottom ? scroll_pos - 0.25 : scroll_bottom;
        }
        scroll_pos += scroll_speed;
        scroll_pos = C2D_Clamp(scroll_pos, top_pos, bottom_pos);

        // keep cursor on screen
        while ((cursor - 1) * entry_height - scroll_pos < 0)
            cursor++;
        while ((cursor + 1) * entry_height - scroll_pos > 240)
            cursor--;

        // control with buttons
        if (entry_count > 0)
        {
            u32 kDown = hidKeysDown();
            u32 kHeld = hidKeysHeld();
            int xaxis = (bool)(kHeld & KEY_RIGHT) - (bool)(kHeld & KEY_LEFT);
            int yaxis = (bool)(kHeld & KEY_DOWN) - (bool)(kHeld & KEY_UP);
            if ((kDown & KEY_RIGHT) || (kDown & KEY_LEFT)) {
                xdas_length = xdas_time = das_start;
                xdas_count = 0;
            }
            if ((kDown & KEY_DOWN) || (kDown & KEY_UP)) {
                ydas_length = ydas_time = das_start;
                ydas_count = 0;
            }

            if (xaxis != 0 && ++xdas_time >= xdas_length) {
                xdas_time = 0;
                xdas_count++;
                if (xdas_count > 1) {
                    xdas_length = 4;
                } else if (xdas_count > 5) {
                    xdas_length = 2;
                } else if (xdas_count > 10) {
                    xdas_length = 1;
                }
            } else {
                xaxis = 0;
            }
            if (yaxis != 0 && ++ydas_time >= ydas_length) {
                ydas_time = 0;
                ydas_count++;
                if (ydas_count > 1) {
                    ydas_length = 4;
                } else if (ydas_count > 5) {
                    ydas_length = 2;
                } else if (ydas_count > 10) {
                    ydas_length = 1;
                }
            } else {
                yaxis = 0;
            }

            if (xaxis != 0 || yaxis != 0) {
                if (yaxis != 0) cursor += yaxis;
                if (xaxis != 0) cursor += xaxis * 6;

                // wrap if direction pressed and at end
                if (cursor - xaxis * 6 - yaxis == 0 && (kDown & (KEY_UP | KEY_LEFT)))
                    cursor = entry_count-1;
                if (cursor - xaxis * 6 - yaxis == entry_count - 1 && (kDown & (KEY_DOWN | KEY_RIGHT)))
                    cursor = 0;
                // limit otherwise
                if (cursor < 0) cursor = 0;
                if (cursor >= entry_count) cursor = entry_count-1;
                
                int temp = (cursor-1) * entry_height;
                if (scroll_pos > temp) scroll_pos = temp;

                temp = (cursor-1) * entry_height - 176;
                if (scroll_pos < temp) scroll_pos = temp;
            }

            if ((kDown & KEY_A)) {
                clicked_entry = cursor;
            }
        }

        // draw
        C2D_TextBufClear(dynamic_textbuf);
        float y = -scroll_pos;
        C2D_Text text;
        // entries
        for (int i = 0; i < entry_count; i++) {
            if (y + entry_height >= 0 && y < 240) {
                C2D_TextParse(&text, dynamic_textbuf, i < dirCount ? dirs[i] : files[i - dirCount]);
                C2D_TextOptimize(&text);
                C2D_DrawRectSolid(56, y, 0, 264, entry_height, clicked_entry == i ? C2D_Color32(TINT_R*0.5, TINT_G*0.5, TINT_B*0.5, 255) : C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
                if (cursor == i) C2D_DrawRectSolid(56 + 4, y + entry_height - 8, 0, 264 - 8, 1, C2D_Color32(0, 0, 0, 255));
                C2D_DrawText(&text, C2D_AlignLeft, 64, y + 8, 0, 0.5, 0.5);
            }
            y += entry_height;
        }
        // scrollbar
        if (scroll_top != scroll_bottom) {
            C2D_DrawRectSolid(320-2, 32, 0, 2, 240-32, C2D_Color32(0, 0, 0, 255));
            C2D_DrawRectSolid(320-2, 32 + (240-32-8) * (scroll_pos - scroll_top) / (scroll_bottom - scroll_top), 0, 2, 8, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        }
        // path
        C2D_TextParse(&text, dynamic_textbuf, path);
        C2D_TextOptimize(&text);
        C2D_DrawRectSolid(0, 0, 0, 320, 32, C2D_Color32(0, 0, 0, 255));
        C2D_DrawText(&text, C2D_AlignLeft | C2D_WithColor, 48, 8, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));

        // up button indicator
        C2D_DrawText(&text_btn_X, C2D_AlignLeft | C2D_WithColor, 8, 32, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    LOOP_END(rom_loader_buttons);

    buttonLock = false;

    for (int i = 0; i < dirCount; i++) free(dirs[i]);
    for (int i = 0; i < fileCount; i++) free(files[i]);
    free(dirs);
    free(files);

    if (clicked_entry < 0) {
        switch (button) {
            case ROM_LOADER_UP: // Up
                int len = strlen(path);
                // don't get shorter than sdmc:/
                if (len > 6) {
                    // the stuff at the start of the file will get rid of everything after the last slash
                    // so we can just get rid of the slash at the end
                    path[len - 1] = 0;
                }
                return rom_loader();
            case ROM_LOADER_BACK: return main_menu(MAIN_MENU_LOAD_ROM);
        }
    } else if (clicked_entry < dirCount) {
        return rom_loader();
    } else {
        // clear screen buffer
        C2D_TargetClear(screenTarget, 0);
        C3D_FrameBegin(0);
        video_flush(true);
        C3D_FrameEnd(0);

        tVBOpt.ROM_PATH = realloc(tVBOpt.ROM_PATH, path_cap);
        memcpy(tVBOpt.ROM_PATH, path, path_cap);
        tVBOpt.RAM_PATH = realloc(tVBOpt.RAM_PATH, path_cap);
        memcpy(tVBOpt.RAM_PATH, path, path_cap);
        strcpy(strrchr(tVBOpt.RAM_PATH, '.'), ".ram");
        saveFileOptions();
        return load_rom();
    }
}

static void draw_abxy(Button *self) {
    const int FACEX = self->x + self->w / 2;
    const int FACEY = self->y + self->h / 2;
    const int FACEW = self->w;
    const int FACEH = self->h;
    const int OFFSET = 22;
    // Shifting the draw x offset by 0.5 is a workaround for a Citro2D text rendering bug; it's being done to the (3DS) L, R, A, B, X, Y buttons here
    C2D_DrawText(tVBOpt.ABXY_MODE == 0 || tVBOpt.ABXY_MODE == 3 || tVBOpt.ABXY_MODE == 5 ? &text_btn_A : &text_btn_B, C2D_AlignCenter, FACEX - 0.5, FACEY - OFFSET - 16, 0, 1, 1);
    C2D_DrawText(tVBOpt.ABXY_MODE == 0 || tVBOpt.ABXY_MODE == 3 || tVBOpt.ABXY_MODE == 4 ? &text_btn_B : &text_btn_A, C2D_AlignCenter, FACEX - 0.5, FACEY + OFFSET - 16, 0, 1, 1);
    C2D_DrawText(tVBOpt.ABXY_MODE < 2 || tVBOpt.ABXY_MODE == 5 ? &text_btn_B : &text_btn_A, C2D_AlignCenter, FACEX - OFFSET - 0.5, FACEY - 16, 0, 1, 1);
    C2D_DrawText(tVBOpt.ABXY_MODE < 2 || tVBOpt.ABXY_MODE == 4 ? &text_btn_A : &text_btn_B, C2D_AlignCenter, FACEX + OFFSET - 0.5, FACEY - 16, 0, 1, 1);
}

static void draw_shoulders(Button *self) {
    const int SHOULDX = self->x + self->w / 2;
    const int SHOULDY = self->y;
    const int SHOULDW = self->w;
    const int SHOULDH = self->h;
    const int OFFSET = 22;
    // Shifting the draw x offset by 0.5 is a workaround for a Citro2D text rendering bug; it's being done to the (3DS) L, R, A, B, X, Y buttons here
    C2D_DrawText(tVBOpt.ZLZR_MODE <= 1 ? &text_btn_L : tVBOpt.ZLZR_MODE <= 2 ? &text_btn_B : &text_btn_A, C2D_AlignCenter, SHOULDX - OFFSET*2 - 0.5, SHOULDY + 5, 0, 1, 1);
    C2D_DrawText(tVBOpt.ZLZR_MODE >= 2 ? &text_btn_L : tVBOpt.ZLZR_MODE == 0 ? &text_btn_B : &text_btn_A, C2D_AlignCenter, SHOULDX - OFFSET*0.75, SHOULDY + 5, 0, 1, 1);
    C2D_DrawText(tVBOpt.ZLZR_MODE >= 2 ? &text_btn_R : tVBOpt.ZLZR_MODE == 0 ? &text_btn_A : &text_btn_B, C2D_AlignCenter, SHOULDX + OFFSET*0.75, SHOULDY + 5, 0, 1, 1);
    C2D_DrawText(tVBOpt.ZLZR_MODE <= 1 ? &text_btn_R : tVBOpt.ZLZR_MODE <= 2 ? &text_btn_A : &text_btn_B, C2D_AlignCenter, SHOULDX + OFFSET*2 + 0.5, SHOULDY + 5, 0, 1, 1);
}

static void controls(int initial_button) {
    bool shoulder_pressed = false;
    bool face_pressed = false;
    bool new_3ds = false;
    APT_CheckNew3DS(&new_3ds);
    controls_buttons[CONTROLS_FACE].y = new_3ds ? 52 : 40;
    controls_buttons[CONTROLS_SHOULDER].hidden = !new_3ds;
    const int SHOULDX = 160;
    const int SHOULDY = 0;
    const int SHOULDW = 128;
    const int SHOULDH = 40;
    const int FACEX = 160;
    const int FACEY = new_3ds ? 92 : 80;
    const int FACEW = 128;
    const int FACEH = 80;
    const int OFFSET = 22;
    bool changed = false;
    LOOP_BEGIN(controls_buttons, initial_button);
    LOOP_END(controls_buttons);
    switch (button) {
        case CONTROLS_FACE:
            tVBOpt.ABXY_MODE = (tVBOpt.ABXY_MODE + 1) % 6;
            return controls(CONTROLS_FACE);
        case CONTROLS_SHOULDER:
            tVBOpt.ZLZR_MODE = (tVBOpt.ZLZR_MODE + 1) % 4;
            return controls(CONTROLS_SHOULDER);
        case CONTROLS_TOUCHSCREEN:
            return touchscreen_settings();
        case CONTROLS_BACK:
            setTouchControls(buttons_on_screen);
            saveFileOptions();
            return main_menu(MAIN_MENU_CONTROLS);
    }
}

static void touchscreen_settings() {
    // 1: pause, 2: dpad/a, 3: b
    int dragging = 0;
    int xoff = 0, yoff = 0;
    int BUTTON_RAD = 24;
    int PAD_RAD = 48;
    int PAUSE_RAD = 4;
    buttonLock = true;
    LOOP_BEGIN(touchscreen_settings_buttons, -1);
        // handle dragging
        touchPosition touch_pos;
        hidTouchRead(&touch_pos);
        if (hidKeysDown() & KEY_TOUCH) {
            if (guiShouldSwitch()) {
                buttons_on_screen = !buttons_on_screen;
            } else if (abs(tVBOpt.PAUSE_RIGHT - touch_pos.px) < PAUSE_RAD) {
                // pause slider
                dragging = 1;
                xoff = tVBOpt.PAUSE_RIGHT - touch_pos.px;
            } else if (buttons_on_screen) {
                int adx = tVBOpt.TOUCH_AX - touch_pos.px;
                int ady = tVBOpt.TOUCH_AY - touch_pos.py;
                int bdx = tVBOpt.TOUCH_BX - touch_pos.px;
                int bdy = tVBOpt.TOUCH_BY - touch_pos.py;
                if (adx*adx + ady*ady < BUTTON_RAD*BUTTON_RAD) {
                    // a button
                    dragging = 2;
                    xoff = adx;
                    yoff = ady;
                } else if (bdx*bdx + bdy*bdy < BUTTON_RAD*BUTTON_RAD) {
                    // b button
                    dragging = 3;
                    xoff = bdx;
                    yoff = bdy;
                }
            } else {
                int dx = tVBOpt.TOUCH_PADX - touch_pos.px;
                int dy = tVBOpt.TOUCH_PADY - touch_pos.py;
                if (dx*dx + dy*dy < PAD_RAD*PAD_RAD) {
                    // dpad
                    dragging = 2;
                    xoff = dx;
                    yoff = dy;
                }
            }
        }
        if (hidKeysHeld() & KEY_TOUCH) {
            if (dragging == 1) {
                // pause slider
                int dest_x = touch_pos.px + xoff;
                if (dest_x > tVBOpt.TOUCH_AX - BUTTON_RAD - PAUSE_RAD)
                    dest_x = tVBOpt.TOUCH_AX - BUTTON_RAD - PAUSE_RAD;
                if (dest_x > tVBOpt.TOUCH_BX - BUTTON_RAD - PAUSE_RAD)
                    dest_x = tVBOpt.TOUCH_BX - BUTTON_RAD - PAUSE_RAD;
                if (dest_x > tVBOpt.TOUCH_PADX - PAD_RAD - PAUSE_RAD)
                    dest_x = tVBOpt.TOUCH_PADX - PAD_RAD - PAUSE_RAD;
                if (dest_x > 192)
                    dest_x = 192;
                if (dest_x < 80 + PAUSE_RAD) 
                    dest_x = 80 + PAUSE_RAD;
                tVBOpt.PAUSE_RIGHT = dest_x;
            } else if (dragging != 0) {
                // button or dpad
                int dest_x = touch_pos.px + xoff;
                int dest_y = touch_pos.py + yoff;
                int rad = buttons_on_screen ? BUTTON_RAD : PAD_RAD;
                if (dest_x > 320 - rad)
                    dest_x = 320 - rad;
                if (dest_y > 240 - rad)
                    dest_y = 240 - rad;
                if (dest_x < tVBOpt.PAUSE_RIGHT + PAUSE_RAD + rad)
                    dest_x = tVBOpt.PAUSE_RIGHT + PAUSE_RAD + rad;
                // lower top range to account for switch button
                if (dest_y < 32 + rad)
                    dest_y = 32 + rad;
                if (buttons_on_screen) {
                    if (dragging == 2) {
                        // a button
                        int dx = dest_x - tVBOpt.TOUCH_BX;
                        int dy = dest_y - tVBOpt.TOUCH_BY;
                        int dist_sqr = dx*dx + dy*dy;
                        if (dist_sqr < 4*BUTTON_RAD*BUTTON_RAD && dist_sqr != 0) {
                            float dist = sqrt(dist_sqr);
                            dest_x = tVBOpt.TOUCH_BX + dx * BUTTON_RAD * 2 / dist;
                            dest_y = tVBOpt.TOUCH_BY + dy * BUTTON_RAD * 2 / dist;
                        }
                    } else {
                        // b button
                        int dx = dest_x - tVBOpt.TOUCH_AX;
                        int dy = dest_y - tVBOpt.TOUCH_AY;
                        int dist_sqr = dx*dx + dy*dy;
                        if (dist_sqr < 4*BUTTON_RAD*BUTTON_RAD && dist_sqr != 0) {
                            float dist = sqrt(dist_sqr);
                            dest_x = tVBOpt.TOUCH_AX + dx * BUTTON_RAD * 2 / dist;
                            dest_y = tVBOpt.TOUCH_AY + dy * BUTTON_RAD * 2 / dist;
                        }
                    }
                    // additional check in case the button check shunted us OOB
                    if (dest_x <= 320 - BUTTON_RAD && dest_x >= tVBOpt.PAUSE_RIGHT + PAUSE_RAD + BUTTON_RAD
                        && dest_y <= 240 - BUTTON_RAD && dest_y >= 32 + BUTTON_RAD
                    ) {
                        if (dragging == 2) {
                            tVBOpt.TOUCH_AX = dest_x;
                            tVBOpt.TOUCH_AY = dest_y;
                        } else {
                            tVBOpt.TOUCH_BX = dest_x;
                            tVBOpt.TOUCH_BY = dest_y;
                        }
                    }
                } else {
                    tVBOpt.TOUCH_PADX = dest_x;
                    tVBOpt.TOUCH_PADY = dest_y;
                }
            }
        } else {
            dragging = 0;
        }

        // exit on B
        if (hidKeysDown() & KEY_B) button = TOUCHSCREEN_BACK;

        // draw
        drawTouchControls(
            dragging == 2 ? VB_KEY_A :
            dragging == 3 ? VB_KEY_B :
            dragging ? VB_KEY_START : 0);
        C2D_DrawRectSolid(
            tVBOpt.PAUSE_RIGHT - PAUSE_RAD, 240/2 - 8, 0,
            PAUSE_RAD * 2, 8*2, dragging == 1 ? C2D_Color32(TINT_R*0.9, TINT_G*0.9, TINT_B*0.9, 255) : C2D_Color32(TINT_R*0.5, TINT_G*0.5, TINT_B*0.5, 255)
        );
    LOOP_END(touchscreen_settings_buttons);
    buttonLock = false;
    switch (button) {
        case TOUCHSCREEN_BACK: // Back
            saveFileOptions();
            return controls(CONTROLS_TOUCHSCREEN);
        case TOUCHSCREEN_RESET: // Reset
            tVBOpt.PAUSE_RIGHT = 160;
            tVBOpt.TOUCH_AX = 250;
            tVBOpt.TOUCH_AY = 64;
            tVBOpt.TOUCH_BX = 250;
            tVBOpt.TOUCH_BY = 160;
            tVBOpt.TOUCH_PADX = 240;
            tVBOpt.TOUCH_PADY = 128;
            return touchscreen_settings();
    }
}

static void colour_filter() {
    bool dragging = false;
    const float circle_x = colour_wheel_sprite.params.pos.x;
    const float circle_y = colour_wheel_sprite.params.pos.y;
    const float circle_w = colour_wheel_sprite.params.pos.w;
    const float circle_h = colour_wheel_sprite.params.pos.h;

    LOOP_BEGIN(colour_filter_buttons, -1);
        touchPosition touch_pos;
        hidTouchRead(&touch_pos);
        float touch_dx = (touch_pos.px - circle_x) / (circle_w / 2);
        float touch_dy = (touch_pos.py - circle_y) / (circle_h / 2);
        float sat = sqrt(touch_dx * touch_dx + touch_dy * touch_dy);
        if ((hidKeysDown() & KEY_TOUCH) && sat <= 1)
            dragging = true;
        if (hidKeysUp() & KEY_TOUCH)
            dragging = false;
        if (dragging) {
            if (sat > 1) {
                touch_dx /= sat;
                touch_dy /= sat;
                sat = 1;
            }
            // touch position to hue saturation, then to rgb
            float hue = atan2(touch_dy, touch_dx);
            float hprime = fmod(hue / (M_PI / 3) + 6, 6);
            float sub = sat * (1 - fabs(fmod(hprime, 2) - 1));
            float col[3] = {0};
            if (hprime < 1) {
                col[0] = sat;
                col[1] = sub;
            } else if (hprime < 2) {
                col[0] = sub;
                col[1] = sat;
            } else if (hprime < 3) {
                col[1] = sat;
                col[2] = sub;
            } else if (hprime < 4) {
                col[1] = sub;
                col[2] = sat;
            } else if (hprime < 5) {
                col[0] = sub;
                col[2] = sat;
            } else {
                col[0] = sat;
                col[2] = sub;
            }
            tVBOpt.TINT = 0xff000000 |
                ((int)((col[0] + 1 - sat) * 255)) |
                ((int)((col[1] + 1 - sat) * 255) << 8) |
                ((int)((col[2] + 1 - sat) * 255) << 16);
        }
        C2D_DrawSprite(&colour_wheel_sprite);
        if (!dragging) {
            // tint to hue saturation
            float col[3] = {
                (tVBOpt.TINT & 0xff) / 255.0,
                ((tVBOpt.TINT >> 8) & 0xff) / 255.0,
                ((tVBOpt.TINT >> 16) & 0xff) / 255.0,
            };
            float max = col[0] > col[1] ? col[0] : col[1];
            max = max > col[2] ? max : col[2];
            float min = col[0] < col[1] ? col[0] : col[1];
            min = min < col[2] ? min : col[2];
            if (max == min) {
                // white
                touch_dx = touch_dy = 0;
            } else {
                float chroma = max - min;
                float hprime;
                if (max == col[0]) {
                    hprime = (col[1] - col[2]) / chroma;
                } else if (max == col[1]) {
                    hprime = (col[2] - col[0]) / chroma + 2;
                } else {
                    hprime = (col[0] - col[1]) / chroma + 4;
                }
                float hue = hprime * (M_PI / 3);
                float sat = chroma / max;
                touch_dx = sat * cos(hue);
                touch_dy = sat * sin(hue);
            }
        }
        C2D_DrawCircleSolid(
            circle_x + touch_dx * (circle_w / 2),
            circle_y + touch_dy * (circle_h / 2),
            0, 4, 0xff000000);
        C2D_DrawCircleSolid(
            circle_x + touch_dx * (circle_w / 2),
            circle_y + touch_dy * (circle_h / 2),
            0, 2, tVBOpt.TINT);
    LOOP_END(colour_filter_buttons);
    switch (button) {
        case COLOUR_BACK: // Back
            saveFileOptions();
            return video_settings(VIDEO_COLOUR);
        case COLOUR_RED: // Red
            tVBOpt.TINT = 0xff0000ff;
            return colour_filter();
        case COLOUR_GRAY: // Gray
            tVBOpt.TINT = 0xffffffff;
            return colour_filter();
    }
}

static void dev_options(int initial_button) {
    bool new_3ds = false;
    APT_CheckNew3DS(&new_3ds);
    dev_options_buttons[DEV_N3DS].hidden = !new_3ds;
    dev_options_buttons[DEV_PERF].toggle = tVBOpt.PERF_INFO;
    dev_options_buttons[DEV_N3DS].toggle = tVBOpt.N3DS_SPEEDUP;
    LOOP_BEGIN(dev_options_buttons, initial_button);
    LOOP_END(dev_options_buttons);
    switch (button) {
        case DEV_PERF:
            tVBOpt.PERF_INFO = !tVBOpt.PERF_INFO;
            return dev_options(DEV_PERF);
        case DEV_N3DS:
            tVBOpt.N3DS_SPEEDUP = !tVBOpt.N3DS_SPEEDUP;
            osSetSpeedupEnable(tVBOpt.N3DS_SPEEDUP);
            return dev_options(DEV_N3DS);
        case DEV_BACK:
            saveFileOptions();
            return options(OPTIONS_DEV);
    }
}

static void save_debug_info();
static void options(int initial_button) {
    options_buttons[OPTIONS_FF].toggle = tVBOpt.FF_TOGGLE;
    options_buttons[OPTIONS_SOUND].toggle = tVBOpt.SOUND;
    options_buttons[OPTIONS_DEBUG].hidden = !game_running;
    LOOP_BEGIN(options_buttons, initial_button);
    LOOP_END(options_buttons);
    switch (button) {
        case OPTIONS_VIDEO: // Video settings
            return video_settings(0);
        case OPTIONS_FF: // Fast forward
            tVBOpt.FF_TOGGLE = !tVBOpt.FF_TOGGLE;
            saveFileOptions();
            return options(OPTIONS_FF);
        case OPTIONS_SOUND: // Sound
            tVBOpt.SOUND = !tVBOpt.SOUND;
            //if (tVBOpt.SOUND) sound_enable();
            //else sound_disable();
            return options(OPTIONS_SOUND);
        case OPTIONS_DEV: // Developer settings
            return dev_options(0);
        case OPTIONS_ABOUT: // About
            return about();
        case OPTIONS_BACK: // Back
            return main_menu(MAIN_MENU_OPTIONS);
        case OPTIONS_DEBUG: // Save debug info
            return save_debug_info();
    }
}

static void video_settings(int initial_button) {
    video_settings_buttons[VIDEO_SLIDER].toggle = tVBOpt.SLIDERMODE;
    video_settings_buttons[VIDEO_DEFAULT_EYE].toggle = tVBOpt.DEFAULT_EYE;
    LOOP_BEGIN(video_settings_buttons, initial_button);
    LOOP_END(video_settings_buttons);
    switch (button) {
        case VIDEO_COLOUR: // Colour filter
            return colour_filter();
        case VIDEO_SLIDER: // Slider mode
            tVBOpt.SLIDERMODE = !tVBOpt.SLIDERMODE;
            saveFileOptions();
            return video_settings(VIDEO_SLIDER);
        case VIDEO_DEFAULT_EYE: // Default eye
            tVBOpt.DEFAULT_EYE = !tVBOpt.DEFAULT_EYE;
            saveFileOptions();
            return video_settings(VIDEO_DEFAULT_EYE);
        case VIDEO_BACK: // Back
            return options(OPTIONS_VIDEO);
    }
}

static void sound_error() {
    LOOP_BEGIN(sound_error_buttons, 0);
        C2D_DrawText(&text_sound_error, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    LOOP_END(sound_error_buttons);
    return;
}

static bool areyousure(C2D_Text *message) {
    #undef DEFAULT_RETURN
    #define DEFAULT_RETURN false
    LOOP_BEGIN(areyousure_buttons, AREYOUSURE_NO);
        C2D_DrawText(message, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    LOOP_END(areyousure_buttons);
    #undef DEFAULT_RETURN
    #define DEFAULT_RETURN
    return button == AREYOUSURE_YES;
}

static void about() {
    C2D_SpriteSetPos(&logo_sprite, 320 / 2, 36);
    C2D_SetTintMode(C2D_TintMult);
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, C2D_Color32(255, 0, 0, 255), 1);
    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawSpriteTinted(&logo_sprite, &tint);
        C2D_DrawText(&text_about, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    LOOP_END(about_buttons);
    return options(OPTIONS_ABOUT);
}

static void load_error(int err, bool unloaded) {
    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawText(&text_loaderr, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        if (unloaded) {
            C2D_DrawText(&text_unloaded, C2D_AlignCenter | C2D_WithColor, 320 / 2, 120, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        }
    LOOP_END(about_buttons);
    return rom_loader();
}

static void load_rom() {
    if (save_thread) threadJoin(save_thread, U64_MAX);
    int ret;
    if ((ret = v810_load_init())) {
        // instant fail
        return load_error(ret, false);
    }
    C2D_Text text;
    C2D_TextBufClear(dynamic_textbuf);
    char *filename = strrchr(tVBOpt.ROM_PATH, '/');
    if (filename) filename++;
    else filename = tVBOpt.ROM_PATH;
    C2D_TextParse(&text, dynamic_textbuf, filename);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(load_rom_buttons, -1);
        ret = v810_load_step();
        if (ret < 0) {
            // error
            loop = false;
        } else {
            C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 10, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
            C2D_DrawText(&text_loading, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.8, 0.8, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
            C2D_DrawRectSolid(60, 140, 0, 200, 16, C2D_Color32(0.5 * TINT_R, 0.5 * TINT_G, 0.5 * TINT_B, 255));
            C2D_DrawRectSolid(60, 140, 0, 2 * ret, 16, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
            if (ret == 100) {
                // complete
                loop = false;
            }
        }
    LOOP_END(load_rom_buttons);
    if (ret == 100) {
        // complete
        game_running = true;
        return;
    } else {
        game_running = false;
        // redraw logo since we unloaded
        C3D_FrameBegin(0);
        draw_logo();
        C3D_FrameEnd(0);
        if (ret < 0) {
            // error
            return load_error(ret, true);
        } else {
            // cancelled
            v810_load_cancel();
            return rom_loader();
        }
    }
}

static inline int handle_buttons(Button buttons[], int count) {
    static int pressed = -1;
    int ret = -1;
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    
    // d-pad input
    u32 kDown = hidKeysDown();
    if ((kDown & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) && !buttonLock)
    {
        // if no button is selected just use the first one
        if (selectedButton == NULL)
            selectedButton = &buttons[0];
        else
        {
            // search for button nearest to the selected one
            Button* temp = NULL;
            int xaxis = (bool)(kDown & KEY_RIGHT) - (bool)(kDown & KEY_LEFT);
            int yaxis = (bool)(kDown & KEY_DOWN) - (bool)(kDown & KEY_UP);
            int selectx = (selectedButton->x + (selectedButton->w / 2)) + ((selectedButton->w / 2) * xaxis);
            int selecty = (selectedButton->y + (selectedButton->h / 2)) + ((selectedButton->h / 2) * yaxis);

            for (int i = 0; i < count; i++)
            {
                // skip if selected button
                if (selectedButton == &buttons[i])
                    continue;

                // skip if hidden
                if (buttons[i].hidden)
                    continue;

                // skip if button is not in the right direction
                int buttonx = (buttons[i].x + (buttons[i].w / 2)) - ((buttons[i].w / 2) * xaxis);
                int buttony = (buttons[i].y + (buttons[i].h / 2)) - ((buttons[i].h / 2) * yaxis);

                if (((kDown & KEY_UP)    && buttony >= selecty) ||
                    ((kDown & KEY_DOWN)  && buttony <= selecty) ||
                    ((kDown & KEY_LEFT)  && buttonx >= selectx) ||
                    ((kDown & KEY_RIGHT) && buttonx <= selectx))
                {
                    continue;
                }

                // use the nearest button that is in the right direction
                if (temp) {
                    int tempx = (temp->x + (temp->w / 2)) - ((temp->w / 2) * xaxis);
                    int tempy = (temp->y + (temp->h / 2)) - ((temp->h / 2) * yaxis);
                    int olddist = dis(selectx, selecty, tempx, tempy);
                    int newdist = dis(selectx, selecty, buttonx, buttony);
                    if (newdist > olddist)
                        continue;
                    // bias leftwards
                    if (olddist == newdist && buttonx > tempx)
                        continue;
                }

                temp = &buttons[i];
            }

            if (temp)
                selectedButton = temp;
        }
    }

    // check buttons for input
    for (int i = 0; i < count; i++) {
        if (buttons[i].hidden) continue;
        if (
            touch_pos.px >= buttons[i].x && touch_pos.py >= buttons[i].y &&
            touch_pos.px < buttons[i].x + buttons[i].w && touch_pos.py < buttons[i].y + buttons[i].h
        ) {
            // touching the button
            if (hidKeysDown() & KEY_TOUCH) {
                pressed = i;
                selectedButton = &buttons[i];
            }
        } else {
            // not touching the button
            if (pressed == i && (hidKeysHeld() & KEY_TOUCH)) {
                pressed = -1;
            }
        }

        if ((hidKeysUp() & KEY_TOUCH) && pressed == i) {
            ret = i;
        }

        // select with the A button
        if ((kDown & KEY_A) && selectedButton == &buttons[i] && !buttonLock) {
            ret = i;
        }

        // text-based checks, ignore when there is not text
        if (!buttons[i].str) continue;

        // move back with the B button
        if ((kDown & KEY_B) && (strcmp(buttons[i].str, "Back") == 0 || strcmp(buttons[i].str, "No") == 0)) {
            ret = i;
        }

        // move up a directory with X
        if ((kDown & KEY_X) && strcmp(buttons[i].str, "Up") == 0) {
            ret = i;
        }
    }
    // draw buttons
    for (int i = 0; i < count; i++) {
        if (buttons[i].hidden) continue;
        u32 normal_colour = C2D_Color32(TINT_R, TINT_G, TINT_B, 255);
        u32 pressed_colour = C2D_Color32(TINT_R*0.5, TINT_G*0.5, TINT_B*0.5, 255);
        C2D_DrawRectSolid(buttons[i].x, buttons[i].y, 0, buttons[i].w, buttons[i].h,
            pressed == i ? pressed_colour : normal_colour);
        if (selectedButton == &buttons[i]) {
            u32 line_colour = C2D_Color32(0, 0, 0, 255);
            float ls = 3.5;
            float le = 5.5;
            C2D_DrawRectSolid(buttons[i].x + 4, buttons[i].y + buttons[i].h - 4, 0, buttons[i].w - 8, 1, C2D_Color32(0, 0, 0, 255));
        }
        if (buttons[i].custom_draw) {
            buttons[i].custom_draw(&buttons[i]);
        } else if (buttons[i].str) {
            int yoff = -10;
            char *strptr = buttons[i].str;
            while ((strptr = strchr(strptr, '\n'))) {
                yoff -= 10;
                strptr++;
            }
            C2D_DrawText(&buttons[i].text, C2D_AlignCenter, buttons[i].x + buttons[i].w / 2, buttons[i].y + buttons[i].h / 2 + yoff, 0, 0.7, 0.7);
        }
        if (buttons[i].show_toggle) C2D_DrawText(buttons[i].toggle ? buttons[i].toggle_text_on : buttons[i].toggle_text_off, C2D_AlignLeft, buttons[i].x, buttons[i].y, 0, 0.5, 0.5);
    }
    if (save_thread) C2D_DrawText(&text_saving, C2D_AlignLeft, 0, 224, 0, 0.5, 0.5);
    if (ret >= 0) pressed = -1;
    return ret;
}

static void draw_status_bar(float total_time, float drc_time) {
    if (!tVBOpt.PERF_INFO) return;
    C2D_DrawRectSolid(0, 240 - 12, 0, 320, 12, C2D_Color32(128, 128, 128, 255));
    C2D_TextBufClear(dynamic_textbuf);
    C2D_Text text;
    char buf[32];

    // All
    sprintf(buf, "A:%5.2fms", total_time);
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 310-60*5, 240 - 12, 0, 0.35, 0.35);

    // DRC
    sprintf(buf, "D:%5.2fms", drc_time);
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 310-60*4, 240 - 12, 0, 0.35, 0.35);

    // C3D
    sprintf(buf, "C:%5.2fms", C3D_GetProcessingTime());
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 310-60*3, 240 - 12, 0, 0.35, 0.35);

    // PICA
    sprintf(buf, "P:%5.2fms", C3D_GetDrawingTime());
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 310-60*2, 240 - 12, 0, 0.35, 0.35);

    // Memory
    sprintf(buf, "M:%5.2f%%", (cache_pos-cache_start)*4*100./CACHE_SIZE);
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 310-60, 240 - 12, 0, 0.35, 0.35);
}

bool old_2ds = 0;

void guiInit() {
    u8 model = 0;
    cfguInit();
    CFGU_GetSystemModel(&model);
    cfguExit();
    old_2ds = (model == CFG_MODEL_2DS);

    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    screen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    sprite_sheet = C2D_SpriteSheetLoadFromMem(sprites_t3x, sprites_t3x_size);
    C2D_SpriteFromSheet(&colour_wheel_sprite, sprite_sheet, sprites_colour_wheel_idx);
    C2D_SpriteSetCenter(&colour_wheel_sprite, 0.5, 0.5);
    C2D_SpriteSetPos(&colour_wheel_sprite, 176, 112);
    C2D_SpriteFromSheet(&logo_sprite, sprite_sheet, sprites_logo_idx);
    C2D_SpriteSetCenter(&logo_sprite, 0.5, 0.5);

    setTouchControls(buttons_on_screen);

    static_textbuf = C2D_TextBufNew(1024);
    dynamic_textbuf = C2D_TextBufNew(4096);
    SETUP_ALL_BUTTONS;
    STATIC_TEXT(&text_A, "A");
    STATIC_TEXT(&text_B, "B");
    STATIC_TEXT(&text_btn_A, "\uE000");
    STATIC_TEXT(&text_btn_B, "\uE001");
    STATIC_TEXT(&text_btn_X, "\uE002");
    STATIC_TEXT(&text_btn_L, "\uE004");
    STATIC_TEXT(&text_btn_R, "\uE005");
    STATIC_TEXT(&text_switch, "Switch");
    STATIC_TEXT(&text_saving, "Saving...");
    STATIC_TEXT(&text_on, "On");
    STATIC_TEXT(&text_off, "Off");
    STATIC_TEXT(&text_toggle, "Toggle");
    STATIC_TEXT(&text_hold, "Hold");
    STATIC_TEXT(&text_3ds, "Nintendo 3DS");
    STATIC_TEXT(&text_vbipd, "Virtual Boy IPD");
    STATIC_TEXT(&text_left, "Left");
    STATIC_TEXT(&text_right, "Right");
    STATIC_TEXT(&text_sound_error, "Error: couldn't initialize audio.\nDid you dump your DSP firmware?");
    STATIC_TEXT(&text_debug_filenames, "Please share debug_info.txt and\ndebug_replay.bin.gz in your bug\nreport.");
    STATIC_TEXT(&text_anykeyexit, "Press any key to exit");
    STATIC_TEXT(&text_about, VERSION "\nBy Floogle, danielps, & others\nHeavily based on Reality Boy by David Tucker\nMore info at:\ngithub.com/skyfloogle/red-viper");
    STATIC_TEXT(&text_loading, "Loading...");
    STATIC_TEXT(&text_loaderr, "Failed to load ROM.");
    STATIC_TEXT(&text_unloaded, "The current ROM has been unloaded.");
    STATIC_TEXT(&text_yes, "Yes");
    STATIC_TEXT(&text_no, "No");
    STATIC_TEXT(&text_areyousure_reset, "Are you sure you want to reset?");
    STATIC_TEXT(&text_areyousure_exit, "Are you sure you want to exit?");
}

static bool shouldRedrawMenu = true;
static bool inMenu = false;

void openMenu() {
    inMenu = true;
    shouldRedrawMenu = true;
    if (game_running) {
        sound_pause();
        save_sram();
    }
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    main_menu(game_running ? MAIN_MENU_RESUME : MAIN_MENU_LOAD_ROM);
    if (guiop == 0) sound_resume();
    else if (!(guiop & GUIEXIT)) sound_reset();
    inMenu = false;
}

bool backlightEnabled = true;

bool toggleBacklight(bool enable) {
    gspLcdInit();
    enable ? GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM) : GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
    gspLcdExit();
    return enable;
}

void toggleVsync(bool enable) {
    // setup the thread
    endThread(frame_pacer_thread);
    u32 vtotal_top, vtotal_bottom;
    if (enable) {
        // 990 is closer to 50Hz but capture cards don't like when the two screens are out of sync
        vtotal_top = old_2ds ? 494 : 989;
        vtotal_bottom = 494;
        startPeriodicVsync(frame_pacer_thread);
    } else {
        vtotal_top = old_2ds ? 413 : 827;
        vtotal_bottom = 413;
        startPeriodic(frame_pacer_thread, 20000000);
    }
    // update VTotal only when necessary
    static bool old_enable = false;
    if (enable == old_enable) return;
    old_enable = enable;
    gspWaitForVBlank();
    if (!is_citra) {
        // wait for touchscreen's VCount to roll over to avoid potential glitches on IPS panels
        // https://github.com/skyfloogle/red-viper/issues/46#issuecomment-2034326985
        u32 old_vcount, vcount = 0;
        do {
            old_vcount = vcount;
            GSPGPU_ReadHWRegs(0x400554, &vcount, 4);
        } while (vcount >= old_vcount);
    }
    GSPGPU_WriteHWRegs(0x400424, &vtotal_top, 4);
    GSPGPU_WriteHWRegs(0x400524, &vtotal_bottom, 4);
}

void aptBacklight(APT_HookType hook, void* param) {
    if (hook == APTHOOK_ONSLEEP && !inMenu) {
        // don't save if we saved in the last 10 secs (unsure if this works)
        static u64 last_sleep_time = 0;
        if (osGetTime() - last_sleep_time >= 10000) {
            save_sram();
            last_sleep_time = osGetTime();
        }
    }
    if (tVBOpt.VSYNC) {
        switch (hook) {
            case APTHOOK_ONRESTORE:
            case APTHOOK_ONWAKEUP:
                toggleVsync(true);
                break;
            default:
                toggleVsync(false);
                break;
        }
    }
    if (!backlightEnabled) {
        switch (hook) {
            case APTHOOK_ONRESTORE:
            case APTHOOK_ONWAKEUP:
                toggleBacklight(false);
                break;
            default:
                toggleBacklight(true);
                break;
        }
    }
}

void showSoundError() {
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    sound_error();
}

static void save_debug_info() {
    C3D_FrameBegin(0);
    C2D_TargetClear(screen, 0);
    C2D_SceneBegin(screen);
    C2D_DrawText(&text_saving, C2D_AlignCenter | C2D_WithColor, 320 / 2, 100, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    C2D_Flush();
    C3D_FrameEnd(0);

    drc_dumpDebugInfo(0);

    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawText(&text_debug_filenames, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    LOOP_END(about_buttons);
    return options(OPTIONS_DEBUG);
}

void showError(int code) {
    sound_pause();
    if (!backlightEnabled) toggleBacklight(true);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C2D_TextBufClear(dynamic_textbuf);
    char buf[100];
    sprintf(buf, "DRC error #%d\nPC=0x%08lx\nDumping debug info...", code, v810_state->PC);
    C2D_Text text;
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C3D_FrameBegin(0);
    C2D_TargetClear(screen, 0);
    C2D_SceneBegin(screen);
    C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 40, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    C2D_Flush();
    C3D_FrameEnd(0);

    drc_dumpDebugInfo(code);

    C3D_FrameBegin(0);
    C3D_FrameDrawOn(screen);
    C2D_DrawText(&text_debug_filenames, C2D_AlignCenter | C2D_WithColor, 320 / 2, 120, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    C2D_DrawText(&text_anykeyexit, C2D_AlignCenter | C2D_WithColor, 320 / 2, 180, 0, 0.7, 0.7, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
    C2D_Flush();
    C3D_FrameEnd(0);
}

void setTouchControls(bool buttons) {
    shouldRedrawMenu = true;
    buttons_on_screen = buttons;
    if (buttons) {
        vbkey[__builtin_ctz(KEY_A)] = VB_RPAD_R;
        vbkey[__builtin_ctz(KEY_X)] = VB_RPAD_U;
        vbkey[__builtin_ctz(KEY_B)] = VB_RPAD_D;
        vbkey[__builtin_ctz(KEY_Y)] = VB_RPAD_L;
    } else {
        vbkey[__builtin_ctz(KEY_A)] = tVBOpt.ABXY_MODE < 2 || tVBOpt.ABXY_MODE == 4 ? VB_KEY_A : VB_KEY_B;
        vbkey[__builtin_ctz(KEY_Y)] = tVBOpt.ABXY_MODE < 2 || tVBOpt.ABXY_MODE == 5 ? VB_KEY_B : VB_KEY_A;
        vbkey[__builtin_ctz(KEY_B)] = tVBOpt.ABXY_MODE == 0 || tVBOpt.ABXY_MODE == 3 || tVBOpt.ABXY_MODE == 4 ? VB_KEY_B : VB_KEY_A;
        vbkey[__builtin_ctz(KEY_X)] = tVBOpt.ABXY_MODE == 0 || tVBOpt.ABXY_MODE == 3 || tVBOpt.ABXY_MODE == 5 ? VB_KEY_A : VB_KEY_B;
    }
    bool new_3ds = false;
    APT_CheckNew3DS(&new_3ds);
    if (new_3ds) {
        vbkey[__builtin_ctz(KEY_L)] = tVBOpt.ZLZR_MODE <= 1 ? VB_KEY_L : tVBOpt.ZLZR_MODE == 2 ? VB_KEY_B : VB_KEY_A;
        vbkey[__builtin_ctz(KEY_R)] = tVBOpt.ZLZR_MODE <= 1 ? VB_KEY_R : tVBOpt.ZLZR_MODE == 2 ? VB_KEY_A : VB_KEY_B;
        vbkey[__builtin_ctz(KEY_ZL)] = tVBOpt.ZLZR_MODE == 0 ? VB_KEY_B : tVBOpt.ZLZR_MODE == 1 ? VB_KEY_A : VB_KEY_L;
        vbkey[__builtin_ctz(KEY_ZR)] = tVBOpt.ZLZR_MODE == 0 ? VB_KEY_A : tVBOpt.ZLZR_MODE == 1 ? VB_KEY_B : VB_KEY_R;
    }
}

bool guiShouldSwitch() {
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    return touch_pos.px >= 320 - 64 && touch_pos.py < 32;
}

void drawTouchControls(int inputs) {
    int col_up = C2D_Color32(TINT_R*0.5, TINT_G*0.5, TINT_B*0.5, 255);
    int col_down = C2D_Color32(TINT_R*0.75, TINT_G*0.75, TINT_B*0.75, 255);
    int col_drag = C2D_Color32(TINT_R*0.9, TINT_G*0.9, TINT_B*0.9, 255);
    int col_line = C2D_Color32(32, 32, 32, 255);
    if (buttons_on_screen) {
        float mx = (float)(tVBOpt.TOUCH_AX + tVBOpt.TOUCH_BX) / 2;
        float my = (float)(tVBOpt.TOUCH_AY + tVBOpt.TOUCH_BY) / 2;
        if (tVBOpt.TOUCH_AY == tVBOpt.TOUCH_BY) {
            // edge case so we don't div0
            C2D_DrawLine(mx, 0, col_line, mx, 240, col_line, 1, 0);
        } else {
            float rico = -(tVBOpt.TOUCH_BX - tVBOpt.TOUCH_AX) / (float)(tVBOpt.TOUCH_BY - tVBOpt.TOUCH_AY);
            int oy = -rico * mx + my;
            int ly = oy + rico * tVBOpt.PAUSE_RIGHT;
            int ry = oy + rico * 320;
            C2D_DrawLine(tVBOpt.PAUSE_RIGHT, ly, col_line, 320, ry, col_line, 1, 0);
        }
    } else {
        C2D_DrawLine(
            tVBOpt.PAUSE_RIGHT, tVBOpt.TOUCH_PADY - tVBOpt.TOUCH_PADX + tVBOpt.PAUSE_RIGHT, col_line,
            320, tVBOpt.TOUCH_PADY - tVBOpt.TOUCH_PADX + 320, col_line,
            1, 0);
        C2D_DrawLine(
            tVBOpt.PAUSE_RIGHT, tVBOpt.TOUCH_PADX + tVBOpt.TOUCH_PADY - tVBOpt.PAUSE_RIGHT, col_line,
            320, tVBOpt.TOUCH_PADX + tVBOpt.TOUCH_PADY - 320, col_line,
            1, 0);
    }

    C2D_DrawLine(
        tVBOpt.PAUSE_RIGHT, 0, C2D_Color32(64, 64, 64, 255),
        tVBOpt.PAUSE_RIGHT, 240, C2D_Color32(64, 64, 64, 255),
        1, 0);

    bool dragging = inputs != 0;
    if (inputs == 0) inputs = guiGetInput(false);

    int pause_square_height = 70;
    C2D_DrawRectSolid(tVBOpt.PAUSE_RIGHT / 2 - pause_square_height / 2, 240 / 2 - pause_square_height / 2, 0,
        pause_square_height * 0.4, pause_square_height, C2D_Color32(64, 64, 64, 255));
    if (replay_playing()) {
        C2D_DrawTriangle(
            tVBOpt.PAUSE_RIGHT / 2 - pause_square_height / 2 + pause_square_height * 0.6, 240 / 2 - pause_square_height / 2, C2D_Color32(64, 64, 64, 255),
            tVBOpt.PAUSE_RIGHT / 2 - pause_square_height / 2 + pause_square_height * 0.6, 240 / 2 + pause_square_height / 2, C2D_Color32(64, 64, 64, 255),
            tVBOpt.PAUSE_RIGHT / 2 - pause_square_height / 2 + pause_square_height * 0.6 + pause_square_height * 0.6, 240 / 2, C2D_Color32(64, 64, 64, 255), 0
        );
    } else {
        C2D_DrawRectSolid(tVBOpt.PAUSE_RIGHT / 2 - pause_square_height / 2 + pause_square_height * 0.6, 240 / 2 - pause_square_height / 2, 0,
            pause_square_height * 0.4, pause_square_height, C2D_Color32(64, 64, 64, 255));
    }

    if (buttons_on_screen) {
        C2D_DrawCircleSolid(tVBOpt.TOUCH_AX, tVBOpt.TOUCH_AY, 0, 24, inputs & VB_KEY_A ? (dragging ? col_drag : col_down) : col_up);
        C2D_DrawCircleSolid(tVBOpt.TOUCH_BX, tVBOpt.TOUCH_BY, 0, 24, inputs & VB_KEY_B ? (dragging ? col_drag : col_down) : col_up);
        C2D_DrawText(&text_A, C2D_AlignCenter, tVBOpt.TOUCH_AX, tVBOpt.TOUCH_AY - 12, 0, 0.7, 0.7);
        C2D_DrawText(&text_B, C2D_AlignCenter, tVBOpt.TOUCH_BX, tVBOpt.TOUCH_BY - 12, 0, 0.7, 0.7);
    } else {
        C2D_DrawRectSolid(tVBOpt.TOUCH_PADX - 16, tVBOpt.TOUCH_PADY - 48, 0, 16*2, 48*2, inputs & VB_KEY_A ? col_drag : col_up);
        C2D_DrawRectSolid(tVBOpt.TOUCH_PADX - 48, tVBOpt.TOUCH_PADY - 16, 0, 48*2, 16*2, inputs & VB_KEY_A ? col_drag : col_up);
        if (!dragging) {
            if (inputs & VB_RPAD_L)
                C2D_DrawRectSolid(tVBOpt.TOUCH_PADX - 48, tVBOpt.TOUCH_PADY - 16, 0, 16*2, 16*2, col_down);
            if (inputs & VB_RPAD_R)
                C2D_DrawRectSolid(tVBOpt.TOUCH_PADX + 16, tVBOpt.TOUCH_PADY - 16, 0, 16*2, 16*2, col_down);
            if (inputs & VB_RPAD_U)
                C2D_DrawRectSolid(tVBOpt.TOUCH_PADX - 16, tVBOpt.TOUCH_PADY - 48, 0, 16*2, 16*2, col_down);
            if (inputs & VB_RPAD_D)
                C2D_DrawRectSolid(tVBOpt.TOUCH_PADX - 16, tVBOpt.TOUCH_PADY + 16, 0, 16*2, 16*2, col_down);
        }
    }

    C2D_DrawRectSolid(320 - 64, 0, 0, 64, 32, C2D_Color32(TINT_R*0.5, TINT_G*0.5, TINT_B*0.5, 255));
    C2D_DrawText(&text_switch, C2D_AlignCenter, 320 - 32, 6, 0, 0.7, 0.7);
}

void guiUpdate(float total_time, float drc_time) {
    if (!backlightEnabled) return;

    static int last_inputs = 0;
    static bool last_fastforward = false;
    static bool last_saving = false;
    static bool last_replay = false;
    int new_inputs = guiGetInput(false);
    if (new_inputs != last_inputs) shouldRedrawMenu = true;
    last_inputs = new_inputs;
    if (last_fastforward != tVBOpt.FASTFORWARD) shouldRedrawMenu = true;
    last_fastforward = tVBOpt.FASTFORWARD;
    if (!!save_thread != last_saving) shouldRedrawMenu = true;
    last_saving = !!save_thread;
    if (replay_playing() != last_replay) shouldRedrawMenu = true;
    last_replay = replay_playing();

    if (!shouldRedrawMenu && !tVBOpt.PERF_INFO)
        return;

    C2D_Prepare();
    C2D_SceneBegin(screen);

    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

    if (shouldRedrawMenu) {
        shouldRedrawMenu = false;
        C2D_TargetClear(screen, 0);
        drawTouchControls(0);
        // fastforward icon
        if (tVBOpt.FASTFORWARD) C2D_DrawRectSolid(0, 240-32, 0, 32, 32, C2D_Color32(32, 32, 32, 255));
        int col_line = C2D_Color32(64, 64, 64, 255);
        C2D_DrawLine(0, 240-32, col_line, 31.5, 240-32, col_line, 1, 0);
        C2D_DrawLine(31.5, 240-32, col_line, 31.5, 240, col_line, 1, 0);
        int t1l = 8 - 5 * !tVBOpt.PERF_INFO;
        int t1r = 15 - !tVBOpt.PERF_INFO;
        int t2l = 18 - !tVBOpt.PERF_INFO;
        int t2r = 25 + 3 * !tVBOpt.PERF_INFO;
        int tu = 240-28;
        int tm = 240-22 + 6 * !tVBOpt.PERF_INFO;
        int tb = 240-16 + 12 * !tVBOpt.PERF_INFO;
        C2D_DrawTriangle(t1l, tu, col_line, t1l, tb, col_line, t1r, tm, col_line, 0);
        C2D_DrawTriangle(t2l, tu, col_line, t2l, tb, col_line, t2r, tm, col_line, 0);
        if (!old_2ds) {
            // backlight icon
            int col_top = C2D_Color32(TINT_R*0.5, TINT_G*0.5, TINT_B*0.5, 255);
            int col_off = C2D_Color32(32, 32, 32, 255);
            C2D_DrawLine(31.5, 0, col_line, 31.5, 31.5, col_line, 1, 0);
            C2D_DrawLine(0, 31.5, col_line, 31.5, 31.5, col_line, 1, 0);
            C2D_DrawCircleSolid(16, 10, 0, 8, col_line);
            C2D_DrawTriangle(
                10, 15, col_line,
                12, 22, col_line,
                32 - 12, 22, col_line, 0);
            C2D_DrawTriangle(
                32 - 10, 15, col_line,
                10, 15, col_line,
                32 - 12, 22, col_line, 0);
            C2D_DrawRectSolid(12, 22, 0, 8, 3, col_line);
            C2D_DrawRectSolid(13, 25, 0, 6, 2, col_line);
        }
    }
    
    draw_status_bar(total_time, drc_time);

    if (save_thread) C2D_DrawText(&text_saving, C2D_AlignLeft, 0, 224, 0, 0.5, 0.5);

    C2D_Flush();

    C3D_ColorLogicOp(GPU_LOGICOP_COPY);
}

bool guiShouldPause() {
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    return (touch_pos.px < tVBOpt.PAUSE_RIGHT && (touch_pos.px >= 32 || (touch_pos.py > (old_2ds ? 0 : 32) && touch_pos.py < 240-32))) && backlightEnabled;
}

int guiGetInput(bool do_switching) {
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    if (backlightEnabled) {
        if ((hidKeysHeld() & KEY_TOUCH) && guiShouldSwitch()) {
            if (do_switching && (hidKeysDown() & KEY_TOUCH)) setTouchControls(!buttons_on_screen);
            return 0;
        }
        if (do_switching) {
            if (touch_pos.px < 32 && touch_pos.py >= 240-32) {
                if ((tVBOpt.FF_TOGGLE ? hidKeysDown() : hidKeysHeld()) & KEY_TOUCH) {
                    tVBOpt.FASTFORWARD = !tVBOpt.FASTFORWARD;
                }
                return 0;
            }
        }
        if (hidKeysDown() & KEY_TOUCH && (touch_pos.px <= 32 && touch_pos.py <= 32) && !old_2ds) {
            backlightEnabled = toggleBacklight(false);
            return 0;
        }
        if (touch_pos.px < tVBOpt.PAUSE_RIGHT) {
            return 0;
        }
        if (buttons_on_screen) {
            int axdist = touch_pos.px - tVBOpt.TOUCH_AX;
            int aydist = touch_pos.py - tVBOpt.TOUCH_AY;
            int bxdist = touch_pos.px - tVBOpt.TOUCH_BX;
            int bydist = touch_pos.py - tVBOpt.TOUCH_BY;
            if (axdist*axdist + aydist*aydist < bxdist*bxdist + bydist*bydist)
                return VB_KEY_A;
            else
                return VB_KEY_B;
        } else {
            int xdist = touch_pos.px - tVBOpt.TOUCH_PADX;
            int ydist = touch_pos.py - tVBOpt.TOUCH_PADY;
            if (abs(xdist) >= abs(ydist)) {
                return xdist >= 0 ? VB_RPAD_R : VB_RPAD_L;
            } else {
                return ydist <= 0 ? VB_RPAD_U : VB_RPAD_D;
            }
        }
    } else if (hidKeysDown() & KEY_TOUCH && (touch_pos.px > 32 || touch_pos.py > 32)) {
        backlightEnabled = toggleBacklight(true);
    }
    return 0;
}
