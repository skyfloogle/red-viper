#include <stdio.h>
#include <unistd.h>
#include "stdlib.h"
#include "time.h"
#include "vb_set.h"
#include "v810_cpu.h"
#include "replay.h"
#include "v810_mem.h"
#include "vb_dsp.h"
#include "drc_core.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

// dummy
#include "vb_dsp.h"
#include "vb_sound.h"
VB_DSPCACHE tDSPCACHE;
void video_download_vip(int drawn_fb) {}

#if DRC_AVAILABLE
#else
int drc_handleInterrupts(WORD cpsr, WORD* PC) { return 0; }
void drc_relocTable(void) {}
#endif

SDL_Window *window;
SDL_Surface *game_surface, *window_surface;

void sdl_flush(bool displayed_fb, int player) {
    SDL_LockSurface(game_surface);
    uint16_t *vb_fb = (uint16_t*)(vb_players[player].V810_DISPLAY_RAM.off + 0x8000 * displayed_fb);
    uint32_t *out_fb = (uint32_t*)game_surface->pixels;
    uint32_t brightnesses[4] = {
        0,
        vb_players[player].tVIPREG.BRTA,
        vb_players[player].tVIPREG.BRTB,
        vb_players[player].tVIPREG.BRTA + vb_players[player].tVIPREG.BRTB + vb_players[player].tVIPREG.BRTC};
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
    SDL_Rect rect = {.x = 0, .y = 224*2 * player, .w = 384*2, .h = 224*2};
    SDL_BlitScaled(game_surface, NULL, window_surface, &rect);
}

void video_init();
void video_hard_render(int drawn_fb);

int main(int argc, char* argv[]) {
    int err;

    if (argc < 2) {
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

    #if DRC_AVAILABLE
    drc_init();
    #endif

    strncpy(tVBOpt.ROM_PATH, argv[1], sizeof(tVBOpt.ROM_PATH)-1);
    tVBOpt.ROM_PATH[sizeof(tVBOpt.ROM_PATH)-1] = 0;

    // -m for multiplayer
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            is_multiplayer = true;
        }
    }

    v810_load_init();
    while (true) {
        int ret = v810_load_step();
        if (ret < 0) return ret;
        if (ret == 100) break;
    }

    tVBOpt.RENDERMODE = RM_TOGPU;

    clearCache();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    window = SDL_CreateWindow("Red Viper", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 384*2, 224*2*(1+is_multiplayer), SDL_WINDOW_OPENGL);
    // window_surface = SDL_GetWindowSurface(window);
    // game_surface = SDL_CreateRGBSurfaceWithFormat(0, 384, 224, 32, SDL_PIXELFORMAT_XBGR8888);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    int lasttime = SDL_GetTicks();

    sound_init();

    video_init();

    while (true) {
        int displayed_fb = vb_state->tVIPREG.tDisplayedFB;
        // for (int i = 0; i < 2; i++) {
        //     vb_state = &vb_players[i];
        //     clearCache();
        //     if(vb_state->tVIPREG.tFrame == 0 && !vb_state->tVIPREG.drawing) {
        //         if (vb_state->tVIPREG.XPCTRL & XPEN) {
        //             if (tDSPCACHE.CharCacheInvalid) {
        //                 update_texture_cache_soft();
        //             }

        //             video_soft_render(!vb_state->tVIPREG.tDisplayedFB);

        //             // we need to have these caches during rendering
        //             tDSPCACHE.CharCacheInvalid = false;
        //             memset(tDSPCACHE.BGCacheInvalid, 0, sizeof(tDSPCACHE.BGCacheInvalid));
        //             memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
        //         }

        //         sdl_flush(vb_state->tVIPREG.tDisplayedFB, i);
        //     }
        // }
        // SDL_UpdateWindowSurface(window);
        video_render(displayed_fb, false);
        SDL_GL_SwapWindow(window);

        // if (tVBOpt.RENDERMODE != RM_CPUONLY) {
            if (vb_state->tVIPREG.XPCTRL & 0x0002) {
                if (tDSPCACHE.DDSPDataState[!displayed_fb] != GPU_CLEAR) {
                    memset((uint8_t*)vb_state->V810_DISPLAY_RAM.off + 0x8000 * !displayed_fb, 0, 0x6000);
                    memset((uint8_t*)vb_state->V810_DISPLAY_RAM.off + 0x10000 + 0x8000 * !displayed_fb, 0, 0x6000);
                    for (int i = 0; i < 64; i++) {
                        tDSPCACHE.SoftBufWrote[!displayed_fb][i].min = 0xff;
                        tDSPCACHE.SoftBufWrote[!displayed_fb][i].max = 0;
                    }
                    memset(tDSPCACHE.OpaquePixels.u32[!displayed_fb], 0, sizeof(tDSPCACHE.OpaquePixels.u32[!displayed_fb]));
                    tDSPCACHE.DDSPDataState[!displayed_fb] = GPU_CLEAR;
                }
            }
        // }

        vb_state = &vb_players[0];

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
                int mouse_y;
                SDL_GetMouseState(NULL, &mouse_y);
                int player_id = is_multiplayer ? mouse_y >= 224*2 : 0;
                if (e.type == SDL_KEYDOWN) {
                    vb_players[player_id].tHReg.SLB |= flag & 0xff;
                    vb_players[player_id].tHReg.SHB |= flag >> 8;
                } else {
                    vb_players[player_id].tHReg.SLB &= ~(flag & 0xff);
                    vb_players[player_id].tHReg.SHB &= ~(flag >> 8);
                }
            }
        }
    }

    return 0;
}
