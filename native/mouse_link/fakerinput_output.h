#pragma once
#include "virtual_mouse_packets.h"
#include <Windows.h>
#include <string>

namespace virtual_mouse {
class FakerOutput {
public:
    ~FakerOutput();
    bool open();
    bool send(const Report& report);
    void close();
    int axis_limit() const { return axis_limit_; }
    const std::wstring& raw_path() const { return raw_path_; }
    unsigned long error() const { return error_; }
private:
    HANDLE control_ = INVALID_HANDLE_VALUE, event_ = nullptr;
    std::wstring raw_path_;
    int axis_limit_ = 0;
    unsigned long error_ = 0;
    bool write(HANDLE device, const void* data, unsigned long size);
};
std::wstring raw_path_for_hardware(const std::wstring& hardware, bool require_physical_bus = false);
} // namespace virtual_mouse
