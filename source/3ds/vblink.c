#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <string.h>
#include <zlib.h>
#include <3ds.h>
#include "v810_mem.h"
#include "vb_set.h"
#include "vblink.h"
#include "rom_db.h"
#include "patches.h"

volatile int vblink_progress = -1;
volatile int vblink_error = 0;
char vblink_fname[300];
Handle vblink_event;

static int listenfd = -1, datafd = -1;
static Thread thread;

static void vblink_thread(void*);

void vblink_init(void) {
    socInit(memalign(0x1000, 0x40000), 0x40000);
    svcCreateEvent(&vblink_event, RESET_STICKY);
}

int vblink_open(void) {
    vblink_error = 0;
    vblink_progress = -1;
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(22082),
    };
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) return errno;

    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(listenfd);
        listenfd = -1;
        return errno;
    }

    listen(listenfd, 1);

    // set nonblocking
    int flags = fcntl(listenfd, F_GETFL);
    if (flags == -1) return errno;
    if (fcntl(listenfd, F_SETFL, flags | O_NONBLOCK) == -1) return errno;
    
    datafd = -1;

    svcClearEvent(vblink_event);

    thread = threadCreate(vblink_thread, NULL, 4000, 0x20, 1, true);
    return 0;
}

void vblink_close(void) {
    if (listenfd >= 0) close(listenfd);
    if (datafd >= 0) close(datafd);
    listenfd = -1;
    datafd = -1;
    svcSignalEvent(vblink_event);
    threadJoin(thread, U64_MAX);
    thread = NULL;
}

int sendall(int sock, const void *addr, int size, int flags) {
    int remaining = size;
    while (remaining > 0) {
        int ret = send(sock, addr, remaining, flags);
        if (ret <= 0) {
            if (errno == EWOULDBLOCK) continue;
            return ret;
        }
        addr += ret;
        remaining -= ret;
    }
    return size;
}

int recvall(int sock, void *addr, int size, int flags) {
    int remaining = size;
    while (remaining > 0) {
        int ret = recv(sock, addr, remaining, flags);
        if (ret <= 0) {
            if (ret < 0 && errno == EWOULDBLOCK) continue;
            return ret;
        }
        addr += ret;
        remaining -= ret;
    }
    return size;
}

