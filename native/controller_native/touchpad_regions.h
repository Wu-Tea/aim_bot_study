#pragma once

namespace controller_native {

// Shared boundary: the rightmost 25% is split into fire (top 60%, inclusive)
// and triangle (bottom 40%, exclusive). A contact belongs to one region only.
inline constexpr float kTouchpadMacroSplitY = 0.60f;

}  // namespace controller_native
