#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <arpa/inet.h>
#include <zlib.h>
#include "c2d/sprite.h"
#include "c2d/text.h"
#include <citro2d.h>
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_gui.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "replay.h"
#include "main.h"
#include "periodic.h"
#include "sprites_t3x.h"
#include "sprites.h"
#include "splash_t3x.h"
#include "splash.h"
#include "vblink.h"
#include "cpp.h"
#include "extrapad.h"
#include "multiplayer.h"

#define COLOR_R(COLOR) ( ((COLOR) & 0x000000FF) )
#define COLOR_G(COLOR) ( ((COLOR) & 0x0000FF00) >> 8)
#define COLOR_B(COLOR) ( ((COLOR) & 0x00FF0000) >> 16 )
#define COLOR_BRIGHTNESS(COLOR, BRIGHTNESS) ( C2D_Color32(COLOR_R(COLOR) * ( BRIGHTNESS ), COLOR_G(COLOR) * ( BRIGHTNESS ), COLOR_B(COLOR) * ( BRIGHTNESS ), 255) )

#define TINT_R ( COLOR_R(tVBOpt.TINT) )
#define TINT_G ( COLOR_G(tVBOpt.TINT) )
#define TINT_B ( COLOR_B(tVBOpt.TINT) )

#define BLACK ( C2D_Color32(0, 0, 0, 255) )
#define TINT_BRIGHTNESS(BRIGHTNESS) ( COLOR_BRIGHTNESS(tVBOpt.TINT, BRIGHTNESS) )
#define TINT_100 TINT_BRIGHTNESS(1.0)
#define TINT_90 TINT_BRIGHTNESS(0.9)
#define TINT_75 TINT_BRIGHTNESS(0.75)
#define TINT_50 TINT_BRIGHTNESS(0.5)
#define TINT_COLOR TINT_100

#define PERFORM_FOR_EACH_3DS_BUTTON(MACRO) \
    MACRO(DUP) \
    MACRO(DDOWN) \
    MACRO(DLEFT) \
    MACRO(DRIGHT) \
    MACRO(CPAD_UP) \
    MACRO(CPAD_DOWN) \
    MACRO(CPAD_LEFT) \
    MACRO(CPAD_RIGHT) \
    MACRO(CSTICK_UP) \
    MACRO(CSTICK_DOWN) \
    MACRO(CSTICK_LEFT) \
    MACRO(CSTICK_RIGHT) \
    MACRO(A) \
    MACRO(X) \
    MACRO(B) \
    MACRO(Y) \
    MACRO(START) \
    MACRO(SELECT) \
    MACRO(L) \
    MACRO(R) \
    MACRO(ZL) \
    MACRO(ZR)

#define PERFORM_FOR_EACH_VB_BUTTON(MACRO) \
    MACRO(KEY_L) \
    MACRO(KEY_R) \
    MACRO(KEY_SELECT) \
    MACRO(KEY_START) \
    MACRO(KEY_B) \
    MACRO(KEY_A) \
    MACRO(RPAD_R) \
    MACRO(RPAD_L) \
    MACRO(RPAD_D) \
    MACRO(RPAD_U) \
    MACRO(LPAD_R) \
    MACRO(LPAD_L) \
    MACRO(LPAD_D) \
    MACRO(LPAD_U)

bool buttons_on_screen = false;
bool guiShouldSwitch(void);
void drawTouchControls(int inputs);

bool old_2ds = false;
bool any_2ds = false;

static C3D_RenderTarget *screen;

static C2D_TextBuf static_textbuf;
static C2D_TextBuf dynamic_textbuf;

static C2D_Text text_A, text_B, text_btn_A, text_btn_B, text_btn_X, text_btn_L, text_btn_R,
                text_switch, text_saving, text_on, text_off, text_toggle, text_hold, text_nintendo_3ds,
                text_vbipd, text_left, text_right, text_sound_error, text_anykeyexit, text_about,
                text_debug_filenames, text_loading, text_loaderr, text_unloaded, text_yes, text_no,
                text_areyousure_reset, text_areyousure_exit, text_savestate_menu, text_save, text_load,
                text_vb_lpad, text_vb_rpad, text_mirror_abxy, text_vblink, text_preset, text_custom,
                text_error, text_3ds, text_vb, text_map, text_currently_mapped_to, text_normal, text_turbo,
                text_current_default, text_anaglyph, text_depth, text_cpp_on, text_cpp_off,
                text_monochrome, text_multicolor, text_brighten, text_brightness_disclaimer,
                text_multi_init_error, text_multi_disconnect;

#define CUSTOM_3DS_BUTTON_TEXT(BUTTON) static C2D_Text text_custom_3ds_button_##BUTTON;
PERFORM_FOR_EACH_3DS_BUTTON(CUSTOM_3DS_BUTTON_TEXT)
#define CUSTOM_VB_BUTTON_TEXT(BUTTON) static C2D_Text text_custom_vb_button_##BUTTON;
PERFORM_FOR_EACH_VB_BUTTON(CUSTOM_VB_BUTTON_TEXT)

static C2D_SpriteSheet sprite_sheet;
static C2D_SpriteSheet splash_sheet;
static C2D_Sprite colour_wheel_sprite, logo_sprite, vb_icon_sprite;
static C2D_Sprite text_3ds_sprite, text_vb_sprite, text_toggle_sprite, text_turbo_sprite;
static C2D_Sprite splash_left, splash_right;

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
    bool show_toggle, toggle, show_option, hidden, draw_selected_rect;
    int option;
    int colour;
    C2D_Text *toggle_text_on;
    C2D_Text *toggle_text_off;
    C2D_Text **option_texts;
    C2D_Text text;
    void (*custom_draw)(Button*);
};

static Button* selectedButton = NULL;
static bool buttonLock = false;

static inline int handle_buttons(Button buttons[], int count);
#define HANDLE_BUTTONS(buttons) handle_buttons((buttons), sizeof((buttons)) / sizeof((buttons)[0]))

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
    if (initial_button >= 0) selectedButton = &(buttons)[initial_button]; \
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
        button = HANDLE_BUTTONS((buttons)); \
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
    {.str="Multi\nplayer", .x=0, .y=176, .w=80, .h=64},
    {.str="Options", .x=240, .y=176, .w=80, .h=64},
    {.str="Quit", .x=112, .y=192, .w=96, .h=48},
};

static void game_menu(int initial_button);
static Button game_menu_buttons[] = {
    #define MAIN_MENU_LOAD_ROM 0
    {.str="Load ROM", .x=232 - 16, .y=64, .w=80 + 16, .h=80},
    #define MAIN_MENU_MULTI 1
    {.str="Multi\nplayer", .x=0, .y=176, .w=80, .h=64},
    #define MAIN_MENU_OPTIONS 2
    {.str="Options", .x=240, .y=176, .w=80, .h=64},
    #define MAIN_MENU_QUIT 3
    {.str="Quit", .x=112, .y=192, .w=96, .h=48},
    #define MAIN_MENU_RESUME 4
    {.str="Resume", .x=0, .y=0, .w=320, .h=48},
    #define MAIN_MENU_RESET 5
    {.str="Reset", .x=8, .y=64, .w=80 + 16, .h=80},
    #define MAIN_MENU_SAVESTATES 6
    {.str="Savestates", .x=112, .y=64, .w=80 + 16, .h=80},
};

