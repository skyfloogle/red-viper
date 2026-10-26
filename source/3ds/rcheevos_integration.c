/*
 * rcheevos_integration.c -- RetroAchievements integration for Red Viper
 *
 * rcheevos rc_client API, libctru httpc transport on a worker thread,
 * stb_image for badge decoding.
 */

#include <3ds.h>

#include "rcheevos_integration.h"

#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"

#include "main.h"
#include "utils.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#ifndef RA_UA_VERSION
#error "RA_UA_VERSION must be defined by the Makefile (e.g. -DRA_UA_VERSION=\\\"v1.2.5\\\")"
#endif

/* ------------------------------------------------------------------
 * Config
 * ------------------------------------------------------------------ */

#define RA_DIR              "sdmc:/red-viper"
#define RA_CREDENTIALS_PATH RA_DIR "/ra_login.bin"
#define RA_BADGE_DIR        RA_DIR "/ra_badges"

#define FPS                 50
#define POPUP_FRAMES        (FPS * 4)
#define PROGRESS_FRAMES     (FPS * 2)
#define POPUP_QUEUE_LEN     8
#define IMAGE_CACHE_LEN     8
#define MAX_BADGE_DIM       128

/* ------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------ */

static rc_client_t *s_client = NULL;
static const rc_memory_regions_t *s_regions = NULL;

static char s_user_agent[160];
static char s_rom_hash[33];
static volatile bool s_rom_hash_valid = false;

static bool s_reset_requested = false;

/* ---- worker thread / job queues ---- */

typedef enum { JOB_API, JOB_BADGE } job_kind_t;

typedef struct job {
    struct job *next;
    job_kind_t kind;
    char *url;
    char *post_data;
    rc_client_server_callback_t callback;
    void *callback_data;
    /* results */
    int http_status;
    char *body;
    size_t body_len;
    /* badge */
    char key[40];
    uint8_t *rgba;
    int w, h;
} job_t;

static Thread     s_thread = NULL;
static LightLock  s_lock;
static LightEvent s_wake;
static volatile bool s_quit = false;
static job_t *s_pending_head = NULL, *s_pending_tail = NULL;
static job_t *s_done_head = NULL, *s_done_tail = NULL;

/* ---- decoded badge images (main thread) ---- */

typedef struct {
    char key[40];
    uint8_t *rgba;
    int w, h;
    uint32_t id;
    uint32_t age;
} image_t;

static image_t s_images[IMAGE_CACHE_LEN];
static uint32_t s_next_image_id = 1;
static uint32_t s_image_age = 0;

/* badges requested but not yet returned */
static char s_badge_pending[POPUP_QUEUE_LEN * 2][40];

/* ---- popups (main thread) ---- */

typedef struct {
    char title[96];
    char description[160];
    char key[40];
    int frames;
} popup_t;

static popup_t s_queue[POPUP_QUEUE_LEN];
static int s_q_head = 0, s_q_count = 0;
static bool s_cur_active = false;
static popup_t s_cur;
static uint32_t s_popup_serial = 0;
static bool s_dirty = false;

static char s_progress[64];
static int s_progress_frames = 0;
static uint32_t s_progress_serial = 0;

/* ------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------ */

