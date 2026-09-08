#pragma once

// C-compatible user/kernel ABI for the minimal mouse relay driver.  This file
// contains transport data only; ADS, BodyLock, calibration math and final-T
// remain in user mode.

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <Windows.h>
#include <winioctl.h>
#endif

#define COD_MOUSE_RELAY_DEVICE_TYPE 0x00008337u
#define COD_MOUSE_RELAY_IOCTL(index, method) \
    CTL_CODE(COD_MOUSE_RELAY_DEVICE_TYPE, 0x800u + (index), (method), FILE_ANY_ACCESS)

#define IOCTL_COD_MOUSE_BEGIN \
    COD_MOUSE_RELAY_IOCTL(0u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_HEARTBEAT \
    COD_MOUSE_RELAY_IOCTL(1u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_ARM \
    COD_MOUSE_RELAY_IOCTL(2u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_DISARM \
    COD_MOUSE_RELAY_IOCTL(3u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_READ_BATCH \
    COD_MOUSE_RELAY_IOCTL(4u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_SUBMIT_FINAL \
    COD_MOUSE_RELAY_IOCTL(5u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_SUBMIT_CALIBRATION \
    COD_MOUSE_RELAY_IOCTL(6u, METHOD_BUFFERED)
#define IOCTL_COD_MOUSE_STATUS \
    COD_MOUSE_RELAY_IOCTL(7u, METHOD_BUFFERED)

#define COD_MOUSE_RELAY_PROTOCOL_VERSION 1u
#define COD_MOUSE_RELAY_MAX_BATCH 64u

#define COD_MOUSE_LEFT_BUTTON_DOWN 0x0001u
#define COD_MOUSE_LEFT_BUTTON_UP 0x0002u
#define COD_MOUSE_RIGHT_BUTTON_DOWN 0x0004u
#define COD_MOUSE_RIGHT_BUTTON_UP 0x0008u
#define COD_MOUSE_MIDDLE_BUTTON_DOWN 0x0010u
#define COD_MOUSE_MIDDLE_BUTTON_UP 0x0020u
#define COD_MOUSE_BUTTON_4_DOWN 0x0040u
#define COD_MOUSE_BUTTON_4_UP 0x0080u
#define COD_MOUSE_BUTTON_5_DOWN 0x0100u
#define COD_MOUSE_BUTTON_5_UP 0x0200u

EXTERN_C const GUID GUID_DEVINTERFACE_COD_MOUSE_RELAY;

typedef enum _COD_MOUSE_RELAY_STATE {
    CodMouseRelayBypass = 0,
    CodMouseRelayArmed = 1
} COD_MOUSE_RELAY_STATE;

typedef enum _COD_MOUSE_RELAY_FAILURE {
    CodMouseRelayFailureNone = 0,
    CodMouseRelayFailureExplicitDisarm = 1,
    CodMouseRelayFailureOwnerClosed = 2,
    CodMouseRelayFailureHeartbeatExpired = 3,
    CodMouseRelayFailureRingFull = 4,
    CodMouseRelayFailureVirtualSubmit = 5,
    CodMouseRelayFailurePower = 6,
    CodMouseRelayFailureProtocol = 7
} COD_MOUSE_RELAY_FAILURE;

#pragma pack(push, 8)

typedef struct _COD_MOUSE_RELAY_TOKEN {
    UINT64 LeaseId;
    UINT64 CaptureEpoch;
} COD_MOUSE_RELAY_TOKEN, *PCOD_MOUSE_RELAY_TOKEN;

typedef struct _COD_MOUSE_BEGIN_REQUEST {
    UINT32 Version;
    UINT32 Reserved;
    UINT64 LeaseId;
} COD_MOUSE_BEGIN_REQUEST, *PCOD_MOUSE_BEGIN_REQUEST;

typedef struct _COD_MOUSE_BEGIN_REPLY {
    UINT32 Version;
    UINT32 HeartbeatTimeoutUs;
    COD_MOUSE_RELAY_TOKEN Token;
} COD_MOUSE_BEGIN_REPLY, *PCOD_MOUSE_BEGIN_REPLY;

typedef struct _COD_MOUSE_TOKEN_REQUEST {
    UINT32 Version;
    UINT32 Reserved;
    COD_MOUSE_RELAY_TOKEN Token;
} COD_MOUSE_TOKEN_REQUEST, *PCOD_MOUSE_TOKEN_REQUEST;

typedef struct _COD_MOUSE_SOURCE_PACKET {
    UINT64 Sequence;
    UINT64 ObservedAt100ns;
    INT32 DxCounts;
    INT32 DyCounts;
    INT32 WheelCounts;
    UINT32 ButtonFlags;
    UINT32 Flags;
    UINT32 Reserved;
} COD_MOUSE_SOURCE_PACKET, *PCOD_MOUSE_SOURCE_PACKET;

typedef struct _COD_MOUSE_READ_BATCH {
    UINT32 Version;
    UINT32 Count;
    COD_MOUSE_SOURCE_PACKET Packets[COD_MOUSE_RELAY_MAX_BATCH];
} COD_MOUSE_READ_BATCH, *PCOD_MOUSE_READ_BATCH;

typedef struct _COD_MOUSE_FINAL_REPORT {
    UINT32 Version;
    UINT32 Reserved;
    COD_MOUSE_RELAY_TOKEN Token;
    UINT64 ReportSequence;
    UINT64 ThroughSourceSequence;
    INT32 DxCounts;
    INT32 DyCounts;
} COD_MOUSE_FINAL_REPORT, *PCOD_MOUSE_FINAL_REPORT;

typedef struct _COD_MOUSE_CALIBRATION_REPORT {
    UINT32 Version;
    UINT32 Reserved;
    COD_MOUSE_RELAY_TOKEN Token;
    INT32 DxCounts;
    INT32 DyCounts;
} COD_MOUSE_CALIBRATION_REPORT, *PCOD_MOUSE_CALIBRATION_REPORT;

typedef struct _COD_MOUSE_RELAY_STATUS {
    UINT32 Version;
    UINT32 State;
    UINT32 Failure;
    UINT32 PhysicalButtons;
    COD_MOUSE_RELAY_TOKEN Token;
    UINT64 LastCapturedSequence;
    UINT64 LastReportSequence;
    UINT64 LastReportedSourceSequence;
    UINT64 DroppedPacketCount;
} COD_MOUSE_RELAY_STATUS, *PCOD_MOUSE_RELAY_STATUS;

#pragma pack(pop)
