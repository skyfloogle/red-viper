#ifndef DRAW_H
#define DRAW_H

void drawBox(int screen, int side,int x, int y, int w, int h, u32 color);
void itoa(int n, char s[]);
void reverse(char s[]);
void ftoa(float n, char *res, int afterpoint);
void paint_word(u8* dest,char* word, int x, int y, char r, char g, char b);
void paintLetter(u8* dest,char letter, int x, int y, char r, char g, char b);
void paintPixel(u8* dest,int x, int y, char r, char g, char b);

#endif
