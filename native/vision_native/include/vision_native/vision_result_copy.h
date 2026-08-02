#pragma once

#include "types.h"

namespace vision_native {

inline void copy_selector_identity_fields(
    VisionResult& destination,
    const VisionResult& source) noexcept {
    destination.selector_identity_protocol = source.selector_identity_protocol;
    destination.selector_target_generation = source.selector_target_generation;
    destination.selector_target_changed = source.selector_target_changed;
    destination.has_selected_detection = source.has_selected_detection;
    destination.selected_detection_index = source.selected_detection_index;
}

}  // namespace vision_native
