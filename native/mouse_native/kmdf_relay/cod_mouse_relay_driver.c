#include "cod_mouse_relay_driver.h"

#include <initguid.h>

#pragma warning(disable:4055)  // PVOID to mouse service callback
#pragma warning(disable:4152)  // function/data pointer conversion in CONNECT_DATA

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, CodMouseEvtDeviceAdd)
#pragma alloc_text(PAGE, CodMouseEvtIoInternalDeviceControl)
#endif

DEFINE_GUID(
    GUID_DEVINTERFACE_COD_MOUSE_RELAY,
    0x8bf10d71,
    0x95ec,
    0x4712,
    0xa3,
    0xef,
    0x08,
    0xca,
    0xe4,
    0x8b,
    0x30,
    0x4b);

static const UCHAR g_CodMouseReportDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x16, 0x00, 0x80,  //     Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0               // End Collection
};

#pragma pack(push, 1)
typedef struct _COD_MOUSE_HID_REPORT {
    SHORT X;
    SHORT Y;
} COD_MOUSE_HID_REPORT;
#pragma pack(pop)

static UINT64 CodMouseNow100ns(VOID)
{
    return KeQueryUnbiasedInterruptTime();
}

static BOOLEAN CodMouseTokenEqual(
    _In_ const COD_MOUSE_RELAY_TOKEN* Left,
    _In_ const COD_MOUSE_RELAY_TOKEN* Right)
{
    return Left->LeaseId == Right->LeaseId &&
        Left->CaptureEpoch == Right->CaptureEpoch;
}

static VOID CodMouseResetRingLocked(_Inout_ PCOD_MOUSE_DEVICE_CONTEXT Context)
{
    Context->RingHead = 0;
    Context->RingTail = 0;
    Context->RingCount = 0;
    Context->LastCapturedSequence = 0;
    Context->LastReportSequence = 0;
    Context->LastReportedSourceSequence = 0;
}

static VOID CodMouseBypassLocked(
    _Inout_ PCOD_MOUSE_DEVICE_CONTEXT Context,
    _In_ COD_MOUSE_RELAY_FAILURE Failure,
    _In_ BOOLEAN ReleaseOwner)
{
    Context->RelayState = CodMouseRelayBypass;
    Context->Failure = Failure;
    CodMouseResetRingLocked(Context);
    if (ReleaseOwner) {
        Context->OwnerFile = NULL;
        RtlZeroMemory(&Context->Token, sizeof(Context->Token));
        Context->LastHeartbeat100ns = 0;
    }
}

static BOOLEAN CodMouseCurrentOwnerLocked(
    _In_ PCOD_MOUSE_DEVICE_CONTEXT Context,
    _In_ WDFFILEOBJECT FileObject,
    _In_ const COD_MOUSE_RELAY_TOKEN* Token)
{
    return Context->OwnerFile == FileObject &&
        CodMouseTokenEqual(&Context->Token, Token);
}

static VOID CodMouseUpdateButtonsLocked(
    _Inout_ PCOD_MOUSE_DEVICE_CONTEXT Context,
    _In_ USHORT ButtonFlags)
{
    if ((ButtonFlags & MOUSE_LEFT_BUTTON_DOWN) != 0) Context->PhysicalButtons |= 1u << 0;
    if ((ButtonFlags & MOUSE_LEFT_BUTTON_UP) != 0) Context->PhysicalButtons &= ~(1u << 0);
    if ((ButtonFlags & MOUSE_RIGHT_BUTTON_DOWN) != 0) Context->PhysicalButtons |= 1u << 1;
    if ((ButtonFlags & MOUSE_RIGHT_BUTTON_UP) != 0) Context->PhysicalButtons &= ~(1u << 1);
    if ((ButtonFlags & MOUSE_MIDDLE_BUTTON_DOWN) != 0) Context->PhysicalButtons |= 1u << 2;
    if ((ButtonFlags & MOUSE_MIDDLE_BUTTON_UP) != 0) Context->PhysicalButtons &= ~(1u << 2);
    if ((ButtonFlags & MOUSE_BUTTON_4_DOWN) != 0) Context->PhysicalButtons |= 1u << 3;
    if ((ButtonFlags & MOUSE_BUTTON_4_UP) != 0) Context->PhysicalButtons &= ~(1u << 3);
    if ((ButtonFlags & MOUSE_BUTTON_5_DOWN) != 0) Context->PhysicalButtons |= 1u << 4;
    if ((ButtonFlags & MOUSE_BUTTON_5_UP) != 0) Context->PhysicalButtons &= ~(1u << 4);
}

