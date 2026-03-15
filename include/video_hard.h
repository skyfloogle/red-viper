#include "vb_types.h"
#include "vb_dsp.h"

#ifdef __3DS__
#include <citro3d.h>
typedef C3D_Tex Texture;
typedef C3D_RenderTarget *Framebuffer;
#else
#include <GLES2/gl2.h>
typedef GLuint Texture, Framebuffer;
#endif

extern Framebuffer screenTargetHard[2];

// Virtual Bowling needs at least 4 for good performance
#define AFFINE_CACHE_SIZE 8
typedef struct {
	Texture tex;
	Framebuffer target;
	int bg;
	short umin, umax, vmin, vmax;
	short lumin, lumax, lvmin, lvmax;
	u16 tiles[64 * 64];
	u8 GPLT[4];
	bool visible;
	bool used;
} AffineCacheEntry;
extern AffineCacheEntry tileMapCache[AFFINE_CACHE_SIZE];

typedef struct {
	short x, y;
	u8 u, v, palette, orient;
} __attribute__((aligned(4))) vertex;

typedef struct {
	short x1, y1, u1, v1, uoff1, voff1;
	short x2, y2, u2, v2, uoff2, voff2;
} avertex;

#define VBUF_SIZE 64 * 64 * 2 * 32
extern vertex *vbuf, *vcur;
#define AVBUF_SIZE 4096 * 8
extern avertex *avbuf, *avcur;

// 224 works on hardware but breaks in azahar
#define DOWNLOADED_FRAMEBUFFER_WIDTH 256
extern uint16_t *rgba4_framebuffers;

void video_hard_init(void);

void gpu_init(void);
void gpu_clear_screen(int start_eye, int end_eye);
void gpu_setup_drawing(void);
void gpu_setup_tile_drawing(void);
void gpu_set_tile_offset(float xoffset, float yoffset);
void gpu_init_vip_download(int previous_transfer_count, int start_eye, int end_eye, int drawn_fb);
void gpu_target_screen(int drawn_fb);
void gpu_target_affine(int cache_id);
void gpu_set_scissor(bool enabled, u32 left, u32 top, u32 right, u32 bottom);
void gpu_set_opaque(bool opaque);
void gpu_draw_tiles(int first, int count);
void gpu_draw_affine(WORLD *world, int umin, int vmin, int umax, int vmax, int drawn_fb, avertex *vbufs[], bool visible[]);
void update_texture_cache_hard(void);
