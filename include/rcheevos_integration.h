#ifndef RCHEEVOS_INTEGRATION_H
#define RCHEEVOS_INTEGRATION_H

#include <stdbool.h>
#include <stdint.h>

/* Lifecycle */
void ra_init(void);
void ra_shutdown(void);

/* Called after ROM is fully loaded and game_running == true */
void ra_load_game(void);

/* Called before loading a new ROM or quitting */
void ra_unload_game(void);

/* Called once per emulated frame, after v810_run() */
void ra_do_frame(void);

/* Called while in menus (no frame running) to keep network alive */
void ra_idle(void);

/* Login */
void ra_login_with_password(const char *username, const char *password);
void ra_login_with_token(const char *username, const char *token);
void ra_logout(void);
bool ra_is_logged_in(void);
const char *ra_get_username(void);

/* Hardcore mode */
void ra_set_hardcore_enabled(bool enabled);
bool ra_get_hardcore_enabled(void);

/* Hardcore guards */
bool ra_allow_load_state(void);  /* returns false in Hardcore */

/* Popup state -- polled by GUI for display */
typedef struct {
    char title[128];
    char description[256];
    int  frames_remaining;
} ra_popup_t;

bool ra_has_popup(void);
bool ra_get_popup(ra_popup_t *out);
void ra_tick_popup(void);

#endif /* RCHEEVOS_INTEGRATION_H */