static char *dupstr(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static void free_job(job_t *j)
{
    if (!j) return;
    free(j->url);
    free(j->post_data);
    free(j->body);
    free(j->rgba);
    free(j);
}

static void ensure_dirs(void)
{
    mkdir(RA_DIR, 0777);
    mkdir(RA_BADGE_DIR, 0777);
}

/* ------------------------------------------------------------------
 * Popups
 * ------------------------------------------------------------------ */

static void request_badge(const char *key, const char *url);
static void advance_popup(void);

static void push_popup(const char *title, const char *desc, const char *key, const char *image_url)
{
    if (s_q_count == POPUP_QUEUE_LEN) {
        /* drop the oldest queued popup */
        s_q_head = (s_q_head + 1) % POPUP_QUEUE_LEN;
        s_q_count--;
    }
    popup_t *p = &s_queue[(s_q_head + s_q_count) % POPUP_QUEUE_LEN];
    snprintf(p->title, sizeof(p->title), "%s", title ? title : "");
    snprintf(p->description, sizeof(p->description), "%s", desc ? desc : "");
    snprintf(p->key, sizeof(p->key), "%s", key ? key : "");
    p->frames = POPUP_FRAMES;
    s_q_count++;

    if (key && key[0] && image_url && image_url[0])
        request_badge(key, image_url);

    /* make it visible right away if nothing is showing */
    if (!s_cur_active) advance_popup();
}

static const image_t *find_image(const char *key)
{
    if (!key || !key[0]) return NULL;
    for (int i = 0; i < IMAGE_CACHE_LEN; i++)
        if (s_images[i].rgba && strcmp(s_images[i].key, key) == 0)
            return &s_images[i];
    return NULL;
}

static void store_image(const char *key, uint8_t *rgba, int w, int h)
{
    image_t *slot = NULL;
    for (int i = 0; i < IMAGE_CACHE_LEN && !slot; i++)
        if (!s_images[i].rgba) slot = &s_images[i];
    if (!slot) {
        /* evict the oldest image that is not on screen */
        for (int i = 0; i < IMAGE_CACHE_LEN; i++) {
            if (s_cur_active && strcmp(s_images[i].key, s_cur.key) == 0) continue;
            if (!slot || s_images[i].age < slot->age) slot = &s_images[i];
        }
        if (!slot) { free(rgba); return; }
        free(slot->rgba);
    }
    snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->rgba = rgba;
    slot->w = w;
    slot->h = h;
    slot->id = s_next_image_id++;
    slot->age = ++s_image_age;

    if (s_cur_active && strcmp(s_cur.key, key) == 0) s_dirty = true;
}

/* ------------------------------------------------------------------
 * HTTP (worker thread only)
 * ------------------------------------------------------------------ */

static void http_perform(const char *in_url, const char *post_data,
                         int *out_status, char **out_body, size_t *out_len)
{
    *out_status = 0;
    *out_body = NULL;
    *out_len = 0;

    /* The 3DS TLS stack cannot negotiate with retroachievements.org,
     * so requests are sent over plain HTTP. */
    char *url = NULL;
    if (strncmp(in_url, "https://", 8) == 0) {
        size_t n = strlen(in_url) + 1;
        url = (char *)malloc(n);
        if (!url) return;
        snprintf(url, n, "http://%s", in_url + 8);
    } else {
        url = dupstr(in_url);
        if (!url) return;
    }

    httpcContext ctx;
    Result ret = httpcOpenContext(&ctx, post_data ? HTTPC_METHOD_POST : HTTPC_METHOD_GET, url, 1);
    free(url);
    if (R_FAILED(ret)) return;

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "Connection", "Keep-Alive");
    httpcAddRequestHeaderField(&ctx, "User-Agent", s_user_agent);

    if (post_data) {
        httpcAddRequestHeaderField(&ctx, "Content-Type", "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(&ctx, (const u32 *)post_data, (u32)strlen(post_data));
    }

    if (R_FAILED(httpcBeginRequest(&ctx))) {
        httpcCloseContext(&ctx);
        return;
    }

    u32 status = 0;
    if (R_FAILED(httpcGetResponseStatusCode(&ctx, &status))) {
        httpcCloseContext(&ctx);
        return;
    }

    size_t cap = 8192, len = 0;
    char *body = (char *)malloc(cap);
    if (!body) {
        httpcCloseContext(&ctx);
        return;
    }

    for (;;) {
        if (cap - len < 4097) {
            char *nb = (char *)realloc(body, cap * 2);
            if (!nb) break;
            body = nb;
            cap *= 2;
        }
        u32 got = 0;
        ret = httpcDownloadData(&ctx, (u8 *)body + len, (u32)(cap - len - 1), &got);
        len += got;
        if (ret != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) break;
    }
    httpcCloseContext(&ctx);

    body[len] = '\0';
    *out_status = (int)status;
    *out_body = body;
    *out_len = len;
}

static bool read_file(const char *path, char **out, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    char *buf = (char *)malloc(n);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return false; }
    fclose(f);
    *out = buf;
    *len = (size_t)n;
    return true;
}

