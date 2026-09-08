#include "mouse_native/mouse_relay_client.h"

#include <SetupAPI.h>
#include <initguid.h>

#include <algorithm>
#include <cstddef>
#include <vector>

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

namespace mouse_native {
namespace {

HANDLE native_handle(void* handle) noexcept {
    return static_cast<HANDLE>(handle);
}

bool valid_handle(void* handle) noexcept {
    return handle != nullptr && native_handle(handle) != INVALID_HANDLE_VALUE;
}

}  // namespace

MouseRelayClient::~MouseRelayClient() {
    close();
}

bool MouseRelayClient::open() {
    close();
    HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_COD_MOUSE_RELAY,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        last_error_ = GetLastError();
        return false;
    }

    bool opened = false;
    for (DWORD index = 0; !opened; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(
                devices,
                nullptr,
                &GUID_DEVINTERFACE_COD_MOUSE_RELAY,
                index,
                &interface_data)) {
            last_error_ = GetLastError();
            break;
        }

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(
            devices, &interface_data, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            last_error_ = GetLastError();
            continue;
        }
        std::vector<std::byte> storage(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devices,
                &interface_data,
                detail,
                required,
                nullptr,
                nullptr)) {
            last_error_ = GetLastError();
            continue;
        }

        HANDLE candidate = CreateFileW(
            detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (candidate == INVALID_HANDLE_VALUE) {
            last_error_ = GetLastError();
            continue;
        }
        handle_ = candidate;
        last_error_ = ERROR_SUCCESS;
        opened = true;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return opened;
}

void MouseRelayClient::close() noexcept {
    if (!valid_handle(handle_)) {
        handle_ = nullptr;
        clear_session();
        return;
    }
    if (has_lease()) {
        (void)disarm();
    }
    CloseHandle(native_handle(handle_));
    handle_ = nullptr;
    clear_session();
}

bool MouseRelayClient::begin(std::uint64_t lease_id) {
    if (!is_open() || lease_id == 0) return false;
    COD_MOUSE_BEGIN_REQUEST request{};
    request.Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
    request.LeaseId = lease_id;
    COD_MOUSE_BEGIN_REPLY reply{};
    if (!ioctl(
            IOCTL_COD_MOUSE_BEGIN,
            &request,
            sizeof(request),
            &reply,
            sizeof(reply)) ||
        reply.Version != COD_MOUSE_RELAY_PROTOCOL_VERSION ||
        reply.Token.LeaseId != lease_id || reply.Token.CaptureEpoch == 0) {
        return false;
    }
    token_ = reply.Token;
    armed_ = false;
    return true;
}

bool MouseRelayClient::heartbeat() {
    return token_request(IOCTL_COD_MOUSE_HEARTBEAT);
}

bool MouseRelayClient::arm() {
    if (!token_request(IOCTL_COD_MOUSE_ARM)) return false;
    armed_ = true;
    return true;
}

bool MouseRelayClient::disarm() noexcept {
    if (!is_open() || !has_lease()) {
        clear_session();
        return true;
    }
    const bool delivered = token_request(IOCTL_COD_MOUSE_DISARM);
    clear_session();
    return delivered;
}

bool MouseRelayClient::emergency_disarm(
    void* native_handle_value,
    COD_MOUSE_RELAY_TOKEN token) noexcept {
    // The session captures this immutable handle/token pair before arming.
    // DeviceIoControl can therefore run on the independent hotkey thread
    // without taking a mutex that a broken controller tick could be holding.
    if (!valid_handle(native_handle_value) ||
        token.LeaseId == 0 || token.CaptureEpoch == 0) {
        return true;
    }
    COD_MOUSE_TOKEN_REQUEST request{};
    request.Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
    request.Token = token;
    DWORD bytes = 0;
    return DeviceIoControl(
        native_handle(native_handle_value),
        IOCTL_COD_MOUSE_DISARM,
        &request,
        sizeof(request),
        nullptr,
        0,
        &bytes,
        nullptr) != FALSE;
}

