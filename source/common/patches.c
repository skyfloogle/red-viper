#include "v810_mem.h"
#include "vb_set.h"
#include "patches.h"

typedef struct {
    const char gameid[6];
    uint8_t size;
    WORD address;
    const uint8_t *original;
    const uint8_t *patched;
} Patch;

static const Patch patches[] = {
    // Jack Bros.expects CPU and VIP timings to line up in a certain
    // way during level transitions.
    // A flag at address 050000c0 is set during multiple tile copies,
    // blocking VRAM updates. The flag takes two XPENDs to clear.
    // Very precise delays are set up in-between the tile copies,
    // presumably to line up closely with how long the CPU takes to do
    // the tile copy and how long the VIP takes to render the scene.
    // It is not clear which combination of durations produces the same
    // result as when the game is played on original hardware.
    // With the current state of emulation, it will miss several beats
    // and display the next transition for 1 frame at the end of the
    // transition.
    // Some areas in Dragon's Belly take longer to copy, so adding 1
    // frame of delay isn't enough to prevent glitches.
    // Therefore, I add a delay of 2 frames to get them to line up well
    // enough that it at least doesn't produce those visual bugs.
    {
        .gameid = "EBVJBE",
        .address = 0x13714,
        .size = 2,
        .original = (const uint8_t[]){
            0x21, 0x40, // mov 1,r1
        },
        .patched = (const uint8_t[]){
            0x23, 0x40, // mov 3,r1
        },
    },
    {
        .gameid = "EBVJBJ",
        .address = 0x136e2,
        .size = 2,
        .original = (const uint8_t[]){
            0x21, 0x40, // mov 1,r1
        },
        .patched = (const uint8_t[]){
            0x23, 0x40, // mov 3,r1
        },
    },
    // Innsmouth no Yakata does a lot during its busywait that can be skipped.
    {
        .gameid = "8FVIMJ",
        .address = 0x19040,
        .size = 2,
        .original = (const uint8_t[]){
            0x44, 0x85, // be -0xbc
        },
        .patched = (const uint8_t[]){
            0xf0, 0x85, // be -0x10
        },
    },
    {
        .gameid = "8FVIME",
        .address = 0x19040,
        .size = 2,
        .original = (const uint8_t[]){
            0x44, 0x85, // be -0xbc
        },
        .patched = (const uint8_t[]){
            0xf0, 0x85, // be -0x10
        },
    },
    // Teleroboxer increments an otherwise unused memory value while busywaiting,
    // which messes with busywait detection.
    // Therefore, we patch out the increment.
    {
        .gameid = "01VTBJ",
        .address = 0x2e9dc,
        .size = 2,
        .original = (const uint8_t[]){
            0x61, 0x45, // add 1,r11
        },
        .patched = (const uint8_t[]){
            0x00, 0x00, // mov r0,r0
        },
    },
    // Virtual Boy Wario Land needs a similar patch.
    {
        .gameid = "01VWCJ",
        .address = 0x1c2cda,
        .size = 2,
        .original = (const uint8_t[]){
            0x61, 0x45, // add 1,r11
        },
        .patched = (const uint8_t[]){
            0x00, 0x00, // mov r0,r0
        }
    },
};

void apply_patches() {
    for (int i = 0; i < sizeof(patches) / sizeof(patches[0]); i++) {
        char *gameid = (char*)(V810_ROM1.off + (V810_ROM1.highaddr & 0xFFFFFDF9));
        if (CHECK_GAMEID(patches[i].gameid)) {
            BYTE *patch_ptr = V810_ROM1.pmemory + patches[i].address;
            if (memcmp(patch_ptr, patches[i].original, patches[i].size) == 0) {
                memcpy(patch_ptr, patches[i].patched, patches[i].size);
            }
        }
    }
}