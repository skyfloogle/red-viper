#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include "text.h"
#include "font_bin.h"

#define CHAR_SIZE_X (8)
#define CHAR_SIZE_Y (8)

void drawCharacter(uint8_t* fb, char c, uint16_t x, uint16_t y) {
    if (c < ' ' || c > '~')
        return;

    c -= ' ';
    uint8_t* charData = (uint8_t*) &font_bin[CHAR_SIZE_X * CHAR_SIZE_Y * c];
    fb += (x * 240 + y) * 3;

    int i, j;
    for (i = 0; i < CHAR_SIZE_X; i++) {
        for (j = 0; j < CHAR_SIZE_Y; j++) {
            uint8_t v = *(charData++);
            if (v)
                fb[0] = fb[1] = fb[2] = (v == 1) ? 0xFF : 0x00;
            fb += 3;
        }
        fb += (240 - CHAR_SIZE_Y) * 3;
    }
}

void drawString(uint8_t* fb, char* str, uint16_t x, uint16_t y) {
    if (!str)
        return;

    y = 232 - y;
    int k;
    int dx = 0, dy = 0;
    for (k = 0; k < strlen(str); k++) {
        if (str[k] >= ' ' && str[k] < 0x80)
            drawCharacter(fb, str[k], x + dx, y + dy);
        dx += CHAR_SIZE_X;
        if (str[k] == '\n') {
            dx = 0;
            dy -= CHAR_SIZE_Y;
        }
    }
}
