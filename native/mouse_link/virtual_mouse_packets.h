#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace virtual_mouse {
enum class Mode { Pass, Block, Invert };
struct Source { unsigned state = 0, flags = 0; int wheel = 0, x = 0, y = 0; };
struct Report { std::uint8_t buttons = 0; std::int16_t x = 0, y = 0; std::int8_t wheel = 0, hwheel = 0; };
struct Pending {
    std::uint8_t buttons = 0;
    std::int64_t x = 0, y = 0;
    int wheel = 0, hwheel = 0;
    bool first = true;
    bool empty() const { return !first && !x && !y && !wheel && !hwheel; }
    Report next(int axis_limit) {
        const auto dx = std::clamp<std::int64_t>(x, -axis_limit, axis_limit);
        const auto dy = std::clamp<std::int64_t>(y, -axis_limit, axis_limit);
        const int dw = std::clamp(wheel, -127, 127), dh = std::clamp(hwheel, -127, 127);
        x -= dx; y -= dy; wheel -= dw; hwheel -= dh; first = false;
        return {buttons, static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy),
                static_cast<std::int8_t>(dw), static_cast<std::int8_t>(dh)};
    }
};
// Validate the source format before consuming any edge. These boundaries are
// genuine device-format limits, not fallbacks that can leak a second output.
inline bool translate(const Source& source, Mode mode, std::uint8_t held, Pending& out) {
    if ((source.flags & ~8u) || (source.state & ~0xfffu)) return false;
    if ((source.state & 0xc00u) ? (source.wheel % 120 != 0) : source.wheel != 0) return false;
    std::uint8_t buttons = held;
    for (unsigned i = 0; i < 5; ++i) {
        const unsigned edges = (source.state >> (2 * i)) & 3u;
        if (edges == 3) return false; // No ordering information in this packet.
        if (edges == 1) buttons |= static_cast<std::uint8_t>(1u << i);
        if (edges == 2) buttons &= static_cast<std::uint8_t>(~(1u << i));
    }
    out = {}; out.buttons = buttons;
    const std::int64_t sign = mode == Mode::Invert ? -1 : mode == Mode::Block ? 0 : 1;
    out.x = sign * source.x; out.y = sign * source.y;
    out.wheel = source.state & 0x400 ? source.wheel / 120 : 0;
    out.hwheel = source.state & 0x800 ? source.wheel / 120 : 0;
    return true;
}
inline std::array<std::uint8_t, 65> encode(const Report& r) {
    std::array<std::uint8_t, 65> bytes{};
    bytes[0] = 0x40; bytes[1] = 8; bytes[2] = 3; bytes[3] = r.buttons;
    const auto x = static_cast<std::uint16_t>(r.x), y = static_cast<std::uint16_t>(r.y);
    bytes[4] = static_cast<std::uint8_t>(x); bytes[5] = static_cast<std::uint8_t>(x >> 8);
    bytes[6] = static_cast<std::uint8_t>(y); bytes[7] = static_cast<std::uint8_t>(y >> 8);
    bytes[8] = static_cast<std::uint8_t>(r.wheel); bytes[9] = static_cast<std::uint8_t>(r.hwheel);
    return bytes;
}
} // namespace virtual_mouse