static BOOLEAN CodMouseEnqueueLocked(
    _Inout_ PCOD_MOUSE_DEVICE_CONTEXT Context,
    _In_ const MOUSE_INPUT_DATA* Input)
{
    PCOD_MOUSE_SOURCE_PACKET packet;
    if (Context->RingCount >= COD_MOUSE_SOURCE_RING_CAPACITY) {
        ++Context->DroppedPacketCount;
        CodMouseBypassLocked(Context, CodMouseRelayFailureRingFull, FALSE);
        return FALSE;
    }

    packet = &Context->Ring[Context->RingTail];
    RtlZeroMemory(packet, sizeof(*packet));
    packet->Sequence = ++Context->NextSourceSequence;
    packet->ObservedAt100ns = CodMouseNow100ns();
    packet->DxCounts = Input->LastX;
    packet->DyCounts = Input->LastY;
    packet->ButtonFlags = Input->ButtonFlags;
    packet->Flags = Input->Flags;
    if ((Input->ButtonFlags & MOUSE_WHEEL) != 0) {
        packet->WheelCounts = (SHORT)Input->ButtonData;
    }
    Context->RingTail = (Context->RingTail + 1u) % COD_MOUSE_SOURCE_RING_CAPACITY;
    ++Context->RingCount;
    Context->LastCapturedSequence = packet->Sequence;
    return TRUE;
}

static NTSTATUS CodMouseSubmitVirtual(
    _Inout_ PCOD_MOUSE_DEVICE_CONTEXT Context,
    _In_ INT32 Dx,
    _In_ INT32 Dy)
{
    COD_MOUSE_HID_REPORT report;
    HID_XFER_PACKET transfer;
    if (Dx < -32768 || Dx > 32767 || Dy < -32768 || Dy > 32767) {
        return STATUS_INVALID_PARAMETER;
    }
    report.X = (SHORT)Dx;
    report.Y = (SHORT)Dy;
    RtlZeroMemory(&transfer, sizeof(transfer));
    transfer.reportBuffer = (PUCHAR)&report;
    transfer.reportBufferLen = sizeof(report);
    transfer.reportId = 0;
    return VhfReadReportSubmit(Context->VhfHandle, &transfer);
}

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, CodMouseEvtDeviceAdd);
    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);
}

NTSTATUS CodMouseEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES timerAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_TIMER_CONFIG timerConfig;
    VHF_CONFIG vhfConfig;
    WDFDEVICE device;
    PCOD_MOUSE_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        WDF_NO_EVENT_CALLBACK,
        WDF_NO_EVENT_CALLBACK,
        CodMouseEvtFileCleanup);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDeviceD0Exit = CodMouseEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, COD_MOUSE_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = CodMouseEvtDeviceCleanup;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;

    context = CodMouseGetContext(device);
    RtlZeroMemory(context, sizeof(*context));
    context->NextCaptureEpoch = 1;
    context->RelayState = CodMouseRelayBypass;

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfSpinLockCreate(&lockAttributes, &context->Lock);
    if (!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = CodMouseEvtIoInternalDeviceControl;
    queueConfig.EvtIoDeviceControl = CodMouseEvtIoDeviceControl;
    status = WdfIoQueueCreate(
        device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) return status;

    status = WdfDeviceCreateDeviceInterface(
        device, &GUID_DEVINTERFACE_COD_MOUSE_RELAY, NULL);
    if (!NT_SUCCESS(status)) return status;

    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig, CodMouseEvtHeartbeatTimer, COD_MOUSE_HEARTBEAT_TIMER_MS);
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = device;
    status = WdfTimerCreate(&timerConfig, &timerAttributes, &context->HeartbeatTimer);
    if (!NT_SUCCESS(status)) return status;
    WdfTimerStart(context->HeartbeatTimer, WDF_REL_TIMEOUT_IN_MS(COD_MOUSE_HEARTBEAT_TIMER_MS));

    VHF_CONFIG_INIT(
        &vhfConfig,
        WdfDeviceWdmGetDeviceObject(device),
        (USHORT)sizeof(g_CodMouseReportDescriptor),
        (PUCHAR)g_CodMouseReportDescriptor);
    vhfConfig.VendorID = 0x1209;
    vhfConfig.ProductID = 0xC0D1;
    vhfConfig.VersionNumber = 1;
    status = VhfCreate(&vhfConfig, &context->VhfHandle);
    if (!NT_SUCCESS(status)) return status;
    status = VhfStart(context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
        return status;
    }
    return STATUS_SUCCESS;
}

