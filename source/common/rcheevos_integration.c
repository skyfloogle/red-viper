/*
 * rcheevos_integration.c -- RetroAchievements integration for Red Viper
 *
 * Uses the rc_client API from rcheevos v12.x.
 * HTTP transport: libctru httpc (built-in 3DS system service).
 */

#include "rcheevos_integration.h"

#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"

#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_types.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef __3DS__
#include <3ds.h>
#endif

/* ------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------ */

static rc_client_t *s_client = NULL;

/* Simple popup queue (single slot -- newest wins) */
static ra_popup_t s_popup;
static bool       s_popup_active = false;

#define POPUP_DURATION_FRAMES (50 * 5)  /* ~5 seconds at 50 Hz */

/* Persisted login credentials */
#define RA_CREDENTIALS_DIR  "sdmc:/red-viper"
#define RA_CREDENTIALS_PATH "sdmc:/red-viper/ra_login.bin"

/* ------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------ */

static uint32_t ra_read_memory(uint32_t address, uint8_t *buffer,
                               uint32_t num_bytes, rc_client_t *client);
static void ra_server_call(const rc_api_request_t *request,
                           rc_client_server_callback_t callback,
                           void *callback_data, rc_client_t *client);
static void ra_event_handler(const rc_client_event_t *event,
                             rc_client_t *client);
static void ra_log_callback(const char *message, const rc_client_t *client);

/* ------------------------------------------------------------------
 * Memory read callback
 *
 * RA address map for Virtual Boy (from rcheevos consoleinfo.c):
 *   0x000000 - 0x00FFFF  ->  WRAM   (V810_VB_RAM,   64 KB)
 *   0x010000 - 0x011FFF  ->  SRAM   (V810_GAME_RAM,  8 KB)
 * ------------------------------------------------------------------ */

static uint32_t ra_read_memory(uint32_t address, uint8_t *buffer,
                               uint32_t num_bytes, rc_client_t *client)
{
    (void)client;

    const uint32_t WRAM_END   = 0x010000u; /* 64 KB */
    const uint32_t SRAM_START = 0x010000u;
    const uint32_t SRAM_END   = 0x012000u; /* 8 KB */

    if (address < WRAM_END) {
        uint32_t avail = WRAM_END - address;
        if (num_bytes > avail) num_bytes = avail;
        if (!vb_state || !vb_state->V810_VB_RAM.pmemory)
            return 0;
        if (address + num_bytes > vb_state->V810_VB_RAM.size)
            return 0;
        memcpy(buffer, vb_state->V810_VB_RAM.pmemory + address, num_bytes);
        return num_bytes;
    }

    if (address >= SRAM_START && address < SRAM_END) {
        uint32_t offset = address - SRAM_START;
        uint32_t avail  = SRAM_END - address;
        if (num_bytes > avail) num_bytes = avail;
        if (!vb_state || !vb_state->V810_GAME_RAM.pmemory)
            return 0;
        if (offset + num_bytes > vb_state->V810_GAME_RAM.size) {
            if (offset >= vb_state->V810_GAME_RAM.size)
                return 0;
            num_bytes = vb_state->V810_GAME_RAM.size - offset;
        }
        memcpy(buffer, vb_state->V810_GAME_RAM.pmemory + offset, num_bytes);
        return num_bytes;
    }

    return 0; /* unmapped */
}

/* ------------------------------------------------------------------
 * HTTP server callback — libctru httpc
 *
 * The httpc service is part of the 3DS system software. It supports
 * HTTP and HTTPS (with OS-level TLS), GET and POST. No extra
 * packages are required beyond -lctru.
 *
 * We init/exit httpc per-request to avoid holding system resources
 * while the emulator is running.
 * ------------------------------------------------------------------ */

#ifdef __3DS__

