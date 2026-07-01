#pragma once

#include "controller_vision_snapshot.h"
#include "output_mixer.h"
#include "runtime_config.h"

#include "../tracking_native/tracker_backend.h"

#include <memory>
#include <vector>

namespace controller_native {

class TargetSnapshotProvider {
public:
    TargetSnapshotProvider(
        GamepadAiAimConfig ai_config = {},
        tracking_native::TrackerBackendKind tracker_backend =
            tracking_native::TrackerBackendKind::FpsReference);

    void reset();
    void submit_vision_state(
        const NativeControllerVisionState& state,
        double now_seconds,
        bool ads_active);
    void submit_vision_snapshot(
        const ControllerVisionSnapshot& snapshot,
        double now_seconds,
        bool ads_active);
    NativeControllerVisionState vision_state_for_frame(
        double now_seconds,
        bool ads_active);
    void record_output(
        const NativeControllerOutputComponents& components,
        double now_seconds);
    void clear_ads_transient_state();
    bool candidate_reacquire_snap_active(double now_seconds) const;
    bool candidate_output_hold_active(double now_seconds);

private:
    void ingest_tracker_observation(
        const NativeControllerVisionState& state,
        const std::vector<tracking_native::TrackerDetection>& detections,
        std::uint64_t frame_id,
        double capture_time_seconds,
        double ready_time_seconds,
        double fallback_now_seconds);
    NativeControllerVisionState credibility_gated_vision_state(
        const NativeControllerVisionState& state,
        double query_time_seconds,
        bool ads_active,
        bool* suppress_tracker_ingest);
    bool has_fresh_aim_target(
        const NativeControllerVisionState& vision_state,
        double now_seconds) const;
    bool is_strong_aim_target(const NativeControllerVisionState& vision_state) const;

    GamepadAiAimConfig ai_config_;
    std::unique_ptr<tracking_native::TrackerBackend> target_tracker_;
    NativeControllerVisionState latest_vision_state_;
    double last_output_at_seconds_ = 0.0;
    std::uint64_t latest_vision_sequence_ = 0;
    std::uint64_t raw_vision_sequence_consumed_ = 0;
    bool has_committed_target_ = false;
    float committed_target_dx_ = 0.0f;
    float committed_target_dy_ = 0.0f;
    bool has_candidate_target_ = false;
    float candidate_target_dx_ = 0.0f;
    float candidate_target_dy_ = 0.0f;
    double candidate_first_observed_at_seconds_ = 0.0;
    double candidate_last_observed_at_seconds_ = 0.0;
    int candidate_fresh_samples_ = 0;
    double candidate_projection_hold_until_seconds_ = 0.0;
    double candidate_reacquire_snap_until_seconds_ = 0.0;
    double candidate_output_hold_until_seconds_ = 0.0;
};

}  // namespace controller_native
