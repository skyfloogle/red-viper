// This file handles communication with the Circle Pad Pro.
// For interactions with the calibration applet, see extrapad.c.

#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/svc.h>
#include <3ds/srv.h>
#include <3ds/ipc.h>
#include <3ds/synchronization.h>
#include <stdio.h>
#include <stdlib.h>

#include "cpp.h"
#include "crc8.h"
#include "3ds/services/apt.h"
#include "3ds/services/hid.h"
#include "3ds/thread.h"

#define MILLIS 1000000

typedef struct {
    u32 latest_receive_error_result;
    u32 latest_send_error_result;
    u8 connection_status;
    u8 trying_to_connect_status;
    u8 connection_role;
    u8 machine_id;
    u8 connected;
    u8 network_id;
    u8 initialized;
} SharedMemoryHeader;

typedef struct {
    u32 begin_index;
    u32 end_index;
    u32 packet_count;
    u32 unknown;
} BufferInfo;

typedef struct {
    u32 offset;
    u32 size;
} PacketInfo;

typedef struct {
    SharedMemoryHeader header;
    BufferInfo recvBufferInfo;
    PacketInfo recvPackets[];
} SharedMemory;

typedef struct {
    u8 head;
    u8 response_time;
    u8 unknown;
} InputRequest;

typedef struct __attribute__((packed)) {
    u8 head;
	u16 pad_x: 12;
	u16 pad_y: 12;
    u8 battery_level: 5;
    bool zl_up: 1;
    bool zr_up: 1;
    bool r_up: 1;
    u8 unknown;
} InputResponse;

typedef struct {
	u8 head;
	u8 response_time;
	u16 offset;
	u16 size;
} CalibrationRequest;

typedef struct {
	u16 x_offset;
	u16 y_offset;
	float x_scale;
	float y_scale;
} CalibrationData;

typedef struct __attribute__((packed)) {
	u8 _padding1;
	u16 x_offset: 12;
	u16 y_offset: 12;
	float x_scale;
	float y_scale;
	u8 _padding2[3];
	u8 crc8;
} CalibrationResponseData;

typedef struct __attribute__((packed)) {
	u8 head;
	u16 offset;
	u16 size;
	CalibrationResponseData candidates[4];
} CalibrationResponse;

static Handle iruserHandle;

static SharedMemory *iruserSharedMemAddr;
static u32 iruserSharedMemSize;
static Handle iruserSharedMemHandle;

static Thread iruserThread;
static Handle iruserExitEvent;

static volatile u32 kHeld = 0;
static volatile circlePosition cPos = {0, 0};
static volatile u8 batteryLevel = 0;

static int iruserRefCount = 0;

static int iruserPacketId;
static int iruserRecvPacketCapacity;
static int iruserRecvBuffSize;
static int iruserSendPacketCapacity;
static int iruserSendBuffSize;
static u8 iruserBaudRate;
static volatile bool iruserConnected = false;

static Result __IRUSER_FinalizeIrnop(void);
static Result __IRUSER_ClearReceiveBuffer(void);
static Result __IRUSER_ClearSendBuffer(void);
static Result __IRUSER_RequireConnection(u8 deviceId);
static Result __IRUSER_Disconnect(void);
static Result __IRUSER_GetReceiveEvent(Handle *out);
static Result __IRUSER_GetConnectionStatusEvent(Handle *out);
static Result __IRUSER_SendIrnop(const void *buffer, u32 size);
static Result __IRUSER_InitializeIrnopShared
    (Handle shared_memory,size_t shared_buff_size,size_t recv_buff_size,
    size_t recv_buff_packet_count,size_t send_buff_size,size_t send_buff_packet_count,
    uint8_t baud_rate);
static Result __IRUSER_ReleaseSharedData(u32 count);

static void iruserThreadFunc(void*);
static u32 iruserReadPacket(void *out, u32 out_len);
static Result iruserClearPacket(int count);

