#pragma once

#include "controller_vision_snapshot.h"

#include "../vision_native/include/vision_native/types.h"

namespace controller_native {

ControllerVisionSnapshot adapt_vision_result(
    const vision_native::VisionResult& result);

}  // namespace controller_native
