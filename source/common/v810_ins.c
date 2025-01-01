////////////////////////////////////////////////////////////
//Instruction handler routines for
//the V810 processor

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "vb_types.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"  //Remove Me!!!
#include "v810_ins.h"
#include "vb_dsp.h"
#include "vb_set.h"

static int get_read_cycles(WORD addr) {
    if (((addr >> 24) & 7) == 0) {
        // VIP
        if (((addr >> 12) & 0x07e) == 0x05e) {
            // registers
            return 2*2;
        } else {
            return 5*2;
        }
    } else if (((addr >> 24) & 7) == 7) {
        // ROM
        return (2 - (tHReg.WCR & 1)) * 2;
    } else {
        return 1;
    }
}

static int get_readwrite_cycles(WORD addr) {
    if (((addr >> 24) & 7) == 0) {
        // VIP
        return 2*2 + get_read_cycles(addr);
    } else {
        // corresponding read will always be 1, assuming we aren't trying to write to 1
        return 1*2 + 1*2;
    }
}

//The Instructions
void ins_err(int arg1, int arg2) { //Mode1/2
    //dtprintf(6,ferr,"\nInvalid code! err");
}

//Bitstring routines, wrapper functions for bitstring instructions!
void get_bitstr(WORD *str, WORD src, WORD srcoff, WORD len) {
    WORD i=0,tword,tmp;

    memset(str,0,(((len>>5)+1)<<2)); //clear bitstring data

    tmp = ((i+srcoff)>>5);
    tword = mem_rword(src+(tmp<<2));
    while (i < len) {
        if (((i+srcoff)>>5) != tmp) {
            tmp = ((i+srcoff)>>5);
            tword = mem_rword(src+(tmp<<2));
        }
        str[i>>5] |= (((tword >> ((srcoff+i)&0x1F)) & 1) << (i&0x1F));
        i++;
    }
}

void set_bitstr(WORD *str, WORD dst, WORD dstoff, WORD len) {
    WORD i=0,tword,tmp;

    tmp = ((i+dstoff)>>5);
    tword = mem_rword(dst+(tmp<<2));
    while (i < len) {
        if (((i+dstoff)>>5) != tmp) {
            tmp = ((i+dstoff)>>5);
            tword = mem_rword(dst+(tmp<<2));
        }
        tword &= (~(1<<((dstoff+i)&0x1F)));
        tword |= (((str[i>>5]>>(i&0x1F))&1)<<((dstoff+i)&0x1F));
        i++;
        if (!((i+dstoff)&0x1F)) mem_wword(dst+(tmp<<2),tword);
    }
    mem_wword(dst+(tmp<<2),tword);
}

//Bitstring SubOpcodes
bool ins_sch0bsu (WORD src, WORD skipped, WORD len, WORD offs) {
    #define FLIP(x) ~(x)
    bool searching = true;
    if (offs != 0 && len > 32 - offs) {
        WORD data = mem_rword(src) & ~((1 << offs) - 1);
        data = FLIP(data) & ~((1 << offs) - 1);
        if (data) {
            // we found a zero bit
            int trailing = __builtin_ctz(data);
            len -= trailing - offs;
            skipped += trailing - offs;
            offs = trailing - 1;
            searching = false;
        } else {
            // not found, continue
            src += 4;
            skipped += 32 - offs;
            len -= 32 - offs;
            offs = 0;
        }
    }
    if (searching) while (len >= 32) {
        WORD data = FLIP(mem_rword(src));
        if (data) {
            // we found a zero bit
            int trailing = __builtin_ctz(data);
            len -= trailing;
            skipped += trailing;
            if (trailing == 0) {
                offs = 31;
                src -= 4;
            } else {
                offs = trailing - 1;
            }
            searching = false;
            break;
        } else {
            // not found, continue
            src += 4;
            skipped += 32;
            len -= 32;
        }
    }
    if (searching && len > 0) {
        WORD data = mem_rword(src) & (((1 << len) - 1) << offs);
        data = FLIP(data) & (((1 << len) - 1) << offs);
        if (data) {
            // we found a zero bit
            int trailing = __builtin_ctz(data);
            len -= trailing;
            skipped += trailing;
            if (trailing == 0) {
                offs = 31;
            } else {
                offs = trailing - 1;
            }
            searching = false;
        } else {
            // not found
            skipped += len;
            offs += len;
            if (offs == 32) {
                offs = 0;
                src += 4;
            }
            len = 0;
        }
    }
    #undef FLIP
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = skipped;
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = offs;
    return !searching;
}

