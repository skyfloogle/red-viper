#include "multiplayer.h"

#include <stdlib.h>
#include <string.h>

#include "crc8.h"

static udsNetworkStruct network;
static udsBindContext bindctx;
static const u32 wlancommID = 0x2d23bfdb;
static u8 net_id = 0;
static u8 data_channel = 1;
static const char passphrase[] = "Red Viper " VERSION;
static udsNetworkScanInfo *beacons;
#define RECV_HEAP_COUNT 8
static Packet recv_heap[RECV_HEAP_COUNT];
static u8 recv_id; // TODO initialize somewhere
#define SEND_QUEUE_COUNT 8
static Packet send_queue[SEND_QUEUE_COUNT];
static u8 sent_id;

Result create_network(void) {
    udsGenerateDefaultNetworkStruct(&network, wlancommID, net_id, 2);
    return udsCreateNetwork(&network, passphrase, sizeof(passphrase), &bindctx, data_channel, UDS_DEFAULT_RECVBUFSIZE);
}

Result scan_beacons(udsNetworkScanInfo **networks, size_t *total_networks) {
    static u8 tmpbuf[0x4000];
    return udsScanBeacons(tmpbuf, sizeof(tmpbuf), networks, total_networks, wlancommID, net_id, NULL, false);
}

Result connect_to_network(const udsNetworkStruct *network) {
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
    while (packet < recv_heap + RECV_HEAP_COUNT) {
        if (packet->packet_type != PACKET_NULL) {
            packet++;
            continue;
        }
        size_t actual_size;
        Result res = udsPullPacket(&bindctx, packet, sizeof(*packet), &actual_size, NULL);
        if (R_FAILED(res)) {
            packet->packet_type = PACKET_NULL;
            return res;
        }
        if (actual_size == 0) {
            // no data
            return 0;
        }
        if (actual_size < PACKET_HEADER_SIZE || actual_size != packet_size(packet)) {
            continue;
        }
        if (packet_crc(packet) != packet->crc8) {
            packet->packet_type = PACKET_NULL;
            continue;
        }
        // valid packet, leave in heap and continue
        packet++;
    }
    return 0;
}

Packet *read_next_packet(void) {
    Packet *out = NULL;
    for (int i = 0; i < RECV_HEAP_COUNT; i++) {
        if (recv_heap[i].packet_id == recv_id - 1) {
            recv_heap[i].packet_type = PACKET_NULL;
        } else if (recv_heap[i].packet_id == recv_id) {
            out = &recv_heap[i];
        }
    }
    if (out) {
        recv_id++;
        if ((s8)(out->ack_id - sent_id) > 0) {
            sent_id = out->ack_id;
        }
    }
    return out;
}
