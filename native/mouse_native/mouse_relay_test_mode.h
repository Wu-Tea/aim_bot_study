#pragma once

#include "mouse_native/mouse_control_types.h"
#include <cstdint>
#include <limits>

namespace mouse_native {

// Explicit link diagnostics inside the normal runtime. None is the controller
// path. Tests never start Vision or synthesize clicks/calibration reports.
enum class MouseRelayTestMode { None, Passthrough, Block, Invert };

inline MouseSourceCounts relay_test_output(
    MouseRelayTestMode mode, MouseSourceCounts source) noexcept {
    if (mode == MouseRelayTestMode::Block) return {};
    if (mode == MouseRelayTestMode::Invert) {
        const auto inverse = [](std::int32_t value) {
            return value == std::numeric_limits<std::int32_t>::min()
                ? std::numeric_limits<std::int32_t>::max() : -value;
        };
        return {inverse(source.dx), inverse(source.dy)};
    }
    return source;
}

}  // namespace mouse_native
