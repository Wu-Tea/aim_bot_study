#pragma once

#include "blind_window_baseline.h"
#include "runtime_config.h"

namespace controller_native::blind_window {

BlindControllerFactory native_bodylock_controller_factory(
    GamepadRuntimeConfig config,
    double assist_scale = 1.0);

}  // namespace controller_native::blind_window