static void badge_fetch(job_t *j)
{
    char path[128];
    snprintf(path, sizeof(path), RA_BADGE_DIR "/%s.png", j->key);

    char *png = NULL;
    size_t png_len = 0;

    if (!read_file(path, &png, &png_len)) {
        int status = 0;
        http_perform(j->url, NULL, &status, &png, &png_len);
        if (status != 200 || !png || png_len == 0) {
            free(png);
            return;
        }
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(png, 1, png_len, f);
            fclose(f);
        }
    }

    int w = 0, h = 0, n = 0;
    uint8_t *rgba = stbi_load_from_memory((const stbi_uc *)png, (int)png_len, &w, &h, &n, 4);
    free(png);
    if (!rgba) return;
    if (w <= 0 || h <= 0 || w > MAX_BADGE_DIM || h > MAX_BADGE_DIM) {
        stbi_image_free(rgba);
        return;
    }
    /* hand over a malloc'd copy so the main thread can free() it */
    size_t sz = (size_t)w * h * 4;
    j->rgba = (uint8_t *)malloc(sz);
    if (j->rgba) {
        memcpy(j->rgba, rgba, sz);
        j->w = w;
        j->h = h;
    }
    stbi_image_free(rgba);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        LightLock_Lock(&s_lock);
        bool quit = s_quit;
        job_t *j = s_pending_head;
        if (j && !quit) {
            s_pending_head = j->next;
            if (!s_pending_head) s_pending_tail = NULL;
            j->next = NULL;
        }
        LightLock_Unlock(&s_lock);

        if (quit) break;
        if (!j) {
            LightEvent_Wait(&s_wake);
            continue;
        }

        if (j->kind == JOB_API)
            http_perform(j->url, j->post_data, &j->http_status, &j->body, &j->body_len);
        else
            badge_fetch(j);

        LightLock_Lock(&s_lock);
        if (s_done_tail) s_done_tail->next = j; else s_done_head = j;
        s_done_tail = j;
        LightLock_Unlock(&s_lock);
    }
}

static void enqueue(job_t *j)
{
    LightLock_Lock(&s_lock);
    if (s_pending_tail) s_pending_tail->next = j; else s_pending_head = j;
    s_pending_tail = j;
    LightLock_Unlock(&s_lock);
    LightEvent_Signal(&s_wake);
}

/* main thread: dispatch finished jobs */
static void pump(void)
{
    LightLock_Lock(&s_lock);
    job_t *list = s_done_head;
    s_done_head = s_done_tail = NULL;
    LightLock_Unlock(&s_lock);

    while (list) {
        job_t *j = list;
        list = j->next;

        if (j->kind == JOB_API) {
            rc_api_server_response_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.http_status_code = j->http_status;
            resp.body = j->body;
            resp.body_length = j->body_len;
            if (j->http_status == 0) {
                // timeout / no Wi-Fi: let rc_client queue a retry
                resp.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
                resp.body = "Network error";
                resp.body_length = strlen(resp.body);
            }
            j->callback(&resp, j->callback_data);
        } else {
            for (int i = 0; i < (int)(sizeof(s_badge_pending) / sizeof(s_badge_pending[0])); i++)
                if (strcmp(s_badge_pending[i], j->key) == 0) s_badge_pending[i][0] = 0;
            if (j->rgba) {
                store_image(j->key, j->rgba, j->w, j->h);
                j->rgba = NULL; /* ownership moved */
            }
        }
        free_job(j);
    }
}

