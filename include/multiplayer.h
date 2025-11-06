#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

#include <3ds.h>

typedef enum {
    PACKET_NULL, // Not a packet
    PACKET_NOP,
    PACKET_LOADED,
    PACKET_DLPLAY_RQ,
    PACKET_DLPLAY_SIZE,
    PACKET_RESUME,
    PACKET_RESET,
    PACKET_INPUTS,
    PACKET_DATA,
} PacketType;

typedef struct {
    #define PACKET_HEADER_SIZE 4
    u8 crc8;
    u8 packet_id;
    u8 ack_id;
    u8 packet_type;
    union {
        u16 inputs;
        struct {
            u32 rom_size;
        } dlplay_size;
        struct {
            u8 input_buffer;
        } resume;
        struct {
            u8 data[0x400];
        } data;
    };
} Packet;

#define PROTOCOL_VERSION 0

typedef struct {
    u32 protocol_version;
    char emulator_version[32];
    u32 rom_crc32;
    char rom_name[48];
} NetAppData;

Result create_network(void);
Result scan_beacons(udsNetworkScanInfo **networks, size_t *total_networks);
Result connect_to_network(const udsNetworkStruct *network);
void local_disconnect(void);
bool wait_for_packet(u64 max);
Packet *read_next_packet(void);
Packet *new_packet_to_send(void);
void ship_packet(Packet *packet);
bool wait_for_free_send_slot(u64 max);
bool send_queue_empty(void);

#endif
