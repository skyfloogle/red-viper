#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

#include <3ds.h>

Result create_network(void);
Result scan_beacons(udsNetworkScanInfo **networks, size_t *total_networks);
Result connect_to_network(const udsNetworkStruct *network);
void local_disconnect(void);

#endif
