#include <stdio.h>
#include <unistd.h>
#include "stdlib.h"
#include "time.h"
#include "vb_set.h"
#include "v810_cpu.h"
#include "replay.h"
#include "v810_mem.h"
#include "vb_dsp.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

// dummy
#include "vb_dsp.h"
#include "vb_sound.h"
VB_DSPCACHE tDSPCACHE;
void sound_update(uint32_t cycles) {}
void sound_write(int addr, uint16_t val) {}
int drc_handleInterrupts(WORD cpsr, WORD* PC) { return 0; }
void drc_relocTable(void) {}

SDL_Window *window;
SDL_Surface *game_surface, *window_surface;

void sdl_flush(bool displayed_fb) {
    SDL_LockSurface(game_surface);
    uint16_t *vb_fb = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x8000 * displayed_fb);
    uint32_t *out_fb = (uint32_t*)game_surface->pixels;
    uint32_t brightnesses[4] = {0, tVIPREG.BRTA, tVIPREG.BRTB, tVIPREG.BRTA + tVIPREG.BRTB + tVIPREG.BRTC};
    for (int x = 0; x < 384; x++) {
        for (int y = 0; y < 224; y += 8) {
            uint64_t vb_word = vb_fb[x * 32 + (y / 8)];
            for (int i = 0; i < 8; i++) {
                uint32_t brt = brightnesses[vb_word & 3] * 2;
                if (brt > 255) brt = 255;
                out_fb[(y + i) * 384 + x] = brt;
                vb_word = vb_word >> 2;
            }
        }
    }
    SDL_UnlockSurface(game_surface);
    SDL_BlitScaled(game_surface, NULL, window_surface, NULL);
    SDL_UpdateWindowSurface(window);
}

int main(int argc, char* argv[]) {
    int err;

    if (argc != 2) {
        puts("Pass a ROM please");
        return 1;
    }

    if (access(argv[1], F_OK) != 0) {
        printf("Error: ROM %s doesn't exist\n", argv[1]);
        return 1;
    }

    setDefaults();
    v810_init();
    replay_init();

    tVBOpt.RENDERMODE = 2;

    strncpy(tVBOpt.ROM_PATH, argv[1], sizeof(tVBOpt.ROM_PATH));

    v810_load_init();
    while (true) {
        int ret = v810_load_step();
        if (ret < 0) return ret;
        if (ret == 100) break;
    }

    clearCache();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    window = SDL_CreateWindow("Red Viper", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 384*2, 224*2, 0);
    window_surface = SDL_GetWindowSurface(window);
    game_surface = SDL_CreateRGBSurfaceWithFormat(0, 384, 224, 32, SDL_PIXELFORMAT_XBGR8888);

    int lasttime = SDL_GetTicks();

    while (true) {
        if(tVIPREG.tFrame == tVIPREG.FRMCYC && !tVIPREG.drawing) {
            if (tVIPREG.XPCTRL & XPEN) {
                tVIPREG.tDisplayedFB = !tVIPREG.tDisplayedFB;

                if (tDSPCACHE.CharCacheInvalid) {
                    tDSPCACHE.CharCacheInvalid = false;
                    update_texture_cache_soft();
                }

                video_soft_render(!tVIPREG.tDisplayedFB);

                // we need to have this cache during rendering
                memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
            }

            sdl_flush(tVIPREG.tDisplayedFB);
        }

        err = v810_run();
        if (err) {
            printf("Error code %d\n", err);
            return 1;
        }
        
        if (!tVBOpt.FASTFORWARD) {
            int remaining = 20 - (SDL_GetTicks() - lasttime);
            if (remaining > 0) {
                SDL_Delay(remaining);
            }
            lasttime = SDL_GetTicks();
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                return 0;
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int flag = 0;
                switch (e.key.keysym.scancode) {
                    case SDL_SCANCODE_LEFT: flag = VB_LPAD_L; break;
                    case SDL_SCANCODE_RIGHT: flag = VB_LPAD_R; break;
                    case SDL_SCANCODE_UP: flag = VB_LPAD_U; break;
                    case SDL_SCANCODE_DOWN: flag = VB_LPAD_D; break;
                    case SDL_SCANCODE_I: flag = VB_RPAD_U; break;
                    case SDL_SCANCODE_J: flag = VB_RPAD_L; break;
                    case SDL_SCANCODE_K: flag = VB_RPAD_D; break;
                    case SDL_SCANCODE_L: flag = VB_RPAD_R; break;
                    case SDL_SCANCODE_RETURN: flag = VB_KEY_START; break;
                    case SDL_SCANCODE_RSHIFT: flag = VB_KEY_SELECT; break;
                    case SDL_SCANCODE_Z: flag = VB_KEY_A; break;
                    case SDL_SCANCODE_X: flag = VB_KEY_B; break;
                    case SDL_SCANCODE_A: flag = VB_KEY_L; break;
                    case SDL_SCANCODE_S: flag = VB_KEY_R; break;
                    case SDL_SCANCODE_TAB: tVBOpt.FASTFORWARD = e.type == SDL_KEYDOWN; break;
                    case SDL_SCANCODE_ESCAPE: return 0;
                    default: flag = 0; break;
                }
                if (e.type == SDL_KEYDOWN) {
                    tHReg.SLB |= flag & 0xff;
                    tHReg.SHB |= flag >> 8;
                } else {
                    tHReg.SLB &= ~(flag & 0xff);
                    tHReg.SHB &= ~(flag >> 8);
                }
            }
        }
    }

    return 0;
}