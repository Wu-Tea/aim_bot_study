#pragma once

#include "controller_native/runtime_config.h"

namespace runtime_app {

// Configuration policy only: no worker, device or shared-memory dependency.
std::string hot_reload_restrictions(const controller_native::RuntimeConfig& before,
                                   const controller_native::RuntimeConfig& after);
bool preserve_response_learning_on_reload(const controller_native::RuntimeConfig& before,
                                          const controller_native::RuntimeConfig& after);

} // namespace runtime_app
