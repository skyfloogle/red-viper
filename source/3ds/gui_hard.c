#include <stdlib.h>
#include <string.h>
#include <citro2d.h>
#include "vb_types.h"

static C3D_RenderTarget *screen;

static C2D_TextBuf static_textbuf;

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

static void rom_loader() {
    FS_Archive sdmcArchive;
    FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, (FS_Path){PATH_EMPTY, 1, (uint8_t*)"/"});

    static u16 *path = NULL;
    static int path_len = 128;
    if (!path) {
        path = calloc(path_len, 2);
        path[0] = '/';
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
            if (strcmp(thisEntry.shortExt, "VB") == 0) {
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
    LOOP_BEGIN(rom_loader_buttons);
        touchPosition touch_pos;
        hidTouchRead(&touch_pos);
    LOOP_END(rom_loader_buttons);

    free(dirs);
    free(files);

    switch (button) {
        case 0: return rom_loader();
        case 1: return main_menu();
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
    #define SETUP_BUTTONS(arr) \
        for (int i = 0; i < sizeof(arr) / sizeof(arr[0]); i++) { \
            C2D_TextParse(&arr[i].text, static_textbuf, arr[i].str); \
            C2D_TextOptimize(&arr[i].text); \
        }
    SETUP_ALL_BUTTONS;
}

void openMenu(bool rom_loaded) {
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    main_menu();
}

void guiUpdate() {
    C2D_Prepare();
    C2D_TargetClear(screen, 0);
    C2D_SceneBegin(screen);

    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);

    C2D_Flush();

	C3D_ColorLogicOp(GPU_LOGICOP_COPY);
}