bool ins_sch0bsd (WORD src, WORD skipped, WORD len, WORD offs) {
    #define FLIP(x) ~(x)
    bool searching = true;
    if (offs != 31 && len > offs) {
        WORD data = mem_rword(src) & ((1 << (offs + 1)) - 1);
        data = FLIP(data) & ((1 << (offs + 1)) - 1);
        if (data) {
            // we found a zero bit
            int trailing = __builtin_clz(data) + 1;
            len -= trailing - (33 - (offs + 1));
            skipped += trailing - (33 - (offs + 1));
            if (trailing == 0) {
                offs = 31;
            } else {
                offs = 33 - trailing;
            }
            searching = false;
        } else {
            // not found, continue
            src += -4;
            skipped += offs;
            len -= offs;
            offs = 31;
        }
    }
    if (searching) while (len >= 32) {
        WORD data = FLIP(mem_rword(src));
        if (data) {
            // we found a zero bit
            int trailing = __builtin_clz(data) + 1;
            len -= trailing;
            skipped += trailing;
            if (trailing == 1) {
                offs = 0;
                src -= -4;
            } else {
                if (trailing == 32) {
                    len += 32;
                    skipped -= 32;
                }
                offs = 33 - trailing;
            }
            searching = false;
            break;
        } else {
            // not found, continue
            src += -4;
            skipped += 32;
            len -= 32;
        }
    }
    if (searching && len > 0) {
        WORD data = mem_rword(src) & (((1 << len) - 1) << (32 - offs));
        data = FLIP(data) & (((1 << len) - 1) << (32 - offs));
        if (data) {
            // we found a zero bit
            int trailing = __builtin_clz(data) + 1;
            len -= trailing;
            skipped += trailing;
            if (trailing == 1) {
                offs = 0;
                src -= -4;
            } else {
                if (trailing == 32) {
                    len += 32;
                    skipped -= 32;
                }
                offs = 33 - trailing;
            }
            searching = false;
        } else {
            // not found
            skipped += len;
            offs -= len - 1;
            if (offs == 0) {
                offs = 31;
                src += -4;
            }
            len = 0;
        }
    }
    #undef FLIP
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = skipped;
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = offs;
    return !searching;
}

bool ins_sch1bsu (WORD src, WORD skipped, WORD len, WORD offs) {
    #define FLIP(x) (x)
    bool searching = true;
    if (offs != 0 && len > 32 - offs) {
        WORD data = mem_rword(src) & ~((1 << offs) - 1);
        data = FLIP(data) & ~((1 << offs) - 1);
        if (data) {
            // we found a zero bit
            int trailing = __builtin_ctz(data);
            len -= trailing - offs;
            skipped += trailing - offs;
            offs = trailing - 1;
            searching = false;
        } else {
            // not found, continue
            src += 4;
            skipped += 32 - offs;
            len -= 32 - offs;
            offs = 0;
        }
    }
    if (searching) while (len >= 32) {
        WORD data = FLIP(mem_rword(src));
        if (data) {
            // we found a zero bit
            int trailing = __builtin_ctz(data);
            len -= trailing;
            skipped += trailing;
            if (trailing == 0) {
                offs = 31;
                src -= 4;
            } else {
                offs = trailing - 1;
            }
            searching = false;
            break;
        } else {
            // not found, continue
            src += 4;
            skipped += 32;
            len -= 32;
        }
    }
    if (searching && len > 0) {
        WORD data = mem_rword(src) & (((1 << len) - 1) << offs);
        data = FLIP(data) & (((1 << len) - 1) << offs);
        if (data) {
            // we found a zero bit
            int trailing = __builtin_ctz(data);
            len -= trailing;
            skipped += trailing;
            if (trailing == 0) {
                offs = 31;
            } else {
                offs = trailing - 1;
            }
            searching = false;
        } else {
            // not found
            skipped += len;
            offs += len;
            if (offs == 32) {
                offs = 0;
                src += 4;
            }
            len = 0;
        }
    }
    #undef FLIP
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = skipped;
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = offs;
    return !searching;
}