static void request_badge(const char *key, const char *url)
{
    if (find_image(key)) return;
    int free_slot = -1;
    for (int i = 0; i < (int)(sizeof(s_badge_pending) / sizeof(s_badge_pending[0])); i++) {
        if (strcmp(s_badge_pending[i], key) == 0) return;
        if (!s_badge_pending[i][0] && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return;

    job_t *j = (job_t *)calloc(1, sizeof(job_t));
    if (!j) return;
    j->kind = JOB_BADGE;
    j->url = dupstr(url);
    snprintf(j->key, sizeof(j->key), "%s", key);
    if (!j->url) { free_job(j); return; }
    snprintf(s_badge_pending[free_slot], sizeof(s_badge_pending[0]), "%s", key);
    enqueue(j);
}

/* rc_client server callback: runs on the main thread, must not block */
static void ra_server_call(const rc_api_request_t *request,
                           rc_client_server_callback_t callback,
                           void *callback_data, rc_client_t *client)
{
    (void)client;
    job_t *j = (job_t *)calloc(1, sizeof(job_t));
    if (j) {
        j->kind = JOB_API;
        j->url = dupstr(request->url);
        j->post_data = dupstr(request->post_data);
        j->callback = callback;
        j->callback_data = callback_data;
    }
    if (!j || !j->url || (request->post_data && !j->post_data) || !s_thread) {
        free_job(j);
        rc_api_server_response_t resp;
        memset(&resp, 0, sizeof(resp));
        callback(&resp, callback_data);
        return;
    }
    enqueue(j);
}

/* ------------------------------------------------------------------
 * Memory
 *
 * Uses rcheevos' own Virtual Boy memory map (rc_console_memory_regions)
 * and translates each region's real V810 address into Red Viper's
 * memory, applying the same mirroring masks the emulator uses.
 * Always reads the local player's state so multiplayer evaluates
 * achievements for this console.
 * ------------------------------------------------------------------ */

static uint32_t read_real(uint32_t real, uint8_t *out, uint32_t n)
{
    VB_STATE *st = &vb_players[my_player_id];
    const V810_MEMORYFETCH *m;
    switch (real & 0x07000000) {
        case 0x05000000: m = &st->V810_VB_RAM; break;
        case 0x06000000: m = &st->V810_GAME_RAM; break;
        default: return 0;
    }
    if (!m->pmemory || m->size == 0) return 0;
    uint32_t mask = m->size - 1; /* region sizes are powers of two */
    for (uint32_t i = 0; i < n; i++)
        out[i] = m->pmemory[(real + i) & mask];
    return n;
}

static uint32_t ra_read_memory(uint32_t address, uint8_t *buffer,
                               uint32_t num_bytes, rc_client_t *client)
{
    (void)client;
    if (!s_regions) return 0;

    uint32_t done = 0;
    while (done < num_bytes) {
        uint32_t a = address + done;
        const rc_memory_region_t *r = NULL;
        for (uint32_t i = 0; i < s_regions->num_regions; i++) {
            if (a >= s_regions->region[i].start_address && a <= s_regions->region[i].end_address) {
                r = &s_regions->region[i];
                break;
            }
        }
        if (!r) break;
        uint32_t chunk = r->end_address - a + 1;
        if (chunk > num_bytes - done) chunk = num_bytes - done;
        uint32_t got = read_real(r->real_address + (a - r->start_address), buffer + done, chunk);
        done += got;
        if (got < chunk) break;
    }
    return done;
}

/* ------------------------------------------------------------------
 * Credentials (username + API token, never the password)
 * ------------------------------------------------------------------ */

static void save_credentials(const char *username, const char *token)
{
    ensure_dirs();
    FILE *f = fopen(RA_CREDENTIALS_PATH, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", username, token);
        fclose(f);
    }
}

static bool load_credentials(char *user, size_t usize, char *token, size_t tsize)
{
    FILE *f = fopen(RA_CREDENTIALS_PATH, "r");
    if (!f) return false;
    bool ok = fgets(user, (int)usize, f) && fgets(token, (int)tsize, f);
    fclose(f);
    if (!ok) return false;
    user[strcspn(user, "\r\n")] = 0;
    token[strcspn(token, "\r\n")] = 0;
    return user[0] && token[0];
}

/* ------------------------------------------------------------------
 * Callbacks (main thread)
 * ------------------------------------------------------------------ */

static void game_popup(const char *title)
{
    const rc_client_game_t *game = rc_client_get_game_info(s_client);
    char desc[160] = "";
    char url[256] = "";
    char key[40] = "";
    if (game) {
        rc_client_user_game_summary_t sum;
        memset(&sum, 0, sizeof(sum));
        rc_client_get_user_game_summary(s_client, &sum);
        snprintf(desc, sizeof(desc), "%.100s - %u/%u", game->title,
                 (unsigned)sum.num_unlocked_achievements, (unsigned)sum.num_core_achievements);
        if (rc_client_game_get_image_url(game, url, sizeof(url)) == RC_OK)
            snprintf(key, sizeof(key), "g%s", game->badge_name);
    }
    push_popup(title, desc, key, url);
}

static void event_handler(const rc_client_event_t *event, rc_client_t *client)
{
    (void)client;
    char buf[160];
    char url[256];

    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
        const rc_client_achievement_t *a = event->achievement;
        snprintf(buf, sizeof(buf), "%.120s (%u)", a->title, (unsigned)a->points);
        url[0] = 0;
        rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url));
        push_popup("Achievement Unlocked", buf, a->badge_name, url);
        break;
    }
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        game_popup(ra_get_hardcore_enabled() ? "Game Mastered!" : "Game Completed!");
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        push_popup("Leaderboard Attempt Started", event->leaderboard->title, NULL, NULL);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        push_popup("Leaderboard Attempt Failed", event->leaderboard->title, NULL, NULL);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        snprintf(buf, sizeof(buf), "%.100s: %s", event->leaderboard->title, event->leaderboard->tracker_value);
        push_popup("Leaderboard Submitted", buf, NULL, NULL);
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        snprintf(s_progress, sizeof(s_progress), "%.40s %s",
                 event->achievement->title, event->achievement->measured_progress);
        s_progress_frames = PROGRESS_FRAMES;
        s_progress_serial++;
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        if (s_progress_frames) s_dirty = true;
        s_progress_frames = 0;
        break;

    case RC_CLIENT_EVENT_RESET:
        s_reset_requested = true;
        break;

    case RC_CLIENT_EVENT_SERVER_ERROR:
        push_popup("RetroAchievements Error",
                   event->server_error ? event->server_error->error_message : "Unknown error", NULL, NULL);
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        push_popup("RetroAchievements", "Connection lost. Unlocks will be retried.", NULL, NULL);
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        push_popup("RetroAchievements", "Reconnected. Pending unlocks submitted.", NULL, NULL);
        break;

    default:
        break;
    }
}

