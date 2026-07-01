#pragma once

#include "controller_tick_context.h"

#include "../tracking_native/tracker_contract.h"
#include "../vision_native/include/vision_native/types.h"

#include <cstdint>
#include <vector>

namespace controller_native {

struct ControllerVisionSnapshot {
    bool frame_updated = false;
    NativeControllerVisionState state;
    std::vector<tracking_native::TrackerDetection> tracker_detections;
    std::uint64_t frame_id = 0;
    double capture_time_seconds = 0.0;
    double ready_time_seconds = 0.0;
};

ControllerVisionSnapshot adapt_vision_result(
    const vision_native::VisionResult& result);

}  // namespace controller_native
