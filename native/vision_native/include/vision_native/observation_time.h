#pragma once

#include "vision_native/types.h"

namespace vision_native {

// Keep captured_at_ns as the copy-completion telemetry field. Controller
// observations use the image's calibrated presentation time when supplied.
inline uint64_t observation_time_ns(const VisionResult& result) noexcept {
    if (result.source_present_steady_available) {
        return result.source_present_steady_ns;
    }
    // A DXGI source whose present clock could not be mapped is not eligible
    // for control. Non-DXGI/offline producers retain their capture contract.
    if (result.source_present_available) return 0;
    return result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns;
}

}  // namespace vision_native