static void vblink_thread(void*) {
    int ok;
    while (true) {
        bool inflate_running = false;
        while (true) {
            datafd = accept(listenfd, NULL, NULL);
            if (datafd >= 0) {
                break;
            } else if (errno != EWOULDBLOCK) {
                vblink_error = errno;
                close(listenfd);
                listenfd = -1;
                svcClearEvent(vblink_event);
                thread = NULL;
                vblink_progress = -2;
                return;
            }
            svcSleepThread(100000000);
        }

        vblink_progress = 0;
        // get acknowledged by main thread
        svcWaitSynchronization(vblink_event, INT64_MAX);
        svcClearEvent(vblink_event);

        uint32_t size;

        FILE *f = NULL;

        // get filename length
        if (recvall(datafd, &size, 4, 0) != 4) {vblink_error = errno; goto conn_abort;}

        if (size == 0 || size >= 256) {
            // bad filename length
            ok = -1;
            goto conn_fail;
        }

        // get filename
        if (recvall(datafd, vblink_fname, size, 0) != size) {vblink_error = errno; goto conn_abort;}
        vblink_fname[size] = 0;

        char *ext = strrchr(vblink_fname, '.');
        if (strchr(vblink_fname, '/') || !ext || strcasecmp(ext, ".vb")) {
            // bad filename
            ok = -2;
            goto conn_fail;
        }

        // get rom size
        if (recvall(datafd, &size, 4, 0) != 4) {vblink_error = errno; goto conn_abort;};

        if (size > 0x1000000 || size < 0x10 || (size & (size - 1)) != 0) {
            // bad rom size
            ok = -3;
            goto conn_fail;
        }

        // looking good so far
        ok = 0;
        if (sendall(datafd, &ok, 4, 0) < 0) {vblink_error = errno; goto conn_abort;}

        // zlib setup
        static uint8_t in[65536];
        z_stream strm = {0};
        int ret;
        if ((ret = inflateInit(&strm)) != Z_OK) {vblink_error = ret; goto conn_abort;};
        inflate_running = true;
        uint8_t *out = V810_ROM1.pmemory;
        strm.next_out = out;
        strm.avail_out = 0x1000000;

        int total = 0;

        // file write setup
        char vblink_path[300];
        snprintf(vblink_path, sizeof(vblink_path), "%s/vblink/", tVBOpt.HOME_PATH);
        struct stat st;
        if (stat(vblink_path, &st) == -1) {
            if (mkdir(vblink_path, 0777)) goto after_file_open;
        }
        int path_len = strlen(vblink_path) + strlen(vblink_fname);
        if (path_len + 1 < sizeof(tVBOpt.ROM_PATH)) {
            strncat(vblink_path, vblink_fname, sizeof(vblink_path) - strlen(vblink_path) - 1);
            f = fopen(vblink_path, "wb");
        }

        after_file_open:

        while (true) {
            int new_progress = 100 * total / size;
            vblink_progress = new_progress < 99 ? new_progress : 99;

            // read chunk size
            if (recvall(datafd, &strm.avail_in, 4, 0) != 4) {vblink_error = errno; goto conn_abort;}
            if (strm.avail_in > 16384) {
                // bad chunk size
            }

            // read chunk
            if (recvall(datafd, in, strm.avail_in, 0) != strm.avail_in) {vblink_error = errno; goto conn_abort;}

            // decompress chunk
            strm.next_in = in;
            void *last_out = strm.next_out;
            int last_avail = strm.avail_out;
            int ret;
            while (strm.avail_in > 0) {
                ret = inflate(&strm, Z_NO_FLUSH);
                int count = last_avail - strm.avail_out;
                if (f != NULL) {
                    if (fwrite(last_out, 1, count, f) < count) {
                        fclose(f);
                        f = NULL;
                    }
                }
                total += count;
                if (total > size) {
                    // data too big
                    goto conn_abort;
                }
                if (ret == Z_STREAM_END) goto inflate_done;
                else if (ret) {
                    vblink_error = ret;
                    goto conn_abort;
                }
            }
        }
        inflate_done:
        inflateEnd(&strm);
        inflate_running = false;
        if (total != size) {
            // data too small
            goto conn_abort;
        }

        // all looks good, send the ok (it's fine if it fails)
        sendall(datafd, &ok, 4, 0);

        // final setup
        if (f) {
            strcpy(tVBOpt.ROM_PATH, vblink_path);
            strcpy(tVBOpt.RAM_PATH, tVBOpt.ROM_PATH);
            // we know there's a dot
            strcpy(strrchr(tVBOpt.RAM_PATH, '.'), ".ram");
        }

        V810_ROM1.highaddr = 0x7000000 + size - 1;
        is_sram = false;
        gen_table();
        tVBOpt.CRC32 = get_crc(size);
        memcpy(tVBOpt.GAME_ID, (char*)(V810_ROM1.off + (V810_ROM1.highaddr & 0xFFFFFDF9)), 6);
        apply_patches();
        v810_reset();

        vblink_progress = 100;

        // we don't need the network connection anymore
        close(datafd);
        datafd = -1;

        fclose(f);
        f = NULL;

        // get acknowledged by main thread
        svcWaitSynchronization(vblink_event, INT64_MAX);
        svcClearEvent(vblink_event);
        continue;

        conn_fail:
        sendall(datafd, &ok, 4, 0);
        conn_abort:
        close(datafd);
        datafd = -1;
        vblink_progress = -1;
        if (inflate_running) inflateEnd(&strm);
        if (f != NULL) {
            fclose(f);
            f = NULL;
        }
        // get acknowledged by main thread
        svcWaitSynchronization(vblink_event, INT64_MAX);
        svcClearEvent(vblink_event);
    }
}