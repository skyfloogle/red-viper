#include "multiplayer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "crc8.h"

// #define NET_LOGGING

static udsNetworkStruct network;
static udsBindContext bindctx;
static const u32 wlancommID = 0x2d23bfdb;
static u8 net_id = 0;
static u8 data_channel = 1;
static const char passphrase[] = "Red Viper " VERSION;
static udsNetworkScanInfo *beacons;
#define RECV_HEAP_COUNT 8
static Packet recv_heap[RECV_HEAP_COUNT] = {0};
static volatile u8 recv_id;
static volatile u8 recv_ack_id;
#define SEND_QUEUE_COUNT 8
static Packet send_queue[SEND_QUEUE_COUNT] = {0};
static volatile u8 sent_id;
static volatile u8 shippable_packet;

#define TRANSPORT_MASK 0x80

static bool events_created = false;
static Handle end_event, send_event;
static Thread send_thread_handle, recv_thread_handle;

FILE *logfile;
#ifdef NET_LOGGING
#define NET_LOG(...) fprintf(logfile, __VA_ARGS__)
#else
#define NET_LOG(...)
#endif

static Result handle_receiving(void);
static void handle_sending(void);

static void recv_thread(void *args) {
    Handle handles[2] = {end_event, bindctx.event};
    s32 event_id;
    while (true) {
        Result res = svcWaitSynchronizationN(&event_id, handles, 2, false, U64_MAX);
        if (R_SUCCEEDED(res)) {
            NET_LOG("recv event %ld triggered\n", event_id);
            if (event_id == 0) {
                break;
            }
            svcClearEvent(bindctx.event);
            handle_receiving();
        } else {
            NET_LOG("recv wait error %lx\n", res);
        }
    }
    NET_LOG("recv thread ending\n");
}

static void send_thread(void *args) {
    Handle handles[2] = {end_event, send_event};
    s32 event_id;
    while (true) {
        Result res = svcWaitSynchronizationN(&event_id, handles, 2, false, 10000000);
        if (R_SUCCEEDED(res)) {
            if (event_id == 0) {
                break;
            }
            svcClearEvent(send_event);
            handle_sending();
        }
    }
}

static void init_ids(void) {
    recv_id = 0;
    recv_ack_id = UINT8_MAX;
    sent_id = 0;
    shippable_packet = UINT8_MAX;
    for (int i = 0; i < RECV_HEAP_COUNT; i++) {
        recv_heap[i].packet_type = PACKET_NULL;
        recv_heap[i].packet_id = UINT8_MAX;
    }
    for (int i = 0; i < SEND_QUEUE_COUNT; i++) {
        send_queue[i].packet_type = PACKET_NULL;
        recv_heap[i].packet_id = UINT8_MAX;
    }
    if (!events_created) {
        svcCreateEvent(&end_event, RESET_STICKY);
        svcCreateEvent(&send_event, RESET_STICKY);
    }
    svcClearEvent(end_event);
    svcClearEvent(send_event);
    recv_thread_handle = threadCreate(recv_thread, NULL, 4000, 0x19, 1, true);
    send_thread_handle = threadCreate(send_thread, NULL, 4000, 0x19, 1, true);
}

Result create_network(void) {
    #ifdef NET_LOGGING
    logfile = fopen("sdmc:/uds.log", "w");
    #endif
    NET_LOG("creating network\n");
    udsGenerateDefaultNetworkStruct(&network, wlancommID, net_id, 2);
    Result res = udsCreateNetwork(&network, passphrase, sizeof(passphrase), &bindctx, data_channel, UDS_DEFAULT_RECVBUFSIZE);
    if (R_SUCCEEDED(res)) {
        init_ids();
    }
    return res;
}

Result scan_beacons(udsNetworkScanInfo **networks, size_t *total_networks) {
    static u8 tmpbuf[0x4000];
    return udsScanBeacons(tmpbuf, sizeof(tmpbuf), networks, total_networks, wlancommID, net_id, NULL, false);
}

