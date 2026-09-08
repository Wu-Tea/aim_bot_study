#pragma once
#include "link_contract.h"
#include <Windows.h>

namespace mouse_link {
constexpr LONG kMagic = 0x4D4C3031;
inline LONG64 read64(volatile LONG64& value) { return InterlockedCompareExchange64(&value, 0, 0); }
inline LONG read32(volatile LONG& value) { return InterlockedCompareExchange(&value, 0, 0); }
struct alignas(8) Counters {
    volatile LONG64 packets = 0, x = 0, y = 0, abs_x = 0, abs_y = 0;
    volatile LONG64 buttons = 0, wheel = 0;
    void add(int dx, int dy, unsigned flags, short rolling) {
        InterlockedIncrement64(&packets);
        InterlockedAdd64(&x, dx); InterlockedAdd64(&y, dy);
        InterlockedAdd64(&abs_x, dx < 0 ? -static_cast<LONG64>(dx) : dx);
        InterlockedAdd64(&abs_y, dy < 0 ? -static_cast<LONG64>(dy) : dy);
        if (flags) InterlockedIncrement64(&buttons);
        InterlockedAdd64(&wheel, rolling);
    }
    Totals snapshot() { return {read64(packets), read64(x), read64(y), read64(abs_x), read64(abs_y)}; }
};
struct alignas(8) Phase {
    volatile LONG mode = 0;
    volatile LONG64 unmarked_baseline = 0;
    Counters source{}, sent{}, received{};
};
struct alignas(8) Shared {
    LONG magic = kMagic;
    unsigned marker = 0;
    volatile LONG shutdown = 0, receiver_shutdown = 0, device = 0, command = 0;
    volatile LONG driver_state = 0, receiver_state = 0, worker_state = 0;
    volatile LONG acknowledged = 0, error = 0;
    volatile LONG64 worker_heartbeat = 0, receiver_heartbeat = 0;
    // No marker does not mean physical: untagged SendInput may reach WM_INPUT.
    volatile LONG64 unmarked_motion = 0, tagged_packets = 0;
    volatile LONG64 active_since = 0;
    wchar_t hardware[10][256]{};
    Phase phases[kMaxPhases]{};
};
struct Mapping {
    HANDLE handle = nullptr;
    Shared* state = nullptr;
    bool open(const wchar_t* name, bool create) {
        handle = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                     0, sizeof(Shared), name) : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!handle) return false;
        state = static_cast<Shared*>(MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        return state != nullptr;
    }
    ~Mapping() { if (state) UnmapViewOfFile(state); if (handle) CloseHandle(handle); }
};
} // namespace mouse_link