bool ins_sch1bsd (WORD src, WORD skipped, WORD len, WORD offs) {
    #define FLIP(x) (x)
    bool searching = true;
    if (offs != 31 && len > offs) {
        WORD data = mem_rword(src) & ((1 << (offs + 1)) - 1);
        data = FLIP(data) & ((1 << (offs + 1)) - 1);
        if (data) {
            // we found a zero bit
            int trailing = __builtin_clz(data) + 1;
            len -= trailing - (33 - (offs + 1));
            skipped += trailing - (33 - (offs + 1));
            if (trailing == 0) {
                offs = 31;
            } else {
                offs = 33 - trailing;
            }
            searching = false;
        } else {
            // not found, continue
            src += -4;
            skipped += offs;
            len -= offs;
            offs = 31;
        }
    }
    if (searching) while (len >= 32) {
        WORD data = FLIP(mem_rword(src));
        if (data) {
            // we found a zero bit
            int trailing = __builtin_clz(data) + 1;
            len -= trailing;
            skipped += trailing;
            if (trailing == 1) {
                offs = 0;
                src -= -4;
            } else {
                if (trailing == 32) {
                    len += 32;
                    skipped -= 32;
                }
                offs = 33 - trailing;
            }
            searching = false;
            break;
        } else {
            // not found, continue
            src += -4;
            skipped += 32;
            len -= 32;
        }
    }
    if (searching && len > 0) {
        WORD data = mem_rword(src) & (((1 << len) - 1) << (32 - offs));
        data = FLIP(data) & (((1 << len) - 1) << (32 - offs));
        if (data) {
            // we found a zero bit
            int trailing = __builtin_clz(data) + 1;
            len -= trailing;
            skipped += trailing;
            if (trailing == 1) {
                offs = 0;
                src -= -4;
            } else {
                if (trailing == 32) {
                    len += 32;
                    skipped -= 32;
                }
                offs = 33 - trailing;
            }
            searching = false;
        } else {
            // not found
            skipped += len;
            offs -= len - 1;
            if (offs == 0) {
                offs = 31;
                src += -4;
            }
            len = 0;
        }
    }
    #undef FLIP
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = skipped;
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = offs;
    return !searching;
}

#define OPT_XORBSU { \
    if (src == dst) { \
        /* niko-chan battle speedhack */ \
        if (dst == 0x78800 && len == 0x3c000) { \
            memset(V810_DISPLAY_RAM.pmemory + 0x06800, 0, 0x1800); \
            memset(V810_DISPLAY_RAM.pmemory + 0x0e000, 0, 0x2000); \
            memset(V810_DISPLAY_RAM.pmemory + 0x16000, 0, 0x2000); \
            memset(V810_DISPLAY_RAM.pmemory + 0x1e000, 0, 0x2000); \
            memset(tDSPCACHE.CharacterCache + 0x80, 1, 0x780); \
            src += 0x7800; \
            dst += 0x7800; \
            len = 0; \
        } else while (len >= 32) { \
            mem_wword(dst, 0); \
            src += 4; \
            dst += 4; \
            len -= 32; \
        } \
        optimized = true; \
    } \
    \
}

