#include "multiplayer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "crc8.h"

static udsNetworkStruct network;
static udsBindContext bindctx;
static const u32 wlancommID = 0x2d23bfdb;
static u8 net_id = 0;
static u8 data_channel = 1;
static const char passphrase[] = "Red Viper " VERSION;
static udsNetworkScanInfo *beacons;
#define RECV_HEAP_COUNT 8
static Packet recv_heap[RECV_HEAP_COUNT] = {0};
static u8 recv_id = 0; // TODO initialize somewhere
static u8 recv_ack_id = UINT8_MAX;
#define SEND_QUEUE_COUNT 8
static Packet send_queue[SEND_QUEUE_COUNT] = {0};
static u8 sent_id = 0;

FILE *logfile;
#ifdef NET_LOGGING
#define NET_LOG(...) fprintf(logfile, __VA_ARGS__)
#else
#define NET_LOG(...)
#endif

static void init_ids(void) {
    recv_id = 0;
    recv_ack_id = UINT8_MAX;
    sent_id = 0;
    for (int i = 0; i < RECV_HEAP_COUNT; i++) {
        recv_heap[i].packet_type = PACKET_NULL;
    }
    for (int i = 0; i < SEND_QUEUE_COUNT; i++) {
        send_queue[i].packet_type = PACKET_NULL;
    }
}

Result create_network(void) {
    #ifdef NET_LOGGING
    logfile = fopen("sdmc:/uds.log", "w");
    #endif
    NET_LOG("creating network\n");
    init_ids();
    udsGenerateDefaultNetworkStruct(&network, wlancommID, net_id, 2);
    return udsCreateNetwork(&network, passphrase, sizeof(passphrase), &bindctx, data_channel, UDS_DEFAULT_RECVBUFSIZE);
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
    init_ids();
    return udsConnectNetwork(network, passphrase, sizeof(passphrase), &bindctx, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, data_channel, UDS_DEFAULT_RECVBUFSIZE);
}

void local_disconnect(void) {
    static udsConnectionStatus status;
    udsGetConnectionStatus(&status);
    if (status.cur_NetworkNodeID == UDS_HOST_NETWORKNODEID) {
        udsDestroyNetwork();
    } else {
        udsDisconnectNetwork();
    }
    udsUnbind(&bindctx);
    #ifdef NET_LOGGING
    fclose(logfile);
    #endif
}

static size_t packet_size(const Packet *packet) {
    switch ((PacketType)packet->packet_type) {
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

Result handle_packets(void) {
    Packet *packet = recv_heap;
    Result res = 0;
    u8 seen_ids[RECV_HEAP_COUNT];
    while (packet < recv_heap + RECV_HEAP_COUNT) {
        seen_ids[packet - recv_heap] = packet->packet_id;
        if (packet->packet_type != PACKET_NULL && (s8)(packet->packet_id - recv_id) > 0) {
            bool duplicate = false;
            for (int i = 0; i < packet - recv_heap; i++) {
                if (packet->packet_id == seen_ids[i]) {
                    duplicate = true;
                }
            }
            if (!duplicate) {
                NET_LOG("unrecvd packet %d in buffer\n", packet->packet_id);
                packet++;
                continue;
            }
        }
        size_t actual_size;
        res = udsPullPacket(&bindctx, packet, sizeof(*packet), &actual_size, NULL);
        if (R_FAILED(res)) {
            NET_LOG("recv errored\n");
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
        if ((s8)(packet->packet_id - recv_ack_id) <= 0) {
            NET_LOG("recv'd old packet %d\n", packet->packet_id);
            continue;
        }
        if (packet_crc(packet) != packet->crc8) {
            NET_LOG("recv checksum doesn't match\n");
            packet->packet_type = PACKET_NULL;
            continue;
        }
        // check for duplicates
        for (int i = 0; i < RECV_HEAP_COUNT; i++) {
            if (&recv_heap[i] != packet && recv_heap[i].packet_id == packet->packet_id) {
                NET_LOG("recv'd a duplicate %d\n", packet->packet_id);
                continue;
            }
        }
        NET_LOG("recv valid packet with id %d type %d size %d content %d,%d (acking %d)\n", packet->packet_id, packet->packet_type, actual_size, packet->inputs.shb, packet->inputs.slb, packet->ack_id);
        // valid packet
        // ack the ack
        if ((s8)(packet->ack_id - sent_id) >= 0 && send_queue[packet->ack_id % SEND_QUEUE_COUNT].packet_id == packet->ack_id) {
            send_queue[packet->ack_id % SEND_QUEUE_COUNT].packet_type = PACKET_NULL;
            sent_id = packet->ack_id + 1;
        }
        // if it happens to be the next packet we're looking for, ack it
        if ((u8)(recv_ack_id + 1) == packet->packet_id) {
            recv_ack_id++;
        }
        // leave in heap and continue
        packet++;
    }
    for (int i = sent_id; i < sent_id + 8; i++) {
        packet = &send_queue[i % SEND_QUEUE_COUNT];
        if (packet->packet_type == PACKET_NULL || (s8)(packet->packet_id - sent_id) < 0) {
            NET_LOG("send breaking on type %d id %d sent_id %d\n", packet->packet_type, packet->packet_id, sent_id);
            break;
        }
        packet->ack_id = recv_ack_id;
        packet->crc8 = packet_crc(packet);
        NET_LOG("sending packet %d with type %d size %d content %d,%d (acking %d)\n", packet->packet_id, packet->packet_type, packet_size(packet), packet->inputs.shb, packet->inputs.slb, packet->ack_id);
        udsSendTo(UDS_BROADCAST_NETWORKNODEID, data_channel, UDS_SENDFLAG_Default, packet, packet_size(packet));
    }
    return res;
}

Packet *read_next_packet(void) {
    Packet *out = NULL;
    for (int i = 0; i < RECV_HEAP_COUNT; i++) {
        if (recv_heap[i].packet_id == recv_id - 1) {
            recv_heap[i].packet_type = PACKET_NULL;
        } else if (recv_heap[i].packet_type != PACKET_NULL && recv_heap[i].packet_id == recv_id) {
            out = &recv_heap[i];
        }
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
    for (int i = sent_id; i < sent_id + 8; i++) {
        Packet *packet = &send_queue[i % SEND_QUEUE_COUNT];
        if (packet->packet_type == PACKET_NULL || (s8)(sent_id - packet->packet_id) > 0) {
            packet->packet_type = PACKET_NOP;
            packet->packet_id = i;
            NET_LOG("attempting to send %d (sent_id %d)\n", i, sent_id);
            return packet;
        }
    }
    NET_LOG("send buffer full\n");
    return NULL;
}