VOID CodMouseEvtDeviceCleanup(_In_ WDFOBJECT Object)
{
    WDFDEVICE device = (WDFDEVICE)Object;
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(device);
    if (context->VhfHandle != NULL) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
    }
}

NTSTATUS CodMouseEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(Device);
    UNREFERENCED_PARAMETER(TargetState);
    WdfSpinLockAcquire(context->Lock);
    CodMouseBypassLocked(context, CodMouseRelayFailurePower, TRUE);
    WdfSpinLockRelease(context->Lock);
    return STATUS_SUCCESS;
}

static VOID CodMousePassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target)
{
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if (!WdfRequestSend(Request, Target, &options)) {
        WdfRequestComplete(Request, WdfRequestGetStatus(Request));
    }
}

VOID CodMouseEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(device);
    PCONNECT_DATA connectData;
    size_t length;
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    PAGED_CODE();

    switch (IoControlCode) {
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        if (context->UpperConnectData.ClassService != NULL) {
            status = STATUS_SHARING_VIOLATION;
            break;
        }
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(CONNECT_DATA), (PVOID*)&connectData, &length);
        if (!NT_SUCCESS(status)) break;
        context->UpperConnectData = *connectData;
        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(device);
        connectData->ClassService = CodMouseServiceCallback;
        break;
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        status = STATUS_NOT_IMPLEMENTED;
        break;
    default:
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    CodMousePassThrough(Request, WdfDeviceGetIoTarget(device));
}

static NTSTATUS CodMouseRetrieveInput(
    _In_ WDFREQUEST Request,
    _In_ size_t Required,
    _Outptr_ PVOID* Buffer)
{
    size_t length;
    return WdfRequestRetrieveInputBuffer(Request, Required, Buffer, &length);
}

