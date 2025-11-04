#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

#include <3ds.h>

typedef enum {
    PACKET_NULL, // Not a packet
    PACKET_NOP,
    PACKET_INPUTS,
    PACKET_LOADED,
    PACKET_RESUME,
    PACKET_PAUSE,
    PACKET_DATA,
    PACKET_RESET,
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
            char name[64];
            u32 crc32;
            u32 sram_size;
        } loaded;
        struct {
            u8 input_buffer;
        } resume;
        struct {
            u8 data[0x400];
        } data;
    };
} Packet;

#define APPDATA_VERSION 0

typedef struct {
    u32 protocol_version;
    u32 emulator_version;
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
