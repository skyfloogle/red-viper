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
void ins_sch0bsu (WORD src, WORD dst, WORD len, WORD offs) {
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
}

void ins_sch0bsd (WORD src, WORD dst, WORD len, WORD offs) {
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
}

void ins_sch1bsu (WORD src, WORD dst, WORD len, WORD offs) {
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
}

void ins_sch1bsd (WORD src, WORD dst, WORD len, WORD offs) {
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
}

#define OPT_XORBSU { \
    if (src == dst) { \
        while (len >= 32) { \
            mem_wword(dst, 0); \
            src += 4; \
            dst += 4; \
            len -= 32; \
        } \
        optimized = true; \
    } \
    \
}

void ins_orbsu   (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf | (s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_andbsu  (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf & (s)
    #define FILTER(s,f) s | ~(f)
    #define OPTIMIZE
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_xorbsu  (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf ^ (s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE OPT_XORBSU
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_movbsu  (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf | (s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    #define CLEARDST
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_ornbsu  (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf | ~(s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_andnbsu (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf & ~(s)
    #define FILTER(s,f) s | ~(f)
    #define OPTIMIZE
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_xornbsu (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf ^ ~(s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
}

void ins_notbsu  (WORD src, WORD dst, WORD len, WORD offs) {
    #define ADD(s) dstbuf | ~(s)
    #define FILTER(s,f) s & f
    #define OPTIMIZE
    #define CLEARDST
    if (len == 0) return;
    WORD srcoff = offs & 31;
    WORD dstoff = offs >> 16;
    WORD dstbuf;
    bool optimized = false;

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
                    mem_wword(dst, ADD(mem_rword(src)));
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
    v810_state->P_REG[28] = len;
    v810_state->P_REG[27] = srcoff;
    v810_state->P_REG[26] = dstoff;
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
    dprintf(0, "[FP]: %s - %f %f\n", __func__, (float)reg1, (float)reg2);
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
