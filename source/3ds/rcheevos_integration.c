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
#error "RA_UA_VERSION must be defined by the Makefile"
#endif

/* ------------------------------------------------------------------
 * Config
 * ------------------------------------------------------------------ */

#define RA_DIR              "sdmc:/red-viper"
#define RA_CREDENTIALS_PATH RA_DIR "/ra_login.bin"
#define RA_SETTINGS_PATH    RA_DIR "/ra_settings.txt"
#define RA_BADGE_DIR        RA_DIR "/ra_badges"

#define FPS                 50
#define POPUP_FRAMES        (FPS * 4)
#define PROGRESS_FRAMES     (FPS * 2)
#define POPUP_QUEUE_LEN     8
#define IMAGE_CACHE_LEN     48
#define BADGE_PENDING_LEN   48
#define MAX_BADGE_DIM       128
/* minimum unpaused time between pauses in hardcore, on top of rc_client_can_pause */
#define MIN_UNPAUSED_FRAMES (FPS * 1)
#define MAX_PROGRESS_SIZE   (1024 * 1024)

/* ------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------ */

static rc_client_t *s_client = NULL;
static const rc_memory_regions_t *s_regions = NULL;

static char s_user_agent[160];
static char s_rom_hash[33];
static volatile bool s_rom_hash_valid = false;

static bool s_reset_requested = false;
static bool s_hardcore_pref = true;      /* what the user chose */
static bool s_hardcore_auto_off = false; /* disabled because the game has no achievements */
static uint32_t s_unpaused_frames = 0xFFFFFFFF;

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
static char s_badge_pending[BADGE_PENDING_LEN][40];

/* ---- popups / HUD (main thread) ---- */

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

typedef struct { uint32_t id; char display[32]; } tracker_t;
typedef struct { uint32_t id; char key[40]; } challenge_t;
static tracker_t s_trackers[RA_MAX_TRACKERS];
static int s_num_trackers = 0;
static challenge_t s_challenges[RA_MAX_CHALLENGES];
static int s_num_challenges = 0;
static uint32_t s_hud_serial = 0;

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

static void set_progress_text(const char *text)
{
    snprintf(s_progress, sizeof(s_progress), "%s", text);
    s_progress_frames = PROGRESS_FRAMES;
    s_progress_serial++;
}

/* ------------------------------------------------------------------
 * Popups
 * ------------------------------------------------------------------ */

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
        ra_request_badge(key, image_url);

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

static bool image_in_use(const char *key)
{
    if (s_cur_active && strcmp(s_cur.key, key) == 0) return true;
    for (int i = 0; i < s_num_challenges; i++)
        if (strcmp(s_challenges[i].key, key) == 0) return true;
    return false;
}

static void store_image(const char *key, uint8_t *rgba, int w, int h)
{
    image_t *slot = NULL;
    for (int i = 0; i < IMAGE_CACHE_LEN && !slot; i++)
        if (!s_images[i].rgba) slot = &s_images[i];
    if (!slot) {
        /* evict the least recently stored image that is not on screen */
        for (int i = 0; i < IMAGE_CACHE_LEN; i++) {
            if (image_in_use(s_images[i].key)) continue;
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

    if (image_in_use(key)) s_dirty = true;
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
    if (!rgba) {
        remove(path); /* corrupt cache entry */
        return;
    }
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
                /* timeout / no Wi-Fi: let rc_client queue a retry */
                resp.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
                resp.body = "Network error";
                resp.body_length = strlen(resp.body);
            }
            j->callback(&resp, j->callback_data);
        } else {
            for (int i = 0; i < BADGE_PENDING_LEN; i++)
                if (strcmp(s_badge_pending[i], j->key) == 0) s_badge_pending[i][0] = 0;
            if (j->rgba) {
                store_image(j->key, j->rgba, j->w, j->h);
                j->rgba = NULL; /* ownership moved */
            }
        }
        free_job(j);
    }
}