static void ra_server_call(const rc_api_request_t *request,
                           rc_client_server_callback_t callback,
                           void *callback_data, rc_client_t *client)
{
    (void)client;

    Result ret;
    httpcContext context;
    u32 status_code = 0;
    u32 read_size = 0;
    u32 total_read = 0;
    char *body = NULL;
    size_t capacity = 0;
    bool context_opened = false;

    /* Rewrite https -> http (3DS TLS can't handle modern ciphers) */
    char url_buf[512];
    const char *url = request->url;
    if (strncmp(url, "https://", 8) == 0) {
        snprintf(url_buf, sizeof(url_buf), "http://%s", url + 8);
        url = url_buf;
    }

    /* Debug log */
    FILE *dbg = fopen("sdmc:/red-viper/ra_debug.txt", "a");
    if (dbg) {
        fprintf(dbg, "--- NEW REQUEST ---\n");
        fprintf(dbg, "URL: %.200s\n", request->url);
        fprintf(dbg, "Rewritten: %.200s\n", url);
        fprintf(dbg, "POST: %s\n", request->post_data ? "yes" : "no");
        if (request->post_data)
            fprintf(dbg, "POST data: %.300s\n", request->post_data);
        fprintf(dbg, "Hardcore enabled: %d\n",
                s_client ? rc_client_get_hardcore_enabled(s_client) : -1);
    }

    /* Open context with appropriate HTTP method */
    if (request->post_data) {
        ret = httpcOpenContext(&context, HTTPC_METHOD_POST,
                               url, 1);
    } else {
        ret = httpcOpenContext(&context, HTTPC_METHOD_GET,
                               url, 1);
    }
    if (R_FAILED(ret)) {
        if (dbg) { fprintf(dbg, "OpenContext FAILED: %08lX\n", ret); fclose(dbg); }
        goto fail;
    }
    context_opened = true;
    if (dbg) fprintf(dbg, "OpenContext OK\n");

    httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");

    /* User-Agent */
    {
        char ua[256];
        int ua_len = snprintf(ua, sizeof(ua), "RedViper/%s ", VERSION);
        if (s_client && ua_len > 0 && (size_t)ua_len < sizeof(ua)) {
            rc_client_get_user_agent_clause(s_client,
                ua + ua_len, sizeof(ua) - ua_len);
        }
        httpcAddRequestHeaderField(&context, "User-Agent", ua);
    }

    /* POST body */
    if (request->post_data) {
        u32 post_len = (u32)strlen(request->post_data);
        httpcAddRequestHeaderField(&context, "Content-Type",
            "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(&context, (const u32 *)request->post_data,
                            post_len);
    }

    ret = httpcBeginRequest(&context);
    if (R_FAILED(ret)) {
        if (dbg) { fprintf(dbg, "BeginRequest FAILED: %08lX\n", ret); fclose(dbg); }
        goto fail;
    }
    if (dbg) fprintf(dbg, "BeginRequest OK\n");

    httpcGetResponseStatusCode(&context, &status_code);
    if (dbg) fprintf(dbg, "HTTP Status: %lu\n", status_code);

    /* Read response body */
    capacity = 8192;
    body = (char *)malloc(capacity);
    if (!body) {
        if (dbg) { fprintf(dbg, "malloc FAILED\n"); fclose(dbg); }
        goto fail;
    }

    do {
        if (total_read + 4096 > capacity) {
            capacity *= 2;
            char *new_body = (char *)realloc(body, capacity);
            if (!new_body) break;
            body = new_body;
        }
        ret = httpcDownloadData(&context,
            (u8 *)(body + total_read),
            (u32)(capacity - total_read - 1),
            &read_size);
        total_read += read_size;
    } while (ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    body[total_read] = '\0';

    if (dbg) {
        fprintf(dbg, "Body: %lu bytes\nFirst 200 chars: %.200s\n", total_read, body);
        fclose(dbg);
    }

    httpcCloseContext(&context);
    context_opened = false;

    /* Deliver response */
    {
        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = (int)status_code;
        response.body             = body;
        response.body_length      = total_read;
        callback(&response, callback_data);
    }

    free(body);
    return;

fail:
    if (context_opened) httpcCloseContext(&context);

    {
        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = 0;
        callback(&response, callback_data);
    }

    free(body);
}

#else /* non-3DS stub for compilation on other platforms */

static void ra_server_call(const rc_api_request_t *request,
                           rc_client_server_callback_t callback,
                           void *callback_data, rc_client_t *client)
{
    (void)request; (void)client;
    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    callback(&response, callback_data);
}

#endif /* __3DS__ */

/* ------------------------------------------------------------------
 * Event handler
 * ------------------------------------------------------------------ */

static void ra_event_handler(const rc_client_event_t *event,
                             rc_client_t *client)
{
    (void)client;

    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        if (event->achievement) {
            snprintf(s_popup.title, sizeof(s_popup.title),
                     "Achievement Unlocked!");
            snprintf(s_popup.description, sizeof(s_popup.description),
                     "%s", event->achievement->title);
            s_popup.frames_remaining = POPUP_DURATION_FRAMES;
            s_popup_active = true;
        }
        break;

    case RC_CLIENT_EVENT_GAME_COMPLETED:
        snprintf(s_popup.title, sizeof(s_popup.title),
                 "Congratulations!");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "All achievements earned!");
        s_popup.frames_remaining = POPUP_DURATION_FRAMES;
        s_popup_active = true;
        break;

    case RC_CLIENT_EVENT_RESET:
        snprintf(s_popup.title, sizeof(s_popup.title), "Game Reset");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "Hardcore mode requires a reset.");
        s_popup.frames_remaining = POPUP_DURATION_FRAMES;
        s_popup_active = true;
        break;

    case RC_CLIENT_EVENT_SERVER_ERROR:
        snprintf(s_popup.title, sizeof(s_popup.title), "RA Server Error");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "%s", event->server_error
                     ? event->server_error->error_message
                     : "Unknown error");
        s_popup.frames_remaining = POPUP_DURATION_FRAMES;
        s_popup_active = true;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------
 * Logging callback
 * ------------------------------------------------------------------ */

