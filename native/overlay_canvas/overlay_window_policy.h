#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace fusion_overlay {

inline constexpr DWORD fusion_canvas_extended_style() noexcept {
    return WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
        WS_EX_LAYERED | WS_EX_TRANSPARENT;
}

inline constexpr DWORD fusion_canvas_window_style() noexcept {
    return WS_POPUP | WS_DISABLED;
}

inline constexpr bool supports_layered_passthrough_style(
    DWORD extended_style) noexcept {
    constexpr DWORD required = WS_EX_LAYERED | WS_EX_TRANSPARENT;
    return (extended_style & required) == required &&
        (extended_style & WS_EX_NOREDIRECTIONBITMAP) == 0;
}

inline constexpr bool satisfies_mouse_passthrough_contract(
    DWORD extended_style,
    DWORD window_style) noexcept {
    return supports_layered_passthrough_style(extended_style) &&
        (window_style & WS_DISABLED) != 0;
}

}  // namespace fusion_overlay