void ra_request_badge(const char *key, const char *url)
{
    if (!key || !key[0] || !url || !url[0] || !s_thread) return;
    if (find_image(key)) return;
    int free_slot = -1;
    for (int i = 0; i < BADGE_PENDING_LEN; i++) {
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

bool ra_find_badge(const char *key, const uint8_t **rgba, int *w, int *h, uint32_t *id)
{
    const image_t *img = find_image(key);
    if (!img) return false;
    *rgba = img->rgba;
    *w = img->w;
    *h = img->h;
    *id = img->id;
    return true;
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
        resp.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        resp.body = "Out of memory";
        resp.body_length = strlen(resp.body);
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
 * Persistence (username + API token, never the password)
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

static void save_settings(void)
{
    ensure_dirs();
    FILE *f = fopen(RA_SETTINGS_PATH, "w");
    if (f) {
        fprintf(f, "hardcore=%d\n", s_hardcore_pref ? 1 : 0);
        fclose(f);
    }
}

static void load_settings(void)
{
    FILE *f = fopen(RA_SETTINGS_PATH, "r");
    if (!f) return; /* default: hardcore on */
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "hardcore=", 9) == 0) s_hardcore_pref = atoi(line + 9) != 0;
    }
    fclose(f);
}

/* hardcore as it should be with no game loaded */
static void apply_hardcore_pref(void)
{
    s_hardcore_auto_off = false;
    if (s_client)
        rc_client_set_hardcore_enabled(s_client, (s_hardcore_pref && ra_hardcore_available()) ? 1 : 0);
}

/* ------------------------------------------------------------------
 * Callbacks (main thread)
 * ------------------------------------------------------------------ */

static void game_badge(char *key, size_t key_size, char *url, size_t url_size)
{
    const rc_client_game_t *game = rc_client_get_game_info(s_client);
    key[0] = url[0] = 0;
    if (game && rc_client_game_get_image_url(game, url, url_size) == RC_OK)
        snprintf(key, key_size, "g%s", game->badge_name);
}

static void game_placard(void)
{
    const rc_client_game_t *game = rc_client_get_game_info(s_client);
    if (!game) return;
    rc_client_user_game_summary_t sum;
    memset(&sum, 0, sizeof(sum));
    rc_client_get_user_game_summary(s_client, &sum);

    char title[96], desc[160], key[40], url[256];
    snprintf(title, sizeof(title), "%.70s", game->title);
    const char *mode = s_hardcore_auto_off ? "Hardcore off (no achievements)"
                     : ra_get_hardcore_enabled() ? "Hardcore" : "Softcore";
    if (sum.num_core_achievements == 0)
        snprintf(desc, sizeof(desc), "%s - This game has no achievements.", mode);
    else
        snprintf(desc, sizeof(desc), "%s - %u of %u unlocked", mode,
                 (unsigned)sum.num_unlocked_achievements, (unsigned)sum.num_core_achievements);
    game_badge(key, sizeof(key), url, sizeof(url));
    push_popup(title, desc, key, url);
}

static void mastery_popup(const char *what, const char *name)
{
    const rc_client_user_t *user = rc_client_get_user_info(s_client);
    char title[96], key[40], url[256];
    snprintf(title, sizeof(title), "%s %.70s", what, name);
    game_badge(key, sizeof(key), url, sizeof(url));
    push_popup(title, user ? user->display_name : "", key, url);
}

static void hud_changed(void)
{
    s_hud_serial++;
    s_dirty = true;
}