static void ra_log_callback(const char *message, const rc_client_t *client)
{
    (void)client;
    dprintf(1, "[RA] %s\n", message);
}

/* ------------------------------------------------------------------
 * Credential persistence
 * ------------------------------------------------------------------ */

static void ra_save_credentials(const char *username, const char *token)
{
    mkdir(RA_CREDENTIALS_DIR, 0777);
    FILE *f = fopen(RA_CREDENTIALS_PATH, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", username, token);
        fclose(f);
    }
}

static bool ra_load_credentials(char *username, size_t usize,
                                char *token, size_t tsize)
{
    FILE *f = fopen(RA_CREDENTIALS_PATH, "r");
    if (!f) return false;

    if (!fgets(username, (int)usize, f)) { fclose(f); return false; }
    if (!fgets(token, (int)tsize, f))    { fclose(f); return false; }
    fclose(f);

    char *nl;
    if ((nl = strchr(username, '\n'))) *nl = '\0';
    if ((nl = strchr(username, '\r'))) *nl = '\0';
    if ((nl = strchr(token, '\n')))    *nl = '\0';
    if ((nl = strchr(token, '\r')))    *nl = '\0';

    return username[0] != '\0' && token[0] != '\0';
}

static void ra_delete_credentials(void)
{
    remove(RA_CREDENTIALS_PATH);
}

/* ------------------------------------------------------------------
 * Login callback
 * ------------------------------------------------------------------ */

static void ra_login_callback(int result, const char *error_message,
                              rc_client_t *client, void *userdata)
{
    (void)userdata;

    if (result == RC_OK) {
        const rc_client_user_t *user = rc_client_get_user_info(client);
        if (user && user->token) {
            ra_save_credentials(user->username, user->token);
        }
        snprintf(s_popup.title, sizeof(s_popup.title), "RA Login OK");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "Welcome, %s!", user ? user->display_name : "");
    } else {
        snprintf(s_popup.title, sizeof(s_popup.title), "RA Login Failed");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "%s", error_message ? error_message : "Unknown error");
    }
    s_popup.frames_remaining = POPUP_DURATION_FRAMES;
    s_popup_active = true;
}

/* ------------------------------------------------------------------
 * Game load callback
 * ------------------------------------------------------------------ */

static void ra_game_loaded_callback(int result, const char *error_message,
                                    rc_client_t *client, void *userdata)
{
    (void)userdata; (void)client;

    if (result == RC_OK) {
        snprintf(s_popup.title, sizeof(s_popup.title), "Game Identified");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "Achievements loaded!");
    } else if (result == RC_NO_GAME_LOADED) {
        snprintf(s_popup.title, sizeof(s_popup.title), "No Achievements");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "This game has no achievements.");
    } else {
        snprintf(s_popup.title, sizeof(s_popup.title), "RA Load Error");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "%s", error_message ? error_message : "Unknown error");
    }
    s_popup.frames_remaining = POPUP_DURATION_FRAMES;
    s_popup_active = true;
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

