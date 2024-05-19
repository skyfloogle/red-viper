#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <zlib.h>
#include <3ds.h>
#include "v810_mem.h"
#include "vb_set.h"
#include "vblink.h"
#include "rom_db.h"

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

    APT_SetAppCpuTimeLimit(30);
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
            if (errno == EWOULDBLOCK) continue;
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
                svcClearEvent(vblink_event);
                return;
            }
            svcSleepThread(100000000);
        }

        vblink_progress = 0;
        // get acknowledged by main thread
        svcWaitSynchronization(vblink_event, INT64_MAX);
        svcClearEvent(vblink_event);

        uint32_t size;

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
            int last_out = strm.avail_out;
            int ret;
            while (strm.avail_in > 0) {
                ret = inflate(&strm, Z_NO_FLUSH);
                total += last_out - strm.avail_out;
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
        V810_ROM1.highaddr = 0x7000000 + size - 1;
        is_sram = false;
        gen_table();
        tVBOpt.CRC32 = get_crc(size);
        v810_reset();

        vblink_progress = 100;
        // get acknowledged by main thread
        svcWaitSynchronization(vblink_event, INT64_MAX);
        svcClearEvent(vblink_event);

        // we don't need the network connection anymore
        close(datafd);
        datafd = -1;

        // save file
        int path_len = strlen(tVBOpt.HOME_PATH) + strlen("/vblink/") + strlen(vblink_fname);
        tVBOpt.ROM_PATH = realloc(tVBOpt.ROM_PATH, path_len);
        sprintf(tVBOpt.ROM_PATH, "%s/vblink/%s", tVBOpt.HOME_PATH, vblink_fname);
        tVBOpt.RAM_PATH = realloc(tVBOpt.RAM_PATH, path_len + 1);
        strcpy(tVBOpt.RAM_PATH, tVBOpt.ROM_PATH);
        // we know there's a dot
        strcpy(strrchr(tVBOpt.RAM_PATH, '.'), ".ram");

        FILE *f = fopen(tVBOpt.ROM_PATH, "wb");
        if (f == NULL) {
            goto write_fail;
        }

        if (fwrite(V810_ROM1.pmemory, 1, size, f) != size) {
            goto write_fail;
        }

        fclose(f);
        continue;

        write_fail:
        vblink_error = errno;
        if (f != NULL) fclose(f);
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
        // get acknowledged by main thread
        svcWaitSynchronization(vblink_event, INT64_MAX);
        svcClearEvent(vblink_event);
    }
}