static void event_handler(const rc_client_event_t *event, rc_client_t *client)
{
    (void)client;
    char buf[160];
    char url[256];

    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
        const rc_client_achievement_t *a = event->achievement;
        snprintf(buf, sizeof(buf), "%.110s (%u)", a->title, (unsigned)a->points);
        url[0] = 0;
        rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url));
        push_popup(a->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL
                       ? "Unofficial Achievement Unlocked" : "Achievement Unlocked",
                   buf, a->badge_name, url);
        break;
    }
    case RC_CLIENT_EVENT_GAME_COMPLETED: {
        const rc_client_game_t *game = rc_client_get_game_info(s_client);
        mastery_popup(ra_get_hardcore_enabled() ? "Mastered" : "Completed", game ? game->title : "");
        break;
    }
    case RC_CLIENT_EVENT_SUBSET_COMPLETED:
        mastery_popup(ra_get_hardcore_enabled() ? "Mastered" : "Completed",
                      event->subset ? event->subset->title : "");
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        snprintf(buf, sizeof(buf), "%.70s - %.80s", event->leaderboard->title, event->leaderboard->description);
        push_popup("Leaderboard Attempt Started", buf, NULL, NULL);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        push_popup("Leaderboard Attempt Failed", event->leaderboard->title, NULL, NULL);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        snprintf(buf, sizeof(buf), "Submitted %.30s for %.100s", event->leaderboard->tracker_value, event->leaderboard->title);
        push_popup("Leaderboard", buf, NULL, NULL);
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
        if (s_num_trackers < RA_MAX_TRACKERS) {
            s_trackers[s_num_trackers].id = event->leaderboard_tracker->id;
            snprintf(s_trackers[s_num_trackers].display, sizeof(s_trackers[0].display), "%s", event->leaderboard_tracker->display);
            s_num_trackers++;
            hud_changed();
        }
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
        for (int i = 0; i < s_num_trackers; i++) {
            if (s_trackers[i].id == event->leaderboard_tracker->id) {
                snprintf(s_trackers[i].display, sizeof(s_trackers[0].display), "%s", event->leaderboard_tracker->display);
                s_hud_serial++;
            }
        }
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
        for (int i = 0; i < s_num_trackers; i++) {
            if (s_trackers[i].id == event->leaderboard_tracker->id) {
                memmove(&s_trackers[i], &s_trackers[i + 1], (s_num_trackers - i - 1) * sizeof(tracker_t));
                s_num_trackers--;
                hud_changed();
                break;
            }
        }
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
        if (s_num_challenges < RA_MAX_CHALLENGES) {
            const rc_client_achievement_t *a = event->achievement;
            s_challenges[s_num_challenges].id = a->id;
            snprintf(s_challenges[s_num_challenges].key, sizeof(s_challenges[0].key), "%s", a->badge_name);
            s_num_challenges++;
            url[0] = 0;
            rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url));
            ra_request_badge(a->badge_name, url);
            hud_changed();
        }
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
        for (int i = 0; i < s_num_challenges; i++) {
            if (s_challenges[i].id == event->achievement->id) {
                memmove(&s_challenges[i], &s_challenges[i + 1], (s_num_challenges - i - 1) * sizeof(challenge_t));
                s_num_challenges--;
                hud_changed();
                break;
            }
        }
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        snprintf(buf, sizeof(buf), "%.40s %s", event->achievement->title, event->achievement->measured_progress);
        set_progress_text(buf);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        if (s_progress_frames) s_dirty = true;
        s_progress_frames = 0;
        break;

    case RC_CLIENT_EVENT_RESET:
        s_reset_requested = true;
        break;

    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (event->server_error) {
            snprintf(buf, sizeof(buf), "%.40s: %.110s",
                     event->server_error->api ? event->server_error->api : "",
                     event->server_error->error_message ? event->server_error->error_message : "");
            push_popup("RetroAchievements Error", buf, NULL, NULL);
        }
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
        snprintf(buf, sizeof(buf), "Logged in as %s (%u points)",
                 user ? user->display_name : "?", user ? (unsigned)user->score : 0);
        push_popup("RetroAchievements", buf, NULL, NULL);
        /* game was started before login finished */
        if (game_running && !rc_client_get_game_info(client)) ra_load_game();
    } else {
        push_popup("RA Login Failed", error_message ? error_message : "Unknown error", NULL, NULL);
    }
}

