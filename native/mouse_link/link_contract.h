#pragma once
#include <interception.h>
#include <cstdint>
#include <limits>

namespace mouse_link {
enum class Mode : unsigned { Bypass = 0, Relay = 1, Block = 2, Invert = 3, Pulse = 4 };
constexpr unsigned kMaxPhases = 256;
constexpr int kPulseCounts = 80;

// Device counts, not screen/cursor coordinates. The input packet is consumed
// by Interception; this one report is its replacement, never an added force.
inline bool transform(const InterceptionMouseStroke& source, Mode mode,
                      unsigned tag, InterceptionMouseStroke& output) noexcept {
    output = source;
    if (mode == Mode::Bypass) return true;
    if ((source.flags & INTERCEPTION_MOUSE_MOVE_ABSOLUTE) != 0) return false;
    if (mode == Mode::Block || mode == Mode::Pulse) output.x = output.y = 0;
    else if (mode == Mode::Invert) {
        if (source.x == std::numeric_limits<int>::min() ||
            source.y == std::numeric_limits<int>::min()) return false;
        output.x = -source.x;
        output.y = -source.y;
    } else if (mode != Mode::Relay) return false;
    output.flags |= INTERCEPTION_MOUSE_MOVE_NOCOALESCE;
    output.information = tag;
    return true;
}

struct Totals {
    std::int64_t packets = 0, x = 0, y = 0, abs_x = 0, abs_y = 0;
};
enum class Evidence { WaitingForSource, WaitingForReceiver, Mismatch, Matched };
inline Evidence evaluate(Mode mode, const Totals& source, const Totals& sent,
                         const Totals& received, std::int64_t unmarked_motion,
                         bool receiver_alive, bool saw_tagged_report) noexcept {
    if (!receiver_alive || !saw_tagged_report) return Evidence::WaitingForReceiver;
    if (mode != Mode::Pulse &&
        (source.packets < 8 || source.abs_x + source.abs_y < 32))
        return Evidence::WaitingForSource;
    if (mode == Mode::Bypass) return Evidence::WaitingForSource;
    if (unmarked_motion != 0 || sent.x != received.x || sent.y != received.y ||
        sent.abs_x != received.abs_x || sent.abs_y != received.abs_y)
        return Evidence::Mismatch;
    // A zero/zero check is meaningful only with nonzero physical source work.
    if ((mode == Mode::Block && (sent.abs_x || sent.abs_y)) ||
        (mode == Mode::Pulse && (sent.x != kPulseCounts || sent.y != 0)))
        return Evidence::Mismatch;
    return Evidence::Matched;
}
} // namespace mouse_link