static void log_callback(const char *message, const rc_client_t *client)
{
    (void)client;
    dprintf(1, "[RA] %s\n", message);
}

static void login_callback(int result, const char *error_message, rc_client_t *client, void *userdata)
{
    (void)userdata;
    if (result == RC_OK) {
        const rc_client_user_t *user = rc_client_get_user_info(client);
        if (user && user->token) save_credentials(user->username, user->token);
        char buf[160];
        snprintf(buf, sizeof(buf), "Logged in as %s", user ? user->display_name : "?");
        push_popup("RetroAchievements", buf, NULL, NULL);
        /* game was started before login finished */
        if (game_running && !rc_client_get_game_info(client)) ra_load_game();
    } else {
        push_popup("RA Login Failed", error_message ? error_message : "Unknown error", NULL, NULL);
    }
}

static void load_game_callback(int result, const char *error_message, rc_client_t *client, void *userdata)
{
    (void)client; (void)userdata;
    if (result == RC_OK) {
        game_popup(ra_get_hardcore_enabled() ? "Hardcore Mode" : "Softcore Mode");
    } else if (result == RC_NO_GAME_LOADED) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Unknown game. Hash %s", s_rom_hash);
        push_popup("No Achievements", buf, NULL, NULL);
    } else if (result != RC_ABORTED) {
        push_popup("RA Load Error", error_message ? error_message : "Unknown error", NULL, NULL);
    }
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