int ins_orbsu   (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf | (s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;

    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    // Golf hack
    bool is_golf = memcmp(tVBOpt.GAME_ID, "01VVGE", 6) == 0 || memcmp(tVBOpt.GAME_ID, "E4VVGJ", 6) == 0;
    if (is_golf && !(v810_state->P_REG[31] >= 0x07006e80 && v810_state->P_REG[31] <= 0x070071d0)) {
        len += len_remain;
        len_remain = 0;
        cycles = 0;
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_andbsu  (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf & (s)
    #define FILTER(s,f) s | ~(f)
    #define OPTIMIZE
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;

    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    // Golf hack
    bool is_golf = memcmp(tVBOpt.GAME_ID, "01VVGE", 6) == 0 || memcmp(tVBOpt.GAME_ID, "E4VVGJ", 6) == 0;
    if (is_golf && !(v810_state->P_REG[31] >= 0x07006e80 && v810_state->P_REG[31] <= 0x070071d0)) {
        len += len_remain;
        len_remain = 0;
        cycles = 0;
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_xorbsu  (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf ^ (s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE //OPT_XORBSU
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;
    
    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_movbsu  (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf | (s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    #define CLEARDST
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;
    
    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    // Golf hack
    bool is_golf = memcmp(tVBOpt.GAME_ID, "01VVGE", 6) == 0 || memcmp(tVBOpt.GAME_ID, "E4VVGJ", 6) == 0;
    if (is_golf && !(v810_state->P_REG[31] >= 0x07006e80 && v810_state->P_REG[31] <= 0x070071d0)) {
        len += len_remain;
        len_remain = 0;
        cycles = 0;
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_ornbsu  (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf | (s)
    #define FILTER(s,f) ~(s) & f
    #define OPTIMIZE
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;
    
    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_andnbsu (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf & (s)
    #define FILTER(s,f) ~(s) | ~(f)
    #define OPTIMIZE
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;
    
    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_xornbsu (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf ^ (s)
    #define FILTER(s,f) ~(s) & f
    #define OPTIMIZE
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;
    
    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

int ins_notbsu  (WORD src, WORD dst, WORD len, SWORD offs) {
    #define ADD(s) dstbuf | (s)
    #define FILTER(s,f) ~(s) & f
    #define OPTIMIZE
    #define CLEARDST
    WORD srcoff = offs & 31;
    WORD dstoff = (offs >> 5) & 31;
    WORD dstbuf;
    bool optimized = false;
    
    if (len == 0) { // type 6
        v810_state->P_REG[30] = src;
        v810_state->P_REG[29] = dst;
        v810_state->P_REG[28] = len;
        v810_state->P_REG[27] = srcoff;
        v810_state->P_REG[26] = dstoff;
        return 20;
    }

    int cycle_cap = -(offs >> 10);
    int cycles;
    int len_remain = 0;
    int one_read = get_read_cycles(src);
    int one_readwrite = get_readwrite_cycles(dst);
    if (src == dst && srcoff > dstoff && srcoff + len < 32) {
        // type 7
        cycles = 43 + one_read + one_readwrite;
    } else {
        int one, two, slope, yint;
        if (srcoff == dstoff) {
            if (((srcoff + len) & 31) == 0) {
                // type 1
                one = 38;
                two = 53;
                slope = 12;
                yint = 30;
            } else {
                // type 2
                one = 38;
                two = 54;
                slope = 12;
                yint = 31;
            }
        } else {
            if (((srcoff + len) & ~31) == ((dstoff + len) & ~31)) {
                if (dstoff == 0) {
                    // type 3
                    one = 43;
                    two = 60;
                    slope = 12;
                    yint = 35;
                } else {
                    // type 5
                    one = 38;
                    two = 55;
                    slope = 43;
                    yint = 55;
                }
            } else {
                // type 4
                one = 49;
                two = 61;
                slope = 6;
                yint = 36;
            }
        }
        int words = (srcoff + len + 31) >> 5;
        if (words == 1) cycles = one + one_read + one_readwrite;
        else if (words == 2) cycles = two + 2 * (one_read + one_readwrite);
        else {
            slope += one_read + one_readwrite;
            cycles = slope * words + yint;
            if (cycles > cycle_cap) {
                // we'll need stop partway for an interrupt check
                words = 1 + (cycle_cap - yint) / slope;
                if (words < 3) words = 3;
                cycles = slope * words + yint;
                len_remain = len - (32 - srcoff) - 32 * (words - 1);
                if (len_remain < 0) len_remain = 0;
                len -= len_remain;
            }
        }
    }

    if (srcoff == dstoff) {
        if (srcoff != 0 && len > 32-srcoff) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ((1 << srcoff) - 1);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), ~((1 << srcoff) - 1)));
            mem_wword(dst, dstbuf);
            src += 4;
            dst += 4;
            len -= 32 - srcoff;
            srcoff = dstoff = 0;
        }

        if (srcoff == 0) {
            OPTIMIZE;
            if (!optimized) {
                dstbuf = 0;
                while (len >= 32) {
                    #ifndef CLEARDST
                    dstbuf = mem_rword(dst);
                    #endif
                    mem_wword(dst, ADD(FILTER(mem_rword(src), -1)));
                    src += 4;
                    dst += 4;
                    len -= 32;
                }
            }
        }

        // if srcoff != 0, then len <= 32-srcoff
        // if srcoff == 0, then len < 32
        if (len > 0) {
            dstbuf = mem_rword(dst);
            #ifdef CLEARDST
            dstbuf &= ~(((1 << len) - 1) << srcoff);
            #endif
            dstbuf = ADD(FILTER(mem_rword(src), (((1 << len) - 1) << srcoff)));
            mem_wword(dst, dstbuf);
            srcoff = dstoff += len;
            if (srcoff == 32) {
                srcoff = dstoff = 0;
                src += 4;
                dst += 4;
            }
            len = 0;
        }
    } else {
        WORD srcbuf = mem_rword(src);
        dstbuf = mem_rword(dst);
        while (len > 0) {
            // we align ourself to the destination because that makes things simpler
            if (srcoff > dstoff) {
                int bits_to_transfer = 32 - srcoff;
                if (bits_to_transfer > len) bits_to_transfer = len;
                WORD tmp = srcbuf;
                tmp >>= srcoff - dstoff;
                tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
                #ifdef CLEARDST
                dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
                #endif
                dstbuf = ADD(tmp);
                srcoff += bits_to_transfer;
                if (srcoff >= 32) {
                    src += 4;
                    srcoff &= 31;
                }
                // dstoff + bits_to_transfer < 32 guaranteed
                dstoff += bits_to_transfer;
                len -= bits_to_transfer;
                if (len <= 0) {
                    mem_wword(dst, dstbuf);
                    break;
                }
                else srcbuf = mem_rword(src);
            }
            // if we got here, then dstoff <= srcoff
            int bits_to_transfer = 32 - dstoff;
            if (bits_to_transfer > len) bits_to_transfer = len;
            WORD tmp = srcbuf;
            tmp <<= dstoff - srcoff;
            tmp = FILTER(tmp, ((1 << bits_to_transfer) - 1) << dstoff);
            // only necessary because we overwrite
            #ifdef CLEARDST
            dstbuf &= ~(((1 << bits_to_transfer) - 1) << dstoff);
            #endif
            dstbuf = ADD(tmp);
            mem_wword(dst, dstbuf);
            srcoff += bits_to_transfer;
            if (srcoff >= 32) {
                src += 4;
                srcoff &= 31;
            }
            dstoff += bits_to_transfer;
            if (dstoff >= 32) {
                dst += 4;
                dstoff &= 31;
            }
            len -= bits_to_transfer;
            if (len <= 0) break;
            else {
                dstbuf = mem_rword(dst);
                if (srcoff == 0) srcbuf = mem_rword(src);
            }
        }
    }
    #undef ADD
    #undef CLEARDST
    #undef FILTER
    #undef OPTIMIZE
    v810_state->P_REG[30] = src;
    v810_state->P_REG[29] = dst;
    v810_state->P_REG[28] = len_remain;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;

    return cycles;
}

//FPU SubOpcodes  
float ins_cmpf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    float fTemp = reg2 - reg1;
    if (fTemp == 0.0F) flags = flags | PSW_Z;
    if (fTemp < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=7;
    return reg2;
}

float ins_cvt_ws(int reg1, float reg2) {   //Int to Float
    int flags = 0; // Set Flags, OV set to Zero
    reg2 = (float)reg1;
    if (reg2 == 0) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=5; //5 to 16
    return reg2;
}

int ins_cvt_sw(float reg1, int reg2) {  //Float To Int
    int flags = 0; // Set Flags, CY unchanged, OV set to Zero
    reg2 = lroundf(reg1);
    if (reg2 == 0) flags = flags | PSW_Z;
    if (reg2 & 0x80000000)  flags = flags | PSW_S;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF8)|flags;
    //	clocks+=9; //9 to 14
    return reg2;
}

float ins_addf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 += reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=9; //9 to 28
    return reg2;
}

float ins_subf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 -= reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=12; //12 to 28
    return reg2;
}

float ins_mulf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 *= reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=8; //8 to 30
    return reg2;
}

float ins_divf_s(float reg1, float reg2) {
    int flags = 0; // Set Flags, OV set to Zero
    reg2 /= reg1;
    if (reg2 == 0.0F) flags = flags | PSW_Z;
    if (reg2 < 0.0F)  flags = flags | PSW_S | PSW_CY; //changed according to NEC docs
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF0)|flags;
    //	clocks+=44; //always 44
    return reg2;
}

int ins_trnc_sw(float reg1, int reg2) {
    int flags = 0; // Set Flags, CY unchanged, OV set to Zero
    reg2 = (int)trunc(reg1);
    if (reg2 == 0) flags = flags | PSW_Z;
    if (reg2 & 0x80000000)  flags = flags | PSW_S;
    v810_state->S_REG[PSW] = (v810_state->S_REG[PSW] & 0xFFFFFFF8)|flags;
    //	clocks+=8; //8 to 14
    return reg2;
}

int ins_xb(int arg1, int arg2) {
    return ((arg2&0xFFFF0000) | (((arg2<<8)&0xFF00) | ((arg2>>8)&0xFF)));
    //	clocks+=1; //just a guess
}

int ins_xh(int arg1, unsigned arg2) {
    return (arg2<<16)|(arg2>>16);
    //	clocks+=1; //just a guess
}

int ins_rev(int arg1, int arg2) {
    WORD temp = 0;
    int i;
    for (i = 0; i < 32; i++) temp = ((temp << 1) | ((arg1 >> i) & 1));
    return temp;
}

int ins_mpyhw(short arg1, short arg2) {
    return (int)arg1 * (int)arg2; //signed multiplication
    //	clocks+=9; //always 9
}