static void rom_loader(void);
static Button rom_loader_buttons[] = {
    #define ROM_LOADER_UP 0
    {.str="Up", .x=0, .y=0, .w=32, .h=32},
    #define ROM_LOADER_BACK 1
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void multiplayer_main(int initial_button);
static Button multiplayer_main_buttons[] = {
    #define MULTI_MAIN_HOST 0
    {.str="Host", .x=16, .y=16, .w=288, .h=48},
    #define MULTI_MAIN_JOIN 1
    {.str="Join", .x=16, .y=80, .w=288, .h=48},
    #define MULTI_MAIN_BACK 2
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void multiplayer_join(void);
static Button multiplayer_join_buttons[] = {
    {.str="", .x=72, .y=16+56*0, .w=176, .h=48},
    {.str="", .x=72, .y=16+56*1, .w=176, .h=48},
    {.str="", .x=72, .y=16+56*2, .w=176, .h=48},
    {.str="", .x=72, .y=16+56*3, .w=176, .h=48},
    #define MULTI_JOIN_COUNT 4
    #define MULTI_JOIN_REFRESH 4
    {.str="Refresh", .x=0, .y=0, .w=68, .h=32},
    #define MULTI_JOIN_BACK 5
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void multiplayer_room(int initial_button);
static Button multiplayer_room_buttons[] = {
    #define MULTI_ROOM_LEAVE 0
    {.str="Leave", .x=0, .y=208, .w=56, .h=32},
};

static void multiplayer_error(int err, C2D_Text *messsage);
static Button multiplayer_error_buttons[] = {
    {.str = "Exit", .x=160-48, .y=180, .w=48*2, .h=48},
};

static void controls(int initial_button);
static Button controls_buttons[] = {
    #define CONTROLS_CONTROL_SCHEME 0
    {.str="Control Scheme", .x=60, .y=16, .w=200, .h=48, .show_toggle=true, .toggle_text_on=&text_custom, .toggle_text_off=&text_preset},
    #define CONTROLS_CONFIGURE_SCHEME 1
    {.str="Configure Scheme", .x=60, .y=80, .w=200, .h=48},
    #define CONTROLS_TOUCHSCREEN 2
    {.str="Touchscreen settings", .x=60, .y=144, .w=200, .h=48},
    #define CONTROLS_DISPLAY 3
    {.str="Input display", .x=200, .y=200, .w=120, .h=40, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define CONTROLS_CPP 4
    {.str="Circle Pad Pro", .x=60, .y=200, .w=130, .h=40},
    #define CONTROLS_BACK 5
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void cpp_options(int initial_button);
static Button cpp_options_buttons[] = {
    #define CPP_TOGGLE 0
    {.str = "CPP enabled", .x=60, .y=16, .w=200, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define CPP_CALIBRATE 1
    {.str="Calibrate CPP", .x=60, .y=80, .w=200, .h=48},
    #define CPP_BACK 2
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void draw_abxy(Button*);
static void draw_shoulders(Button*);
static void preset_controls(int initial_button);
static Button preset_controls_buttons[] = {
    #define PRESET_CONTROLS_FACE 0
    // y is set later
    {.x=160-64, .w=128, .h=80, .custom_draw=draw_abxy},
    #define PRESET_CONTROLS_SHOULDER 1
    {.x=160-64, .y=0, .w=128, .h=40, .custom_draw=draw_shoulders},
    #define PRESET_CONTROLS_DPAD_MODE 2
    {.str="3DS D-Pad Mode", .x=60, .y=144, .w=200, .h=40, .show_option=true, .option_texts=(C2D_Text*[]){&text_vb_lpad, &text_vb_rpad, &text_mirror_abxy}},
    #define PRESET_CONTROLS_BACK 3
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

#define DECLARE_DRAW_CUSTOM_3DS_BUTTON_FUNCTION(CUSTOM_3DS_BUTTON) static void draw_custom_3ds_##CUSTOM_3DS_BUTTON(Button*);
PERFORM_FOR_EACH_3DS_BUTTON(DECLARE_DRAW_CUSTOM_3DS_BUTTON_FUNCTION)

static void custom_3ds_mappings(int initial_button);
static Button custom_3ds_mappings_buttons[] = {
    #define CUSTOM_3DS_MAPPINGS_L 0
    {.x=0, .y=0, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_L},
    #define CUSTOM_3DS_MAPPINGS_ZL 1
    {.x=108, .y=0, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_ZL},
    #define CUSTOM_3DS_MAPPINGS_ZR 2
    {.x=162, .y=0, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_ZR},
    #define CUSTOM_3DS_MAPPINGS_R 3
    {.x=270, .y=0, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_R},
    #define CUSTOM_3DS_MAPPINGS_CPAD_UP 4
    {.x=54, .y=30, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CPAD_UP},
    #define CUSTOM_3DS_MAPPINGS_CPAD_DOWN 5
    {.x=54, .y=76, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CPAD_DOWN},
    #define CUSTOM_3DS_MAPPINGS_CPAD_LEFT 6
    {.x=0, .y=53, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CPAD_LEFT},
    #define CUSTOM_3DS_MAPPINGS_CPAD_RIGHT 7
    {.x=108, .y=53, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CPAD_RIGHT},
    #define CUSTOM_3DS_MAPPINGS_DUP 8
    {.x=54, .y=122, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_DUP},
    #define CUSTOM_3DS_MAPPINGS_DDOWN 9
    {.x=54, .y=168, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_DDOWN},
    #define CUSTOM_3DS_MAPPINGS_DLEFT 10
    {.x=0, .y=145, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_DLEFT},
    #define CUSTOM_3DS_MAPPINGS_DRIGHT 11
    {.x=108, .y=145, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_DRIGHT},
    #define CUSTOM_3DS_MAPPINGS_CSTICK_UP 12
    {.x=216, .y=30, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CSTICK_UP},
    #define CUSTOM_3DS_MAPPINGS_CSTICK_DOWN 13
    {.x=216, .y=76, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CSTICK_DOWN},
    #define CUSTOM_3DS_MAPPINGS_CSTICK_LEFT 14
    {.x=162, .y=53, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CSTICK_LEFT},
    #define CUSTOM_3DS_MAPPINGS_CSTICK_RIGHT 15
    {.x=270, .y=53, .w=50, .h=42, .draw_selected_rect=true, .colour=0x808080, .custom_draw=draw_custom_3ds_CSTICK_RIGHT},
    #define CUSTOM_3DS_MAPPINGS_X 16
    {.x=216, .y=122, .w=50, .h=42, .draw_selected_rect=true, .colour=0xFF8000, .custom_draw=draw_custom_3ds_X},
    #define CUSTOM_3DS_MAPPINGS_B 17
    {.x=216, .y=168, .w=50, .h=42, .draw_selected_rect=true, .colour=0x00FFFF, .custom_draw=draw_custom_3ds_B},
    #define CUSTOM_3DS_MAPPINGS_Y 18
    {.x=162, .y=145, .w=50, .h=42, .draw_selected_rect=true, .colour=0x00FF00, .custom_draw=draw_custom_3ds_Y},
    #define CUSTOM_3DS_MAPPINGS_A 19
    {.x=270, .y=145, .w=50, .h=42, .draw_selected_rect=true, .colour=0x0000FF, .custom_draw=draw_custom_3ds_A},
    #define CUSTOM_3DS_MAPPINGS_SELECT 20
    {.x=108, .y=198, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_SELECT},
    #define CUSTOM_3DS_MAPPINGS_START 21
    {.x=162, .y=198, .w=50, .h=42, .draw_selected_rect=true, .custom_draw=draw_custom_3ds_START},
    #define CUSTOM_3DS_MAPPINGS_RESET 22
    {.str="Reset", .x=268, .y=208, .w=52, .h=32},
    #define CUSTOM_3DS_MAPPINGS_BACK 23
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void custom_vb_mappings(int initial_button);
static Button custom_vb_mappings_buttons[] = {
    #define CUSTOM_VB_MAPPINGS_KEY_L 0
    {.str="\uE004", .x=0, .y=0, .w=64, .h=38, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_KEY_R 1
    {.str="\uE005", .x=256, .y=0, .w=64, .h=38, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_LPAD_U 2
    {.str="L\uE079", .x=34, .y=42, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_LPAD_D 3
    {.str="L\uE07A", .x=34, .y=122, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_LPAD_L 4
    {.str="L\uE07B", .x=2, .y=82, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_LPAD_R 5
    {.str="L\uE07C", .x=66, .y=82, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_RPAD_U 6
    {.str="R\uE079", .x=226, .y=42, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_RPAD_D 7
    {.str="R\uE07A", .x=226, .y=122, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_RPAD_L 8
    {.str="R\uE07B", .x=194, .y=82, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_RPAD_R 9
    {.str="R\uE07C", .x=258, .y=82, .w=60, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_KEY_SELECT 10
    {.str="SELECT", .x=58, .y=162, .w=76, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_KEY_START 11
    {.str="START", .x=82, .y=202, .w=76, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_KEY_B 12
    {.str="\uE001", .x=162, .y=202, .w=76, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_KEY_A 13
    {.str="\uE000", .x=186, .y=162, .w=76, .h=36, .draw_selected_rect=true},
    #define CUSTOM_VB_MAPPINGS_BACK 14
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define CUSTOM_VB_MAPPINGS_MOD 15
    {.str="Mod", .x=260, .y=200, .w=60, .h=40, .show_option=true, .option_texts=(C2D_Text*[]){&text_normal, &text_toggle, &text_turbo}},
};

static void touchscreen_settings(void);
static Button touchscreen_settings_buttons[] = {
    #define TOUCHSCREEN_BACK 0
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define TOUCHSCREEN_RESET 1
    {.str="Reset", .x=0, .y=0, .w=52, .h=24},
    #define TOUCHSCREEN_SWITCH 2
    {.str="Toggle Switch", .x=0, .y=28, .w=128, .h=24},
    #define TOUCHSCREEN_DEFAULT 3
    {.str="Make default", .x=0, .y=56, .w=110, .h=24},
};

static void options(int initial_button);
static Button options_buttons[] = {
    #define OPTIONS_VIDEO 0
    {.str="Video\nsettings", .x=16, .y=16, .w=96-8, .h=48},
    #define OPTIONS_CONTROLS 1
    {.str="Controls", .x=112-2, .y=16, .w=96+4, .h=48},
    #define OPTIONS_SOUND 2
    {.str="Sound", .x=208+8, .y=16, .w=96-8, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define OPTIONS_PERF 3
    {.str="Perf.\nsettings", .x=16, .y=80, .w=96-8, .h=48},
    #define OPTIONS_FF 4
    {.str="Fastforward", .x=112-2, .y=80, .w=96+4, .h=48, .show_toggle=true, .toggle_text_on=&text_toggle, .toggle_text_off=&text_hold},
    #define OPTIONS_ABOUT 5
    {.str="About", .x=208+8, .y=80, .w=96-8, .h=48},
    #define OPTIONS_SAVE_GLOBAL 6
    {.str="Save\n(Global)", .x=16, .y=144, .w=96-8, .h=48},
    #define OPTIONS_SAVE_GAME 7
    {.str="Save\n(Game)", .x=112-2, .y=144, .w=96+4, .h=48},
    #define OPTIONS_DISCARD 8
    {.str="Discard", .x=208+8, .y=144, .w=96-8, .h=48},
    #define OPTIONS_RESET_TO_GLOBAL 9
    {.str="Restore\nGlobal", .y=144, .h=48},
    #define OPTIONS_BACK  10
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define OPTIONS_DEBUG 11
    {.str="Save debug info", .x=170, .y=208, .w=150, .h=32},
};

static void video_settings(int initial_button);
static Button video_settings_buttons[] = {
    #define VIDEO_MODE 0
    {.str="3D mode", .x=16, .y=16, .w=128, .h=48, .show_toggle=true, .toggle_text_on=&text_anaglyph, .toggle_text_off=&text_nintendo_3ds},
    #define VIDEO_SETTINGS 1
    {.str="Settings", .x=176, .y=16, .w=128, .h=48},
    #define VIDEO_ANTIFLICKER 2
    {.str="Antiflicker", .x=16, .y=80, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define VIDEO_SLIDER 3
    {.str = "Slider mode", .x=16, .y=80+64, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_vbipd, .toggle_text_off=&text_nintendo_3ds},
    #define VIDEO_BACK 4
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void barrier_settings(int initial_button);
static Button barrier_settings_buttons[] = {
    #define BARRIER_MODE 0
    {.str="Color mode", .x=16, .y=16, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_multicolor, .toggle_text_off=&text_monochrome},
    #define BARRIER_SETTINGS 1
    {.str="Color settings", .x=16, .y=80, .w=288, .h=48},
    #define BARRIER_DEFAULT_EYE 2
    {.str="Default eye", .x=16, .y=80+64, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_right, .toggle_text_off=&text_left},
    #define BARRIER_BACK 3
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void anaglyph_settings(int initial_button);
static Button anaglyph_settings_buttons[] = {
    #define ANAGLYPH_LEFT_OFF 0
    {.x=50, .y=16+24*0, .w=80, .h=20, .colour=0x404040},
    #define ANAGLYPH_LEFT_RED 1
    {.x=50, .y=16+24*1, .w=80, .h=20, .colour=0x0000FF},
    #define ANAGLYPH_LEFT_GREEN 2
    {.x=50, .y=16+24*2, .w=80, .h=20, .colour=0x00FF00},
    #define ANAGLYPH_LEFT_YELLOW 3
    {.x=50, .y=16+24*3, .w=80, .h=20, .colour=0x00FFFF},
    #define ANAGLYPH_LEFT_BLUE 4
    {.x=50, .y=16+24*4, .w=80, .h=20, .colour=0xFF0000},
    #define ANAGLYPH_LEFT_MAGENTA 5
    {.x=50, .y=16+24*5, .w=80, .h=20, .colour=0xFF00FF},
    #define ANAGLYPH_LEFT_CYAN 6
    {.x=50, .y=16+24*6, .w=80, .h=20, .colour=0xFFFF00},
    #define ANAGLYPH_LEFT_WHITE 7
    {.x=50, .y=16+24*7, .w=80, .h=20, .colour=0xFFFFFF},
    
    #define ANAGLYPH_RIGHT_OFF 8
    {.x=176, .y=16+24*0, .w=80, .h=20, .colour=0x404040},
    #define ANAGLYPH_RIGHT_RED 9
    {.x=176, .y=16+24*1, .w=80, .h=20, .colour=0x0000FF},
    #define ANAGLYPH_RIGHT_GREEN 10
    {.x=176, .y=16+24*2, .w=80, .h=20, .colour=0x00FF00},
    #define ANAGLYPH_RIGHT_YELLOW 11
    {.x=176, .y=16+24*3, .w=80, .h=20, .colour=0x00FFFF},
    #define ANAGLYPH_RIGHT_BLUE 12
    {.x=176, .y=16+24*4, .w=80, .h=20, .colour=0xFF0000},
    #define ANAGLYPH_RIGHT_MAGENTA 13
    {.x=176, .y=16+24*5, .w=80, .h=20, .colour=0xFF00FF},
    #define ANAGLYPH_RIGHT_CYAN 14
    {.x=176, .y=16+24*6, .w=80, .h=20, .colour=0xFFFF00},
    #define ANAGLYPH_RIGHT_WHITE 15
    {.x=176, .y=16+24*7, .w=80, .h=20, .colour=0xFFFFFF},

    #define ANAGLYPH_BACK 16
    {.str="Back", .x=0, .y=208, .w=48, .h=32},

    #define ANAGLYPH_DEPTH_PLACEHOLDER 17
    {.x=240, .y=16, .w=1, .h=1, .hidden=true},
};

static void colour_filter(void);
static Button colour_filter_buttons[] = {
    #define COLOUR_BACK 0
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define COLOUR_RED 1
    {.str="Red", .x=16, .y=64, .w=48, .h=32},
    #define COLOUR_GRAY 2
    {.str="Gray", .x=16, .y=128, .w=48, .h=32},
};

static void draw_multislot(Button*);
static void multicolour_picker(int initial_button);
static Button multicolour_picker_buttons[] = {
    {.x=16, .y=16, .w=200, .h=40, .custom_draw=draw_multislot},
    {.str="Edit", .x=224, .y=16, .w=80, .h=40},
    {.x=16, .y=16+48, .w=200, .h=40, .custom_draw=draw_multislot},
    {.str="Edit", .x=224, .y=16+48, .w=80, .h=40},
    {.x=16, .y=16+48*2, .w=200, .h=40, .custom_draw=draw_multislot},
    {.str="Edit", .x=224, .y=16+48*2, .w=80, .h=40},
    {.x=16, .y=16+48*3, .w=200, .h=40, .custom_draw=draw_multislot},
    {.str="Edit", .x=224, .y=16+48*3, .w=80, .h=40},
    #define MULTIPICKER_BACK 8
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void multicolour_settings(int palette_id, int initial_button);
static Button multicolour_settings_buttons[] = {
    #define MULTI_BLACK 0
    {.str="Darkest", .x=16, .y=16, .w=170, .h=40},
    #define MULTI_BRTA
    {.str="Dark", .x=16, .y=16+48, .w=170, .h=40},
    #define MULTI_BRTB
    {.str="Light", .x=16, .y=16+48*2, .w=170, .h=40},
    #define MULTI_BRTC
    {.str="Lightest", .x=16, .y=16+48*3, .w=170, .h=40},
    #define MULTI_BACK 4
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static void multicolour_wheel(int palette_id, int colour_id);
static Button multicolour_wheel_buttons[] = {
    #define MULTIWHEEL_BACK 0
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
    #define MULTIWHEEL_HEX 1
    {.str="", .x=100, .y=212, .w=100, .h=28},
    #define MULTIWHEEL_SCALE 2
    {.str="", .x=220, .y=212, .w=100, .h=28},
};

static void vblink(void);
static Button vblink_buttons[] = {};

static void dev_options(int initial_button);
static Button dev_options_buttons[] = {
    #define PERF_BAR 0
    {.str="Status bar", .x=16, .y=16, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define PERF_VIP 1
    {.str="Overclock VIP", .x=16, .y=80, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define PERF_N3DS 2
    {.str="N3DS speedup", .x=16, .y=80+64, .w=288, .h=48, .show_toggle=true, .toggle_text_on=&text_on, .toggle_text_off=&text_off},
    #define PERF_BACK 3
    {.str="Back", .x=0, .y=208, .w=48, .h=32},
};

static bool areyousure(C2D_Text *message);
static Button areyousure_buttons[] = {
    #define AREYOUSURE_YES 0
    {.str="Yes", .x=160-48-32, .y=180, .w=64, .h=48},
    #define AREYOUSURE_NO 1
    {.str="No", .x=160+32, .y=180, .w=64, .h=48},
};

static void savestate_menu(int initial_button, int selected_state);
static Button savestate_buttons[] = {
    #define SAVESTATE_BACK 0
    {.str="Back", .x=0, .y=208, .w=60, .h=32},
    #define SAVE_SAVESTATE 1
    {.str="Save", .x=80, .y=170, .w=70, .h=70},
    #define LOAD_SAVESTATE 2
    {.str="Load", .x=320 - 150, .y=170, .w=70, .h=70},
    #define DELETE_SAVESTATE 3
    {.str="Delete", .x=260, .y=208, .w=60, .h=32},
    #define PREV_SAVESTATE 4
    {.str="<\n\uE004", .x=16, .y=60, .w=40, .h=100},
    #define NEXT_SAVESTATE 5
    {.str=">\n\uE005", .x=320 - 56, .y=60, .w=40, .h=100},
};

static void savestate_confirm(char *message, int last_button, int selected_state);
static Button savestate_confirm_buttons[] = {
    {.str="Return to game", .x=160-96, .y=140, .w=96*2, .h=48},
    {.str="Return to menu", .x=160-96, .y=190, .w=96*2, .h=48},
};

static void sound_error(void);
static Button sound_error_buttons[] = {
    {.str="Continue without sound", .x=48, .y=130, .w=320-48*2, .h=32},
};

static void about(void);
static Button about_buttons[] = {
    {.str="Back", .x=160-48, .y=180, .w=48*2, .h=48},
};

static void load_rom(void);
static Button load_rom_buttons[] = {
    {.str="Unload & cancel", .x=160-80, .y=180, .w=80*2, .h=48},
};

static void forwarder_error(int err);
static Button forwarder_error_buttons[] = {
    {.str = "Exit", .x=160-48, .y=180, .w=48*2, .h=48},
};

#define SETUP_ALL_BUTTONS \
    SETUP_BUTTONS(first_menu_buttons); \
    SETUP_BUTTONS(game_menu_buttons); \
    SETUP_BUTTONS(rom_loader_buttons); \
    SETUP_BUTTONS(multiplayer_main_buttons); \
    SETUP_BUTTONS(multiplayer_join_buttons); \
    SETUP_BUTTONS(multiplayer_room_buttons); \
    SETUP_BUTTONS(multiplayer_error_buttons); \
    SETUP_BUTTONS(controls_buttons); \
    SETUP_BUTTONS(cpp_options_buttons); \
    SETUP_BUTTONS(preset_controls_buttons); \
    SETUP_BUTTONS(custom_3ds_mappings_buttons); \
    SETUP_BUTTONS(custom_vb_mappings_buttons); \
    SETUP_BUTTONS(options_buttons); \
    SETUP_BUTTONS(video_settings_buttons); \
    SETUP_BUTTONS(barrier_settings_buttons); \
    SETUP_BUTTONS(anaglyph_settings_buttons); \
    SETUP_BUTTONS(colour_filter_buttons); \
    SETUP_BUTTONS(multicolour_picker_buttons); \
    SETUP_BUTTONS(multicolour_settings_buttons); \
    SETUP_BUTTONS(multicolour_wheel_buttons); \
    SETUP_BUTTONS(dev_options_buttons); \
    SETUP_BUTTONS(sound_error_buttons); \
    SETUP_BUTTONS(touchscreen_settings_buttons); \
    SETUP_BUTTONS(about_buttons); \
    SETUP_BUTTONS(load_rom_buttons); \
    SETUP_BUTTONS(areyousure_buttons); \
    SETUP_BUTTONS(savestate_buttons); \
    SETUP_BUTTONS(savestate_confirm_buttons);

static int last_savestate = 0;

static void draw_logo(void) {
    vb_state->tVIPREG.BRTA = 49;
    vb_state->tVIPREG.BRTB = 73;
    vb_state->tVIPREG.BRTC = 145 - vb_state->tVIPREG.BRTA - vb_state->tVIPREG.BRTB;
	memset((u8*)vb_state->V810_DISPLAY_RAM.off + 0x3dc00, 0, 0x400);
    tDSPCACHE.ColumnTableInvalid = true;
    
    for (int i = 0; i < 2; i++) {
        C2D_SceneBegin(screenTargetHard[i]);
        C2D_ViewScale(1, -1);
        C2D_ViewTranslate(0, -512);
        C2D_DrawSprite(&splash_left);
        C2D_DrawSprite(&splash_right);
    }
    C2D_ViewReset();
    C2D_SceneBegin(screen);
}

static void first_menu(int initial_button) {
    static bool attempted_forwarder = false;
    if (!attempted_forwarder) {
        attempted_forwarder = true;

        FILE *filename_txt = fopen("romfs:/filename.txt", "r");
        if (!filename_txt) goto no_forwarder;
        char forwarded_path[300] = {0};
        strcpy(forwarded_path, "romfs:/");
        char *filename = forwarded_path + strlen(forwarded_path);
        fread(filename, 1, sizeof(forwarded_path) - 7, filename_txt);
        fclose(filename_txt);
        {
            // trim any newline characters
            char *cr = strchr(filename, '\r');
            if (cr) *cr = 0;
            char *lf = strchr(filename, '\n');
            if (lf) *lf = 0;
        }
        // we have our filename, bail if it doesn't exist
        if (access(forwarded_path, F_OK)) goto no_forwarder;

        strcpy(tVBOpt.ROM_PATH, forwarded_path);
        if (access(tVBOpt.HOME_PATH, F_OK)) mkdir(tVBOpt.HOME_PATH, 0777);
        snprintf(tVBOpt.RAM_PATH, sizeof(tVBOpt.RAM_PATH), "%s/saves/", tVBOpt.HOME_PATH);
        if (access(tVBOpt.RAM_PATH, F_OK)) mkdir(tVBOpt.RAM_PATH, 0777);
        strncat(tVBOpt.RAM_PATH, filename, sizeof(tVBOpt.RAM_PATH) - 1);
        char *extension = strrchr(tVBOpt.RAM_PATH, '.');
        if (!extension) goto no_forwarder;
        strcpy(extension, ".ram");

        // at this point we know we're a forwarder, so just load the rom
        tVBOpt.FORWARDER = true;
        [[gnu::musttail]] return load_rom();
    }

    no_forwarder:
    LOOP_BEGIN(first_menu_buttons, initial_button);
        draw_logo();
        if (hidKeysDown() & KEY_Y) loop = false;
    LOOP_END(first_menu_buttons);
    if (hidKeysDown() & KEY_Y) [[gnu::musttail]] return vblink();
    guiop = 0;
    switch (button) {
        case MAIN_MENU_LOAD_ROM:
            [[gnu::musttail]] return rom_loader();
        case MAIN_MENU_MULTI:
            [[gnu::musttail]] return multiplayer_main(0);
        case MAIN_MENU_OPTIONS:
            [[gnu::musttail]] return options(0);
        case MAIN_MENU_QUIT:
            if (areyousure(&text_areyousure_exit)) {
            guiop = GUIEXIT;
            return;
            } else [[gnu::musttail]] return first_menu(MAIN_MENU_QUIT);
    }
}

static void game_menu(int initial_button) {
    if (tVBOpt.FORWARDER) {
        game_menu_buttons[MAIN_MENU_LOAD_ROM].hidden = true;
        game_menu_buttons[MAIN_MENU_RESET].x = 16;
        game_menu_buttons[MAIN_MENU_RESET].w = 80 + 48;
        game_menu_buttons[MAIN_MENU_SAVESTATES].x = 224 - 48;
        game_menu_buttons[MAIN_MENU_SAVESTATES].w = 80 + 48;
    }
    LOOP_BEGIN(game_menu_buttons, initial_button);
        if (!tVBOpt.FORWARDER && (hidKeysDown() & KEY_Y)) loop = false;
    LOOP_END(game_menu_buttons);
    if (!tVBOpt.FORWARDER && (hidKeysDown() & KEY_Y)) [[gnu::musttail]] return vblink();
    switch (button) {
        case MAIN_MENU_LOAD_ROM:
            guiop = AKILL | VBRESET;
            [[gnu::musttail]] return rom_loader();
        case MAIN_MENU_MULTI:
            [[gnu::musttail]] return multiplayer_main(0);
        case MAIN_MENU_OPTIONS:
            [[gnu::musttail]] return options(0);
        case MAIN_MENU_QUIT:
            if (areyousure(&text_areyousure_exit)) {
                guiop = AKILL | GUIEXIT;
                return;
            } else [[gnu::musttail]] return game_menu(MAIN_MENU_QUIT);
        case MAIN_MENU_RESUME:
            // curly braces to avoid compiler error
            {
                guiop = 0;
                return;
            }
        case MAIN_MENU_RESET:
            if (areyousure(&text_areyousure_reset)) {
                // clear screen buffer
                for (int i = 0; i < 2; i++) {
                    C2D_TargetClear(screenTargetHard[i], 0);
                }
                C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
                video_flush(true);
                C3D_FrameEnd(0);
                guiop = AKILL | VBRESET;
                return;
            } else [[gnu::musttail]] return game_menu(MAIN_MENU_RESET);
        case MAIN_MENU_SAVESTATES:
            [[gnu::musttail]] return savestate_menu(0, last_savestate);
    }
}

static void main_menu(int initial_button) {
    if (game_running) game_menu(initial_button);
    else first_menu(initial_button);
}

int strptrcmp(const void *s1, const void *s2) {
    return strcasecmp(*(const char**)s1, *(const char**)s2);
}

static void rom_loader(void) {
    static char path[300] = {0};
    static char old_dir[300] = {0};
    if (!path[0]) {
        if (tVBOpt.ROM_PATH[0]) {
            strcpy(path, tVBOpt.ROM_PATH);
        } else {
            path[0] = 0;
        }
    }
    if (!old_dir[0] && tVBOpt.ROM_PATH[0]) strcpy(old_dir, tVBOpt.ROM_PATH);

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

    if (old_dir[0] && strstr(old_dir, path) == old_dir) {
        char *filename = strrchr(old_dir, '/');
        // null check but also skip the slash
        if (filename++) {
            for (int i = 0; i < dirCount; i++) {
                if (strcmp(dirs[i], filename) == 0) {
                    int button_y = i * entry_height;
                    scroll_pos = C2D_Clamp(button_y - (240 / 2), scroll_top, scroll_bottom);
                    cursor = i;
                    break;
                }
            }
            for (int i = 0; i < fileCount; i++) {
                if (strcmp(files[i], filename) == 0) {
                    i += dirCount;
                    int button_y = i * entry_height;
                    scroll_pos = C2D_Clamp(button_y - (240 / 2), scroll_top, scroll_bottom);
                    cursor = i;
                    break;
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
                // accomodate for null terminator and potentially 'vb' -> 'ram' lengthening
                if (new_path_len + 1 < sizeof(path)) {
                    strcat(path, new_entry);
                    if (clicked_entry < dirCount) {
                        // clicked on directory, so add a slash
                        path[new_path_len - 1] = '/';
                        path[new_path_len] = 0;
                    }
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
            top_pos = scroll_pos < scroll_top ? scroll_pos + 1 : scroll_top;
        }
        int bottom_pos = scroll_bottom;
        if (dragging) {
            bottom_pos += 8;
        } else if (scroll_pos < scroll_bottom) {
            bottom_pos += 8;
        } else {
            if (scroll_speed > 0) scroll_speed = 0;
            bottom_pos = scroll_pos > scroll_bottom ? scroll_pos - 1 : scroll_bottom;
        }
        scroll_pos += scroll_speed;
        scroll_pos = C2D_Clamp(scroll_pos, top_pos, bottom_pos);

        // keep cursor on screen
        while ((cursor - 1) * entry_height - scroll_pos < 0 && cursor < entry_count - 1)
            cursor++;
        while ((cursor + 1) * entry_height - scroll_pos > 240 && cursor > 0)
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
                C2D_DrawRectSolid(56, y, 0, 264, entry_height, clicked_entry == i ? TINT_50 : TINT_100);
                if (cursor == i) C2D_DrawRectSolid(56 + 4, y + entry_height - 8, 0, 264 - 8, 1, BLACK);
                C2D_DrawText(&text, C2D_AlignLeft, 64, y + 8, 0, 0.5, 0.5);
            }
            y += entry_height;
        }
        // scrollbar
        if (scroll_top != scroll_bottom) {
            C2D_DrawRectSolid(320-2, 32, 0, 2, 240-32, BLACK);
            C2D_DrawRectSolid(320-2, 32 + (240-32-8) * (scroll_pos - scroll_top) / (scroll_bottom - scroll_top), 0, 2, 8, TINT_COLOR);
        }
        // path
        C2D_TextParse(&text, dynamic_textbuf, path);
        C2D_TextOptimize(&text);
        C2D_DrawRectSolid(0, 0, 0, 320, 32, BLACK);
        C2D_DrawText(&text, C2D_AlignLeft | C2D_WithColor, 48, 8, 0, 0.5, 0.5, TINT_COLOR);

        // up button indicator
        C2D_DrawText(&text_btn_X, C2D_AlignLeft | C2D_WithColor, 8, 32, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(rom_loader_buttons);

    buttonLock = false;

    for (int i = 0; i < dirCount; i++) free(dirs[i]);
    for (int i = 0; i < fileCount; i++) free(files[i]);
    free(dirs);
    free(files);

    old_dir[0] = 0;

    if (clicked_entry < 0) {
        switch (button) {
            case ROM_LOADER_UP: // Up
                strcpy(old_dir, path);
                // cut trailing slash from old dir
                char *last_slash = strrchr(old_dir, '/');
                if (last_slash && !last_slash[1]) last_slash[0] = 0;

                int len = strlen(path);
                // don't get shorter than sdmc:/
                if (len > 6) {
                    // the stuff at the start of the file will get rid of everything after the last slash
                    // so we can just get rid of the slash at the end
                    path[len - 1] = 0;
                }
                [[gnu::musttail]] return rom_loader();
            case ROM_LOADER_BACK: [[gnu::musttail]] return main_menu(MAIN_MENU_LOAD_ROM);
        }
    } else if (clicked_entry < dirCount) {
        [[gnu::musttail]] return rom_loader();
    } else {
        // clear screen buffer
        for (int i = 0; i < 2; i++) {
            C2D_TargetClear(screenTargetHard[i], 0);
        }
        tDSPCACHE.DDSPDataState[0] = tDSPCACHE.DDSPDataState[1] = GPU_CLEAR;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 64; j++) {
                tDSPCACHE.SoftBufWrote[i][j].min = 0xff;
                tDSPCACHE.SoftBufWrote[i][j].max = 0;
            }
            memset(&tDSPCACHE.OpaquePixels, 0, sizeof(tDSPCACHE.OpaquePixels));
        }
        C3D_FrameBegin(0);
        video_flush(true);
        C3D_FrameEnd(0);

        // reload global settings in advance
        if (tVBOpt.GAME_SETTINGS) loadFileOptions();

        strcpy(tVBOpt.ROM_PATH, path);
        strcpy(tVBOpt.RAM_PATH, path);
        // we know there's a dot
        strcpy(strrchr(tVBOpt.RAM_PATH, '.'), ".ram");
        saveFileOptions();
        [[gnu::musttail]] return load_rom();
    }
}

static void multiplayer_main(int initial_button) {
    Result res = udsInit(0x3000, NULL);
    if (R_FAILED(res)) {
        return multiplayer_error(res, &text_multi_init_error);
    }
    LOOP_BEGIN(multiplayer_main_buttons, initial_button);
    LOOP_END(multiplayer_main_buttons);
    switch (button) {
        case MULTI_MAIN_HOST:
            res = create_network();
            if (R_FAILED(res)) {
                udsExit();
                [[gnu::musttail]] return multiplayer_error(res, &text_multi_init_error);
            } else {
                [[gnu::musttail]] return multiplayer_room(0);
            }
        case MULTI_MAIN_JOIN:
            [[gnu::musttail]] return multiplayer_join();
        case MULTI_MAIN_BACK:
            udsExit();
            [[gnu::musttail]] return main_menu(MAIN_MENU_MULTI);
    }
}

static void multiplayer_join() {
    udsNetworkScanInfo *networks;
    size_t total_networks;
    scan_beacons(&networks, &total_networks);

    C2D_TextBufClear(dynamic_textbuf);

    for (int i = 0; i < MULTI_JOIN_COUNT; i++) {
        multiplayer_join_buttons[i].hidden = i >= total_networks;
        if (!multiplayer_join_buttons[i].hidden) {
            char text[11] = {0};
            udsGetNodeInfoUsername(&networks[i].nodes[0], text);
            C2D_TextParse(&multiplayer_join_buttons[i].text, dynamic_textbuf, text);
            C2D_TextOptimize(&multiplayer_join_buttons[i].text);
        }
    }

    LOOP_BEGIN(multiplayer_join_buttons, 0);
    LOOP_END(multiplayer_join_buttons);
    if (button < MULTI_JOIN_COUNT) {
        Result res = connect_to_network(&networks[button].network);
        if (R_FAILED(res)) {
            udsExit();
            [[gnu::musttail]] return multiplayer_error(res, &text_multi_init_error);
        } else {
            [[gnu::musttail]] return multiplayer_room(0);
        }
    } else if (button == MULTI_JOIN_REFRESH) {
        [[gnu::musttail]] return multiplayer_join();
    } else if (button == MULTI_JOIN_BACK) {
        [[gnu::musttail]] return multiplayer_main(MULTI_MAIN_JOIN);
    }
}

static void multiplayer_room(int initial_button) {
    static udsConnectionStatus status;
    static char chars[16] = "";
    static C2D_Text text;
    int other_x, other_y;

    // udsGetConnectionStatus(&status);
    C2D_TextBufClear(dynamic_textbuf);
    C2D_TextParse(&text, dynamic_textbuf, chars);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(multiplayer_room_buttons, initial_button);
        if (udsWaitConnectionStatusEvent(false, false)) {
            udsGetConnectionStatus(&status);
            if (status.total_nodes == 0) {
                C3D_FrameEnd(0);
                local_disconnect();
                udsExit();
                [[gnu::musttail]] return multiplayer_error(0, &text_multi_disconnect);
            }
            snprintf(chars, sizeof(chars), "%d/%d", status.cur_NetworkNodeID, status.total_nodes);
            C2D_TextParse(&text, dynamic_textbuf, chars);
            C2D_TextOptimize(&text);
        }
        C2D_DrawText(&text, C2D_WithColor, 20, 20, 0, 1.0, 1.0, TINT_100);
        if (status.total_nodes == 2) {
            touchPosition touchPos;
            hidTouchRead(&touchPos);
            Packet *send_packet = new_packet_to_send();
            if (send_packet && (hidKeysHeld() & KEY_TOUCH)) {
                send_packet->packet_type = PACKET_INPUTS;
                send_packet->inputs.shb = touchPos.px;
                send_packet->inputs.slb = touchPos.py;
            }
            ship_packet(send_packet);
        }
        Packet *recv_packet;
        while ((recv_packet = read_next_packet())) {
            if (recv_packet->packet_type == PACKET_INPUTS) {
                C2D_DrawCircle(recv_packet->inputs.shb, recv_packet->inputs.slb, 0, 5, TINT_COLOR, TINT_COLOR, TINT_COLOR, TINT_COLOR);
            }
        }
    LOOP_END(multiplayer_room_buttons);
    if (button == MULTI_ROOM_LEAVE) {
        local_disconnect();
    }
    [[gnu::musttail]] return multiplayer_main(MULTI_MAIN_HOST);
}

static void multiplayer_error(int err, C2D_Text *message) {
    C2D_Text text;
    char code_message[32];
    snprintf(code_message, sizeof(code_message), "Error code: %d", err);
    C2D_TextParse(&text, dynamic_textbuf, code_message);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(multiplayer_error_buttons, 0);
        C2D_DrawText(message, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, TINT_COLOR);
        if (err != 0) C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 120, 0, 0.5, 0.5, TINT_COLOR);
    LOOP_END(multiplayer_error_buttons);
    [[gnu::musttail]] return main_menu(MAIN_MENU_MULTI);
}

static void controls(int initial_button) {
    bool new_3ds;
    APT_CheckNew3DS(&new_3ds);
    controls_buttons[CONTROLS_CONTROL_SCHEME].toggle = tVBOpt.CUSTOM_CONTROLS;
    controls_buttons[CONTROLS_DISPLAY].toggle = tVBOpt.INPUTS;
    controls_buttons[CONTROLS_CPP].hidden = new_3ds;

    LOOP_BEGIN(controls_buttons, initial_button);
    LOOP_END(controls_buttons);
    switch (button) {
        case CONTROLS_CONTROL_SCHEME:
            tVBOpt.CUSTOM_CONTROLS = !tVBOpt.CUSTOM_CONTROLS;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return controls(CONTROLS_CONTROL_SCHEME);
        case CONTROLS_CONFIGURE_SCHEME:
            if (tVBOpt.CUSTOM_CONTROLS) {
                [[gnu::musttail]] return custom_3ds_mappings(CUSTOM_3DS_MAPPINGS_BACK);
            } else {
                [[gnu::musttail]] return preset_controls(0);
            }
        case CONTROLS_TOUCHSCREEN:
            [[gnu::musttail]] return touchscreen_settings();
        case CONTROLS_DISPLAY:
            tVBOpt.INPUTS = !tVBOpt.INPUTS;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return controls(CONTROLS_DISPLAY);
        case CONTROLS_CPP:
            [[gnu::musttail]] return cpp_options(0);
        case CONTROLS_BACK:
            tVBOpt.CUSTOM_CONTROLS ? setCustomControls() : setPresetControls(buttons_on_screen);
            [[gnu::musttail]] return options(OPTIONS_CONTROLS);
    }
}

static void cpp_options(int initial_button) {
    cpp_options_buttons[CPP_TOGGLE].toggle = tVBOpt.CPP_ENABLED;
    LOOP_BEGIN(cpp_options_buttons, initial_button);
        if (tVBOpt.CPP_ENABLED) C2D_DrawText(cppGetConnected() ? &text_cpp_on : &text_cpp_off, C2D_AlignCenter | C2D_WithColor, 160, 150, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(cpp_options_buttons);
    switch (button) {
        case CPP_TOGGLE:
            tVBOpt.CPP_ENABLED = !tVBOpt.CPP_ENABLED;
            tVBOpt.MODIFIED = true;
            if (tVBOpt.CPP_ENABLED) {
                cppInit();
            } else {
                cppExit();
            }
            [[gnu::musttail]] return cpp_options(button);
        case CPP_CALIBRATE:
            toggleVsync(false);
            if (tVBOpt.CPP_ENABLED) cppExit();
            extraPadConf conf;
            extraPadInit(&conf);
            extraPadLaunch(&conf);
            if (tVBOpt.CPP_ENABLED) cppInit();
            toggleVsync(tVBOpt.VSYNC);
            [[gnu::musttail]] return cpp_options(button);
        case CPP_BACK:
            [[gnu::musttail]] return controls(CONTROLS_CPP);
    }
}

static C2D_Text * vb_button_code_to_vb_button_text(int vb_button_code) {
    switch (vb_button_code) {
        #define VB_BUTTON_CODE_TO_TEXT_CASE(VB_BUTTON) \
        case VB_##VB_BUTTON: \
            return &text_custom_vb_button_##VB_BUTTON;
        PERFORM_FOR_EACH_VB_BUTTON(VB_BUTTON_CODE_TO_TEXT_CASE)
        default:
            return &text_error;
    }
}

#define DRAW_CUSTOM_3DS_BUTTON_FUNCTION(CUSTOM_3DS_BUTTON) \
static void draw_custom_3ds_##CUSTOM_3DS_BUTTON(Button *self) { \
    C2D_DrawText(&text_custom_3ds_button_##CUSTOM_3DS_BUTTON, C2D_AlignLeft, self->x + 5, self->y + 3, 0, 0.6, 0.6); \
    C2D_DrawRectSolid(self->x + 12, self->y + self->h / 2, 0, self->w - 24, 1, BLACK); \
    C2D_DrawText(vb_button_code_to_vb_button_text(tVBOpt.CUSTOM_MAPPING_##CUSTOM_3DS_BUTTON), C2D_AlignRight, self->x + self->w - 5, self->y + self->h / 2, 0, 0.6, 0.6); \
    C2D_Sprite *sprite = NULL; \
    if (tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_##CUSTOM_3DS_BUTTON)] == 1) sprite = &text_toggle_sprite; \
    if (tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_##CUSTOM_3DS_BUTTON)] == 2) sprite = &text_turbo_sprite; \
    if (sprite) { \
        C2D_Flush(); \
        C3D_ColorLogicOp(GPU_LOGICOP_AND); \
        C2D_SpriteSetPos(sprite, self->x + 4, self->y + self->h - 9); \
        C2D_DrawSprite(sprite); \
        C2D_Flush(); \
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA); \
    } \
}
PERFORM_FOR_EACH_3DS_BUTTON(DRAW_CUSTOM_3DS_BUTTON_FUNCTION)

static int vb_button_code_to_vb_ui_button_index(int vb_button_code) {
    switch (vb_button_code) {
        #define VB_BUTTON_CODE_TO_INDEX_CASE(VB_BUTTON) \
        case VB_##VB_BUTTON: \
            return CUSTOM_VB_MAPPINGS_##VB_BUTTON;
        PERFORM_FOR_EACH_VB_BUTTON(VB_BUTTON_CODE_TO_INDEX_CASE)
        default:
            return CUSTOM_VB_MAPPINGS_BACK;
    }
}

static int current_custom_mapping_3ds_button;
static C2D_Text *current_custom_mapping_3ds_button_text;
static int *current_custom_mapping_vb_option;
static int *current_custom_mapping_mod;

static void custom_3ds_mappings(int initial_button) {
    bool new_3ds = tVBOpt.CPP_ENABLED;
    APT_CheckNew3DS(&new_3ds);
    if (!new_3ds) {
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_ZL].hidden = true;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_ZR].hidden = true;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_UP].hidden = true;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_DOWN].hidden = true;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_LEFT].hidden = true;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_RIGHT].hidden = true;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_A].y = custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_RIGHT].y;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_B].y = custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_DOWN].y;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_X].y = custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_UP].y;
        custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_Y].y = custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_LEFT].y;
    }
    LOOP_BEGIN(custom_3ds_mappings_buttons, initial_button);
        const Button *CPAD_LEFT = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CPAD_LEFT];
        const Button *CPAD_RIGHT = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CPAD_RIGHT];
        const Button *DPAD_LEFT = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_DLEFT];
        const Button *DPAD_RIGHT = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_DRIGHT];
        const Button *Y_BUTTON = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_Y];
        const Button *A_BUTTON = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_A];
        C2D_DrawRectSolid(CPAD_LEFT->x + CPAD_LEFT->w, CPAD_LEFT->y, 0, CPAD_RIGHT->x - (CPAD_LEFT->x + CPAD_LEFT->w), CPAD_RIGHT->h, 0xff404040);
        C2D_DrawRectSolid(DPAD_LEFT->x + DPAD_LEFT->w, DPAD_LEFT->y, 0, DPAD_RIGHT->x - (DPAD_LEFT->x + DPAD_LEFT->w), DPAD_RIGHT->h, TINT_50);
        if (new_3ds) {
            const Button *CSTICK_LEFT = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_LEFT];
            const Button *CSTICK_RIGHT = &custom_3ds_mappings_buttons[CUSTOM_3DS_MAPPINGS_CSTICK_RIGHT];
            C2D_DrawRectSolid(CSTICK_LEFT->x + CSTICK_LEFT->w, CSTICK_LEFT->y, 0, CSTICK_RIGHT->x - (CSTICK_LEFT->x + CSTICK_LEFT->w), CSTICK_RIGHT->h, 0xff404040);
        }
        C2D_DrawText(&text_3ds, C2D_AlignLeft | C2D_WithColor, 109, 99, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawRectSolid(120, 120, 0, 50 - 24, 1, TINT_COLOR);
        C2D_DrawText(&text_vb, C2D_AlignRight | C2D_WithColor, 108 + 50 - 2, 99 + 21, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&text_map, C2D_AlignLeft | C2D_WithColor, 163, 99 + 21 - 11, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(custom_3ds_mappings_buttons);
    switch (button) {
        #define CUSTOM_3DS_CASE(CUSTOM_3DS_BUTTON) \
        case CUSTOM_3DS_MAPPINGS_##CUSTOM_3DS_BUTTON: \
            current_custom_mapping_3ds_button = CUSTOM_3DS_MAPPINGS_##CUSTOM_3DS_BUTTON; \
            current_custom_mapping_3ds_button_text = &text_custom_3ds_button_##CUSTOM_3DS_BUTTON; \
            current_custom_mapping_vb_option = &tVBOpt.CUSTOM_MAPPING_##CUSTOM_3DS_BUTTON; \
            current_custom_mapping_mod = &tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_##CUSTOM_3DS_BUTTON)]; \
            [[gnu::musttail]] return custom_vb_mappings(vb_button_code_to_vb_ui_button_index(tVBOpt.CUSTOM_MAPPING_##CUSTOM_3DS_BUTTON));
        PERFORM_FOR_EACH_3DS_BUTTON(CUSTOM_3DS_CASE)
        case CUSTOM_3DS_MAPPINGS_BACK:
            [[gnu::musttail]] return controls(CONTROLS_CONFIGURE_SCHEME);
        case CUSTOM_3DS_MAPPINGS_RESET:
            setCustomMappingDefaults();
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return custom_3ds_mappings(CUSTOM_3DS_MAPPINGS_RESET);
    }
}

static void custom_vb_mappings(int initial_button) {
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, TINT_COLOR, 1);
    custom_vb_mappings_buttons[CUSTOM_VB_MAPPINGS_MOD].option = *current_custom_mapping_mod;
    LOOP_BEGIN(custom_vb_mappings_buttons, initial_button);
        const Button *LPAD_UP = &custom_vb_mappings_buttons[CUSTOM_VB_MAPPINGS_LPAD_U];
        const Button *LPAD_DOWN = &custom_vb_mappings_buttons[CUSTOM_VB_MAPPINGS_LPAD_D];
        const Button *RPAD_UP = &custom_vb_mappings_buttons[CUSTOM_VB_MAPPINGS_RPAD_U];
        const Button *RPAD_DOWN = &custom_vb_mappings_buttons[CUSTOM_VB_MAPPINGS_RPAD_D];
        C2D_DrawRectSolid(LPAD_UP->x, LPAD_UP->y + LPAD_UP->h, 0, LPAD_DOWN->w, LPAD_DOWN->y - (LPAD_UP->y + LPAD_UP->h), TINT_50);
        C2D_DrawRectSolid(RPAD_UP->x, RPAD_UP->y + RPAD_UP->h, 0, RPAD_DOWN->w, RPAD_DOWN->y - (RPAD_UP->y + RPAD_UP->h), TINT_50);
        C2D_DrawSpriteTinted(&text_3ds_sprite, &tint);
        C2D_DrawSpriteTinted(&text_vb_sprite, &tint);
        C2D_DrawSpriteTinted(&vb_icon_sprite, &tint);
        C2D_DrawLine(128, 25, TINT_COLOR, 180, 25, TINT_COLOR, 1, 0);
        C2D_DrawLine(142, 95, TINT_COLOR, 186, 95, TINT_COLOR, 1, 0);
        C2D_DrawText(current_custom_mapping_3ds_button_text, C2D_AlignCenter | C2D_WithColor, 160, 2, 0, 0.8, 0.8, TINT_COLOR);
        C2D_DrawText(&text_currently_mapped_to, C2D_AlignCenter | C2D_WithColor, 160, 23, 0, 0.8, 0.8, TINT_COLOR);
        C2D_DrawText(vb_button_code_to_vb_button_text(*current_custom_mapping_vb_option), C2D_AlignCenter | C2D_WithColor, 160, 72, 0, 0.8, 0.8, TINT_COLOR);
    LOOP_END(custom_vb_mappings_buttons);
    switch (button) {
        #define CUSTOM_VB_CASE(CUSTOM_VB_BUTTON) \
        case CUSTOM_VB_MAPPINGS_##CUSTOM_VB_BUTTON: \
            *current_custom_mapping_vb_option = VB_##CUSTOM_VB_BUTTON; \
            tVBOpt.MODIFIED = true; \
            [[gnu::musttail]] return custom_3ds_mappings(current_custom_mapping_3ds_button);
        PERFORM_FOR_EACH_VB_BUTTON(CUSTOM_VB_CASE)
        case CUSTOM_VB_MAPPINGS_MOD:
            *current_custom_mapping_mod = (*current_custom_mapping_mod + 1) % 3;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return custom_vb_mappings(button);
        case CUSTOM_VB_MAPPINGS_BACK:
            [[gnu::musttail]] return custom_3ds_mappings(current_custom_mapping_3ds_button);
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

static void draw_multislot(Button *self) {
    int id = (self - multicolour_picker_buttons) / 2;
    C2D_DrawRectSolid(self->x + 100 - 4 - 32 - 8 - 32, self->y + 8, 0, 32, 24, 0xff000000);
    C2D_DrawRectSolid(self->x + 100 - 4 - 32 - 8 - 32 + 1, self->y + 8 + 1, 0, 30, 22, 0xff000000 | tVBOpt.MTINT[id][0]);
    C2D_DrawRectSolid(self->x + 100 - 4 - 32, self->y + 8, 0, 32, 24, 0xff000000);
    C2D_DrawRectSolid(self->x + 100 - 4 - 32 + 1, self->y + 8 + 1, 0, 30, 22, 0xff000000 | tVBOpt.MTINT[id][1]);
    C2D_DrawRectSolid(self->x + 100 + 4, self->y + 8, 0, 32, 24, 0xff000000);
    C2D_DrawRectSolid(self->x + 100 + 4 + 1, self->y + 8 + 1, 0, 30, 22, 0xff000000 | tVBOpt.MTINT[id][2]);
    C2D_DrawRectSolid(self->x + 100 + 4 + 32 + 8, self->y + 8, 0, 32, 24, 0xff000000);
    C2D_DrawRectSolid(self->x + 100 + 4 + 32 + 8 + 1, self->y + 8 + 1, 0, 30, 22, 0xff000000 | tVBOpt.MTINT[id][3]);
    if (id == tVBOpt.MULTIID) C2D_DrawCircleSolid(self->x + 5, self->y + self->h / 2, 0, 4, 0xff000000);
}

static void preset_controls(int initial_button) {
    bool shoulder_pressed = false;
    bool face_pressed = false;
    bool new_3ds = tVBOpt.CPP_ENABLED;
    if (!new_3ds) APT_CheckNew3DS(&new_3ds);
    preset_controls_buttons[PRESET_CONTROLS_FACE].y = new_3ds ? 52 : 40;
    preset_controls_buttons[PRESET_CONTROLS_SHOULDER].hidden = !new_3ds;
    preset_controls_buttons[PRESET_CONTROLS_DPAD_MODE].option = tVBOpt.DPAD_MODE;
    const int SHOULDX = 160;
    const int SHOULDY = 0;
    const int SHOULDW = 128;
    const int SHOULDH = 40;
    const int FACEX = 160;
    const int FACEY = new_3ds ? 92 : 80;
    const int FACEW = 128;
    const int FACEH = 80;
    const int OFFSET = 22;
    LOOP_BEGIN(preset_controls_buttons, initial_button);
    LOOP_END(preset_controls_buttons);
    switch (button) {
        case PRESET_CONTROLS_FACE:
            tVBOpt.ABXY_MODE = (tVBOpt.ABXY_MODE + 1) % 6;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return preset_controls(PRESET_CONTROLS_FACE);
        case PRESET_CONTROLS_SHOULDER:
            tVBOpt.ZLZR_MODE = (tVBOpt.ZLZR_MODE + 1) % 4;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return preset_controls(PRESET_CONTROLS_SHOULDER);
        case PRESET_CONTROLS_DPAD_MODE:
            tVBOpt.DPAD_MODE = (tVBOpt.DPAD_MODE + 1) % 3;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return preset_controls(PRESET_CONTROLS_DPAD_MODE);
        case PRESET_CONTROLS_BACK:
            setPresetControls(buttons_on_screen);
            [[gnu::musttail]] return controls(CONTROLS_CONFIGURE_SCHEME);
    }
}

static void touchscreen_settings() {
    touchscreen_settings_buttons[TOUCHSCREEN_DEFAULT].hidden = buttons_on_screen == tVBOpt.TOUCH_BUTTONS;
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
                touchscreen_settings_buttons[TOUCHSCREEN_DEFAULT].hidden = buttons_on_screen == tVBOpt.TOUCH_BUTTONS;
            } else if (abs(tVBOpt.PAUSE_RIGHT - touch_pos.px) < PAUSE_RAD) {
                // pause slider
                tVBOpt.MODIFIED = true;
                dragging = 1;
                xoff = tVBOpt.PAUSE_RIGHT - touch_pos.px;
            } else if (buttons_on_screen) {
                int adx = tVBOpt.TOUCH_AX - touch_pos.px;
                int ady = tVBOpt.TOUCH_AY - touch_pos.py;
                int bdx = tVBOpt.TOUCH_BX - touch_pos.px;
                int bdy = tVBOpt.TOUCH_BY - touch_pos.py;
                if (adx*adx + ady*ady < BUTTON_RAD*BUTTON_RAD) {
                    // a button
                    tVBOpt.MODIFIED = true;
                    dragging = 2;
                    xoff = adx;
                    yoff = ady;
                } else if (bdx*bdx + bdy*bdy < BUTTON_RAD*BUTTON_RAD) {
                    // b button
                    tVBOpt.MODIFIED = true;
                    dragging = 3;
                    xoff = bdx;
                    yoff = bdy;
                }
            } else {
                int dx = tVBOpt.TOUCH_PADX - touch_pos.px;
                int dy = tVBOpt.TOUCH_PADY - touch_pos.py;
                if (dx*dx + dy*dy < PAD_RAD*PAD_RAD) {
                    // dpad
                    tVBOpt.MODIFIED = true;
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
            PAUSE_RAD * 2, 8*2, dragging == 1 ? TINT_90 : TINT_50
        );
        if (touchscreen_settings_buttons[TOUCHSCREEN_DEFAULT].hidden) {
            C2D_DrawText(&text_current_default, C2D_WithColor, 4, 60, 0, 0.5, 0.5, TINT_COLOR);
        }
    LOOP_END(touchscreen_settings_buttons);
    buttonLock = false;
    switch (button) {
        case TOUCHSCREEN_BACK: // Back
            [[gnu::musttail]] return controls(CONTROLS_TOUCHSCREEN);
        case TOUCHSCREEN_RESET: // Reset
            tVBOpt.MODIFIED = true;
            tVBOpt.PAUSE_RIGHT = 160;
            tVBOpt.TOUCH_AX = 250;
            tVBOpt.TOUCH_AY = 64;
            tVBOpt.TOUCH_BX = 250;
            tVBOpt.TOUCH_BY = 160;
            tVBOpt.TOUCH_PADX = 240;
            tVBOpt.TOUCH_PADY = 128;
            [[gnu::musttail]] return touchscreen_settings();
        case TOUCHSCREEN_SWITCH:
            tVBOpt.MODIFIED = true;
            tVBOpt.TOUCH_SWITCH = !tVBOpt.TOUCH_SWITCH;
            [[gnu::musttail]] return touchscreen_settings();
        case TOUCHSCREEN_DEFAULT:
            tVBOpt.MODIFIED = true;
            tVBOpt.TOUCH_BUTTONS = buttons_on_screen;
            [[gnu::musttail]] return touchscreen_settings();
    }
}

static void init_colour_wheel(int col_int, float *hue_p, float *saturation_p, float *lightness_p) {
    // tint to hue saturation
    float col[3] = {
        (col_int & 0xff) / 255.0,
        ((col_int >> 8) & 0xff) / 255.0,
        ((col_int >> 16) & 0xff) / 255.0,
    };
    float max_rg = col[0] > col[1] ? col[0] : col[1];
    float lightness = max_rg > col[2] ? max_rg : col[2];
    if (lightness_p) *lightness_p = lightness;
    if (lightness != 0) {
        col[0] /= lightness;
        col[1] /= lightness;
        col[2] /= lightness;
    }
    float max = col[0] > col[1] ? col[0] : col[1];
    max = max > col[2] ? max : col[2];
    float min = col[0] < col[1] ? col[0] : col[1];
    min = min < col[2] ? min : col[2];
    if (max == min) {
        // white
        *hue_p = *saturation_p = 0;
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
        *hue_p = hprime * (M_PI / 3);
        *saturation_p = chroma / max;
    }
}

static int make_color(float hue, float saturation, float lightness) {
    float hprime = fmod(hue / (M_PI / 3) + 6, 6);
    float sub = saturation * (1 - fabs(fmod(hprime, 2) - 1));
    float col[3] = {0};
    if (hprime < 1) {
        col[0] = saturation;
        col[1] = sub;
    } else if (hprime < 2) {
        col[0] = sub;
        col[1] = saturation;
    } else if (hprime < 3) {
        col[1] = saturation;
        col[2] = sub;
    } else if (hprime < 4) {
        col[1] = sub;
        col[2] = saturation;
    } else if (hprime < 5) {
        col[0] = sub;
        col[2] = saturation;
    } else {
        col[0] = saturation;
        col[2] = sub;
    }
    return
        ((int)((col[0] + 1 - saturation) * lightness * 255)) |
        ((int)((col[1] + 1 - saturation) * lightness * 255) << 8) |
        ((int)((col[2] + 1 - saturation) * lightness * 255) << 16);
}

static void col_to_str(char *out, int col) {
    snprintf(out, 8, "#%02x%02x%02x", col & 0xff, (col >> 8) & 0xff, (col >> 16) & 0xff);
}

static SwkbdCallbackResult swkbd_colour_callback(void *user, const char **message, const char *text, size_t text_len) {
    if (text_len != 7) goto fail;
    if (text[0] != '#') goto fail;
    for (int i = 1; i <= 6; i++) {
        if (!(text[i] >= '0' && text[i] <= '9') && !(text[i] >= 'a' && text[i] <= 'f') && !(text[i] >= 'A' && text[i] <= 'F')) goto fail;
    }
    return SWKBD_CALLBACK_OK;
    fail:
    *message = "Please provide a valid hex\ncolor code (# followed by six\nhex digits, eg. #1234ab).";
    return SWKBD_CALLBACK_CONTINUE;
}

static bool swkbd_colour(int *save_col) {
    char string_buf[8];
    col_to_str(string_buf, *save_col);

    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_QWERTY, 2, sizeof(string_buf)-1);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
    swkbdSetFilterCallback(&swkbd, swkbd_colour_callback, NULL);

    swkbdSetInitialText(&swkbd, string_buf);

    toggleVsync(false);
    SwkbdButton button = swkbdInputText(&swkbd, string_buf, sizeof(string_buf));
    toggleVsync(tVBOpt.VSYNC);

    if (button == SWKBD_BUTTON_RIGHT) {
        int rev_col = strtol(string_buf + 1, NULL, 16);
        int new_col = (rev_col >> 16) | (rev_col & 0xff00) | ((rev_col & 0xff) << 16);
        if (*save_col != new_col) {
            *save_col = new_col;
            return true;
        }
    }

    return false;
}

static SwkbdCallbackResult swkbd_scale_callback(void *user, const char **message, const char *text, size_t text_len) {
    float scale = atoff(text);
    if (scale < 1 || scale > 4) {
        *message = "Scale must be a number between 1.0 and 4.0.";
        return SWKBD_CALLBACK_CONTINUE;
    }
    return SWKBD_CALLBACK_OK;
}

static bool swkbd_scale(float *scale) {
    char string_buf[8];
    snprintf(string_buf, sizeof(string_buf), "%.5f", *scale);

    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, sizeof(string_buf)-1);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetNumpadKeys(&swkbd, '.', '.');
    swkbdSetFilterCallback(&swkbd, swkbd_scale_callback, NULL);

    swkbdSetInitialText(&swkbd, string_buf);

    toggleVsync(false);
    SwkbdButton button = swkbdInputText(&swkbd, string_buf, sizeof(string_buf));
    toggleVsync(tVBOpt.VSYNC);

    if (button == SWKBD_BUTTON_RIGHT) {
        *scale = atoff(string_buf);
        return true;
    }
    return false;
}

static void handle_colour_wheel(int *save_col, int wheel_x, int wheel_y, float *hue, float *saturation, float *lightness) {
    static bool dragging_wheel = false;
    static bool dragging_lightness = false;
    C2D_SpriteSetPos(&colour_wheel_sprite, wheel_x, wheel_y);
    const float circle_x = colour_wheel_sprite.params.pos.x;
    const float circle_y = colour_wheel_sprite.params.pos.y;
    const float circle_w = colour_wheel_sprite.params.pos.w;
    const float circle_h = colour_wheel_sprite.params.pos.h;

    const int lightness_x = 16;
    const int lightness_y = 16;
    const int lightness_width = 32;
    const int lightness_height = 180;
    const int lightness_cursor_height = 10;

    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    float touch_dx = (touch_pos.px - circle_x) / (circle_w / 2);
    float touch_dy = (touch_pos.py - circle_y) / (circle_h / 2);
    float sat = sqrt(touch_dx * touch_dx + touch_dy * touch_dy);
    if ((hidKeysDown() & KEY_TOUCH)) {
        if (sat <= 1) dragging_wheel = true;
        if (lightness &&
            (unsigned)(touch_pos.px - (lightness_x - 2)) < lightness_width &&
            abs((int)(touch_pos.py - lightness_y)) < lightness_height
        ) dragging_lightness = true;
    }
    if (hidKeysUp() & KEY_TOUCH) {
        dragging_wheel = false;
        dragging_lightness = false;
    }
    if (dragging_wheel) {
        tVBOpt.MODIFIED = true;
        if (sat > 1) {
            touch_dx /= sat;
            touch_dy /= sat;
            sat = 1;
        }
        *saturation = sat;
        // touch position to hue saturation, then to rgb
        *hue = atan2(touch_dy, touch_dx);
        *save_col = make_color(*hue, *saturation, lightness ? *lightness : 1);
    }
    if (dragging_lightness) {
        *lightness = 1 - C2D_Clamp((float)(touch_pos.py - lightness_y) / lightness_height, 0, 1);
        *save_col = make_color(*hue, *saturation, *lightness);
    }
    C2D_DrawSprite(&colour_wheel_sprite);
    if (!dragging_wheel) {
        touch_dx = *saturation * cos(*hue);
        touch_dy = *saturation * sin(*hue);
    }
    C2D_DrawCircleSolid(
        circle_x + touch_dx * (circle_w / 2),
        circle_y + touch_dy * (circle_h / 2),
        0, 4, 0xff000000);
    C2D_DrawCircleSolid(
        circle_x + touch_dx * (circle_w / 2),
        circle_y + touch_dy * (circle_h / 2),
        0, 2, 0xff000000 | *save_col);
    if (lightness) {
        int max_lightness = 0xff000000 | make_color(*hue, *saturation, 1);
        C2D_DrawRectSolid(lightness_x - 1, lightness_y - 1, 0, lightness_width + 2, lightness_height + 2, 0xff000000 | make_color(*hue + M_PI, *saturation, 1));
        C2D_DrawRectangle(lightness_x, lightness_y, 0, lightness_width, lightness_height, max_lightness, max_lightness, 0xff000000, 0xff000000);
        C2D_DrawRectSolid(lightness_x - 2, lightness_y + lightness_height * (1 - *lightness) - lightness_cursor_height / 2, 0, lightness_width + 4, lightness_cursor_height, 0xffffffff);
        C2D_DrawRectSolid(lightness_x - 1, lightness_y + lightness_height * (1 - *lightness) - lightness_cursor_height / 2 + 1, 0, lightness_width + 2, lightness_cursor_height - 2, 0xff000000);
        C2D_DrawRectSolid(lightness_x, lightness_y + lightness_height * (1 - *lightness) - lightness_cursor_height / 2 + 2, 0, lightness_width, lightness_cursor_height - 4, 0xff000000 | *save_col);
    }
}

static void colour_filter(void) {
    float hue, saturation, lightness;
    init_colour_wheel(tVBOpt.TINT, &hue, &saturation, NULL);

    LOOP_BEGIN(colour_filter_buttons, -1);
        handle_colour_wheel(&tVBOpt.TINT, 176, 112, &hue, &saturation, NULL);
    LOOP_END(colour_filter_buttons);

    switch (button) {
        case COLOUR_BACK: // Back
            [[gnu::musttail]] return barrier_settings(BARRIER_SETTINGS);
        case COLOUR_RED: // Red
            tVBOpt.TINT = 0x0000ff;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return colour_filter();
        case COLOUR_GRAY: // Gray
            tVBOpt.TINT = 0xffffff;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return colour_filter();
    }
}

static void multicolour_wheel(int palette_id, int colour_id) {
    float hue, saturation, lightness;
    init_colour_wheel(tVBOpt.MTINT[palette_id][colour_id], &hue, &saturation, &lightness);

    const int scale_x = 272;
    const int scale_y = 40;
    const int scale_width = 32;
    const int scale_height = 160;
    const int scale_cursor_height = 10;
    const int scale_offset = 1;
    const int scale_range = 3;

    bool dragging_scale = 0;
    
    multicolour_wheel_buttons[MULTIWHEEL_SCALE].hidden = colour_id == 0;

    LOOP_BEGIN(multicolour_wheel_buttons, 0);
        char initial_string[9];
        C2D_TextBufClear(dynamic_textbuf);

        col_to_str(initial_string, tVBOpt.MTINT[palette_id][colour_id]);
        C2D_TextParse(&multicolour_wheel_buttons[MULTIWHEEL_HEX].text, dynamic_textbuf, initial_string);
        C2D_TextOptimize(&multicolour_wheel_buttons[MULTIWHEEL_HEX].text);

        snprintf(initial_string, sizeof(initial_string), "%.5f", tVBOpt.STINT[palette_id][colour_id - 1]);
        C2D_TextParse(&multicolour_wheel_buttons[MULTIWHEEL_SCALE].text, dynamic_textbuf, initial_string);
        C2D_TextOptimize(&multicolour_wheel_buttons[MULTIWHEEL_SCALE].text);

        handle_colour_wheel(&tVBOpt.MTINT[palette_id][colour_id], 160, 112, &hue, &saturation, &lightness);

        if (colour_id != 0) {
            touchPosition touch_pos;
            hidTouchRead(&touch_pos);
            
            if ((hidKeysDown() & KEY_TOUCH)) {
                if ((unsigned)(touch_pos.px - (scale_x)) < scale_width &&
                    abs((int)(touch_pos.py - scale_y)) < scale_height
                ) dragging_scale = true;
            }
            if (hidKeysUp() & KEY_TOUCH) {
                dragging_scale = false;
            }
            if (dragging_scale) {
                tVBOpt.MODIFIED = true;
                float slider = 1 - C2D_Clamp((float)(touch_pos.py - scale_y) / scale_height, 0, 1);
                tVBOpt.STINT[palette_id][colour_id - 1] = slider * scale_range + scale_offset;
            }

            C2D_DrawText(&text_brighten, C2D_AlignCenter | C2D_WithColor, scale_x + scale_width / 2, scale_y - 24, 0, 0.5, 0.5, TINT_COLOR);
            C2D_DrawText(&text_brightness_disclaimer, C2D_AlignRight | C2D_WithColor, 316, 0, 0, 0.5, 0.5, TINT_COLOR);
            C2D_DrawRectSolid(scale_x + scale_width / 2 - 1, scale_y, 0, 2, scale_height, 0xff404040);
            C2D_DrawRectSolid(scale_x, scale_y + scale_height * (1 - (tVBOpt.STINT[palette_id][colour_id - 1] - scale_offset) / scale_range) - scale_cursor_height / 2, 0, scale_width, scale_cursor_height, TINT_COLOR);
        }

    LOOP_END(multicolour_wheel_buttons);

    switch (button) {
        case MULTIWHEEL_BACK:
            [[gnu::musttail]] return multicolour_settings(palette_id, colour_id);
        case MULTIWHEEL_HEX:
            if (swkbd_colour(&tVBOpt.MTINT[palette_id][colour_id])) tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return multicolour_wheel(palette_id, colour_id);
        case MULTIWHEEL_SCALE:
            if (swkbd_scale(&tVBOpt.STINT[palette_id][colour_id - 1])) tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return multicolour_wheel(palette_id, colour_id);
    }
}

static void multicolour_picker(int initial_button) {
    LOOP_BEGIN(multicolour_picker_buttons, initial_button);
    LOOP_END(multicolour_picker_buttons);
    if (button == MULTIPICKER_BACK) {
        [[gnu::musttail]] return barrier_settings(BARRIER_SETTINGS);
    } else {
        if (tVBOpt.MULTIID != button / 2) {
            tVBOpt.MODIFIED = true;
            tVBOpt.MULTIID = button / 2;
        }
        if (button % 2) [[gnu::musttail]] return multicolour_settings(button / 2, 0);
        else [[gnu::musttail]] return multicolour_picker(button);
    }
}

static void multicolour_settings(int palette_id, int initial_button) {
    // local variables are static to avoid blowing up frame size and preventing tail call optimization
    static C2D_Text darkest_col, dark_col, light_col, lightest_col;
    static C2D_Text dark_scale, light_scale, lightest_scale;

    static char textbuf[8];
    C2D_TextBufClear(dynamic_textbuf);

    col_to_str(textbuf, tVBOpt.MTINT[palette_id][0]);
    C2D_TextParse(&darkest_col, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&darkest_col);

    col_to_str(textbuf, tVBOpt.MTINT[palette_id][1]);
    C2D_TextParse(&dark_col, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&dark_col);

    col_to_str(textbuf, tVBOpt.MTINT[palette_id][2]);
    C2D_TextParse(&light_col, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&light_col);

    col_to_str(textbuf, tVBOpt.MTINT[palette_id][3]);
    C2D_TextParse(&lightest_col, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&lightest_col);

    snprintf(textbuf, sizeof(textbuf), "%.5f", tVBOpt.STINT[palette_id][0]);
    C2D_TextParse(&dark_scale, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&dark_scale);

    snprintf(textbuf, sizeof(textbuf), "%.5f", tVBOpt.STINT[palette_id][1]);
    C2D_TextParse(&light_scale, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&light_scale);

    snprintf(textbuf, sizeof(textbuf), "%.5f", tVBOpt.STINT[palette_id][2]);
    C2D_TextParse(&lightest_scale, dynamic_textbuf, textbuf);
    C2D_TextOptimize(&lightest_scale);

    LOOP_BEGIN(multicolour_settings_buttons, initial_button);
        C2D_DrawRectSolid(16 + 170 + 8, 48 * 0 + 16 + 1, 0, 38, 38, 0xff000000 | tVBOpt.MTINT[palette_id][0]);
        C2D_DrawRectSolid(16 + 170 + 8, 48 * 1 + 16 + 1, 0, 38, 38, 0xff000000 | tVBOpt.MTINT[palette_id][1]);
        C2D_DrawRectSolid(16 + 170 + 8, 48 * 2 + 16 + 1, 0, 38, 38, 0xff000000 | tVBOpt.MTINT[palette_id][2]);
        C2D_DrawRectSolid(16 + 170 + 8, 48 * 3 + 16 + 1, 0, 38, 38, 0xff000000 | tVBOpt.MTINT[palette_id][3]);

        C2D_DrawText(&darkest_col, C2D_WithColor | C2D_AlignRight, 316, 48 * 0 + 24, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&dark_col, C2D_WithColor | C2D_AlignRight, 316, 48 * 1 + 16, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&light_col, C2D_WithColor | C2D_AlignRight, 316, 48 * 2 + 16, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&lightest_col, C2D_WithColor | C2D_AlignRight, 316, 48 * 3 + 16, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&dark_scale, C2D_WithColor | C2D_AlignRight, 316, 48 * 1 + 36, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&light_scale, C2D_WithColor | C2D_AlignRight, 316, 48 * 2 + 36, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&lightest_scale, C2D_WithColor | C2D_AlignRight, 316, 48 * 3 + 36, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(multicolour_settings_buttons);
    if (button == MULTI_BACK) {
        [[gnu::musttail]] return multicolour_picker(palette_id * 2 + 1);
    } else {
        [[gnu::musttail]] return multicolour_wheel(palette_id, button);
    }
}

static bool vblink_transfer() {
    C2D_Text fname;
    C2D_TextBufClear(dynamic_textbuf);
    C2D_TextParse(&fname, dynamic_textbuf, vblink_fname);
    C2D_TextOptimize(&fname);
    #undef DEFAULT_RETURN
    #define DEFAULT_RETURN false
    LOOP_BEGIN(load_rom_buttons, -1);
        C2D_DrawText(&fname, C2D_AlignCenter | C2D_WithColor, 320 / 2, 10, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        C2D_DrawText(&text_loading, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.8, 0.8, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        C2D_DrawRectSolid(60, 140, 0, 200, 16, C2D_Color32(0.5 * TINT_R, 0.5 * TINT_G, 0.5 * TINT_B, 255));
        C2D_DrawRectSolid(60, 140, 0, 2 * vblink_progress, 16, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        if (vblink_progress == 100) {
            // complete, all good
            svcSignalEvent(vblink_event);
            guiop = AKILL | VBRESET;
            game_running = true;
            loadGameOptions();
            last_savestate = 0;
            loop = false;
        } else if (vblink_progress < 0 || vblink_error) {
            vblink_error = 0;
            svcSignalEvent(vblink_event);
            loop = false;
        }
    LOOP_END(load_rom_buttons);
    #undef DEFAULT_RETURN
    #define DEFAULT_RETURN
    if (vblink_progress != 100) {
        game_running = false;
        if (tVBOpt.GAME_SETTINGS) loadFileOptions();
        // redraw logo since we unloaded
        C3D_FrameBegin(0);
        draw_logo();
        C3D_FrameEnd(0);
    }
    return vblink_progress == 100;
}

static void vblink(void) {
    bool loaded = false;
    char str[100];
    C2D_Text text;
    int open_err = vblink_open();
    union {
        struct in_addr ia;
        u8 bytes[4];
    } ip_addr, netmask, broadcast;
    LOOP_BEGIN(about_buttons, 0);
        SOCU_GetIPInfo(&ip_addr.ia, &netmask.ia, &broadcast.ia);
        C2D_TextBufClear(dynamic_textbuf);
        if (open_err) {
            snprintf(str, sizeof(str), "Couldn't start: error %d\nPlease ensure Wi-Fi is enabled and try again.", open_err);
        } else if (vblink_progress == -2) {
            snprintf(str, sizeof(str), "Socket closed: error %d\nPlease ensure Wi-Fi is enabled and try again.", vblink_error);
        } else if (vblink_error) {
            snprintf(str, sizeof(str), "Listening at %d.%d.%d.%d...\nTransfer failed: error %d", ip_addr.bytes[0], ip_addr.bytes[1], ip_addr.bytes[2], ip_addr.bytes[3], vblink_error);
        } else {
            snprintf(str, sizeof(str), "Listening at %d.%d.%d.%d...", ip_addr.bytes[0], ip_addr.bytes[1], ip_addr.bytes[2], ip_addr.bytes[3]);
        }
        C2D_TextParse(&text, dynamic_textbuf, str);
        C2D_TextOptimize(&text);
        C2D_DrawText(&text_vblink, C2D_AlignCenter | C2D_WithColor, 320 / 2, 10, 0, 1, 1, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, C2D_Color32(TINT_R, TINT_G, TINT_B, 255));
        if (vblink_progress == 0) loop = false;
    LOOP_END(about_buttons);
    if (vblink_progress == 0) {
        // ready to receive
        svcSignalEvent(vblink_event);
        bool success = vblink_transfer();
        vblink_close();
        if (success) {
            // success
            return;
        } else [[gnu::musttail]] return vblink();
    } else {
        vblink_close();
        [[gnu::musttail]] return main_menu(game_running ? MAIN_MENU_RESUME : MAIN_MENU_LOAD_ROM);
    }
}

static void dev_options(int initial_button) {
    bool new_3ds = false;
    APT_CheckNew3DS(&new_3ds);
    dev_options_buttons[PERF_N3DS].hidden = !new_3ds;
    dev_options_buttons[PERF_BAR].toggle = tVBOpt.PERF_INFO;
    dev_options_buttons[PERF_VIP].toggle = tVBOpt.VIP_OVERCLOCK;
    dev_options_buttons[PERF_N3DS].toggle = tVBOpt.N3DS_SPEEDUP;
    LOOP_BEGIN(dev_options_buttons, initial_button);
    LOOP_END(dev_options_buttons);
    switch (button) {
        case PERF_BAR:
            tVBOpt.PERF_INFO = !tVBOpt.PERF_INFO;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return dev_options(PERF_BAR);
        case PERF_VIP:
            tVBOpt.VIP_OVERCLOCK = !tVBOpt.VIP_OVERCLOCK;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return dev_options(PERF_VIP);
        case PERF_N3DS:
            tVBOpt.N3DS_SPEEDUP = !tVBOpt.N3DS_SPEEDUP;
            tVBOpt.MODIFIED = true;
            osSetSpeedupEnable(tVBOpt.N3DS_SPEEDUP);
            [[gnu::musttail]] return dev_options(PERF_N3DS);
        case PERF_BACK:
            [[gnu::musttail]] return options(OPTIONS_PERF);
    }
}

static void save_debug_info(void);
static void options(int initial_button) {
    options_buttons[OPTIONS_FF].toggle = tVBOpt.FF_TOGGLE;
    options_buttons[OPTIONS_SOUND].toggle = tVBOpt.SOUND;
    options_buttons[OPTIONS_DEBUG].hidden = !game_running;
    options_buttons[OPTIONS_BACK].hidden = tVBOpt.MODIFIED;
    if (game_running) {
        if (tVBOpt.GAME_SETTINGS) {
            if (tVBOpt.MODIFIED) {
                options_buttons[OPTIONS_SAVE_GLOBAL].hidden = false;
                options_buttons[OPTIONS_SAVE_GLOBAL].x = 16;
                options_buttons[OPTIONS_SAVE_GLOBAL].w = 96-8;
                options_buttons[OPTIONS_SAVE_GAME].hidden = false;
                options_buttons[OPTIONS_SAVE_GAME].x = 112-2;
                options_buttons[OPTIONS_SAVE_GAME].w = 96+4;
                options_buttons[OPTIONS_DISCARD].hidden = false;
                options_buttons[OPTIONS_DISCARD].x = 208+8;
                options_buttons[OPTIONS_DISCARD].w = 96+4;
                options_buttons[OPTIONS_RESET_TO_GLOBAL].hidden = true;
            } else {
                options_buttons[OPTIONS_SAVE_GLOBAL].hidden = false;
                options_buttons[OPTIONS_SAVE_GLOBAL].x = 16;
                options_buttons[OPTIONS_SAVE_GLOBAL].w = 128;
                options_buttons[OPTIONS_RESET_TO_GLOBAL].hidden = false;
                options_buttons[OPTIONS_RESET_TO_GLOBAL].x = 176;
                options_buttons[OPTIONS_RESET_TO_GLOBAL].w = 128;
                options_buttons[OPTIONS_SAVE_GAME].hidden = true;
                options_buttons[OPTIONS_DISCARD].hidden = true;
            }
        } else {
            if (tVBOpt.MODIFIED) {
                options_buttons[OPTIONS_SAVE_GLOBAL].hidden = false;
                options_buttons[OPTIONS_SAVE_GLOBAL].x = 16;
                options_buttons[OPTIONS_SAVE_GLOBAL].w = 96-8;
                options_buttons[OPTIONS_SAVE_GAME].hidden = false;
                options_buttons[OPTIONS_SAVE_GAME].x = 112-2;
                options_buttons[OPTIONS_SAVE_GAME].w = 96+4;
                options_buttons[OPTIONS_DISCARD].hidden = false;
                options_buttons[OPTIONS_DISCARD].x = 208+8;
                options_buttons[OPTIONS_DISCARD].w = 96+4;
                options_buttons[OPTIONS_RESET_TO_GLOBAL].hidden = true;
            } else {
                options_buttons[OPTIONS_SAVE_GLOBAL].hidden = true;
                options_buttons[OPTIONS_SAVE_GAME].hidden = true;
                options_buttons[OPTIONS_DISCARD].hidden = true;
                options_buttons[OPTIONS_RESET_TO_GLOBAL].hidden = true;
            }
        }
    } else {
        if (tVBOpt.MODIFIED) {
            options_buttons[OPTIONS_SAVE_GLOBAL].hidden = false;
            options_buttons[OPTIONS_SAVE_GLOBAL].x = 16;
            options_buttons[OPTIONS_SAVE_GLOBAL].w = 128;
            options_buttons[OPTIONS_DISCARD].hidden = false;
            options_buttons[OPTIONS_DISCARD].x = 176;
            options_buttons[OPTIONS_DISCARD].w = 128;
            options_buttons[OPTIONS_SAVE_GAME].hidden = true;
            options_buttons[OPTIONS_RESET_TO_GLOBAL].hidden = true;
        } else {
            options_buttons[OPTIONS_SAVE_GLOBAL].hidden = true;
            options_buttons[OPTIONS_SAVE_GAME].hidden = true;
            options_buttons[OPTIONS_DISCARD].hidden = true;
            options_buttons[OPTIONS_RESET_TO_GLOBAL].hidden = true;
        }
    }
    LOOP_BEGIN(options_buttons, initial_button);
    LOOP_END(options_buttons);
    switch (button) {
        case OPTIONS_VIDEO: // Video settings
            [[gnu::musttail]] return video_settings(0);
        case OPTIONS_FF: // Fast forward
            tVBOpt.FF_TOGGLE = !tVBOpt.FF_TOGGLE;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return options(OPTIONS_FF);
        case OPTIONS_SOUND: // Sound
            tVBOpt.SOUND = !tVBOpt.SOUND;
            [[gnu::musttail]] return options(OPTIONS_SOUND);
        case OPTIONS_PERF: // Developer settings
            [[gnu::musttail]] return dev_options(0);
        case OPTIONS_CONTROLS: // Controls
            [[gnu::musttail]] return controls(0);
        case OPTIONS_ABOUT: // About
            return about();
        case OPTIONS_BACK: // Back
            [[gnu::musttail]] return main_menu(MAIN_MENU_OPTIONS);
        case OPTIONS_DEBUG: // Save debug info
            [[gnu::musttail]] return save_debug_info();
        case OPTIONS_SAVE_GLOBAL:
            if (tVBOpt.GAME_SETTINGS) deleteGameOptions();
            saveFileOptions();
            [[gnu::musttail]] return options(OPTIONS_BACK);
        case OPTIONS_RESET_TO_GLOBAL:
            if (tVBOpt.GAME_SETTINGS) deleteGameOptions();
            loadFileOptions();
            [[gnu::musttail]] return options(OPTIONS_BACK);
        case OPTIONS_SAVE_GAME:
            saveGameOptions();
            [[gnu::musttail]] return options(OPTIONS_BACK);
        case OPTIONS_DISCARD:
            loadFileOptions();
            if (game_running) loadGameOptions();
            [[gnu::musttail]] return options(OPTIONS_BACK);
    }
}

static void video_settings(int initial_button) {
    video_settings_buttons[VIDEO_SLIDER].hidden = any_2ds;

    video_settings_buttons[VIDEO_MODE].toggle = tVBOpt.ANAGLYPH;
    video_settings_buttons[VIDEO_SLIDER].toggle = tVBOpt.SLIDERMODE;
    video_settings_buttons[VIDEO_ANTIFLICKER].toggle = tVBOpt.ANTIFLICKER;
    LOOP_BEGIN(video_settings_buttons, initial_button);
    LOOP_END(video_settings_buttons);
    switch (button) {
        case VIDEO_MODE:
            toggleAnaglyph(!tVBOpt.ANAGLYPH, true);
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return video_settings(button);
        case VIDEO_SETTINGS:
            if (tVBOpt.ANAGLYPH) {
                [[gnu::musttail]] return anaglyph_settings(0);
            } else {
                [[gnu::musttail]] return barrier_settings(0);
            }
        case VIDEO_SLIDER:
            tVBOpt.SLIDERMODE = !tVBOpt.SLIDERMODE;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return video_settings(button);
        case VIDEO_ANTIFLICKER:
            tVBOpt.ANTIFLICKER = !tVBOpt.ANTIFLICKER;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return video_settings(button);
        case VIDEO_BACK:
            [[gnu::musttail]] return options(OPTIONS_VIDEO);
    }
}

static void barrier_settings(int initial_button) {
    barrier_settings_buttons[BARRIER_MODE].toggle = tVBOpt.MULTICOL;
    barrier_settings_buttons[BARRIER_DEFAULT_EYE].toggle = tVBOpt.DEFAULT_EYE;
    LOOP_BEGIN(barrier_settings_buttons, initial_button);
    LOOP_END(barrier_settings_buttons);
    switch (button) {
        case BARRIER_MODE:
            tVBOpt.MULTICOL = !tVBOpt.MULTICOL;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return barrier_settings(button);
        case BARRIER_SETTINGS: // Colour filter
            if (tVBOpt.MULTICOL) {
                [[gnu::musttail]] return multicolour_picker(0);
            } else {
                [[gnu::musttail]] return colour_filter();
            }
        case BARRIER_DEFAULT_EYE: // Default eye
            tVBOpt.DEFAULT_EYE = !tVBOpt.DEFAULT_EYE;
            tVBOpt.MODIFIED = true;
            [[gnu::musttail]] return barrier_settings(button);
        case BARRIER_BACK: // Back
            [[gnu::musttail]] return video_settings(VIDEO_SETTINGS);
    }
}

static void anaglyph_settings(int initial_button) {
    for (int i = 0; i < 8; i++) {
        anaglyph_settings_buttons[i].hidden = (tVBOpt.ANAGLYPH_RIGHT & i) != 0;
        anaglyph_settings_buttons[i+8].hidden = (tVBOpt.ANAGLYPH_LEFT & i) != 0;
        anaglyph_settings_buttons[i].x = 50;
        anaglyph_settings_buttons[i].w = !any_2ds ? 80 : 64;
        anaglyph_settings_buttons[i+8].x = !any_2ds ? 176 : 128;
        anaglyph_settings_buttons[i+8].w = !any_2ds ? 80 : 64;
    }

    bool touch_grab = -1;
    LOOP_BEGIN(anaglyph_settings_buttons, initial_button);
        C2D_DrawText(&text_left, C2D_WithColor, 12, 12, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&text_right, C2D_WithColor, !any_2ds ? 260 : 196, 12, 0, 0.7, 0.7, TINT_COLOR);
        if (any_2ds) {
            // handle depth slider
            int button_id = selectedButton - anaglyph_settings_buttons;
            int kDown = hidKeysDown();
            int xaxis = ((kDown & KEY_RIGHT) != 0) - ((kDown & KEY_LEFT) != 0);
            int yaxis = ((kDown & KEY_DOWN) != 0) - ((kDown & KEY_UP) != 0);
            if (!buttonLock) {
                if ((button_id >= 8 && button_id < 16) && xaxis > 0) {
                    buttonLock = true;
                    selectedButton = NULL;
                }
            } else {
                if (xaxis < 0) {
                    buttonLock = false;
                    // note: this will be moved again later, so this is the pre-move position
                    selectedButton = &anaglyph_settings_buttons[ANAGLYPH_DEPTH_PLACEHOLDER];
                } else if (yaxis != 0) {
                    tVBOpt.ANAGLYPH_DEPTH -= yaxis;
                }
            }

            touchPosition touch_pos;
            hidTouchRead(&touch_pos);

            if (!touch_grab && kDown & KEY_TOUCH &&
                touch_pos.px >= 256 && touch_pos.px < 256 + 48
                && touch_pos.py >= 34 - 8 + (8 - tVBOpt.ANAGLYPH_DEPTH) * 11 && touch_pos.py < 34 + 8 + 14 + (8 - tVBOpt.ANAGLYPH_DEPTH) * 11
            ) {
                touch_grab = true;
                buttonLock = true;
                selectedButton = NULL;
            }
            if (hidKeysHeld() & KEY_TOUCH && touch_grab) {
                tVBOpt.ANAGLYPH_DEPTH = 8 - (touch_pos.py - 37) / 11;
            } else {
                touch_grab = false;
            }

            if (tVBOpt.ANAGLYPH_DEPTH < -8) tVBOpt.ANAGLYPH_DEPTH = -8;
            if (tVBOpt.ANAGLYPH_DEPTH > 8) tVBOpt.ANAGLYPH_DEPTH = 8;

            // draw depth slider
            C2D_DrawText(&text_depth, C2D_AlignCenter | C2D_WithColor, 280, 12, 0, 0.7, 0.7, TINT_COLOR);
            C2D_DrawRectSolid(279, 40, 0, 2, 16*11, 0xff404040);
            for (int y = 40; y <= 40 + 16*11; y += 11) {
                C2D_DrawRectSolid(272, y, 0, 16, 2, 0xff404040);
            }
            C2D_DrawRectSolid(256, 34 + (8 - tVBOpt.ANAGLYPH_DEPTH) * 11, 0, 48, 14, touch_grab ? TINT_50 : TINT_COLOR);
            if (buttonLock) C2D_DrawRectSolid(260, 42 + (8 - tVBOpt.ANAGLYPH_DEPTH) * 11, 0, 40, 1, BLACK);
        }
    LOOP_END(anaglyph_settings_buttons);
    if (button < 8) {
        tVBOpt.ANAGLYPH_LEFT = button;
        [[gnu::musttail]] return anaglyph_settings(button);
    } else if (button < 16) {
        tVBOpt.ANAGLYPH_RIGHT = button;
        [[gnu::musttail]] return anaglyph_settings(button);
    } else switch (button) {
        case ANAGLYPH_BACK:
            [[gnu::musttail]] return video_settings(VIDEO_SETTINGS);
    }
}

static void sound_error(void) {
    LOOP_BEGIN(sound_error_buttons, 0);
        C2D_DrawText(&text_sound_error, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(sound_error_buttons);
    return;
}

static bool areyousure(C2D_Text *message) {
    #undef DEFAULT_RETURN
    #define DEFAULT_RETURN false
    LOOP_BEGIN(areyousure_buttons, AREYOUSURE_NO);
        C2D_DrawText(message, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(areyousure_buttons);
    #undef DEFAULT_RETURN
    #define DEFAULT_RETURN
    return button == AREYOUSURE_YES;
}

static void savestate_error(char *message, int last_button, int selected_state) {
    C2D_Text text;
    C2D_TextBufClear(dynamic_textbuf);
    C2D_TextParse(&text, dynamic_textbuf, message);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(about_buttons);
    [[gnu::musttail]] return savestate_menu(last_button, selected_state);
}

static void savestate_confirm(char *message, int last_button, int selected_state) {
    C2D_Text text;
    C2D_TextBufClear(dynamic_textbuf);
    C2D_TextParse(&text, dynamic_textbuf, message);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(savestate_confirm_buttons, 0);
        C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(savestate_confirm_buttons);
    if (button) [[gnu::musttail]] return savestate_menu(last_button, selected_state);
    else return;
}

static void savestate_menu(int initial_button, int selected_state) {
    char dynamic_text[32];
    sprintf(dynamic_text, "State %d", selected_state);

    C2D_Text selected_state_text;
    C2D_TextBufClear(dynamic_textbuf);
    C2D_TextParse(&selected_state_text, dynamic_textbuf, dynamic_text);
    C2D_TextOptimize(&selected_state_text);

    savestate_buttons[LOAD_SAVESTATE].hidden = savestate_buttons[DELETE_SAVESTATE].hidden = !emulation_hasstate(selected_state);

    LOOP_BEGIN(savestate_buttons, initial_button);
        int keys_down = hidKeysDown();
        int state_shift = !!(keys_down & KEY_R) - !!(keys_down & KEY_L);
        if (state_shift != 0) {
            selected_state = (selected_state + 10 + state_shift) % 10;
            sprintf(dynamic_text, "State %d", selected_state);
            C2D_Text selected_state_text;
            C2D_TextBufClear(dynamic_textbuf);
            C2D_TextParse(&selected_state_text, dynamic_textbuf, dynamic_text);
            C2D_TextOptimize(&selected_state_text);
            savestate_buttons[LOAD_SAVESTATE].hidden = savestate_buttons[DELETE_SAVESTATE].hidden = !emulation_hasstate(selected_state);
        }
        C2D_DrawText(&text_savestate_menu, C2D_AlignCenter | C2D_WithColor, 320 / 2, 10, 0, 0.7, 0.7, TINT_COLOR);
        C2D_DrawText(&selected_state_text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 240 / 3, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(savestate_buttons);

    last_savestate = selected_state;
    
    switch(button) {
        case SAVESTATE_BACK:
            [[gnu::musttail]] return main_menu(MAIN_MENU_SAVESTATES);
        case SAVE_SAVESTATE:
            if (emulation_sstate(selected_state) != 0) {
                [[gnu::musttail]] return savestate_error("Could not save state", SAVE_SAVESTATE, selected_state);
            } else {
                [[gnu::musttail]] return savestate_confirm("Save complete!", SAVE_SAVESTATE, selected_state);
            }
        case LOAD_SAVESTATE:
            if (emulation_lstate(selected_state) != 0) {
                [[gnu::musttail]] return savestate_error("Could not load state", LOAD_SAVESTATE, selected_state);
            } else {
                [[gnu::musttail]] return savestate_confirm("Load complete!", LOAD_SAVESTATE, selected_state);
            }
        case DELETE_SAVESTATE:
            if (emulation_rmstate(selected_state) != 0) {
                [[gnu::musttail]] return savestate_error("Could not delete state", DELETE_SAVESTATE, selected_state);
            } else {
                [[gnu::musttail]] return savestate_menu(SAVE_SAVESTATE, selected_state);
            }
        case PREV_SAVESTATE:
            [[gnu::musttail]] return savestate_menu(PREV_SAVESTATE, selected_state == 0 ? 9 : selected_state - 1);
        case NEXT_SAVESTATE:
            [[gnu::musttail]] return savestate_menu(NEXT_SAVESTATE, selected_state == 9 ? 0 : selected_state + 1);
    }
}

static void about(void) {
    C2D_SpriteSetPos(&logo_sprite, 320 / 2, 36);
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, C2D_Color32(255, 0, 0, 255), 1);
    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawSpriteTinted(&logo_sprite, &tint);
        C2D_DrawText(&text_about, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, TINT_COLOR);
    LOOP_END(about_buttons);
    [[gnu::musttail]] return options(OPTIONS_ABOUT);
}

static void load_error(int err, bool unloaded) {
    C2D_Text text;
    char code_message[32];
    snprintf(code_message, sizeof(code_message), "Error code: %d", err);
    C2D_TextParse(&text, dynamic_textbuf, code_message);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawText(&text_loaderr, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, TINT_COLOR);
        C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 120, 0, 0.5, 0.5, TINT_COLOR);
        if (unloaded) {
            C2D_DrawText(&text_unloaded, C2D_AlignCenter | C2D_WithColor, 320 / 2, 160, 0, 0.5, 0.5, TINT_COLOR);
        }
    LOOP_END(about_buttons);
    [[gnu::musttail]] return rom_loader();
}

static void forwarder_error(int err) {
    C2D_Text text;
    char code_message[32];
    snprintf(code_message, sizeof(code_message), "Error code: %d", err);
    C2D_TextParse(&text, dynamic_textbuf, code_message);
    C2D_TextOptimize(&text);
    LOOP_BEGIN(forwarder_error_buttons, 0);
        C2D_DrawText(&text_loaderr, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.5, 0.5, TINT_COLOR);
        C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 120, 0, 0.5, 0.5, TINT_COLOR);
    LOOP_END(forwarder_error_buttons);
    guiop = GUIEXIT;
    return;
}

static void load_rom(void) {
    if (save_thread) threadJoin(save_thread, U64_MAX);
    int ret;
    if ((ret = v810_load_init())) {
        // instant fail
        if (tVBOpt.FORWARDER) {
            [[gnu::musttail]] return forwarder_error(ret);
        } else {
            [[gnu::musttail]] return load_error(ret, false);
        }
    }
    C2D_Text text;
    C2D_TextBufClear(dynamic_textbuf);
    char *filename = strrchr(tVBOpt.ROM_PATH, '/');
    if (filename) filename++;
    else filename = tVBOpt.ROM_PATH;
    C2D_TextParse(&text, dynamic_textbuf, filename);
    C2D_TextOptimize(&text);

    if (tVBOpt.FORWARDER) load_rom_buttons[0].hidden = true;

    LOOP_BEGIN(load_rom_buttons, -1);
        ret = v810_load_step();
        if (ret < 0) {
            // error
            loop = false;
        } else {
            C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 10, 0, 0.5, 0.5, TINT_COLOR);
            C2D_DrawText(&text_loading, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.8, 0.8, TINT_COLOR);
            C2D_DrawRectSolid(60, 140, 0, 200, 16, TINT_50);
            C2D_DrawRectSolid(60, 140, 0, 2 * ret, 16, TINT_COLOR);
            if (ret == 100) {
                // complete
                loop = false;
            }
        }
    LOOP_END(load_rom_buttons);
    if (tVBOpt.GAME_SETTINGS) loadFileOptions();
    if (ret == 100) {
        // complete
        game_running = true;
        loadGameOptions();
        last_savestate = 0;
        return;
    } else {
        game_running = false;
        // redraw logo since we unloaded
        C3D_FrameBegin(0);
        draw_logo();
        C3D_FrameEnd(0);
        if (ret < 0) {
            // error
            if (tVBOpt.FORWARDER) {
                [[gnu::musttail]] return forwarder_error(ret);
            } else {
                [[gnu::musttail]] return load_error(ret, true);
            }
        } else {
            // cancelled
            v810_load_cancel();
            [[gnu::musttail]] return rom_loader();
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
        int base_colour = buttons[i].colour;
        if (base_colour == 0) base_colour = tVBOpt.TINT;
        u32 normal_colour = COLOR_BRIGHTNESS(base_colour, 1.0);
        u32 pressed_colour = COLOR_BRIGHTNESS(base_colour, 0.5);
        C2D_DrawRectSolid(buttons[i].x, buttons[i].y, 0, buttons[i].w, buttons[i].h,
            pressed == i ? pressed_colour : normal_colour);
        if (selectedButton == &buttons[i]) {
            if (buttons[i].draw_selected_rect) {
                C2D_DrawLine(buttons[i].x + 2, buttons[i].y + 2.5, BLACK, buttons[i].x + buttons[i].w - 2, buttons[i].y + 2.5, BLACK, 1, 0);
                C2D_DrawLine(buttons[i].x + buttons[i].w - 2.5, buttons[i].y + 2, BLACK, buttons[i].x + buttons[i].w - 2.5, buttons[i].y + buttons[i].h - 2, BLACK, 1, 0);
                C2D_DrawLine(buttons[i].x + buttons[i].w - 2, buttons[i].y + buttons[i].h - 2.5, BLACK, buttons[i].x + 2, buttons[i].y + buttons[i].h - 2.5, BLACK, 1, 0);
                C2D_DrawLine(buttons[i].x + 2.5, buttons[i].y + buttons[i].h - 2, BLACK, buttons[i].x + 2.5, buttons[i].y + 2, BLACK, 1, 0);
            } else {
                C2D_DrawRectSolid(buttons[i].x + 4, buttons[i].y + buttons[i].h - 4, 0, buttons[i].w - 8, 1, BLACK);
            }
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
        if (buttons[i].show_option) C2D_DrawText(buttons[i].option_texts[buttons[i].option], C2D_AlignLeft, buttons[i].x, buttons[i].y, 0, 0.5, 0.5);
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
    C2D_DrawText(&text, C2D_AlignLeft, 325-53*6, 240 - 12, 0, 0.35, 0.35);

    // DRC
    sprintf(buf, "D:%5.2fms", drc_time);
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 325-53*5, 240 - 12, 0, 0.35, 0.35);

    // C3D
    sprintf(buf, "C:%5.2fms", C3D_GetProcessingTime());
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 325-53*4, 240 - 12, 0, 0.35, 0.35);

    // PICA
    sprintf(buf, "P:%5.2fms", C3D_GetDrawingTime());
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 325-53*3, 240 - 12, 0, 0.35, 0.35);

    // Memory
    sprintf(buf, "M:%5.2f%%", (cache_pos-cache_start)*4*100./CACHE_SIZE);
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 325-53*2, 240 - 12, 0, 0.35, 0.35);

    // VIP
    sprintf(buf, "V:%d", videoProcessingTime());
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_AlignLeft, 330-54-7, 240 - 12, 0, 0.35, 0.35);
}

void guiInit(void) {
    u8 model = 0;
    cfguInit();
    CFGU_GetSystemModel(&model);
    cfguExit();
    old_2ds = (model == CFG_MODEL_2DS);
    any_2ds = old_2ds || model == CFG_MODEL_N2DSXL;

    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);

    screen = C3D_RenderTargetCreate(GSP_SCREEN_WIDTH, GSP_SCREEN_HEIGHT_BOTTOM, GPU_RB_RGB8, -1);
    C3D_RenderTargetSetOutput(screen, GFX_BOTTOM, GFX_LEFT,
			GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

    C2D_SetTintMode(C2D_TintMult);

    sprite_sheet = C2D_SpriteSheetLoadFromMem(sprites_t3x, sprites_t3x_size);
    C2D_SpriteFromSheet(&colour_wheel_sprite, sprite_sheet, sprites_colour_wheel_idx);
    C2D_SpriteSetCenter(&colour_wheel_sprite, 0.5, 0.5);
    C2D_SpriteFromSheet(&logo_sprite, sprite_sheet, sprites_logo_idx);
    C2D_SpriteSetCenter(&logo_sprite, 0.5, 0.5);
    C2D_SpriteFromSheet(&text_3ds_sprite, sprite_sheet, sprites_3ds_text_idx);
    C2D_SpriteSetPos(&text_3ds_sprite, 128, 18);
    C2D_SpriteFromSheet(&text_vb_sprite, sprite_sheet, sprites_vb_text_idx);
    C2D_SpriteSetPos(&text_vb_sprite, 178, 98);
    C2D_SpriteFromSheet(&vb_icon_sprite, sprite_sheet, sprites_vb_icon_idx);
    C2D_SpriteSetPos(&vb_icon_sprite, 137, 120);
    C2D_SpriteFromSheet(&text_toggle_sprite, sprite_sheet, sprites_toggle_text_idx);
    C2D_SpriteFromSheet(&text_turbo_sprite, sprite_sheet, sprites_turbo_text_idx);

    splash_sheet = C2D_SpriteSheetLoadFromMem(splash_t3x, splash_t3x_size);
    C2D_SpriteFromSheet(&splash_left, splash_sheet, splash_splash_left_idx);
    C2D_SpriteFromSheet(&splash_right, splash_sheet, splash_splash_right_idx);
    C2D_SpriteSetPos(&splash_right, 0, 256);

    static_textbuf = C2D_TextBufNew(2048);
    dynamic_textbuf = C2D_TextBufNew(4096);
    SETUP_ALL_BUTTONS
    STATIC_TEXT(&text_A, "A")
    STATIC_TEXT(&text_B, "B")
    STATIC_TEXT(&text_btn_A, "\uE000")
    STATIC_TEXT(&text_btn_B, "\uE001")
    STATIC_TEXT(&text_btn_X, "\uE002")
    STATIC_TEXT(&text_btn_L, "\uE004")
    STATIC_TEXT(&text_btn_R, "\uE005")
    STATIC_TEXT(&text_switch, "Switch")
    STATIC_TEXT(&text_saving, "Saving...")
    STATIC_TEXT(&text_on, "On")
    STATIC_TEXT(&text_off, "Off")
    STATIC_TEXT(&text_toggle, "Toggle")
    STATIC_TEXT(&text_hold, "Hold")
    STATIC_TEXT(&text_nintendo_3ds, "Nintendo 3DS")
    STATIC_TEXT(&text_vbipd, "Virtual Boy IPD")
    STATIC_TEXT(&text_vb_lpad, "Virtual Boy Left D-Pad")
    STATIC_TEXT(&text_vb_rpad, "Virtual Boy Right D-Pad")
    STATIC_TEXT(&text_mirror_abxy, "Mirror ABXY Buttons")
    STATIC_TEXT(&text_vblink, "VBLink")
    STATIC_TEXT(&text_left, "Left")
    STATIC_TEXT(&text_right, "Right")
    STATIC_TEXT(&text_sound_error, "Error: couldn't initialize audio.\nDid you dump your DSP firmware?")
    STATIC_TEXT(&text_debug_filenames, "Please share debug_info.txt and\ndebug_replay.bin.gz in your bug\nreport.")
    STATIC_TEXT(&text_anykeyexit, "Press any key to exit")
    STATIC_TEXT(&text_about, VERSION "\nBy Floogle, danielps, & others\nSplash screen by Morintari\nCustom control scheme by nevumx\nHeavily based on Reality Boy by David Tucker\nMore info at: github.com/skyfloogle/red-viper")
    STATIC_TEXT(&text_loading, "Loading...")
    STATIC_TEXT(&text_loaderr, "Failed to load ROM.")
    STATIC_TEXT(&text_unloaded, "The current ROM has been unloaded.")
    STATIC_TEXT(&text_yes, "Yes")
    STATIC_TEXT(&text_no, "No")
    STATIC_TEXT(&text_areyousure_reset, "Are you sure you want to reset?")
    STATIC_TEXT(&text_areyousure_exit, "Are you sure you want to exit?")
    STATIC_TEXT(&text_savestate_menu, "Savestates")
    STATIC_TEXT(&text_save, "Save")
    STATIC_TEXT(&text_load, "Load")
    STATIC_TEXT(&text_preset, "Preset")
    STATIC_TEXT(&text_custom, "Custom")
    STATIC_TEXT(&text_custom_3ds_button_DUP, "\uE079")
    STATIC_TEXT(&text_custom_3ds_button_DDOWN, "\uE07A")
    STATIC_TEXT(&text_custom_3ds_button_DLEFT, "\uE07B")
    STATIC_TEXT(&text_custom_3ds_button_DRIGHT, "\uE07C")
    STATIC_TEXT(&text_custom_3ds_button_CPAD_UP, "\uE077\uE01B")
    STATIC_TEXT(&text_custom_3ds_button_CPAD_DOWN, "\uE077\uE01C")
    STATIC_TEXT(&text_custom_3ds_button_CPAD_LEFT, "\uE077\uE01A")
    STATIC_TEXT(&text_custom_3ds_button_CPAD_RIGHT, "\uE077\uE019")
    STATIC_TEXT(&text_custom_3ds_button_CSTICK_UP, "\uE04A\uE01B")     
    STATIC_TEXT(&text_custom_3ds_button_CSTICK_DOWN, "\uE04A\uE01C")     
    STATIC_TEXT(&text_custom_3ds_button_CSTICK_LEFT, "\uE04A\uE01A")     
    STATIC_TEXT(&text_custom_3ds_button_CSTICK_RIGHT, "\uE04A\uE019")     
    STATIC_TEXT(&text_custom_3ds_button_A, "\uE000")
    STATIC_TEXT(&text_custom_3ds_button_X, "\uE002")
    STATIC_TEXT(&text_custom_3ds_button_B, "\uE001")
    STATIC_TEXT(&text_custom_3ds_button_Y, "\uE003")
    STATIC_TEXT(&text_custom_3ds_button_START, "STA")
    STATIC_TEXT(&text_custom_3ds_button_SELECT, "SEL")
    STATIC_TEXT(&text_custom_3ds_button_L, "\uE004")
    STATIC_TEXT(&text_custom_3ds_button_R, "\uE005")
    STATIC_TEXT(&text_custom_3ds_button_ZL, "\uE054")
    STATIC_TEXT(&text_custom_3ds_button_ZR, "\uE055")
    STATIC_TEXT(&text_custom_vb_button_KEY_L, "\uE004")
    STATIC_TEXT(&text_custom_vb_button_KEY_R, "\uE005")
    STATIC_TEXT(&text_custom_vb_button_KEY_SELECT, "SEL")
    STATIC_TEXT(&text_custom_vb_button_KEY_START, "STA")
    STATIC_TEXT(&text_custom_vb_button_KEY_B, "\uE001")
    STATIC_TEXT(&text_custom_vb_button_KEY_A, "\uE000")
    STATIC_TEXT(&text_custom_vb_button_RPAD_R, "R\uE07C")
    STATIC_TEXT(&text_custom_vb_button_RPAD_L, "R\uE07B")
    STATIC_TEXT(&text_custom_vb_button_RPAD_D, "R\uE07A")
    STATIC_TEXT(&text_custom_vb_button_RPAD_U, "R\uE079")
    STATIC_TEXT(&text_custom_vb_button_LPAD_R, "L\uE07C")
    STATIC_TEXT(&text_custom_vb_button_LPAD_L, "L\uE07B")
    STATIC_TEXT(&text_custom_vb_button_LPAD_D, "L\uE07A")
    STATIC_TEXT(&text_custom_vb_button_LPAD_U, "L\uE079")
    STATIC_TEXT(&text_error, "Error")
    STATIC_TEXT(&text_3ds, "3DS")
    STATIC_TEXT(&text_vb, "VB")
    STATIC_TEXT(&text_map, "MAP")
    STATIC_TEXT(&text_currently_mapped_to, "Currently\nmapped to:")
    STATIC_TEXT(&text_normal, "Normal")
    STATIC_TEXT(&text_turbo, "Turbo")
    STATIC_TEXT(&text_current_default, "Current mode is default")
    STATIC_TEXT(&text_anaglyph, "Anaglyph")
    STATIC_TEXT(&text_depth, "Depth")
    STATIC_TEXT(&text_cpp_on, "Circle Pad Pro connected.")
    STATIC_TEXT(&text_cpp_off, "No Circle Pad Pro found.")
    STATIC_TEXT(&text_monochrome, "Monochrome")
    STATIC_TEXT(&text_multicolor, "Multicolor")
    STATIC_TEXT(&text_brighten, "Brighten")
    STATIC_TEXT(&text_brightness_disclaimer, "Actual brightness may vary by game.")
    STATIC_TEXT(&text_multi_init_error, "Could not start wireless.\nIs wireless enabled?")
    STATIC_TEXT(&text_multi_disconnect, "Peer disconnected.")
}

static bool shouldRedrawMenu = true;
static bool inMenu = false;

void openMenu(void) {
    inMenu = true;
    shouldRedrawMenu = true;
    if (game_running) {
        sound_pause();
        save_sram();
    }
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    main_menu(game_running ? MAIN_MENU_RESUME : MAIN_MENU_LOAD_ROM);
    gspWaitForVBlank();
    if (guiop == 0) sound_resume();
    else if (guiop == AKILL) sound_refresh();
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
        vtotal_top = old_2ds || tVBOpt.ANAGLYPH ? 494 : 989;
        vtotal_bottom = 494;
        startPeriodicVsync(frame_pacer_thread);
    } else {
        vtotal_top = old_2ds || tVBOpt.ANAGLYPH ? 413 : 827;
        vtotal_bottom = 413;
        startPeriodic(frame_pacer_thread, 20000000, false);
    }
    // update VTotal only when necessary
    static bool old_enable = false;
    if (enable == old_enable) return;
    old_enable = enable;
    gspWaitForVBlank();
    GSPGPU_WriteHWRegs(0x400424, &vtotal_top, 4);
    GSPGPU_WriteHWRegs(0x400524, &vtotal_bottom, 4);
}

void toggleAnaglyph(bool enable, bool also_update_vsync) {
    tVBOpt.ANAGLYPH = enable;
    gfxSet3D(!enable);
    if (!also_update_vsync) return;
    // updating 3d mode resets VTotal, so turn off VSync in advance to fix the cache
    toggleVsync(false);
    // push 1 frame and wait for VBlank to reset VTotal
    C3D_FrameBegin(0);
    video_flush(true);
    C3D_FrameEnd(0);
    gspWaitForVBlank();
    // re-enable VSync if applicable
    toggleVsync(tVBOpt.VSYNC);
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

void showSoundError(void) {
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    sound_error();
}

static void save_debug_info(void) {
    C3D_FrameBegin(0);
    C2D_TargetClear(screen, 0);
    C2D_SceneBegin(screen);
    C2D_DrawText(&text_saving, C2D_AlignCenter | C2D_WithColor, 320 / 2, 100, 0, 0.7, 0.7, TINT_COLOR);
    C2D_Flush();
    C3D_FrameEnd(0);

    drc_dumpDebugInfo(0);

    LOOP_BEGIN(about_buttons, 0);
        C2D_DrawText(&text_debug_filenames, C2D_AlignCenter | C2D_WithColor, 320 / 2, 80, 0, 0.7, 0.7, TINT_COLOR);
    LOOP_END(about_buttons);
    [[gnu::musttail]] return options(OPTIONS_DEBUG);
}

void showError(int code) {
    sound_pause();
    if (!backlightEnabled) toggleBacklight(true);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C2D_TextBufClear(dynamic_textbuf);
    char buf[100];
    sprintf(buf, "DRC error #%d\nPC=0x%08lx\nDumping debug info...", code, vb_state->v810_state.PC);
    C2D_Text text;
    C2D_TextParse(&text, dynamic_textbuf, buf);
    C2D_TextOptimize(&text);
    C3D_FrameBegin(0);
    C2D_TargetClear(screen, 0);
    C2D_SceneBegin(screen);
    C2D_DrawText(&text, C2D_AlignCenter | C2D_WithColor, 320 / 2, 40, 0, 0.7, 0.7, TINT_COLOR);
    C2D_Flush();
    C3D_FrameEnd(0);

    drc_dumpDebugInfo(code);

    C3D_FrameBegin(0);
    C3D_FrameDrawOn(screen);
    C2D_DrawText(&text_debug_filenames, C2D_AlignCenter | C2D_WithColor, 320 / 2, 120, 0, 0.7, 0.7, TINT_COLOR);
    C2D_DrawText(&text_anykeyexit, C2D_AlignCenter | C2D_WithColor, 320 / 2, 180, 0, 0.7, 0.7, TINT_COLOR);
    C2D_Flush();
    C3D_FrameEnd(0);
}

void setCustomControls(void) {
    vbkey[__builtin_ctz(KEY_DUP)] = tVBOpt.CUSTOM_MAPPING_DUP;
    vbkey[__builtin_ctz(KEY_DDOWN)] = tVBOpt.CUSTOM_MAPPING_DDOWN;
    vbkey[__builtin_ctz(KEY_DLEFT)] = tVBOpt.CUSTOM_MAPPING_DLEFT;
    vbkey[__builtin_ctz(KEY_DRIGHT)] = tVBOpt.CUSTOM_MAPPING_DRIGHT;
    vbkey[__builtin_ctz(KEY_CPAD_UP)] = tVBOpt.CUSTOM_MAPPING_CPAD_UP;
    vbkey[__builtin_ctz(KEY_CPAD_DOWN)] = tVBOpt.CUSTOM_MAPPING_CPAD_DOWN;
    vbkey[__builtin_ctz(KEY_CPAD_LEFT)] = tVBOpt.CUSTOM_MAPPING_CPAD_LEFT;
    vbkey[__builtin_ctz(KEY_CPAD_RIGHT)] = tVBOpt.CUSTOM_MAPPING_CPAD_RIGHT;
    vbkey[__builtin_ctz(KEY_A)] = tVBOpt.CUSTOM_MAPPING_A;
    vbkey[__builtin_ctz(KEY_X)] = tVBOpt.CUSTOM_MAPPING_X;
    vbkey[__builtin_ctz(KEY_B)] = tVBOpt.CUSTOM_MAPPING_B;
    vbkey[__builtin_ctz(KEY_Y)] = tVBOpt.CUSTOM_MAPPING_Y;
    vbkey[__builtin_ctz(KEY_START)] = tVBOpt.CUSTOM_MAPPING_START;
    vbkey[__builtin_ctz(KEY_SELECT)] = tVBOpt.CUSTOM_MAPPING_SELECT;
    vbkey[__builtin_ctz(KEY_L)] = tVBOpt.CUSTOM_MAPPING_L;
    vbkey[__builtin_ctz(KEY_R)] = tVBOpt.CUSTOM_MAPPING_R;

    bool new_3ds = tVBOpt.CPP_ENABLED;
    if (!new_3ds) APT_CheckNew3DS(&new_3ds);
    if (new_3ds) {
        vbkey[__builtin_ctz(KEY_CSTICK_UP)] = tVBOpt.CUSTOM_MAPPING_CSTICK_UP;
        vbkey[__builtin_ctz(KEY_CSTICK_DOWN)] = tVBOpt.CUSTOM_MAPPING_CSTICK_DOWN;
        vbkey[__builtin_ctz(KEY_CSTICK_LEFT)] = tVBOpt.CUSTOM_MAPPING_CSTICK_LEFT;
        vbkey[__builtin_ctz(KEY_CSTICK_RIGHT)] = tVBOpt.CUSTOM_MAPPING_CSTICK_RIGHT;
        vbkey[__builtin_ctz(KEY_ZL)] = tVBOpt.CUSTOM_MAPPING_ZL;
        vbkey[__builtin_ctz(KEY_ZR)] = tVBOpt.CUSTOM_MAPPING_ZR;
    }
}

void setPresetControls(bool buttons) {
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
    switch (tVBOpt.DPAD_MODE) {
        default: // VB LPAD
            vbkey[__builtin_ctz(KEY_DUP)] = VB_LPAD_U;
            vbkey[__builtin_ctz(KEY_DDOWN)] = VB_LPAD_D;
            vbkey[__builtin_ctz(KEY_DLEFT)] = VB_LPAD_L;
            vbkey[__builtin_ctz(KEY_DRIGHT)] = VB_LPAD_R;
            break;
        case 1: // VB RPAD
            vbkey[__builtin_ctz(KEY_DUP)] = VB_RPAD_U;
            vbkey[__builtin_ctz(KEY_DDOWN)] = VB_RPAD_D;
            vbkey[__builtin_ctz(KEY_DLEFT)] = VB_RPAD_L;
            vbkey[__builtin_ctz(KEY_DRIGHT)] = VB_RPAD_R;
            break;
        case 2: // Mirror ABXY buttons
            vbkey[__builtin_ctz(KEY_DUP)] = vbkey[__builtin_ctz(KEY_X)];
            vbkey[__builtin_ctz(KEY_DDOWN)] = vbkey[__builtin_ctz(KEY_B)];
            vbkey[__builtin_ctz(KEY_DLEFT)] = vbkey[__builtin_ctz(KEY_Y)];
            vbkey[__builtin_ctz(KEY_DRIGHT)] = vbkey[__builtin_ctz(KEY_A)];
            break;
    }
    bool new_3ds = tVBOpt.CPP_ENABLED;
    if (!new_3ds) APT_CheckNew3DS(&new_3ds);
    if (new_3ds) {
        vbkey[__builtin_ctz(KEY_L)] = tVBOpt.ZLZR_MODE <= 1 ? VB_KEY_L : tVBOpt.ZLZR_MODE == 2 ? VB_KEY_B : VB_KEY_A;
        vbkey[__builtin_ctz(KEY_R)] = tVBOpt.ZLZR_MODE <= 1 ? VB_KEY_R : tVBOpt.ZLZR_MODE == 2 ? VB_KEY_A : VB_KEY_B;
        vbkey[__builtin_ctz(KEY_ZL)] = tVBOpt.ZLZR_MODE == 0 ? VB_KEY_B : tVBOpt.ZLZR_MODE == 1 ? VB_KEY_A : VB_KEY_L;
        vbkey[__builtin_ctz(KEY_ZR)] = tVBOpt.ZLZR_MODE == 0 ? VB_KEY_A : tVBOpt.ZLZR_MODE == 1 ? VB_KEY_B : VB_KEY_R;
    }
}

bool guiShouldSwitch(void) {
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    return tVBOpt.TOUCH_SWITCH && touch_pos.px >= 320 - 64 && touch_pos.py < 32;
}

void drawTouchControls(int inputs) {
    int col_up = TINT_50;
    int col_down = TINT_75;
    int col_drag = TINT_90;
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

    if (tVBOpt.TOUCH_SWITCH) {
        C2D_DrawRectSolid(320 - 64, 0, 0, 64, 32, TINT_50);
        C2D_DrawText(&text_switch, C2D_AlignCenter, 320 - 32, 6, 0, 0.7, 0.7);
    }
}

extern int input_state;

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

    if (!shouldRedrawMenu && !tVBOpt.PERF_INFO && !tVBOpt.INPUTS)
        return;

    // unclear why this is necessary, but omitting it can lead to weird graphical glitches
    C3D_UpdateUniforms(GPU_GEOMETRY_SHADER);

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
            int col_top = TINT_50;
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

    if (tVBOpt.INPUTS) {
        int guiInputs = guiGetInput(false);
        u8 mods[16] = {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
        if (guiInputs & VB_RPAD_U) mods[__builtin_ctz(VB_RPAD_U)] = 4;
        if (guiInputs & VB_RPAD_D) mods[__builtin_ctz(VB_RPAD_D)] = 4;
        if (guiInputs & VB_RPAD_L) mods[__builtin_ctz(VB_RPAD_L)] = 4;
        if (guiInputs & VB_RPAD_R) mods[__builtin_ctz(VB_RPAD_R)] = 4;
        if (guiInputs & VB_KEY_A) mods[__builtin_ctz(VB_KEY_A)] = 4;
        if (guiInputs & VB_KEY_B) mods[__builtin_ctz(VB_KEY_B)] = 4;
        for (int i = 0; i < 32; i++) {
            if ((input_state & (1 << i)) || (tVBOpt.CUSTOM_MOD[i] == 2 && (hidKeysHeld() & (1 << i)))) {
                int oldmod = mods[__builtin_ctz(vbkey[i])];
                if (tVBOpt.CUSTOM_MOD[i] != 0 || oldmod == 0 || oldmod == 3)
                    if (oldmod != 1) mods[__builtin_ctz(vbkey[i])] = tVBOpt.CUSTOM_MOD[i];
            }
        }
        // normal, toggle, turbo, up
        static u32 cols[] = {0xff808080, 0xff804000, 0xff000080, 0xff000000};
        // dpads
        C2D_DrawRectSolid(320/2-27, 8, 0, 54, 24, C2D_Color32(0, 0, 0, 255));
        C2D_DrawRectSolid(320/2-20-6, 16, 0, 12, 4, C2D_Color32(64, 64, 64, 255));
        C2D_DrawRectSolid(320/2+20-6, 16, 0, 12, 4, C2D_Color32(64, 64, 64, 255));
        C2D_DrawRectSolid(320/2-20-2, 12, 0, 4, 12, C2D_Color32(64, 64, 64, 255));
        C2D_DrawRectSolid(320/2+20-2, 12, 0, 4, 12, C2D_Color32(64, 64, 64, 255));
        for (int i = 0; i < 16; i++) {
            if ((1<<i) == VB_RPAD_U) C2D_DrawRectSolid(320/2+20-1, 13, 0, 2, 3, cols[mods[i]]);
            if ((1<<i) == VB_LPAD_U) C2D_DrawRectSolid(320/2-20-1, 13, 0, 2, 3, cols[mods[i]]);
            if ((1<<i) == VB_LPAD_D) C2D_DrawRectSolid(320/2-20-1, 20, 0, 2, 3, cols[mods[i]]);
            if ((1<<i) == VB_RPAD_D) C2D_DrawRectSolid(320/2+20-1, 20, 0, 2, 3, cols[mods[i]]);
            if ((1<<i) == VB_LPAD_L) C2D_DrawRectSolid(320/2-20-5, 17, 0, 3, 2, cols[mods[i]]);
            if ((1<<i) == VB_RPAD_L) C2D_DrawRectSolid(320/2+20-5, 17, 0, 3, 2, cols[mods[i]]);
            if ((1<<i) == VB_LPAD_R) C2D_DrawRectSolid(320/2-20+2, 17, 0, 3, 2, cols[mods[i]]);
            if ((1<<i) == VB_RPAD_R) C2D_DrawRectSolid(320/2+20+2, 17, 0, 3, 2, cols[mods[i]]);
        }
        // buttons
        C3D_ColorLogicOp(GPU_LOGICOP_COPY);
        C2D_DrawCircleSolid(320/2-12, 24, 0, 3, C2D_Color32(64, 64, 64, 255));
        C2D_DrawCircleSolid(320/2+12, 24, 0, 3, C2D_Color32(64, 64, 64, 255));
        C2D_DrawCircleSolid(320/2-6,  28, 0, 3, C2D_Color32(64, 64, 64, 255));
        C2D_DrawCircleSolid(320/2+6,  28, 0, 3, C2D_Color32(64, 64, 64, 255));
        C2D_DrawCircleSolid(320/2-8,  12, 0, 3, C2D_Color32(64, 64, 64, 255));
        C2D_DrawCircleSolid(320/2+8,  12, 0, 3, C2D_Color32(64, 64, 64, 255));
        for (int i = 0; i < 16; i++) {
            if ((1<<i) == VB_KEY_A)      C2D_DrawCircleSolid(320/2+12, 24, 0, 2, cols[mods[i]]);
            if ((1<<i) == VB_KEY_SELECT) C2D_DrawCircleSolid(320/2-12, 24, 0, 2, cols[mods[i]]);
            if ((1<<i) == VB_KEY_B)      C2D_DrawCircleSolid(320/2+6,  28, 0, 2, cols[mods[i]]);
            if ((1<<i) == VB_KEY_START)  C2D_DrawCircleSolid(320/2-6,  28, 0, 2, cols[mods[i]]);
            if ((1<<i) == VB_KEY_L)      C2D_DrawCircleSolid(320/2-8,  12, 0, 2, cols[mods[i]]);
            if ((1<<i) == VB_KEY_R)      C2D_DrawCircleSolid(320/2+8,  12, 0, 2, cols[mods[i]]);
        }
    }

    C2D_Flush();

    C3D_ColorLogicOp(GPU_LOGICOP_COPY);
}

bool guiShouldPause(void) {
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    return (touch_pos.px < tVBOpt.PAUSE_RIGHT && (touch_pos.px >= 32 || (touch_pos.py > (old_2ds ? 0 : 32) && touch_pos.py < 240-32))) && backlightEnabled;
}

int guiGetInput(bool ingame) {
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    if (backlightEnabled) {
        if ((hidKeysHeld() & KEY_TOUCH) && guiShouldSwitch()) {
            if (ingame && (hidKeysDown() & KEY_TOUCH)) setPresetControls(!buttons_on_screen);
            return 0;
        }
        if (ingame) {
            if (touch_pos.px < 32 && touch_pos.py >= 240-32) {
                if ((tVBOpt.FF_TOGGLE ? hidKeysDown() : hidKeysHeld()) & KEY_TOUCH) {
                    tVBOpt.FASTFORWARD = !tVBOpt.FASTFORWARD;
                }
                return 0;
            }
            if (hidKeysDown() & KEY_TOUCH && (touch_pos.px <= 32 && touch_pos.py <= 32) && !old_2ds) {
                backlightEnabled = toggleBacklight(false);
                return 0;
            }
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
    } else if (hidKeysDown() & KEY_TOUCH) {
        backlightEnabled = toggleBacklight(true);
    }
    return 0;
}
