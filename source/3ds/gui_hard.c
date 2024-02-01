#include <stdlib.h>
#include <string.h>
#include <citro2d.h>
#include "vb_gui.h"
#include "vb_set.h"
#include "vb_types.h"

static C3D_RenderTarget *screen;

static C2D_TextBuf static_textbuf;
static C2D_TextBuf dynamic_textbuf;

// helpers
static inline int sqr(int i) {
    return i * i;
}

static inline int u16len(const u16 *s) {
    const u16 *e = s;
    while (*e) e++;
    return e - s;
}

typedef struct {
    char *str;
    float x, y, w, h;
    C2D_Text text;
} Button;

static inline int handle_buttons(Button buttons[], int count);
#define HANDLE_BUTTONS(buttons) handle_buttons(buttons, sizeof(buttons) / sizeof(buttons[0]))

#define SETUP_BUTTONS(arr) \
    for (int i = 0; i < sizeof(arr) / sizeof(arr[0]); i++) { \
        C2D_TextParse(&arr[i].text, static_textbuf, arr[i].str); \
        C2D_TextOptimize(&arr[i].text); \
    }

#define LOOP_BEGIN(buttons) \
    int button = 0; \
    bool loop = true; \
    while (loop && aptMainLoop()) { \
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW); \
        C2D_TargetClear(screen, 0); \
        C2D_SceneBegin(screen); \
        hidScanInput();

#define LOOP_END(buttons) \
        button = HANDLE_BUTTONS(buttons); \
        if (button >= 0) loop = false; \
        C2D_Flush(); \
        C3D_FrameEnd(0); \
    }

static void main_menu();
static Button main_menu_buttons[] = {
    {"Load ROM", 16, 16, 288, 144},
    {"Controls", 0, 176, 80, 64},
    {"Options", 240, 176, 80, 64},
    {"Quit", 112, 192, 96, 48},
};

static void game_menu();
static Button game_menu_buttons[] = {
    {"Load ROM", 224, 64, 80, 80},
    {"Controls", 0, 176, 80, 64},
    {"Options", 240, 176, 80, 64},
    {"Quit", 112, 192, 96, 48},
    {"Resume", 0, 0, 320, 48},
    {"Reset", 16, 64, 80, 80},
};

static void rom_loader();
static Button rom_loader_buttons[] = {
    {"Up", 0, 0, 32, 32},
    {"Back", 0, 208, 48, 32},
};

static void controls();
static Button controls_buttons[] = {
    {"Touchscreen\nsettings", 96, 144, 128, 64},
    {"Back", 0, 208, 48, 32},
};

#define SETUP_ALL_BUTTONS \
    SETUP_BUTTONS(main_menu_buttons); \
    SETUP_BUTTONS(game_menu_buttons); \
    SETUP_BUTTONS(rom_loader_buttons); \
    SETUP_BUTTONS(controls_buttons);

static void main_menu() {
    LOOP_BEGIN(main_menu_buttons);
    LOOP_END(main_menu_buttons);
    switch (button) {
        case 0:
            return rom_loader();
        case 1:
            return controls();
        case 2:
            return;
        case 3:
            return;
    }
}

static void game_menu() {
    LOOP_BEGIN(game_menu_buttons);
    LOOP_END(game_menu_buttons);
    switch (button) {
        case 0: // Load ROM
            guiop = AKILL | VBRESET;
            return rom_loader();
        case 1: // Controls
            return controls();
        case 2: // Options
            guiop = 0;
            return;
        case 3: // Quit
            guiop = AKILL | GUIEXIT;
            return;
        case 4: // Resume
            guiop = 0;
            return;
        case 5: // Reset
            guiop = AKILL | VBRESET;
            return;
    }
}