Result cppInit(void)
{
    bool new_3ds;
    APT_CheckNew3DS(&new_3ds);
    if (new_3ds) return 0;

    Result ret = 0;

    if (AtomicPostIncrement(&iruserRefCount)) return -1;

    iruserSharedMemSize = 0x1000;

    size_t recv_data_size = 2000;
    size_t recv_packet_count = 0xa0;
    size_t send_data_size = 0x200;
    size_t send_packet_count = 0x20;
    u8 baud_rate = 4;
    size_t recv_buff_size = recv_data_size + recv_packet_count * 8;
    size_t send_buff_size = send_data_size + send_packet_count * 8;

    iruserRecvPacketCapacity = recv_packet_count;
    iruserRecvBuffSize = recv_buff_size;
    iruserSendPacketCapacity = send_packet_count;
    iruserSendBuffSize = send_buff_size;
    iruserBaudRate = baud_rate;
    iruserPacketId = 0;

    iruserSharedMemAddr = aligned_alloc(0x1000, iruserSharedMemSize);
    if (iruserSharedMemAddr == NULL)
    {
        ret = -1;
        goto cleanup0;
    }

    ret = srvGetServiceHandle(&iruserHandle, "ir:USER");
    if(R_FAILED(ret))goto cleanup1;

    ret = svcCreateMemoryBlock(&iruserSharedMemHandle, (u32)iruserSharedMemAddr, iruserSharedMemSize, MEMPERM_READ, MEMPERM_READWRITE);
    if (R_FAILED(ret))goto cleanup2;

    ret = svcCreateEvent(&iruserExitEvent, RESET_ONESHOT);
    if(R_FAILED(ret)) goto cleanup3;

    iruserThread = threadCreate(iruserThreadFunc, NULL, 0x200, 0x28, 0, true);
    if (iruserThread == NULL) {
        ret = -1;
        goto cleanup4;
    }

    return 0;

    cleanup4:
    svcCloseHandle(iruserExitEvent);
    cleanup3:
    svcCloseHandle(iruserSharedMemHandle);
    cleanup2:
    svcCloseHandle(iruserHandle);
    cleanup1:
    free(iruserSharedMemAddr);
    iruserSharedMemAddr = NULL;
    cleanup0:
    AtomicDecrement(&iruserRefCount);
    return ret;
}

void cppExit()
{
    bool new_3ds;
    APT_CheckNew3DS(&new_3ds);
    if (new_3ds) return;

    if (AtomicDecrement(&iruserRefCount)) return;

    svcSignalEvent(iruserExitEvent);
    threadJoin(iruserThread, 20 * MILLIS);
}

bool cppGetConnected(void) {
    return iruserConnected;
}

void cppCircleRead(circlePosition *pos) {
    if (pos) *pos = cPos;
}

u32 cppKeysHeld(void) {
    return kHeld;
}

u8 cppBatteryLevel(void) {
    return batteryLevel;
}

enum {
    SAR_EXIT = -1,
    SAR_READERROR = -2,
    SAR_TIMEOUT = -3,
};

static Result sendAndReceive(const void *request, int request_size, void *response, int response_size, Handle *two_events, int64_t timeout, u8 expected_head) {
    Result subres, res;
    for (int i = 0; i < 4; i++) {
        __IRUSER_SendIrnop(request, request_size);
        s32 synced_handle;
        subres = svcWaitSynchronizationN(&synced_handle, two_events, 2, false, timeout);
        if (R_DESCRIPTION(subres) == RD_TIMEOUT)  {
            // Timeout.
            res = SAR_TIMEOUT;
            continue;
        }
        if (synced_handle == 1) {
            // Exit.
            res = SAR_EXIT;
            break;
        }
        subres = iruserReadPacket(response, response_size);
        if (subres != response_size || *(u8*)response != expected_head) {
            // Read error.
            res = SAR_READERROR;
            continue;
        }
        return 0;
    }
    // Timeout.
    return res;
}

static bool checkCalibrationData(CalibrationData *out, CalibrationResponseData *candidate) {
    if (crc8(candidate, 15) == candidate->crc8) {
        out->x_offset = candidate->x_offset;
        out->y_offset = candidate->y_offset;
        out->x_scale = candidate->x_scale;
        out->y_scale = candidate->y_scale;
        return true;
    }
    return false;
}

static void iruserDisconnect(void) {
    iruserConnected = false;
    __IRUSER_Disconnect();
    __IRUSER_ClearReceiveBuffer();
    __IRUSER_ClearSendBuffer();
    __IRUSER_FinalizeIrnop();
    batteryLevel = 0;
    kHeld = 0;
    cPos.dx = 0;
    cPos.dy = 0;
}

