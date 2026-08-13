#include "vb_types.h"

void video_hard_init(void);
void gpu_init(void);
void gpu_clear_screen(bool outside_rendering);
void gpu_reset_vip_download(void);
void gpu_set_opaque(bool opaque);
void update_texture_cache_hard(void);
void gpu_soft_to_texture(int displayed_fb);
void gpu_blend_antiflicker(void);
void gpu_blend_default(void);
bool gpu_antiflicker_allowed(void);
void gpu_flush(bool default_for_both, int displayed_fb, int vip_displayed_fb);
void gpu_quit(void);

#ifndef __3DS__
void gl_flush(void);
#endif
