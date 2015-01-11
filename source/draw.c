#include <3DS.h>
#include <string.h>
#include <math.h>
#include "draw.h"
#include "font.h"

void drawBox(int screen, int side,int x, int y, int w, int h, u32 color) {
    u8* p = gfxGetFramebuffer(screen, side, NULL, NULL);
	if (!p) return;
//    p += 3*(240-y+x*240);
//    int d = 720-h*3;
int i,j;
    for (i = 0; i < w; i++) {
        for (j = 0; j < h; j++) {
//            *(p++) = color&0xFF;
//            *(p++) = (color>>8)&0xFF;
//            *(p++) = (color>>16)&0xFF;
			p[3*(240-y-j+(x+i)*240)+2] = (color>>16)&0xFF; //red
			p[3*(240-y-j+(x+i)*240)+1] = (color>>8)&0xFF; //green
			p[3*(240-y-j+(x+i)*240)] = color&0xFF; //blue
        }
//        p += d;
    }
}

void paint_word(u8* dest,char* word, int x, int y, char r, char g, char b) {
    int tmp_x = x;
    int i;
    int line = 0;

    for (i = 0; i < strlen(word); i++) {

        if (tmp_x + 8 > 320) {
            line++;
            tmp_x = x;
        }
        paintLetter(dest,word[i], tmp_x, y + (line * 8), r, g, b);
        tmp_x = tmp_x + 8;
    }
}

void paintLetter(u8* dest,char letter, int x, int y, char r, char g, char b) {

    int i = 0;
    int k = 0;
    int c = 0;
    char mask = 0;
    //unsigned char* _letter;
    char l = 0;
    c = letter ;//* 8;

    for (i = 0; i < 8; i++) {
        mask = 0b10000000;
//        l = font[i + c];
        l = font[c][i];
        for (k = 0; k < 8; k++) {
            if ((mask >> k) & l) {
                paintPixel(dest,k + x, i + y, r, g, b);
            }
        }
    }
}

void paintPixel(u8* dest,int x, int y, char r, char g, char b) {
        dest[3*(240-y+x*240)+2] = r; //red
	dest[3*(240-y+x*240)+1] = g; //green
	dest[3*(240-y+x*240)] = b; //blue
}

// itoa: convert n to characters in s 
 void itoa(int n, char s[])
 {
     int i, sign;
 
     if ((sign = n) < 0) // record sign 
         n = -n; // make n positive 
     i = 0;
     do { // generate digits in reverse order 
         s[i++] = n % 10 + '0'; // get next digit 
     } while ((n /= 10) > 0); // delete it 
     if (sign < 0)
         s[i++] = '-';
     s[i] = '\0';
     reverse(s);
 }

 // reverse: reverse string s in place 
 void reverse(char s[])
 {
     int i, j;
     char c;
 
     for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
         c = s[i];
         s[i] = s[j];
         s[j] = c;
     }
 }

// Converts a floating point number to string.
void ftoa(float n, char *res, int afterpoint)
{
    // Extract integer part
    int ipart = (int)n;
 
    // Extract floating part
    float fpart = n - (float)ipart;
 
    // convert integer part to string
    itoa(ipart,res);
    int i = strlen(res);
    // check for display option after point
    if (afterpoint != 0)
    {
        res[i] = '.';  // add dot
 
        // Get the value of fraction part upto given no.
        // of points after dot. The third parameter is needed
        // to handle cases like 233.007
        fpart = fpart * pow(10, afterpoint);
 
        itoa((int)fpart,res + i + 1);
    }
} 
