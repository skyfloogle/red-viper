#ifndef RCHEEVOS_INTEGRATION_H
#define RCHEEVOS_INTEGRATION_H

/*
 * RetroAchievements integration for Red Viper (3DS only).
 *
 * Threading model:
 *   - All rc_client calls, rc_client callbacks and the event handler run
 *     on the MAIN thread.
 *   - HTTP requests and badge downloads/decoding run on a background
 *     worker thread. Results are handed back to the main thread and
 *     dispatched from ra_do_frame() / ra_idle().
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Lifecycle */
void ra_init(void);
void ra_shutdown(void);

/* Call at the start of apply_patches(), i.e. after a ROM has been fully
 * loaded into V810_ROM1 but BEFORE Red Viper patches it, so the hash
 * matches the unmodified ROM. Safe to call from any thread. */
void ra_capture_rom_hash(void);

/* Pair these with game_running = true / false */
void ra_load_game(void);
void ra_unload_game(void);

/* Call when the emulated console is reset (VBRESET) */
void ra_reset(void);

/* Returns true once if rc_client asked for the game to be reset
 * (e.g. hardcore was enabled with a game loaded). */
bool ra_take_reset_request(void);

/* Per-frame / idle pumping (main thread only) */
void ra_do_frame(void);   /* once per emulated frame, after v810_run() */
void ra_idle(void);       /* every frame while in menus */
void ra_tick_popup(void); /* once per emulated frame */

/* Login */
void ra_login_with_password(const char *username, const char *password);
void ra_logout(void);
bool ra_is_logged_in(void);

/* Hardcore */
bool ra_hardcore_available(void);   /* false when running under Citra/Azahar */
void ra_set_hardcore_enabled(bool enabled);
bool ra_get_hardcore_enabled(void);
bool ra_allow_load_state(void);     /* false in hardcore */
bool ra_has_game(void);             /* an achievement set is currently loaded */
bool ra_can_pause(void);            /* hardcore pause-spam limiter, call when the user pauses */

/* Savestates: achievement progress is stored after the emulator state */
int  ra_write_progress(FILE *f);              /* 0 on success */
int  ra_read_progress(FILE *f, void **out);   /* 0 on success, *out may be NULL */
void ra_apply_progress(void *progress);       /* takes ownership, NULL resets progress */

/* Badge images (main thread). key identifies the image, url is where to get it. */
void ra_request_badge(const char *key, const char *url);
bool ra_find_badge(const char *key, const uint8_t **rgba, int *w, int *h, uint32_t *id);

/* Overlay (polled by the GUI on the main thread) */
typedef struct {
    char title[96];
    char description[160];
    const uint8_t *badge_rgba;  /* RGBA8, row-major, NULL if not available yet */
    int badge_w, badge_h;
    uint32_t badge_id;          /* unique per decoded image, 0 = none */
    uint32_t serial;            /* changes whenever the text changes */
} ra_popup_t;

#define RA_MAX_TRACKERS   4
#define RA_MAX_CHALLENGES 6

typedef struct {
    int num_trackers;
    char trackers[RA_MAX_TRACKERS][32];
    int num_challenges;
    char challenge_keys[RA_MAX_CHALLENGES][40];
    uint32_t serial;            /* changes whenever trackers/challenges change */
} ra_hud_t;

bool ra_overlay_active(void);
bool ra_get_popup(ra_popup_t *out);
bool ra_get_progress(char *out, int out_size, uint32_t *serial);
bool ra_get_hud(ra_hud_t *out);
bool ra_overlay_take_dirty(void);   /* true once when the bottom screen needs a clear */

/* Achievement list */
typedef struct {
    bool is_header;
    bool unlocked;
    char title[72];
    char description[136];
    char status[40];            /* "Unlocked", "12/50", "Locked", "Unsupported" */
    char badge_key[40];
    char badge_url[192];
} ra_list_entry_t;

/* Builds a snapshot of the achievement list. Free with ra_list_free(). */
int  ra_list_build(ra_list_entry_t **out, char *summary, int summary_size);
void ra_list_free(ra_list_entry_t *entries);

#endif /* RCHEEVOS_INTEGRATION_H */