static void iruserThreadFunc(void* param) {
    Result ret;

    Handle two_events[2];
    two_events[1] = iruserExitEvent;
    s32 synced_handle;
    Handle connstatus_event = 0, recv_event = 0;

    while (true) {
        CalibrationData calibration_data;
        // Wait for CPP connection.
        while (true) {
            __IRUSER_InitializeIrnopShared(iruserSharedMemHandle, iruserSharedMemSize, iruserRecvBuffSize, iruserRecvPacketCapacity, iruserSendBuffSize, iruserSendPacketCapacity, iruserBaudRate);
            __IRUSER_GetConnectionStatusEvent(&connstatus_event);
            __IRUSER_GetReceiveEvent(&recv_event);
            two_events[0] = connstatus_event;
            // Try four times in quick succession, then wait a second.
            for (int i = 0; i < 4; i++) {
                __IRUSER_RequireConnection(1);
                ret = svcWaitSynchronizationN(&synced_handle, two_events, 2, false, 14 * MILLIS);
                if (R_DESCRIPTION(ret) != RD_TIMEOUT) {
                    if (synced_handle == 0) goto connected;
                    else goto cleanup;
                }
                __IRUSER_Disconnect();
            }
            svcCloseHandle(connstatus_event);
            svcCloseHandle(recv_event);
            iruserDisconnect();
            svcWaitSynchronization(iruserExitEvent, 1000 * MILLIS);
        }
        connected:
        // Retrieve calibration data.
        CalibrationRequest calibration_request = {2, 100, 0, 0x40};
        CalibrationResponse calibration_response;
        two_events[0] = recv_event;
        ret = sendAndReceive(&calibration_request, sizeof(calibration_request), &calibration_response, sizeof(calibration_response), two_events, 20 * MILLIS, 0x11);
        if (ret == SAR_EXIT) goto cleanup;
        if (ret == SAR_READERROR) goto disconnect;
        if (ret == SAR_TIMEOUT) goto disconnect;

        bool calibration_data_found = false;
        for (int i = 0; i < 4; i++) {
            if (checkCalibrationData(&calibration_data, &calibration_response.candidates[i])) {
                calibration_data_found = true;
                break;
            }
        }

        if (!calibration_data_found) {
            for (int i = 0; i < 0x40; i++) {
                calibration_request.offset = 0x400 + i;
                ret = sendAndReceive(&calibration_request, sizeof(calibration_request), &calibration_response, sizeof(calibration_response), two_events, 20 * MILLIS, 0x11);
                if (ret == SAR_EXIT) goto cleanup;
                if (ret == SAR_READERROR) goto disconnect;
                if (ret == SAR_TIMEOUT) goto disconnect;
                for (int j = 0; j < 4; j++) {
                    if (checkCalibrationData(&calibration_data, &calibration_response.candidates[j])) {
                        calibration_data_found = true;
                        break;
                    }
                }
            }
        }

        if (!calibration_data_found) goto disconnect;

        iruserConnected = true;

        while (iruserSharedMemAddr->header.connection_status == 2) {
            InputRequest input_request = {0x01, 0x20, 0x87};
            InputResponse input_response;
            ret = sendAndReceive(&input_request, sizeof(input_request), &input_response, sizeof(input_response), two_events, 20 * MILLIS, 0x10);
            if (ret == SAR_EXIT) goto cleanup;
            if (ret == SAR_TIMEOUT) goto disconnect;
            if (ret == SAR_READERROR) continue; // Ignore read errors.
            if (input_response.head != 0x10) continue;

            cPos.dx = (s16)((float)(s16)(input_response.pad_x - calibration_data.x_offset) * calibration_data.x_scale) / 8;
            cPos.dy = (s16)((float)(s16)(input_response.pad_y - calibration_data.y_offset) * calibration_data.y_scale) / 8;
            
            u32 keys = 0;
            if (!input_response.r_up) keys |= KEY_R;
            if (!input_response.zl_up) keys |= KEY_ZL;
            if (!input_response.zr_up) keys |= KEY_ZR;
            kHeld = keys;

            batteryLevel = input_response.battery_level;
        }
        disconnect:
        svcCloseHandle(connstatus_event);
        svcCloseHandle(recv_event);
        iruserDisconnect();
    }
    cleanup:
    svcCloseHandle(connstatus_event);
    svcCloseHandle(recv_event);
    iruserDisconnect();
    svcCloseHandle(iruserSharedMemHandle);
    svcCloseHandle(iruserHandle);

    free(iruserSharedMemAddr);
    iruserSharedMemAddr = NULL;
    iruserSharedMemSize = 0;
}