bool MouseRelayClient::read_batch(
    std::vector<COD_MOUSE_SOURCE_PACKET>* packets) {
    if (packets == nullptr || !has_lease()) return false;
    COD_MOUSE_TOKEN_REQUEST request{};
    request.Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
    request.Token = token_;
    COD_MOUSE_READ_BATCH batch{};
    unsigned long bytes = 0;
    if (!ioctl(
            IOCTL_COD_MOUSE_READ_BATCH,
            &request,
            sizeof(request),
            &batch,
            sizeof(batch),
            &bytes) ||
        batch.Version != COD_MOUSE_RELAY_PROTOCOL_VERSION ||
        batch.Count > COD_MOUSE_RELAY_MAX_BATCH) {
        return false;
    }
    const auto header_size = offsetof(COD_MOUSE_READ_BATCH, Packets);
    const auto required = header_size +
        static_cast<std::size_t>(batch.Count) * sizeof(COD_MOUSE_SOURCE_PACKET);
    if (bytes < required) return false;
    packets->assign(batch.Packets, batch.Packets + batch.Count);
    return true;
}

bool MouseRelayClient::submit_final(
    std::uint64_t report_sequence,
    std::uint64_t through_source_sequence,
    MouseSourceCounts counts) {
    if (!armed_ || report_sequence == 0) return false;
    COD_MOUSE_FINAL_REPORT report{};
    report.Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
    report.Token = token_;
    report.ReportSequence = report_sequence;
    report.ThroughSourceSequence = through_source_sequence;
    report.DxCounts = counts.dx;
    report.DyCounts = counts.dy;
    return ioctl(
        IOCTL_COD_MOUSE_SUBMIT_FINAL,
        &report,
        sizeof(report),
        nullptr,
        0);
}

bool MouseRelayClient::submit_calibration(MouseSourceCounts counts) {
    if (!has_lease()) return false;
    COD_MOUSE_CALIBRATION_REPORT report{};
    report.Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
    report.Token = token_;
    report.DxCounts = counts.dx;
    report.DyCounts = counts.dy;
    return ioctl(
        IOCTL_COD_MOUSE_SUBMIT_CALIBRATION,
        &report,
        sizeof(report),
        nullptr,
        0);
}

bool MouseRelayClient::status(COD_MOUSE_RELAY_STATUS* relay_status) {
    if (!is_open() || relay_status == nullptr) return false;
    *relay_status = {};
    if (!ioctl(
            IOCTL_COD_MOUSE_STATUS,
            nullptr,
            0,
            relay_status,
            sizeof(*relay_status))) {
        return false;
    }
    return relay_status->Version == COD_MOUSE_RELAY_PROTOCOL_VERSION;
}

bool MouseRelayClient::is_open() const noexcept { return valid_handle(handle_); }
bool MouseRelayClient::has_lease() const noexcept {
    return is_open() && token_.LeaseId != 0 && token_.CaptureEpoch != 0;
}
bool MouseRelayClient::armed() const noexcept { return armed_; }
unsigned long MouseRelayClient::last_error() const noexcept { return last_error_; }
COD_MOUSE_RELAY_TOKEN MouseRelayClient::token() const noexcept { return token_; }
void* MouseRelayClient::native_handle_for_emergency() const noexcept { return handle_; }

bool MouseRelayClient::token_request(unsigned long ioctl_code) {
    if (!has_lease()) return false;
    COD_MOUSE_TOKEN_REQUEST request{};
    request.Version = COD_MOUSE_RELAY_PROTOCOL_VERSION;
    request.Token = token_;
    return ioctl(ioctl_code, &request, sizeof(request), nullptr, 0);
}

bool MouseRelayClient::ioctl(
    unsigned long code,
    void* input,
    unsigned long input_size,
    void* output,
    unsigned long output_size,
    unsigned long* bytes_returned) {
    if (!is_open()) return false;
    DWORD bytes = 0;
    const BOOL success = DeviceIoControl(
        native_handle(handle_),
        code,
        input,
        input_size,
        output,
        output_size,
        &bytes,
        nullptr);
    if (!success) {
        last_error_ = GetLastError();
        return false;
    }
    last_error_ = ERROR_SUCCESS;
    if (bytes_returned != nullptr) *bytes_returned = bytes;
    return true;
}

void MouseRelayClient::clear_session() noexcept {
    token_ = {};
    armed_ = false;
}

}  // namespace mouse_native
