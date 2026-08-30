#include "capture_isolation_guard.h"

#include <dwmapi.h>

namespace fusion_overlay {
namespace {

CaptureIsolationVerification observe(HWND hwnd, bool set_affinity) noexcept {
    CaptureIsolationVerification verification;

    BOOL composition_enabled = FALSE;
    verification.dwm_hresult = DwmIsCompositionEnabled(&composition_enabled);
    verification.observation.dwm_query_succeeded =
        SUCCEEDED(verification.dwm_hresult);
    verification.observation.dwm_composition_enabled =
        verification.observation.dwm_query_succeeded && composition_enabled != FALSE;

    if (set_affinity) {
        SetLastError(ERROR_SUCCESS);
        verification.observation.set_affinity_succeeded =
            SetWindowDisplayAffinity(
                hwnd,
                static_cast<DWORD>(kExcludeFromCaptureAffinity)) != FALSE;
        verification.set_error = verification.observation.set_affinity_succeeded
            ? ERROR_SUCCESS
            : GetLastError();
    } else {
        // Inspection verifies an affinity that was successfully set at startup.
        verification.observation.set_affinity_succeeded = true;
    }

    DWORD affinity = WDA_NONE;
    SetLastError(ERROR_SUCCESS);
    verification.observation.readback_succeeded =
        GetWindowDisplayAffinity(hwnd, &affinity) != FALSE;
    verification.readback_error = verification.observation.readback_succeeded
        ? ERROR_SUCCESS
        : GetLastError();
    verification.observation.affinity = static_cast<std::uint32_t>(affinity);
    verification.decision = decide_capture_isolation(verification.observation);
    return verification;
}

}  // namespace

CaptureIsolationVerification enable_and_verify_capture_isolation(HWND hwnd) noexcept {
    return observe(hwnd, true);
}

CaptureIsolationVerification inspect_capture_isolation(HWND hwnd) noexcept {
    return observe(hwnd, false);
}

}  // namespace fusion_overlay