static void rom_loader() {
    FS_Archive sdmcArchive;
    FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, (FS_Path){PATH_EMPTY, 1, (uint8_t*)"/"});

    static u16 *path = NULL;
    static int path_cap = 128;
    if (!path) {
        path = calloc(path_cap, sizeof(u16));
        path[0] = '/';
    }
    // cut filename from path if we reload mid-game
    {
        u16 *path_end = path + u16len(path) - 1;
        while (*path_end != '/') *path_end-- = 0;
    }

    Handle dirHandle;
    Result res = FSUSER_OpenDirectory(&dirHandle, sdmcArchive, fsMakePath(PATH_UTF16, path));
    if (res) {
        // TODO error
        return;
    }

    FS_DirectoryEntry *dirs = malloc(8 * sizeof(FS_DirectoryEntry));
    FS_DirectoryEntry *files = malloc(8 * sizeof(FS_DirectoryEntry));
    int dirCount = 0, fileCount = 0;
    int dirCap = 8, fileCap = 8;

    // read .vb files and directories
    while (true) {
        u32 entriesRead;
        FS_DirectoryEntry thisEntry;
        FSDIR_Read(dirHandle, &entriesRead, 1, &thisEntry);
        if (!entriesRead) break;
        if (thisEntry.attributes & FS_ATTRIBUTE_DIRECTORY) {
            if (dirCount == dirCap) {
                dirCap *= 2;
                dirs = realloc(dirs, dirCap * sizeof(FS_DirectoryEntry));
            }
            dirs[dirCount++] = thisEntry;
        } else {
            // check the file extension
            bool length_2 = (thisEntry.shortExt[2] == 0 || thisEntry.shortExt[2] == ' ')
                 && (thisEntry.shortExt[3] == 0 || thisEntry.shortExt[3] == ' ');
            if (length_2 && strncmp(thisEntry.shortExt, "VB", 2) == 0) {
                if (fileCount == fileCap) {
                    fileCap *= 2;
                    files = realloc(files, fileCap * sizeof(FS_DirectoryEntry));
                }
                files[fileCount++] = thisEntry;
            }
        }
    }

    FSDIR_Close(dirHandle);
    FSUSER_CloseArchive(sdmcArchive);

    int entry_count = dirCount + fileCount;
    const float entry_height = 32;
    float scroll_top = -entry_height;
    float scroll_bottom = entry_count * entry_height - 240;
    if (scroll_bottom < scroll_top) scroll_bottom = scroll_top;
    float scroll_pos = scroll_top;

    int last_py = 0;
    int clicked_entry = -1;
    bool dragging = false;

    LOOP_BEGIN(rom_loader_buttons);
        // process rom list
        touchPosition touch_pos;
        hidTouchRead(&touch_pos);
        if ((hidKeysDown() & KEY_TOUCH) && touch_pos.px >= 48) {
            last_py = touch_pos.py;
            clicked_entry = floorf((touch_pos.py + scroll_pos) / entry_height);
            if (clicked_entry < 0 || clicked_entry >= entry_count)
                clicked_entry = -1;
            dragging = false;
        } else if (clicked_entry >= 0 && (hidKeysHeld() & KEY_TOUCH)) {
            if (!dragging && abs(touch_pos.py - last_py) >= 5) {
                clicked_entry = -1;
                dragging = true;
            }
        }
        if (dragging) {
            if (!(hidKeysHeld() & KEY_TOUCH)) {
                dragging = false;
            } else {
                scroll_pos -= touch_pos.py - last_py;
                last_py = touch_pos.py;
                scroll_pos = C2D_Clamp(scroll_pos, scroll_top, scroll_bottom);
            }
        } else if (clicked_entry >= 0) {
            if (!(hidKeysHeld() & KEY_TOUCH)) {
                bool clicked_dir = clicked_entry < dirCount;
                const u16 *new_entry = clicked_dir ? dirs[clicked_entry].name : files[clicked_entry - dirCount].name;
                int old_path_len = u16len(path);
                int suffix_len = u16len(new_entry) + clicked_dir;
                int new_path_len = old_path_len + suffix_len;
                if (new_path_len + 1 > path_cap) {
                    path = realloc(path, (path_cap *= 2) * sizeof(u16));
                }
                memcpy(path + old_path_len, new_entry, (suffix_len + 1) * sizeof(u16));
                if (clicked_entry < dirCount) {
                    // clicked on directory, so add a slash
                    path[new_path_len - 1] = '/';
                    path[new_path_len] = 0;
                }
                loop = false;
            }
        }
        // draw
        C2D_TextBufClear(dynamic_textbuf);
        float y = -scroll_pos;
        C2D_Text text;
        char ascii[128];
        ascii[127] = 0;
        // entries
        for (int i = 0; i < entry_count; i++) {
            if (y + entry_height >= 0 && y < 240) {
                for (int j = 0; j < 127; j++) {
                    ascii[j] = (i < dirCount ? dirs : files - dirCount)[i].name[j];
                    if (!ascii[j]) break;
                }
                C2D_TextParse(&text, dynamic_textbuf, ascii);
                C2D_TextOptimize(&text);
                C2D_DrawRectSolid(56, y, 0, 264, entry_height, C2D_Color32(clicked_entry == i ? 144 : 255, 0, 0, 255));
                C2D_DrawText(&text, C2D_AlignLeft, 64, y + 8, 0, 0.5, 0.5);
            }
            y += entry_height;
        }
        // path
        for (int i = 0; i < 127; i++) {
            ascii[i] = path[i];
            if (!ascii[i]) break;
        }
        C2D_TextParse(&text, dynamic_textbuf, ascii);
        C2D_TextOptimize(&text);
        C2D_DrawRectSolid(0, 0, 0, 320, 32, C2D_Color32(0, 0, 0, 255));
        C2D_DrawText(&text, C2D_AlignLeft | C2D_WithColor, 48, 8, 0, 0.5, 0.5, C2D_Color32(255, 0, 0, 255));
    LOOP_END(rom_loader_buttons);

    free(dirs);
    free(files);

    if (clicked_entry < 0) {
        switch (button) {
            case 0:
                int len = u16len(path);
                if (len > 1) {
                    // the stuff at the start of the file will get rid of everything after the last slash
                    // so we can just get rid of the slash at the end
                    path[len - 1] = 0;
                }
                return rom_loader();
            case 1: return main_menu();
        }
    } else if (clicked_entry < dirCount) {
        return rom_loader();
    } else {
        tVBOpt.ROM_PATH = realloc(tVBOpt.ROM_PATH, path_cap * sizeof(u16));
        memcpy(tVBOpt.ROM_PATH, path, path_cap * sizeof(u16));
        return;
    }
}