VOID CodMouseEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(device);
    WDFFILEOBJECT fileObject = WdfRequestGetFileObject(Request);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t information = 0;

    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    switch (IoControlCode) {
    case IOCTL_COD_MOUSE_BEGIN:
    {
        PCOD_MOUSE_BEGIN_REQUEST begin;
        PCOD_MOUSE_BEGIN_REPLY reply;
        status = CodMouseRetrieveInput(Request, sizeof(*begin), (PVOID*)&begin);
        if (!NT_SUCCESS(status)) break;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(*reply), (PVOID*)&reply, NULL);
        if (!NT_SUCCESS(status)) break;
        if (begin->Version != COD_MOUSE_RELAY_PROTOCOL_VERSION || begin->LeaseId == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        WdfSpinLockAcquire(context->Lock);
        if (context->OwnerFile != NULL && context->OwnerFile != fileObject) {
            status = STATUS_SHARING_VIOLATION;
        } else {
            context->OwnerFile = fileObject;
            context->Token.LeaseId = begin->LeaseId;
            context->Token.CaptureEpoch = context->NextCaptureEpoch++;
            context->LastHeartbeat100ns = CodMouseNow100ns();
            context->Failure = CodMouseRelayFailureNone;
            context->RelayState = CodMouseRelayBypass;
            CodMouseResetRingLocked(context);
            reply->Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
            reply->HeartbeatTimeoutUs = 100000u;
            reply->Token = context->Token;
            information = sizeof(*reply);
            status = STATUS_SUCCESS;
        }
        WdfSpinLockRelease(context->Lock);
        break;
    }
    case IOCTL_COD_MOUSE_HEARTBEAT:
    case IOCTL_COD_MOUSE_ARM:
    case IOCTL_COD_MOUSE_DISARM:
    {
        PCOD_MOUSE_TOKEN_REQUEST tokenRequest;
        status = CodMouseRetrieveInput(
            Request, sizeof(*tokenRequest), (PVOID*)&tokenRequest);
        if (!NT_SUCCESS(status)) break;
        if (tokenRequest->Version != COD_MOUSE_RELAY_PROTOCOL_VERSION) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        WdfSpinLockAcquire(context->Lock);
        if (!CodMouseCurrentOwnerLocked(context, fileObject, &tokenRequest->Token)) {
            status = STATUS_ACCESS_DENIED;
        } else if (IoControlCode == IOCTL_COD_MOUSE_HEARTBEAT) {
            context->LastHeartbeat100ns = CodMouseNow100ns();
            status = STATUS_SUCCESS;
        } else if (IoControlCode == IOCTL_COD_MOUSE_ARM) {
            if (context->PhysicalButtons != 0) {
                status = STATUS_DEVICE_BUSY;
            } else {
                context->LastHeartbeat100ns = CodMouseNow100ns();
                context->Failure = CodMouseRelayFailureNone;
                CodMouseResetRingLocked(context);
                context->RelayState = CodMouseRelayArmed;
                status = STATUS_SUCCESS;
            }
        } else {
            CodMouseBypassLocked(
                context, CodMouseRelayFailureExplicitDisarm, TRUE);
            status = STATUS_SUCCESS;
        }
        WdfSpinLockRelease(context->Lock);
        break;
    }
    case IOCTL_COD_MOUSE_READ_BATCH:
    {
        PCOD_MOUSE_TOKEN_REQUEST tokenRequest;
        PCOD_MOUSE_READ_BATCH batch;
        ULONG index;
        status = CodMouseRetrieveInput(
            Request, sizeof(*tokenRequest), (PVOID*)&tokenRequest);
        if (!NT_SUCCESS(status)) break;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(*batch), (PVOID*)&batch, NULL);
        if (!NT_SUCCESS(status)) break;
        WdfSpinLockAcquire(context->Lock);
        if (!CodMouseCurrentOwnerLocked(context, fileObject, &tokenRequest->Token)) {
            status = STATUS_ACCESS_DENIED;
        } else {
            batch->Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
            batch->Count = context->RingCount < COD_MOUSE_RELAY_MAX_BATCH
                ? context->RingCount
                : COD_MOUSE_RELAY_MAX_BATCH;
            for (index = 0; index < batch->Count; ++index) {
                batch->Packets[index] = context->Ring[context->RingHead];
                context->RingHead =
                    (context->RingHead + 1u) % COD_MOUSE_SOURCE_RING_CAPACITY;
                --context->RingCount;
            }
            information = FIELD_OFFSET(COD_MOUSE_READ_BATCH, Packets) +
                batch->Count * sizeof(COD_MOUSE_SOURCE_PACKET);
            status = STATUS_SUCCESS;
        }
        WdfSpinLockRelease(context->Lock);
        break;
    }
    case IOCTL_COD_MOUSE_SUBMIT_FINAL:
    {
        PCOD_MOUSE_FINAL_REPORT report;
        status = CodMouseRetrieveInput(Request, sizeof(*report), (PVOID*)&report);
        if (!NT_SUCCESS(status)) break;
        WdfSpinLockAcquire(context->Lock);
        if (report->Version != COD_MOUSE_RELAY_PROTOCOL_VERSION) {
            status = STATUS_INVALID_PARAMETER;
        } else if (!CodMouseCurrentOwnerLocked(context, fileObject, &report->Token)) {
            status = STATUS_ACCESS_DENIED;
        } else if (context->RelayState != CodMouseRelayArmed) {
            status = STATUS_INVALID_DEVICE_STATE;
        } else if (report->ReportSequence == 0 ||
                   report->ReportSequence <= context->LastReportSequence ||
                   report->ThroughSourceSequence < context->LastReportedSourceSequence ||
                   report->ThroughSourceSequence > context->LastCapturedSequence) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            status = CodMouseSubmitVirtual(context, report->DxCounts, report->DyCounts);
            if (NT_SUCCESS(status)) {
                context->LastReportSequence = report->ReportSequence;
                context->LastReportedSourceSequence = report->ThroughSourceSequence;
            } else {
                CodMouseBypassLocked(
                    context, CodMouseRelayFailureVirtualSubmit, FALSE);
            }
        }
        WdfSpinLockRelease(context->Lock);
        break;
    }
    case IOCTL_COD_MOUSE_SUBMIT_CALIBRATION:
    {
        PCOD_MOUSE_CALIBRATION_REPORT report;
        status = CodMouseRetrieveInput(Request, sizeof(*report), (PVOID*)&report);
        if (!NT_SUCCESS(status)) break;
        WdfSpinLockAcquire(context->Lock);
        if (report->Version != COD_MOUSE_RELAY_PROTOCOL_VERSION) {
            status = STATUS_INVALID_PARAMETER;
        } else if (!CodMouseCurrentOwnerLocked(context, fileObject, &report->Token)) {
            status = STATUS_ACCESS_DENIED;
        } else if (report->DxCounts < -COD_MOUSE_MAX_CALIBRATION_COUNTS ||
                   report->DxCounts > COD_MOUSE_MAX_CALIBRATION_COUNTS ||
                   report->DyCounts < -COD_MOUSE_MAX_CALIBRATION_COUNTS ||
                   report->DyCounts > COD_MOUSE_MAX_CALIBRATION_COUNTS) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            status = CodMouseSubmitVirtual(context, report->DxCounts, report->DyCounts);
            if (!NT_SUCCESS(status) && context->RelayState == CodMouseRelayArmed) {
                CodMouseBypassLocked(
                    context, CodMouseRelayFailureVirtualSubmit, FALSE);
            }
        }
        WdfSpinLockRelease(context->Lock);
        break;
    }
    case IOCTL_COD_MOUSE_STATUS:
    {
        PCOD_MOUSE_RELAY_STATUS relayStatus;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(*relayStatus), (PVOID*)&relayStatus, NULL);
        if (!NT_SUCCESS(status)) break;
        WdfSpinLockAcquire(context->Lock);
        relayStatus->Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
        relayStatus->State = context->RelayState;
        relayStatus->Failure = context->Failure;
        relayStatus->PhysicalButtons = context->PhysicalButtons;
        relayStatus->Token = context->Token;
        relayStatus->LastCapturedSequence = context->LastCapturedSequence;
        relayStatus->LastReportSequence = context->LastReportSequence;
        relayStatus->LastReportedSourceSequence = context->LastReportedSourceSequence;
        relayStatus->DroppedPacketCount = context->DroppedPacketCount;
        WdfSpinLockRelease(context->Lock);
        information = sizeof(*relayStatus);
        status = STATUS_SUCCESS;
        break;
    }
    default:
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, information);
}

