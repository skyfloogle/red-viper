#include "libretro.h"
#include "drc_core.h"
#include "v810_cpu.h"
#include "vb_dsp.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "video_hard.h"
#include "replay.h"

static retro_environment_t environment_cb;
static retro_video_refresh_t video_refresh_cb;
static retro_audio_sample_t audio_sample_cb;
static retro_audio_sample_batch_t audio_sample_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static struct retro_hw_render_callback hw_context;
static bool frontend_supports_bitmasks;

bool sound_init_backend(int16_t **wavebufs) {
    return true;
}

void sound_close_backend() {}

void sound_pause_backend() {}

void sound_resume_backend() {}

bool sound_push_backend(int16_t *buf) {
    audio_sample_batch_cb(buf, SAMPLE_COUNT);
    return true;
}

void gl_flush() {
    static uint32_t pixels[224][384];
    int displayed_fb = vb_state->tVIPREG.tDisplayedFB;
    int src_eye = 0;

    u32 *inbuf = (u32*)(vb_state->V810_DISPLAY_RAM.off + 0x8000 * displayed_fb + 0x10000 * src_eye);

	u32 colors[4] = {
		__builtin_bswap32(video_get_colour(0, 0)) >> 8,
		__builtin_bswap32(video_get_colour(1, vb_state->tVIPREG.BRTA)) >> 8,
		__builtin_bswap32(video_get_colour(2, vb_state->tVIPREG.BRTB)) >> 8,
		__builtin_bswap32(video_get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC))) >> 8,
	};

    for (int x = 0; x < 384; x++) {
        for (int ty = 0; ty < 224 / 16; ty++) {
            u32 intile = *inbuf++;
            for (int p = 0; p < 16; p++) {
                pixels[ty * 16 + p][x] = colors[intile & 3];
                intile >>= 2;
            }
        }
        // inbuf is 256 tall = 32 extra pixels
        inbuf += 2;
    }
    video_refresh_cb(pixels, 384, 224, 384*4);
}

static void update_inputs(void) {
    input_poll_cb();
    for (int p = 0; p < 2; p++) {
        short retro_inputs = 0;
        if (frontend_supports_bitmasks) {
            retro_inputs = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
        } else {
            for (int i = 0; i < 16; i++) {
                retro_inputs |= input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, i) << i;
            }
        }
        static int retro_to_vb_map[16] = {
            [RETRO_DEVICE_ID_JOYPAD_A] = VB_KEY_A,
            [RETRO_DEVICE_ID_JOYPAD_B] = VB_KEY_B,
            [RETRO_DEVICE_ID_JOYPAD_L] = VB_KEY_L,
            [RETRO_DEVICE_ID_JOYPAD_R] = VB_KEY_R,
            [RETRO_DEVICE_ID_JOYPAD_START] = VB_KEY_START,
            [RETRO_DEVICE_ID_JOYPAD_SELECT] = VB_KEY_SELECT,
            [RETRO_DEVICE_ID_JOYPAD_UP] = VB_LPAD_U,
            [RETRO_DEVICE_ID_JOYPAD_DOWN] = VB_LPAD_D,
            [RETRO_DEVICE_ID_JOYPAD_LEFT] = VB_LPAD_L,
            [RETRO_DEVICE_ID_JOYPAD_RIGHT] = VB_LPAD_R,
            [RETRO_DEVICE_ID_JOYPAD_L2] = VB_RPAD_U,
            [RETRO_DEVICE_ID_JOYPAD_R3] = VB_RPAD_R,
            [RETRO_DEVICE_ID_JOYPAD_R2] = VB_RPAD_L,
            [RETRO_DEVICE_ID_JOYPAD_L3] = VB_RPAD_D,
        };
        short vb_inputs = 0;
        for (int i = 0; i < 16; i++) {
            if (retro_inputs & (1 << i)) {
                vb_inputs |= retro_to_vb_map[i];
            }
        }
        vb_players[p].tHReg.SLB = vb_inputs;
        vb_players[p].tHReg.SHB = vb_inputs >> 8;
    }
}

void retro_set_environment(retro_environment_t cb) {
    environment_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_refresh_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    audio_sample_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_sample_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_init() {
    frontend_supports_bitmasks = environment_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
    setDefaults();
    v810_init();
    replay_init();

    #if DRC_AVAILABLE
    drc_init();
    #endif

    clearCache();
    sound_init();
    video_init(false);
}

void retro_deinit() {}

unsigned retro_api_version() {
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info) {
    info->library_name = "Red Viper";
    info->library_version = "0";
    info->valid_extensions = "vb";
    info->need_fullpath = false;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    info->geometry.base_width = 384;
    info->geometry.base_height = 224;
    info->geometry.max_width = 384;
    info->geometry.max_height = 224;
    info->timing.fps = 50;
    info->timing.sample_rate = SAMPLE_RATE;

    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    environment_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
}

void retro_set_controller_port_device(unsigned int port, unsigned int device) {
    (void)port;
    (void)device;
}

void retro_reset() {
    v810_reset();
}

void retro_run() {
    update_inputs();
    int displayed_fb = vb_state->tVIPREG.tDisplayedFB;
    if(vb_state->tVIPREG.tFrame == 0 && !vb_state->tVIPREG.drawing && (vb_state->tVIPREG.DPCTRL & 0x0002)) {
        video_render(displayed_fb, true);
    }
    v810_run();
}

size_t retro_serialize_size() {
    return 0;
}

bool retro_serialize(void *data, size_t len) {
    (void)data;
    (void)len;
    return false;
}

bool retro_unserialize(const void *data, size_t len) {
    (void)data;
    (void)len;
    return false;
}

void retro_cheat_reset() {}

void retro_cheat_set(unsigned int index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

bool retro_load_game(const struct retro_game_info *game) {
    if (game->data == NULL) return false;
    if (game->size < 0x10 || game->size > MAX_ROM_SIZE) return false;
    // require po2
    if ((game->size & (game->size - 1)) != 0) return false;
    memcpy(V810_ROM1.pmemory, game->data, game->size);
    v810_load_finalize(game->size);
    return true;
}

bool retro_load_game_special(unsigned int game_type, const struct retro_game_info *info, size_t num_info) {
    (void)game_type;
    (void)info;
    (void)num_info;
    return false;
}

void retro_unload_game() {}

unsigned retro_get_region() {
    return RETRO_REGION_PAL;
}

void *retro_get_memory_data(unsigned int id) {
    switch (id) {
        case RETRO_MEMORY_SAVE_RAM:
            return vb_state->V810_GAME_RAM.pmemory;
        case RETRO_MEMORY_SYSTEM_RAM:
            return vb_state->V810_VB_RAM.pmemory;
    }
    return NULL;
}

size_t retro_get_memory_size(unsigned int id) {
    switch (id) {
        case RETRO_MEMORY_SAVE_RAM:
            return vb_state->V810_GAME_RAM.size;
        case RETRO_MEMORY_SYSTEM_RAM:
            return vb_state->V810_VB_RAM.size;
    }
    return 0;
}