void ra_init(void)
{
    LightLock_Init(&s_lock);
    LightEvent_Init(&s_wake, RESET_ONESHOT);

    if (R_FAILED(httpcInit(0x1000))) return;

    s_regions = rc_console_memory_regions(RC_CONSOLE_VIRTUAL_BOY);

    s_client = rc_client_create(ra_read_memory, ra_server_call);
    if (!s_client) return;

    {
        int n = snprintf(s_user_agent, sizeof(s_user_agent), "RedViper/%s (Nintendo 3DS) ", RA_UA_VERSION);
        if (n > 0 && (size_t)n < sizeof(s_user_agent))
            rc_client_get_user_agent_clause(s_client, s_user_agent + n, sizeof(s_user_agent) - n);
    }

    s_quit = false;
    s_thread = threadCreate(worker, NULL, 0x8000, 0x19, 1, false);
    if (!s_thread) s_thread = threadCreate(worker, NULL, 0x8000, 0x19, 0, false);

    rc_client_enable_logging(s_client, RC_CLIENT_LOG_LEVEL_INFO, log_callback);
    rc_client_set_event_handler(s_client, event_handler);

    /* Hardcore on by default, never under an emulator */
    rc_client_set_hardcore_enabled(s_client, ra_hardcore_available() ? 1 : 0);

    char user[128], token[128];
    if (load_credentials(user, sizeof(user), token, sizeof(token)))
        rc_client_begin_login_with_token(s_client, user, token, login_callback, NULL);
}

void ra_shutdown(void)
{
    if (s_thread) {
        LightLock_Lock(&s_lock);
        s_quit = true;
        LightLock_Unlock(&s_lock);
        LightEvent_Signal(&s_wake);
        threadJoin(s_thread, 3000000000ULL);
        threadFree(s_thread);
        s_thread = NULL;
    }
    /* drop anything still queued; the client is about to go away */
    job_t *lists[2] = { s_pending_head, s_done_head };
    for (int i = 0; i < 2; i++) {
        while (lists[i]) { job_t *n = lists[i]->next; free_job(lists[i]); lists[i] = n; }
    }
    s_pending_head = s_pending_tail = s_done_head = s_done_tail = NULL;

    if (s_client) {
        rc_client_destroy(s_client);
        s_client = NULL;
    }
    for (int i = 0; i < IMAGE_CACHE_LEN; i++) { free(s_images[i].rgba); s_images[i].rgba = NULL; }
    httpcExit();
}

void ra_capture_rom_hash(void)
{
    s_rom_hash_valid = false;
    if (!V810_ROM1.pmemory || V810_ROM1.size == 0) return;
    char hash[33] = {0};
    if (rc_hash_generate_from_buffer(hash, RC_CONSOLE_VIRTUAL_BOY, V810_ROM1.pmemory, V810_ROM1.size)) {
        memcpy(s_rom_hash, hash, sizeof(hash));
        s_rom_hash_valid = true;
    }
}

void ra_load_game(void)
{
    if (!s_client || !ra_is_logged_in()) return;
    if (rc_client_get_game_info(s_client)) rc_client_unload_game(s_client);
    if (!s_rom_hash_valid) {
        push_popup("RetroAchievements", "Could not hash ROM.", NULL, NULL);
        return;
    }
    rc_client_begin_load_game(s_client, s_rom_hash, load_game_callback, NULL);
}