static void load_game_callback(int result, const char *error_message, rc_client_t *client, void *userdata)
{
    (void)userdata;
    if (result == RC_OK) {
        /* recommended: no hardcore restrictions for games without RA content */
        if (ra_get_hardcore_enabled() && !rc_client_is_processing_required(client)) {
            rc_client_set_hardcore_enabled(client, 0);
            s_hardcore_auto_off = true;
        }
        game_placard();
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

    /* Hardcore on by default (remembered between sessions), never under an emulator */
    load_settings();
    apply_hardcore_pref();

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

static void clear_hud(void)
{
    if (s_num_trackers || s_num_challenges || s_progress_frames) s_dirty = true;
    s_num_trackers = 0;
    s_num_challenges = 0;
    s_progress_frames = 0;
    s_hud_serial++;
}

void ra_unload_game(void)
{
    if (s_client) {
        rc_client_unload_game(s_client);
        if (s_hardcore_auto_off) apply_hardcore_pref();
    }
    clear_hud();
}

void ra_load_game(void)
{
    if (!s_client || !ra_is_logged_in()) return;
    if (rc_client_get_game_info(s_client)) ra_unload_game();
    s_unpaused_frames = 0xFFFFFFFF;
    if (!s_rom_hash_valid) {
        push_popup("RetroAchievements", "Could not hash ROM.", NULL, NULL);
        return;
    }
    rc_client_begin_load_game(s_client, s_rom_hash, load_game_callback, NULL);
}

void ra_reset(void)
{
    if (s_client) rc_client_reset(s_client);
    clear_hud();
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
    if (s_unpaused_frames != 0xFFFFFFFF) s_unpaused_frames++;
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
    if (s_client) {
        rc_client_logout(s_client);
        if (s_hardcore_auto_off) apply_hardcore_pref();
    }
    clear_hud();
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
    s_hardcore_pref = enabled;
    s_hardcore_auto_off = false;
    save_settings();
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

bool ra_can_pause(void)
{
    if (!s_client || !ra_get_hardcore_enabled() || !ra_has_game()) return true;

    uint32_t remaining = 0;
    bool ok;
    if (s_unpaused_frames < MIN_UNPAUSED_FRAMES) {
        ok = false;
        remaining = MIN_UNPAUSED_FRAMES - s_unpaused_frames;
    } else {
        ok = rc_client_can_pause(s_client, &remaining) != 0;
    }

    if (ok) {
        s_unpaused_frames = 0;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Hardcore: pause available in %.1fs", remaining / (float)FPS);
        set_progress_text(buf);
    }
    return ok;
}

/* ------------------------------------------------------------------
 * Savestates
 * ------------------------------------------------------------------ */

int ra_write_progress(FILE *f)
{
    if (!ra_has_game()) return 0;
    size_t size = rc_client_progress_size(s_client);
    if (size == 0 || size > MAX_PROGRESS_SIZE) return 0;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return 0;
    int ret = 0;
    if (rc_client_serialize_progress(s_client, buf) == RC_OK) {
        uint32_t size32 = (uint32_t)size;
        if (fwrite("RCHV", 1, 4, f) != 4 ||
            fwrite(&size32, 1, 4, f) != 4 ||
            fwrite(buf, 1, size, f) != size)
            ret = 1;
    }
    free(buf);
    return ret;
}

int ra_read_progress(FILE *f, void **out)
{
    *out = NULL;
    char marker[4];
    size_t n = fread(marker, 1, 4, f);
    if (n == 0 && feof(f)) return 0;   /* older state, no achievement data */
    if (n != 4 || memcmp(marker, "RCHV", 4) != 0) return 1;
    uint32_t size = 0;
    if (fread(&size, 1, 4, f) != 4 || size == 0 || size > MAX_PROGRESS_SIZE) return 1;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return 1;
    if (fread(buf, 1, size, f) != size) { free(buf); return 1; }
    *out = buf;
    return 0;
}

void ra_apply_progress(void *progress)
{
    /* NULL resets the runtime, as required when a state has no achievement data */
    if (ra_has_game())
        rc_client_deserialize_progress(s_client, (const uint8_t *)progress);
    free(progress);
    clear_hud();
}

/* ------------------------------------------------------------------
 * Overlay queries
 * ------------------------------------------------------------------ */

bool ra_overlay_active(void)
{
    return s_cur_active || s_progress_frames > 0 || s_num_trackers > 0 || s_num_challenges > 0;
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

bool ra_get_hud(ra_hud_t *out)
{
    if (s_num_trackers == 0 && s_num_challenges == 0) return false;
    out->num_trackers = s_num_trackers;
    for (int i = 0; i < s_num_trackers; i++)
        memcpy(out->trackers[i], s_trackers[i].display, sizeof(out->trackers[i]));
    out->num_challenges = s_num_challenges;
    for (int i = 0; i < s_num_challenges; i++)
        memcpy(out->challenge_keys[i], s_challenges[i].key, sizeof(out->challenge_keys[i]));
    out->serial = s_hud_serial;
    return true;
}

bool ra_overlay_take_dirty(void)
{
    bool d = s_dirty;
    s_dirty = false;
    return d;
}

/* ------------------------------------------------------------------
 * Achievement list
 * ------------------------------------------------------------------ */

int ra_list_build(ra_list_entry_t **out, char *summary, int summary_size)
{
    *out = NULL;
    if (summary_size > 0) summary[0] = 0;
    if (!ra_has_game()) return 0;

    const rc_client_game_t *game = rc_client_get_game_info(s_client);
    rc_client_user_game_summary_t sum;
    memset(&sum, 0, sizeof(sum));
    rc_client_get_user_game_summary(s_client, &sum);
    snprintf(summary, summary_size, "%.60s\n%u of %u unlocked - %s", game->title,
             (unsigned)sum.num_unlocked_achievements, (unsigned)sum.num_core_achievements,
             s_hardcore_auto_off ? "Hardcore off" : ra_get_hardcore_enabled() ? "Hardcore" : "Softcore");

    rc_client_achievement_list_t *list = rc_client_create_achievement_list(s_client,
        RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
    if (!list) return 0;

    int count = 0;
    for (uint32_t i = 0; i < list->num_buckets; i++)
        count += 1 + (int)list->buckets[i].num_achievements;

    ra_list_entry_t *e = count ? (ra_list_entry_t *)calloc(count, sizeof(ra_list_entry_t)) : NULL;
    if (!e) {
        rc_client_destroy_achievement_list(list);
        return 0;
    }

    int n = 0;
    for (uint32_t i = 0; i < list->num_buckets; i++) {
        e[n].is_header = true;
        snprintf(e[n].title, sizeof(e[n].title), "%s", list->buckets[i].label ? list->buckets[i].label : "");
        n++;
        for (uint32_t k = 0; k < list->buckets[i].num_achievements; k++) {
            const rc_client_achievement_t *a = list->buckets[i].achievements[k];
            ra_list_entry_t *x = &e[n++];
            x->unlocked = a->unlocked != 0;
            snprintf(x->title, sizeof(x->title), "%s (%u)", a->title, (unsigned)a->points);
            snprintf(x->description, sizeof(x->description), "%s", a->description ? a->description : "");
            if (a->state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED)
                snprintf(x->status, sizeof(x->status), "Unsupported");
            else if (x->unlocked)
                snprintf(x->status, sizeof(x->status), "Unlocked");
            else if (a->measured_percent > 0)
                snprintf(x->status, sizeof(x->status), "%s", a->measured_progress);
            else
                snprintf(x->status, sizeof(x->status), "Locked");
            if (x->unlocked) {
                snprintf(x->badge_key, sizeof(x->badge_key), "%s", a->badge_name);
                rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, x->badge_url, sizeof(x->badge_url));
            } else {
                snprintf(x->badge_key, sizeof(x->badge_key), "%s_lock", a->badge_name);
                rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE, x->badge_url, sizeof(x->badge_url));
            }
        }
    }

    rc_client_destroy_achievement_list(list);
    *out = e;
    return n;
}

void ra_list_free(ra_list_entry_t *entries)
{
    free(entries);
}
