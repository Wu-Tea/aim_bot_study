#pragma once

namespace controller_native {

// Shared boundary: the rightmost 25% is split into fire (top 75%, inclusive)
// and triangle (bottom 25%, exclusive). A contact belongs to one region only.
inline constexpr float kTouchpadMacroSplitY = 0.75f;

}  // namespace controller_native
