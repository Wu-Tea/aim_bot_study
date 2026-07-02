#pragma once

#include "../controller_native/controller_vision_snapshot.h"

#include "../vision_native/include/vision_native/types.h"

namespace runtime_app {

controller_native::ControllerVisionSnapshot adapt_vision_result(
    const vision_native::VisionResult& result);

}  // namespace runtime_app