Result connect_to_network(const udsNetworkStruct *network) {
    #ifdef NET_LOGGING
    logfile = fopen("sdmc:/uds.log", "w");
    #endif
    NET_LOG("connecting\n");
    Result res = udsConnectNetwork(network, passphrase, sizeof(passphrase), &bindctx, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, data_channel, UDS_DEFAULT_RECVBUFSIZE);
    if (R_SUCCEEDED(res)) {
        init_ids();
    }
    return res;
}

void local_disconnect(void) {
    static udsConnectionStatus status;
    svcSignalEvent(end_event);
    udsGetConnectionStatus(&status);
    threadJoin(send_thread_handle, 100000000);
    threadJoin(recv_thread_handle, 100000000);
    if (status.cur_NetworkNodeID == UDS_HOST_NETWORKNODEID) {
        udsDestroyNetwork();
    } else {
        udsDisconnectNetwork();
    }
    udsUnbind(&bindctx);
    #ifdef NET_LOGGING
    NET_LOG("closed\n");
    fclose(logfile);
    #endif
}

static size_t packet_size(const Packet *packet) {
    switch ((PacketType)(packet->packet_type & ~TRANSPORT_MASK)) {
        case PACKET_NULL:
        case PACKET_NOP:
        case PACKET_PAUSE:
        case PACKET_RESUME:
            return PACKET_HEADER_SIZE;
        case PACKET_INPUTS:
            return PACKET_HEADER_SIZE + sizeof(packet->inputs);
        case PACKET_LOADED:
            return PACKET_HEADER_SIZE + sizeof(packet->loaded);
        case PACKET_DATA:
            return PACKET_HEADER_SIZE + sizeof(packet->data);
    }
    return 0;
}

static u8 packet_crc(const Packet *packet) {
    return crc8(((u8*)packet) + 1, packet_size(packet) - 1);
}

static Result handle_receiving(void) {
    Packet *packet = recv_heap;
    Result res = 0;
    while (packet < recv_heap + RECV_HEAP_COUNT) {
        if (packet->packet_type != PACKET_NULL && (s8)(packet->packet_id - recv_id) >= 0) {
            NET_LOG("unrecvd packet %d in buffer (next %d)\n", packet->packet_id, recv_id);
            packet++;
            continue;
        }
        NET_LOG("overwriting packet %d\n", packet->packet_id);
        size_t actual_size;
        res = udsPullPacket(&bindctx, packet, sizeof(*packet), &actual_size, NULL);
        if (R_FAILED(res)) {
            NET_LOG("recv error %ld\n", res);
            packet->packet_type = PACKET_NULL;
            break;
        }
        if (actual_size == 0) {
            // no data
            NET_LOG("recv no data\n");
            packet->packet_type = PACKET_NULL;
            break;
        }
        if (actual_size < PACKET_HEADER_SIZE || actual_size != packet_size(packet)) {
            NET_LOG("recv packet looks too small\n");
            packet->packet_type = PACKET_NULL;
            continue;
        }
        if (packet_crc(packet) != packet->crc8) {
            NET_LOG("recv checksum doesn't match\n");
            packet->packet_type = PACKET_NULL;
            continue;
        }
        // valid packet
        // ack the ack
        if ((s8)(packet->ack_id - sent_id) >= 0 && send_queue[packet->ack_id % SEND_QUEUE_COUNT].packet_id == packet->ack_id) {
            send_queue[packet->ack_id % SEND_QUEUE_COUNT].packet_type = PACKET_NULL;
            sent_id = packet->ack_id + 1;
        }
        // check if the packet is old
        if ((s8)(packet->packet_id - recv_ack_id) <= 0) {
            NET_LOG("recv'd old packet %d acking %d\n", packet->packet_id, packet->ack_id);
            continue;
        }
        // check for duplicates
        for (int i = 0; i < RECV_HEAP_COUNT; i++) {
            if (&recv_heap[i] != packet && recv_heap[i].packet_type != PACKET_NULL && recv_heap[i].packet_id == packet->packet_id) {
                NET_LOG("recv'd a duplicate %d acking %d\n", packet->packet_id, packet->ack_id);
                packet->packet_type = PACKET_NULL;
                continue;
            }
        }
        NET_LOG("recv valid packet with id %d type %d size %d content %d,%d (acking %d)\n", packet->packet_id, packet->packet_type, actual_size, packet->inputs.shb, packet->inputs.slb, packet->ack_id);
        // if it happens to be the next packet we're looking for, ack it
        if ((u8)(recv_ack_id + 1) == packet->packet_id) {
            recv_ack_id++;
        }
        // clear for reception and continue
        packet->packet_type &= ~TRANSPORT_MASK;
        packet++;
    }
    return res;
}