static void controls() {
    LOOP_BEGIN(controls_buttons);
    LOOP_END(controls_buttons);
    switch (button) {
        case 0: return;
        case 1: return main_menu();
    }
}

static inline int handle_buttons(Button buttons[], int count) {
    static int pressed = -1;
    int ret = -1;
    touchPosition touch_pos;
    hidTouchRead(&touch_pos);
    // check buttons for input
    for (int i = 0; i < count; i++) {
        if (
            touch_pos.px >= buttons[i].x && touch_pos.py >= buttons[i].y &&
            touch_pos.px < buttons[i].x + buttons[i].w && touch_pos.py < buttons[i].y + buttons[i].h
        ) {
            // touching the button
            if (hidKeysDown() & KEY_TOUCH) {
                pressed = i;
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
    }
    // draw buttons
    for (int i = 0; i < count; i++) {
        u32 normal_colour = C2D_Color32(255, 0, 0, 255);
        u32 pressed_colour = C2D_Color32(144, 0, 0, 255);
        C2D_DrawRectSolid(buttons[i].x, buttons[i].y, 0, buttons[i].w, buttons[i].h, pressed == i ? pressed_colour : normal_colour);
        C2D_DrawText(&buttons[i].text, C2D_AlignCenter, buttons[i].x + buttons[i].w / 2, buttons[i].y + buttons[i].h / 2 - 6, 0, 0.7, 0.7);
    }
    if (ret >= 0) pressed = -1;
    return ret;
}

void guiInit() {
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    screen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    static_textbuf = C2D_TextBufNew(1024);
    dynamic_textbuf = C2D_TextBufNew(4096);
    #define SETUP_BUTTONS(arr) \
        for (int i = 0; i < sizeof(arr) / sizeof(arr[0]); i++) { \
            C2D_TextParse(&arr[i].text, static_textbuf, arr[i].str); \
            C2D_TextOptimize(&arr[i].text); \
        }
    SETUP_ALL_BUTTONS;
}

void openMenu(bool rom_loaded) {
    if (rom_loaded) {
        gfxSetDoubleBuffering(GFX_TOP, false);
        if (tVBOpt.SOUND)
            ndspSetMasterVol(0.0);
    }
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    if (rom_loaded)
        game_menu();
    else
        main_menu();
    if (rom_loaded) {
        gfxSetDoubleBuffering(GFX_TOP, true);
        if (tVBOpt.SOUND)
            ndspSetMasterVol(1.0);
    }
}

void guiUpdate() {
    C2D_Prepare();
    C2D_TargetClear(screen, 0);
    C2D_SceneBegin(screen);

    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

    C2D_DrawRectSolid(320/2 - 100, 240/2 - 100, 0, 200, 200, C2D_Color32(64, 64, 64, 255));

    C2D_Flush();

	C3D_ColorLogicOp(GPU_LOGICOP_COPY);
}

bool guiShouldPause() {
    return true;
}