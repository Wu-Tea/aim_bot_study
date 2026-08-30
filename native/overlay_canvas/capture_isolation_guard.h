#pragma once

#include "fusion_overlay_contract.h"

#include <Windows.h>

namespace fusion_overlay {

struct CaptureIsolationVerification {
    CaptureIsolationObservation observation;
    CaptureIsolationDecision decision;
    HRESULT dwm_hresult = E_FAIL;
    DWORD set_error = ERROR_SUCCESS;
    DWORD readback_error = ERROR_SUCCESS;
};

CaptureIsolationVerification enable_and_verify_capture_isolation(HWND hwnd) noexcept;
CaptureIsolationVerification inspect_capture_isolation(HWND hwnd) noexcept;

}  // namespace fusion_overlay