void ra_init(void)
{

    #ifdef __3DS__
        httpcInit(0x1000);
    #endif

    /* No extra library init needed -- httpc is initialized per-request */

    s_client = rc_client_create(ra_read_memory, ra_server_call);
    if (!s_client) return;

    rc_client_enable_logging(s_client, RC_CLIENT_LOG_LEVEL_INFO,
                             ra_log_callback);
    rc_client_set_event_handler(s_client, ra_event_handler);

    /* Default: Hardcore ON per RA convention */
    rc_client_set_hardcore_enabled(s_client, 1);

    /* Attempt auto-login with saved token */
    char username[128] = {0};
    char token[128]    = {0};
    if (ra_load_credentials(username, sizeof(username),
                            token, sizeof(token))) {
        rc_client_begin_login_with_token(s_client, username, token,
                                         ra_login_callback, NULL);
    }
}


void ra_shutdown(void)
{
    if (s_client) {
        rc_client_destroy(s_client);
        s_client = NULL;
    }
#ifdef __3DS__
    httpcExit();
#endif
    /* No extra library cleanup needed */
}

void ra_load_game(void)
{
    if (!s_client) return;
    if (!ra_is_logged_in()) return;
    if (!V810_ROM1.pmemory || V810_ROM1.size == 0) return;

    /*
     * Step 1: Compute the ROM hash.
     * Virtual Boy uses MD5 of the entire ROM buffer.
     * rc_hash_generate_from_buffer returns 1 on success.
     * The hash is a 32-character hex string + null terminator.
     */
    char hash[33] = {0};
    if (!rc_hash_generate_from_buffer(hash, RC_CONSOLE_VIRTUAL_BOY,
                                      V810_ROM1.pmemory,
                                      V810_ROM1.size)) {
        snprintf(s_popup.title, sizeof(s_popup.title), "RA Hash Error");
        snprintf(s_popup.description, sizeof(s_popup.description),
                 "Could not identify ROM.");
        s_popup.frames_remaining = POPUP_DURATION_FRAMES;
        s_popup_active = true;
        return;
    }

    /*
     * Step 2: Load the game by hash.
     * rc_client fetches the achievement set from the server.
     */
    rc_client_begin_load_game(s_client, hash,
                              ra_game_loaded_callback, NULL);
}

void ra_unload_game(void)
{
    if (s_client)
        rc_client_unload_game(s_client);
}

void ra_do_frame(void)
{
    if (s_client)
        rc_client_do_frame(s_client);
}

void ra_idle(void)
{
    if (s_client)
        rc_client_idle(s_client);
}

void ra_login_with_password(const char *username, const char *password)
{
    if (s_client)
        rc_client_begin_login_with_password(s_client, username, password,
                                            ra_login_callback, NULL);
}

void ra_login_with_token(const char *username, const char *token)
{
    if (s_client)
        rc_client_begin_login_with_token(s_client, username, token,
                                         ra_login_callback, NULL);
}

void ra_logout(void)
{
    if (s_client)
        rc_client_logout(s_client);
    ra_delete_credentials();
}

bool ra_is_logged_in(void)
{
    if (!s_client) return false;
    return rc_client_get_user_info(s_client) != NULL;
}

const char *ra_get_username(void)
{
    if (!s_client) return NULL;
    const rc_client_user_t *user = rc_client_get_user_info(s_client);
    return user ? user->display_name : NULL;
}

void ra_set_hardcore_enabled(bool enabled)
{
    if (s_client)
        rc_client_set_hardcore_enabled(s_client, enabled ? 1 : 0);
}

bool ra_get_hardcore_enabled(void)
{
    if (!s_client) return false;
    return rc_client_get_hardcore_enabled(s_client) != 0;
}

bool ra_has_popup(void)
{
    return s_popup_active;
}

bool ra_get_popup(ra_popup_t *out)
{
    if (!s_popup_active) return false;
    *out = s_popup;
    return true;
}

void ra_tick_popup(void)
{
    if (s_popup_active && s_popup.frames_remaining > 0) {
        s_popup.frames_remaining--;
        if (s_popup.frames_remaining <= 0)
            s_popup_active = false;
    }
}

bool ra_allow_load_state(void)
{
    return !ra_get_hardcore_enabled();
}