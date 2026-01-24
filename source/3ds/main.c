#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <malloc.h>
#include <citro3d.h>

#include "main.h"
#include "3ds/romfs.h"
#include "cpp.h"
#include "periodic.h"
#include "v810_mem.h"
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_gui.h"
#include "replay.h"
#include "utils.h"
#include "vblink.h"
#include "multiplayer.h"

char rom_path[256] = "sdmc:/vb/";
char rom_name[128];

bool game_running = false;

#define INPUT_BUFFER_SIZE (INPUT_BUFFER_MAX + 2)
static HWORD input_buffer[2][INPUT_BUFFER_SIZE];
static int input_buffer_head[2] = {0, 0};
static int input_buffer_tail = 0;

Handle frame_event;
volatile int lag_frames;
void frame_pacer_thread(void) {
    lag_frames++;
    svcSignalEvent(frame_event);
}

int main(void) {
    int qwe;
    int frame = 0;
    int err = 0;
    bool on_time = false;
    bool just_lagged = false;
    PrintConsole main_console;
#if DEBUGLEVEL == 0
    PrintConsole debug_console;
#endif
    Handle nothingEvent = 0;
    svcCreateEvent(&nothingEvent, 0);

    // gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false); // legacy renderer
    gfxInitDefault(); // hardware renderer
    fsInit();
    archiveMountSdmc();
    romfsInit();
    ptmuInit();

    APT_SetAppCpuTimeLimit(30);

    // CPP defaults to on, so initialize already.
    cppInit();

    setDefaults();
    if (loadFileOptions() < 0)
        saveFileOptions();
    
    vblink_init();

    drc_init();

    consoleDebugInit(debugDevice_3DMOO);

    video_init();
    guiInit();

    v810_init();

    sound_init();

    if (is_citra) {
        tVBOpt.VSYNC = false;
    }

    replay_init();

    toggleAnaglyph(tVBOpt.ANAGLYPH);

    aptHookCookie cookie;
    aptHook(&cookie, aptBacklight, NULL);

    guiop = 0;
    openMenu(true);
    if (guiop & GUIEXIT) {
        goto exit;
    }

    game_running = true;

    clearCache();
    consoleClear();

    svcCreateEvent(&frame_event, RESET_STICKY);

    toggleVsync(tVBOpt.VSYNC);

    TickCounter drcTickCounter;
    TickCounter frameTickCounter;

    lag_frames = 0;
    svcClearEvent(frame_event);

    bool waiting_for_menu = false;
    bool needs_menu_message = false;
    input_buffer_tail = 0;
    for (int i = 0; i < tVBOpt.INPUT_BUFFER; i++) {
        for (int p = 0; p < 2; p++) {
            input_buffer[p][i] = 2;
            input_buffer_head[p]++;
        }
    }

    while(aptMainLoop()) {
        osTickCounterStart(&frameTickCounter);

        while (is_multiplayer) {
            bool received_menu_command = input_buffer[!my_player_id][(input_buffer_head[!my_player_id] + INPUT_BUFFER_SIZE - 1) % INPUT_BUFFER_SIZE] == 0;
            if (received_menu_command) break;

            Packet *recv_packet;
            while ((input_buffer_head[!my_player_id] - input_buffer_tail + INPUT_BUFFER_SIZE) % INPUT_BUFFER_SIZE < INPUT_BUFFER_SIZE - 2 && (recv_packet = read_next_packet())) {
                if (recv_packet->packet_type == PACKET_INPUTS) {
                    input_buffer[!my_player_id][input_buffer_head[!my_player_id]] = recv_packet->inputs;
                    input_buffer_head[!my_player_id] = (input_buffer_head[!my_player_id] + 1) % INPUT_BUFFER_SIZE;
                }
            }
            if (input_buffer_head[!my_player_id] == input_buffer_tail) {
                if (send_queue_empty()) {
                    // they might be waiting for an ack from us, so send one
                    Packet *send_packet = new_packet_to_send();
                    send_packet->packet_type = PACKET_NOP;
                    ship_packet(send_packet);
                }
                wait_for_packet(1000000000);
                lag_frames = 0;
                if (udsWaitConnectionStatusEvent(false, false)) {
                    udsConnectionStatus status;
                    udsGetConnectionStatus(&status);
                    if (status.total_nodes < 2 || status.status == 11) {
                        local_disconnect();
                        udsExit();
                        v810_endmultiplayer();
                        openPeerDisconnectMenu();
                        break;
                    }
                }
            } else {
                break;
            }
        }

        if (aptShouldClose()) break;

        hidScanInput();
        int keys = hidKeysDown();

        if ((keys & KEY_TOUCH) && guiShouldPause() && !waiting_for_menu) {
            waiting_for_menu = true;
            needs_menu_message = is_multiplayer;
        }
        bool show_menu = is_multiplayer
            ? (input_buffer[0][input_buffer_tail] == 0 || input_buffer[1][input_buffer_tail] == 0)
            : waiting_for_menu;

        if (show_menu) {
            save_sram();

            waiting_for_menu = false;
            needs_menu_message = false;

            bool my_menu = !is_multiplayer || (
                input_buffer[my_player_id][input_buffer_tail] == 0 && (
                    // give priority to player 1
                    my_player_id == 0 || input_buffer[0][input_buffer_tail] != 0
                )
            );

            bool was_multiplayer = is_multiplayer;
            int old_input_buffer = tVBOpt.INPUT_BUFFER;

            guiop = 0;
            openMenu(my_menu);
            if (guiop & GUIEXIT) {
                goto exit;
            }
            if (guiop & VBRESET) {
                V810_RControll(true);
                v810_reset();
                clearCache();
                frame = 0;
            }
            if (guiop & AKILL) {
                clearCache();
                drc_clearCache();
            }
            lag_frames = 0;
            svcClearEvent(frame_event);

            // wait for all leftover input packets to come in
            // do this after handling the menu to prevent entry lag
            if (is_multiplayer && was_multiplayer && my_menu) {
                while (input_buffer_head[!my_player_id] != (input_buffer_tail + old_input_buffer) % INPUT_BUFFER_SIZE) {
                    Packet *recv_packet;
                    if ((recv_packet = read_next_packet())) {
                        input_buffer_head[!my_player_id] = (input_buffer_head[!my_player_id] + 1) % INPUT_BUFFER_SIZE;
                    }
                    if (udsWaitConnectionStatusEvent(false, false)) {
                        udsConnectionStatus status;
                        udsGetConnectionStatus(&status);
                        if (status.total_nodes < 2 || status.status == 11) {
                            local_disconnect();
                            udsExit();
                            v810_endmultiplayer();
                            openPeerDisconnectMenu();
                            break;
                        }
                    }
                }
            }

            if (aptShouldClose()) break;

            input_buffer_tail = 0;
            input_buffer_head[0] = input_buffer_head[1] = 0;
            for (int i = 0; i < tVBOpt.INPUT_BUFFER; i++) {
                for (int p = 0; p < 2; p++) {
                    input_buffer[p][i] = 2;
                    input_buffer_head[p] = (input_buffer_head[p] + 1) % INPUT_BUFFER_SIZE;
                }
            }
        }

        // drawing
        {
            vb_state = &vb_players[my_player_id];
            bool is_golf = memcmp(tVBOpt.GAME_ID, "01VVGE", 6) == 0 || memcmp(tVBOpt.GAME_ID, "E4VVGJ", 6) == 0;

            // frameskip can cause bugs in golf when transitioning from VIP to software rendering
            if (is_golf) just_lagged = false;
            // frameskip causes visual glitches
            if (memcmp(tVBOpt.GAME_ID, "PRCHMB", 6) == 0) just_lagged = false;

            // forcefully disable antiflicker if software rendering is in use
            // because we can't easily delay the fb update until afterwards
            // and it's not likely that software rendering games will flicker at 50fps anyway
            if (tDSPCACHE.DDSPDataState[vb_state->tVIPREG.tDisplayedFB] == CPU_WROTE) {
                on_time = false;
            }

            // Display a frame, only after the right number of 'skips'
            // Also don't display if drawing is still ongoing
            if(vb_state->tVIPREG.tFrame == 0 && !vb_state->tVIPREG.drawing) {
                int displayed_fb = vb_state->tVIPREG.tDisplayedFB;
                // pass C3D_FRAME_NONBLOCK to enable frameskip, 0 to disable
                // it's only needed for 1 second in the mario clash intro afaik
                // so just bite the bullet and do the frameskip, rather that than slowdown
                if (C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
                    guiUpdate(osTickCounterRead(&frameTickCounter), osTickCounterRead(&drcTickCounter));

                    // Golf hack: switch to software rendering during gameplay.
                    if (is_golf) {
                        if (*(uint8_t*)(vb_state->V810_DISPLAY_RAM.off + 0x3dbc0) == 0x40 &&
                            *(uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x3dbe6) == 0x48 &&
                            memcmp((uint8_t*)vb_state->V810_DISPLAY_RAM.off + 0x3dbec, "\0\0\x80\x01\x1f\0\0\x80\0\0", 10) == 0
                        ) {
                            // looks like hills, do software rendering
                            if (tVBOpt.RENDERMODE != RM_CPUONLY) {
                                tVBOpt.RENDERMODE = RM_CPUONLY;
                                clearCache();
                            }
                        } else {
                            // switch back to hardware rendering
                            if (tVBOpt.RENDERMODE != RM_TOGPU) {
                                tVBOpt.RENDERMODE = RM_TOGPU;
                                for (int i = 0; i < 3; i++) {
                                    memset((uint8_t*)vb_state->V810_DISPLAY_RAM.off + (0x8000 * i), 0, 0x6000);
                                }
                            }
                        }
                    }

                    // if we just had a lagframe on which drawing happened, don't draw
                    if ((vb_state->tVIPREG.DPCTRL & 0x0002) && (!on_time || !just_lagged)) {
                        video_render(displayed_fb, on_time);
                        on_time = true;
                    } else if (on_time) {
                        if (tVBOpt.ANTIFLICKER) video_flush(false);
                        on_time = false;
                    }
                    C3D_FrameEnd(0);
                }
                if (tVBOpt.RENDERMODE != RM_CPUONLY) {
                    if (vb_state->tVIPREG.XPCTRL & 0x0002) {
                        if (tDSPCACHE.DDSPDataState[!displayed_fb] != GPU_CLEAR) {
                            memset((uint8_t*)vb_state->V810_DISPLAY_RAM.off + 0x8000 * !displayed_fb, 0, 0x6000);
                            memset((uint8_t*)vb_state->V810_DISPLAY_RAM.off + 0x10000 + 0x8000 * !displayed_fb, 0, 0x6000);
                            for (int i = 0; i < 64; i++) {
                                tDSPCACHE.SoftBufWrote[!displayed_fb][i].min = 0xff;
                                tDSPCACHE.SoftBufWrote[!displayed_fb][i].max = 0;
                            }
                            memset(tDSPCACHE.OpaquePixels.u32[!displayed_fb], 0, sizeof(tDSPCACHE.OpaquePixels.u32[!displayed_fb]));
                            uint32_t fb_size;
                            uint32_t *out_fb = (uint32_t*)C3D_Tex2DGetImagePtr(&screenTexSoft[!displayed_fb], 0, &fb_size);
                            memset(out_fb, 0, fb_size);
                            tDSPCACHE.DDSPDataState[!displayed_fb] = GPU_CLEAR;
                        }
                    }
                }
            } else {
                // no game graphics, draw menu if possible
                if (C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
                    // refresh top screen for antiflicker
                    if (tVBOpt.ANTIFLICKER && on_time) video_flush(false);
                    guiUpdate(osTickCounterRead(&frameTickCounter), osTickCounterRead(&drcTickCounter));
                    C3D_FrameEnd(0);
                }
                on_time = false;
            }
        }

        // if hold, turn off fast forward, as it'll be turned back on while reading input
        if (!tVBOpt.FF_TOGGLE) tVBOpt.FASTFORWARD = false;

        // add inputs to buffer
        {
            HWORD new_inputs = V810_RControll(false);
            // we can send multiple, they can just be ignored at the receiving end
            if (waiting_for_menu) new_inputs = 0;
            input_buffer[my_player_id][input_buffer_head[my_player_id]] = new_inputs;
            if (is_multiplayer && (!waiting_for_menu || needs_menu_message)) {
                Packet *send_packet;
                while (!(send_packet = new_packet_to_send())) {
                    wait_for_free_send_slot(1000000000);
                    lag_frames = 0;
                    if (udsWaitConnectionStatusEvent(false, false)) {
                        udsConnectionStatus status;
                        udsGetConnectionStatus(&status);
                        if (status.total_nodes < 2 || status.status == 11) {
                            local_disconnect();
                            udsExit();
                            v810_endmultiplayer();
                            openPeerDisconnectMenu();
                            break;
                        }
                    }
                }
                if (send_packet) {
                    send_packet->packet_type = PACKET_INPUTS;
                    send_packet->inputs = new_inputs;
                    ship_packet(send_packet);
                }
            }
            input_buffer_head[my_player_id] = (input_buffer_head[my_player_id] + 1) % INPUT_BUFFER_SIZE;
        }

        if (aptShouldClose()) break;

        // read inputs from buffer
        for (int p = 0; p < 2; p++) {
            HWORD inputs = input_buffer[p][input_buffer_tail];
            vb_players[p].tHReg.SLB = inputs;
            vb_players[p].tHReg.SHB = inputs >> 8;
        }
        replay_update(input_buffer[my_player_id][input_buffer_tail]);
        input_buffer_tail = (input_buffer_tail + 1) % INPUT_BUFFER_SIZE;


        vb_state = &vb_players[0];
        osTickCounterStart(&drcTickCounter);
        err = v810_run();
        osTickCounterUpdate(&drcTickCounter);
        if (err) {
            showError(err);
            do {
                hidScanInput();
                gspWaitForVBlank();
            } while (aptMainLoop() && !hidKeysDown());
            goto exit;
        }

        // Increment frame
        frame++;

        osTickCounterUpdate(&frameTickCounter);

        if (!tVBOpt.FASTFORWARD) {
            just_lagged = lag_frames > 0;
            if (!just_lagged) {
                svcWaitSynchronization(frame_event, 20000000);
                lag_frames = 1;
            }
            svcClearEvent(frame_event);
            if (--lag_frames > 2)
                lag_frames = 2;
        } else {
            lag_frames = 0;
        }

#if DEBUGLEVEL == 0
        consoleSelect(&main_console);
        /*
        printf("\x1b[1J\x1b[0;0HFrame: %i\nTotal CPU: %5.2fms\tDRC: %5.2fms\nGFX-CPU: %5.2fms\tGFX-GPU: %5.2fms\nPC: 0x%lx\tDRC cache: %5.2f%%",
            frame, osTickCounterRead(&frameTickCounter), osTickCounterRead(&drcTickCounter),
            C3D_GetProcessingTime(), C3D_GetDrawingTime(),
            vb_state->v810_state.PC, (cache_pos-cache_start)*4*100./CACHE_SIZE);
        */
#else
        printf("\x1b[1J\x1b[0;0HFrame: %i\nPC: 0x%x", frame, (unsigned int) vb_state->v810_state.PC);
#endif
    }

    // home menu, so try and save
    save_sram();

exit:
    aptUnhook(&cookie);
    toggleVsync(false);
    sound_close();
    if (save_thread) threadJoin(save_thread, U64_MAX);
    endThreads();
    local_disconnect();
    video_quit();
    drc_exit();
    v810_exit();

    if (tVBOpt.CPP_ENABLED) cppExit();
    udsExit();
    ptmuExit();
    romfsExit();
    fsExit();
    gfxExit();
    return 0;
}