VOID CodMouseEvtFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
    WDFDEVICE device = WdfFileObjectGetDevice(FileObject);
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(device);
    WdfSpinLockAcquire(context->Lock);
    if (context->OwnerFile == FileObject) {
        CodMouseBypassLocked(context, CodMouseRelayFailureOwnerClosed, TRUE);
    }
    WdfSpinLockRelease(context->Lock);
}

VOID CodMouseEvtHeartbeatTimer(_In_ WDFTIMER Timer)
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(device);
    const UINT64 now = CodMouseNow100ns();
    WdfSpinLockAcquire(context->Lock);
    if (context->RelayState == CodMouseRelayArmed &&
        context->OwnerFile != NULL && context->LastHeartbeat100ns != 0 &&
        now >= context->LastHeartbeat100ns &&
        now - context->LastHeartbeat100ns >= COD_MOUSE_HEARTBEAT_TIMEOUT_100NS) {
        CodMouseBypassLocked(
            context, CodMouseRelayFailureHeartbeatExpired, TRUE);
    }
    WdfSpinLockRelease(context->Lock);
}

VOID CodMouseServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed)
{
    WDFDEVICE device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    PCOD_MOUSE_DEVICE_CONTEXT context = CodMouseGetContext(device);
    PMOUSE_INPUT_DATA current;
    ULONG totalConsumed = 0;

    if (context->UpperConnectData.ClassService == NULL) {
        *InputDataConsumed = 0;
        return;
    }

    for (current = InputDataStart; current < InputDataEnd; ++current) {
        MOUSE_INPUT_DATA forwarded = *current;
        ULONG consumed = 0;
        BOOLEAN relayPacket = FALSE;

        WdfSpinLockAcquire(context->Lock);
        CodMouseUpdateButtonsLocked(context, current->ButtonFlags);
        if (context->RelayState == CodMouseRelayArmed) {
            relayPacket = CodMouseEnqueueLocked(context, current);
        }
        WdfSpinLockRelease(context->Lock);

        if (relayPacket) {
            forwarded.LastX = 0;
            forwarded.LastY = 0;
        }
        ((PSERVICE_CALLBACK_ROUTINE)context->UpperConnectData.ClassService)(
            context->UpperConnectData.ClassDeviceObject,
            relayPacket ? &forwarded : current,
            relayPacket ? (&forwarded + 1) : (current + 1),
            &consumed);
        totalConsumed += consumed;
    }
    *InputDataConsumed = totalConsumed;
}