static void handle_sending(void) {
    Packet *packet;
    for (int i = sent_id; i < sent_id + 8; i++) {
        packet = &send_queue[i % SEND_QUEUE_COUNT];
        if (packet->packet_type == PACKET_NULL || (s8)(packet->packet_id - sent_id) < 0) {
            NET_LOG("send breaking on type %d id %d sent_id %d\n", packet->packet_type, packet->packet_id, sent_id);
            break;
        }
        if ((s8)(packet->packet_id - shippable_packet) > 0) {
            NET_LOG("packet %d not ready for shipping (shippable %d, diff %d)\n", packet->packet_id, shippable_packet, (s8)(packet->packet_id - shippable_packet));
            break;
        }
        packet->ack_id = recv_ack_id;
        packet->crc8 = packet_crc(packet);
        NET_LOG("sending packet %d with type %d size %d content %d,%d (acking %d)\n", packet->packet_id, packet->packet_type, packet_size(packet), packet->inputs.shb, packet->inputs.slb, packet->ack_id);
        udsSendTo(UDS_BROADCAST_NETWORKNODEID, data_channel, UDS_SENDFLAG_Default, packet, packet_size(packet));
    }
}

static Packet *try_find_next_packet(void) {
    for (int i = 0; i < RECV_HEAP_COUNT; i++) {
        if (recv_heap[i].packet_type != PACKET_NULL && !(recv_heap[i].packet_type & TRANSPORT_MASK) && recv_heap[i].packet_id == recv_id) {
            return &recv_heap[i];
            break;
        }
    }
    return NULL;
}

Packet *read_next_packet(void) {
    Packet *out = try_find_next_packet();
    if (out == NULL) {
        svcSleepThread(0);
        out = try_find_next_packet();
    }
    if (out) {
        recv_id++;
        // in case there was a skip, ack here as well
        if ((u8)(recv_ack_id + 1) == out->packet_id) {
            recv_ack_id++;
        }
        NET_LOG("properly recv %d (acking %d)\n", out->packet_id, out->ack_id);
    } else {
        NET_LOG("properly recv nothing\n");
    }
    return out;
}

Packet *new_packet_to_send(void) {
    Packet *packet = &send_queue[(shippable_packet + 1) % SEND_QUEUE_COUNT];
    if (packet->packet_type != PACKET_NULL && (s8)(sent_id - packet->packet_id) <= 0) {
        NET_LOG("send buffer full (new id %d sent_id %d packet %d)\n", shippable_packet + 1, sent_id, packet->packet_id);
        return NULL;
    }
    packet->packet_type = PACKET_NOP;
    packet->packet_id = shippable_packet + 1;
    NET_LOG("attempting to send %d (sent_id %d)\n", packet->packet_id, sent_id);
    svcSignalEvent(send_event);
    return packet;
}

void ship_packet(Packet *packet) {
    if (packet == NULL) return;
    shippable_packet = packet->packet_id;
    packet->packet_type |= TRANSPORT_MASK;
    NET_LOG("shipping packet %d\n", shippable_packet);
}
