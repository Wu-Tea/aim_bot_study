#include "fakerinput_output.h"
#include <hidsdi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <vector>

namespace virtual_mouse {
namespace {
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
struct Preparsed { PHIDP_PREPARSED_DATA value = nullptr; ~Preparsed() { if (value) HidD_FreePreparsedData(value); } };
struct DeviceSet { HDEVINFO value; ~DeviceSet() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); } };
const GUID mouse_guid{0x378de44c, 0x56ef, 0x11d1, {0xbc,0x8c,0,0xa0,0xc9,0x14,0x05,0xdd}};
template<class Callback> bool interfaces(const GUID& guid, Callback callback) {
    DeviceSet set{SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (set.value == INVALID_HANDLE_VALUE) return false;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA info{sizeof(info)};
        if (!SetupDiEnumDeviceInterfaces(set.value, nullptr, &guid, index, &info))
            return GetLastError() == ERROR_NO_MORE_ITEMS;
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set.value, &info, nullptr, 0, &size, nullptr);
        if (!size) return false;
        std::vector<BYTE> bytes(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA dev{sizeof(dev)};
        if (!SetupDiGetDeviceInterfaceDetailW(set.value, &info, detail, size, nullptr, &dev)) return false;
        callback(set.value, dev, std::wstring(detail->DevicePath));
    }
}
bool attrs(HANDLE handle, HIDD_ATTRIBUTES& attributes, Preparsed& data, HIDP_CAPS& caps) {
    attributes.Size = sizeof(attributes);
    return HidD_GetAttributes(handle, &attributes) && HidD_GetPreparsedData(handle, &data.value)
        && HidP_GetCaps(data.value, &caps) == HIDP_STATUS_SUCCESS;
}
}
std::wstring raw_path_for_hardware(const std::wstring& hardware, bool require_physical_bus) {
    std::vector<std::wstring> matched;
    if (!interfaces(mouse_guid, [&](HDEVINFO set, SP_DEVINFO_DATA& dev, const std::wstring& path) {
        if (require_physical_bus) {
            DEVINST node = dev.DevInst;
            bool physical = false;
            for (int depth = 0; depth < 16; ++depth) {
                wchar_t id[MAX_DEVICE_ID_LEN]{};
                if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
                if (_wcsnicmp(id, L"USB\\VID_", 8) == 0 || _wcsnicmp(id, L"BTHENUM\\", 8) == 0
                        || _wcsnicmp(id, L"BTHLEDEVICE\\", 12) == 0) { physical = true; break; }
                DEVINST parent = 0;
                if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
                node = parent;
            }
            if (!physical) return;
        }
        wchar_t ids[4096]{};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(ids), sizeof(ids) - sizeof(wchar_t), nullptr)) return;
        for (const wchar_t* id = ids; *id; id += wcslen(id) + 1)
            if (_wcsicmp(id, hardware.c_str()) == 0) { matched.push_back(path); break; }
    })) return {};
    return matched.size() == 1 ? matched.front() : L"";
}
FakerOutput::~FakerOutput() { close(); }
void FakerOutput::close() {
    if (control_ != INVALID_HANDLE_VALUE) CloseHandle(control_);
    if (event_) CloseHandle(event_);
    control_ = INVALID_HANDLE_VALUE; event_ = nullptr; raw_path_.clear(); axis_limit_ = 0;
}
bool FakerOutput::write(HANDLE device, const void* data, unsigned long size) {
    OVERLAPPED overlapped{}; overlapped.hEvent = event_;
    ResetEvent(event_); DWORD written = 0;
    if (!WriteFile(device, data, size, &written, &overlapped)) {
        error_ = GetLastError();
        if (error_ != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(event_, 50) != WAIT_OBJECT_0) {
            CancelIoEx(device, &overlapped);
            GetOverlappedResult(device, &overlapped, &written, TRUE);
            error_ = ERROR_TIMEOUT; return false;
        }
        if (!GetOverlappedResult(device, &overlapped, &written, FALSE)) { error_ = GetLastError(); return false; }
    }
    if (written != size) { error_ = ERROR_WRITE_FAULT; return false; }
    error_ = 0; return true;
}
bool FakerOutput::open() {
    close(); error_ = ERROR_DEVICE_NOT_CONNECTED;
    GUID hid{}; HidD_GetHidGuid(&hid);
    std::vector<std::wstring> controls, methods;
    if (!interfaces(hid, [&](HDEVINFO, SP_DEVINFO_DATA&, const std::wstring& path) {
        Handle file{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) return;
        HIDD_ATTRIBUTES a{}; Preparsed data; HIDP_CAPS caps{};
        if (!attrs(file.value, a, data, caps) || a.VendorID != 0xfe0f || a.ProductID != 0x00ff) return;
        if (caps.UsagePage == 0xff00 && caps.OutputReportByteLength == 65) {
            if (caps.Usage == 1) controls.push_back(path);
            if (caps.Usage == 2) methods.push_back(path);
        }
    }) || controls.size() != 1 || methods.size() != 1) return false;
    unsigned relative_count = 0;
    if (!interfaces(mouse_guid, [&](HDEVINFO, SP_DEVINFO_DATA&, const std::wstring& path) {
        Handle file{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) return;
        HIDD_ATTRIBUTES a{}; Preparsed data; HIDP_CAPS caps{};
        if (!attrs(file.value, a, data, caps) || a.VendorID != 0xfe0f || a.ProductID != 0x00ff) return;
        USHORT count = caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> values(count);
        if (!count || HidP_GetValueCaps(HidP_Input, values.data(), &count, data.value) != HIDP_STATUS_SUCCESS) return;
        bool x = false, y = false; int limit = 32767;
        for (USHORT i = 0; i < count; ++i) {
            const auto& v = values[i];
            if (v.ReportID != 3 || v.UsagePage != 1 || v.IsAbsolute || v.LogicalMin >= 0 || v.LogicalMax <= 0) continue;
            const auto lo = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
            const auto hi = v.IsRange ? v.Range.UsageMax : v.NotRange.Usage;
            if (lo <= 0x30 && hi >= 0x30) x = true;
            if (lo <= 0x31 && hi >= 0x31) y = true;
            if (lo <= 0x31 && hi >= 0x30)
                limit = static_cast<int>(std::min<std::int64_t>({limit, -static_cast<std::int64_t>(v.LogicalMin), v.LogicalMax}));
        }
        if (x && y) { ++relative_count; raw_path_ = path; axis_limit_ = limit; }
    }) || relative_count != 1 || axis_limit_ <= 0) { close(); return false; }
    event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // One producer owns the control endpoint during a relay session. An
    // already connected mapper makes this fail before physical capture starts.
    control_ = CreateFileW(controls[0].c_str(), GENERIC_WRITE | GENERIC_READ,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    Handle method{CreateFileW(methods[0].c_str(), GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
    if (!event_ || control_ == INVALID_HANDLE_VALUE || method.value == INVALID_HANDLE_VALUE) { error_ = GetLastError(); close(); return false; }
    std::array<BYTE, 65> api{}; api[0] = 0x41; api[4] = 1;
    if (!write(method.value, api.data(), static_cast<DWORD>(api.size()))) { close(); return false; }
    api = {}; api[0] = 0x42;
    if (!HidD_GetFeature(method.value, api.data(), static_cast<ULONG>(api.size())) || api[4] != 1 || api[5] || api[6] || api[7]) {
        error_ = ERROR_REVISION_MISMATCH; close(); return false;
    }
    error_ = 0; return true;
}
bool FakerOutput::send(const Report& report) {
    if (control_ == INVALID_HANDLE_VALUE) { error_ = ERROR_INVALID_HANDLE; return false; }
    if (report.x < -axis_limit_ || report.x > axis_limit_ || report.y < -axis_limit_ || report.y > axis_limit_ || report.buttons > 31) {
        error_ = ERROR_INVALID_DATA; return false;
    }
    const auto bytes = encode(report);
    return write(control_, bytes.data(), static_cast<DWORD>(bytes.size()));
}
} // namespace virtual_mouse
