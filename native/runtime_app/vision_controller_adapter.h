#pragma once

#include "../controller_native/controller_vision_snapshot.h"
#include "pipeline_contract/committed_capture_observation.h"

#include "../vision_native/include/vision_native/types.h"

namespace runtime_app {

controller_native::ControllerVisionSnapshot adapt_vision_result(
    const vision_native::VisionResult& result);

pipeline_contract::CommittedCaptureObservation
adapt_committed_capture_observation(
    const vision_native::VisionResult& result,
    const pipeline_contract::TargetPlan& committed_plan,
    std::uint64_t ads_epoch,
    std::uint64_t controller_consume_ns = 0);

}  // namespace runtime_app
