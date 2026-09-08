#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <vhf.h>

#include "../cod_mouse_relay_ioctl.h"

#define COD_MOUSE_SOURCE_RING_CAPACITY 256u
#define COD_MOUSE_HEARTBEAT_TIMEOUT_100NS (100ull * 10ull * 1000ull)
#define COD_MOUSE_HEARTBEAT_TIMER_MS 20u
#define COD_MOUSE_MAX_CALIBRATION_COUNTS 64

typedef struct _COD_MOUSE_DEVICE_CONTEXT {
    CONNECT_DATA UpperConnectData;
    WDFSPINLOCK Lock;
    WDFTIMER HeartbeatTimer;
    VHFHANDLE VhfHandle;
    WDFFILEOBJECT OwnerFile;
    COD_MOUSE_RELAY_TOKEN Token;
    UINT64 NextCaptureEpoch;
    UINT64 LastHeartbeat100ns;
    UINT64 NextSourceSequence;
    UINT64 LastCapturedSequence;
    UINT64 LastReportSequence;
    UINT64 LastReportedSourceSequence;
    UINT64 DroppedPacketCount;
    ULONG RingHead;
    ULONG RingTail;
    ULONG RingCount;
    ULONG PhysicalButtons;
    COD_MOUSE_RELAY_STATE RelayState;
    COD_MOUSE_RELAY_FAILURE Failure;
    COD_MOUSE_SOURCE_PACKET Ring[COD_MOUSE_SOURCE_RING_CAPACITY];
} COD_MOUSE_DEVICE_CONTEXT, *PCOD_MOUSE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(COD_MOUSE_DEVICE_CONTEXT, CodMouseGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CodMouseEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP CodMouseEvtDeviceCleanup;
EVT_WDF_DEVICE_D0_EXIT CodMouseEvtDeviceD0Exit;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CodMouseEvtIoInternalDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL CodMouseEvtIoDeviceControl;
EVT_WDF_FILE_CLEANUP CodMouseEvtFileCleanup;
EVT_WDF_TIMER CodMouseEvtHeartbeatTimer;

VOID CodMouseServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed);
