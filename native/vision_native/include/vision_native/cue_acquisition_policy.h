#pragma once
#include "vision_native/cue_identity.h"
#include "vision_native/types.h"
#include <optional>

namespace vision_native {
struct CueAcquisitionContext {
    float center_x, center_y, width, height, pickup_radius, downward_offset;
};
struct CueAcquisitionPoint {
    CueObservation cue;
    float x, y, radius;
};
// Pure geometry/selection policy. Identity confirmation remains owned by the
// selector; this module holds no history, controller state or image buffers.
CueSearchRegion cue_acquisition_search_region(const CueAcquisitionContext& context);
std::optional<CueAcquisitionPoint> choose_cue_acquisition(
    const std::vector<CueObservation>& cues, const std::vector<Detection>& people,
    const CueAcquisitionContext& context);
}
