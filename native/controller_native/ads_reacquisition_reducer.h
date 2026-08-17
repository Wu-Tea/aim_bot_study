#pragma once

#include "aim_scope_reducer.h"
#include "target_geometry.h"
#include "pipeline_contract/control_event.h"
#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/vision_observation.h"

namespace controller_native {

struct AdsReacquisitionConfig {
    float bodylock_base_radius_px = 150.0f;
    unsigned int max_fresh_frames_waiting = 8;
};

struct AdsReacquisitionDecision {
    bool emitted = false;
    bool request_created = false;
    bool begin_ads_epoch = false;
    pipeline_contract::AdsReacquireDecisionCode code =
        pipeline_contract::AdsReacquireDecisionCode::DeferredWaitingForFreshVision;
    pipeline_contract::EventSequence cause_event{};
    pipeline_contract::VisionFrameId after_frame{};
    pipeline_contract::TargetGeneration bound_target_generation{};
    float observed_error_px = 0.0f;
    float dynamic_bodylock_radius_px = 0.0f;
};

// Converts a physical ADS edge into one bounded reacquisition request. The
// request is evaluated only against a fresh source frame newer than the event;
// a held LT sample can never synthesize another epoch.
class AdsReacquisitionReducer {
public:
    explicit AdsReacquisitionReducer(
        AdsReacquisitionConfig config = {}) noexcept
        : config_(config) {}

    void reset() noexcept { pending_ = {}; }

    AdsReacquisitionDecision on_input(
        const AimScopeSnapshot& scope,
        const pipeline_contract::TargetPlan& current_plan,
        pipeline_contract::EventSequence cause_event) noexcept;

    AdsReacquisitionDecision on_fresh_observation(
        const pipeline_contract::VisionObservationBatch& observations,
        const pipeline_contract::TargetPlan& current_plan,
        bool physical_ads_active) noexcept;

    bool pending() const noexcept { return pending_.active; }

private:
    struct PendingRequest {
        bool active = false;
        pipeline_contract::EventSequence cause_event{};
        pipeline_contract::VisionFrameId after_frame{};
        pipeline_contract::TargetGeneration bound_target_generation{};
        unsigned int fresh_frames_waited = 0;
    };

    AdsReacquisitionConfig config_{};
    PendingRequest pending_{};
};

}  // namespace controller_native
