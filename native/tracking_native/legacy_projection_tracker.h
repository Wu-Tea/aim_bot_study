#pragma once

#include "tracker_backend.h"
#include "tracker_contract.h"

#include "../controller_native/target_tracker.h"

namespace tracking_native {

class LegacyProjectionTracker final : public TrackerBackend {
public:
    explicit LegacyProjectionTracker(
        controller_native::NativeTargetTrackerConfig config = {});

    void reset() override;
    void ingest(const TrackerObservation& observation) override;
    void push_control_sample(const TrackerControlSample& sample) override;
    TrackerSnapshot query(const TrackerQuery& query) const override;
    std::vector<TrackerDebugTrack> debug_tracks() const override;

private:
    controller_native::NativeGamepadTargetTracker inner_;
    common_native::Vec2f last_aim_error_px_;
    common_native::Box2f last_body_box_px_;
    bool last_has_body_box_ = false;
};

}  // namespace tracking_native
