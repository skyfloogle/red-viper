#include "multiplayer.h"

#include <stdlib.h>
#include <string.h>

static udsNetworkStruct network;
static udsBindContext bindctx;
static const u32 wlancommID = 0x2d23bfdb;
static u8 net_id = 0;
static u8 data_channel = 1;
static const char passphrase[] = "Red Viper " VERSION;
static udsNetworkScanInfo *beacons;

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