void ra_unload_game(void)
{
    if (s_client) rc_client_unload_game(s_client);
    s_progress_frames = 0;
}

void ra_reset(void)
{
    if (s_client) rc_client_reset(s_client);
}

bool ra_take_reset_request(void)
{
    bool r = s_reset_requested;
    s_reset_requested = false;
    return r;
}

void ra_do_frame(void)
{
    if (!s_client) return;
    pump();
    rc_client_do_frame(s_client);
}

void ra_idle(void)
{
    if (!s_client) return;
    pump();
    rc_client_idle(s_client);
}

static void advance_popup(void)
{
    if (s_cur_active) { s_cur_active = false; s_dirty = true; }
    if (s_q_count > 0) {
        s_cur = s_queue[s_q_head];
        s_q_head = (s_q_head + 1) % POPUP_QUEUE_LEN;
        s_q_count--;
        s_cur_active = true;
        s_popup_serial++;
        s_dirty = true;
    }
}

void ra_tick_popup(void)
{
    if (s_progress_frames > 0 && --s_progress_frames == 0) s_dirty = true;
    if (s_cur_active && --s_cur.frames > 0) return;
    advance_popup();
}

void ra_login_with_password(const char *username, const char *password)
{
    if (s_client)
        rc_client_begin_login_with_password(s_client, username, password, login_callback, NULL);
}

void ra_logout(void)
{
    if (s_client) rc_client_logout(s_client);
    remove(RA_CREDENTIALS_PATH);
}

bool ra_is_logged_in(void)
{
    return s_client && rc_client_get_user_info(s_client) != NULL;
}

bool ra_hardcore_available(void)
{
    return !is_citra;
}

void ra_set_hardcore_enabled(bool enabled)
{
    if (!s_client) return;
    if (enabled && !ra_hardcore_available()) return;
    /* With a game loaded, enabling hardcore raises RC_CLIENT_EVENT_RESET */
    rc_client_set_hardcore_enabled(s_client, enabled ? 1 : 0);
}

bool ra_get_hardcore_enabled(void)
{
    return s_client && rc_client_get_hardcore_enabled(s_client);
}

bool ra_allow_load_state(void)
{
    return !ra_get_hardcore_enabled();
}

bool ra_has_game(void)
{
    return s_client && rc_client_get_game_info(s_client) != NULL;
}

bool ra_overlay_active(void)
{
    return s_cur_active || s_progress_frames > 0;
}

bool ra_get_popup(ra_popup_t *out)
{
    if (!s_cur_active) return false;
    memcpy(out->title, s_cur.title, sizeof(out->title));
    memcpy(out->description, s_cur.description, sizeof(out->description));
    out->serial = s_popup_serial;
    const image_t *img = find_image(s_cur.key);
    if (img) {
        out->badge_rgba = img->rgba;
        out->badge_w = img->w;
        out->badge_h = img->h;
        out->badge_id = img->id;
    } else {
        out->badge_rgba = NULL;
        out->badge_w = out->badge_h = 0;
        out->badge_id = 0;
    }
    return true;
}

bool ra_get_progress(char *out, int out_size, uint32_t *serial)
{
    if (s_progress_frames <= 0) return false;
    snprintf(out, out_size, "%s", s_progress);
    if (serial) *serial = s_progress_serial;
    return true;
}

bool ra_overlay_take_dirty(void)
{
    bool d = s_dirty;
    s_dirty = false;
    return d;
}

void ra_state_loaded(void)
{
    // our savestates don't contain achievement progress, so reset it
    if (s_client) rc_client_deserialize_progress(s_client, NULL);
}

bool ra_can_pause(void)
{
    return !s_client || rc_client_can_pause(s_client, NULL);
}