static u32 iruserReadPacket(void *out, u32 out_len)
{
    if (iruserSharedMemAddr->recvBufferInfo.packet_count <= 0) {
        return 0;
    }
    PacketInfo *packetInfo = &iruserSharedMemAddr->recvPackets[iruserPacketId];
    u8 *start = ((u8*)&iruserSharedMemAddr->recvPackets[iruserRecvPacketCapacity]);
    u8 *end = start + iruserRecvBuffSize;

    u8 *cursor = start + packetInfo->offset;

    // The last byte is the CRC8 of what came before, so the CRC8 of the whole thing should be 0.
    if (crc8_loop(start, iruserRecvBuffSize, cursor, packetInfo->size)) {
        // Invalid packet, ignore.
        iruserClearPacket(1);
        return 0;
    }

    u8 header[4];
    for (int i = 0; i < 4; i++) {
        header[i] = *cursor++;
        if (cursor == end) cursor = start;
    }

    int content_length = header[2] & 0x3f;
    if (header[2] & 0x40) {
        content_length = (content_length << 8) | header[3];
    } else {
        // Backtrack one byte.
        if (cursor == start) cursor = end;
        cursor--;
    }
    
    int len_to_read = content_length <= out_len ? content_length : out_len;

    for (int i = 0; i < len_to_read; i++) {
        *(u8*)out++ = *cursor++;
        if (cursor == end) cursor = start;
    }

    if (content_length <= out_len) iruserClearPacket(1);

    return content_length;
}

static Result iruserClearPacket(int count) {
    Result ret = __IRUSER_ReleaseSharedData(count);
    if (R_FAILED(ret)) return ret;
    iruserPacketId += count;
    while (iruserPacketId >= iruserRecvPacketCapacity) {
        iruserPacketId -= iruserRecvPacketCapacity;
    }
    return 0;
}

static Result __IRUSER_FinalizeIrnop(void)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x2,0,0); // 0x20000
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_ClearReceiveBuffer(void)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x3,0,0); // 0x30000
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_ClearSendBuffer(void)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x4,0,0); // 0x40000
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_RequireConnection(u8 deviceId)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x6,1,0); // 0x60040
    cmdbuf[1] = deviceId;
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_Disconnect(void)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x9,0,0); // 0x90000
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_GetReceiveEvent(Handle *out)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0xA,0,0); // 0xA0000
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    *out = (Handle)cmdbuf[3];

    return ret;
}

static Result __IRUSER_GetConnectionStatusEvent(Handle *out)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0xC,0,0); // 0xC0000
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];
    
    *out = (Handle)cmdbuf[3];

    return ret;
}

static Result __IRUSER_SendIrnop(const void *buffer, u32 size)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0xD,1,2); // 0xD0042
    cmdbuf[1] = size;
    cmdbuf[2] = (size << 14) | 2;
    cmdbuf[3] = (u32)buffer;
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_InitializeIrnopShared
    (Handle shared_memory,size_t shared_buff_size,size_t recv_buff_size,
    size_t recv_buff_packet_count,size_t send_buff_size,size_t send_buff_packet_count,
    uint8_t baud_rate)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();
    
    cmdbuf[0] = IPC_MakeHeader(0x18,6,2); // 0x180182
    cmdbuf[1] = shared_buff_size;
    cmdbuf[2] = recv_buff_size;
    cmdbuf[3] = recv_buff_packet_count;
    cmdbuf[4] = send_buff_size;
    cmdbuf[5] = send_buff_packet_count;
    cmdbuf[6] = baud_rate;
    cmdbuf[7] = 0;
    cmdbuf[8] = shared_memory;

    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}

static Result __IRUSER_ReleaseSharedData(u32 count)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x19,1,0); // 0x190040
    cmdbuf[1] = count;
    
    if(R_FAILED(ret = svcSendSyncRequest(iruserHandle)))return ret;
    ret = (Result)cmdbuf[1];

    return ret;
}
