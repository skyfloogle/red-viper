#include "vb_dsp.h"
#include "video_hard.h"
#include <stdlib.h>

// stubs
void gpu_quit() {}
void video_download_vip(int drawn_fb) {}
void video_hard_init() {}
void gpu_reset_vip_download() {}
void gpu_clear_screen(bool outside_rendering) {}
bool gpu_antiflicker_allowed() {
    return false;
}
void gpu_soft_to_texture(int displayed_fb) {}
void gpu_blend_antiflicker() {}
void gpu_blend_default() {}
// stubs that should actually crash when called
void video_hard_render(int drawn_fb) {
    abort();
}
void update_texture_cache_hard() {
    abort();
}
void gpu_flush(bool default_for_both, int displayed_fb, int vip_displayed_fb) {
    gl_flush();
}
