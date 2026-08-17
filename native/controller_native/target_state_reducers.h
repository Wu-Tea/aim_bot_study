#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/vision_observation.h"

namespace controller_native {

struct TargetGeometrySnapshot {
    pipeline_contract::Vec2f source_position{};
    common_native::Box2f aim_region{};
    pipeline_contract::AimRegionSource source =
        pipeline_contract::AimRegionSource::None;
    bool available = false;
};

// Sole owner of R and the source-owned anatomical point.
class TargetGeometryReducer {
public:
    void reset() noexcept;
    void adopt(
        const pipeline_contract::VisionCandidate& candidate,
        bool cue_continuation) noexcept;
    const TargetGeometrySnapshot& snapshot() const noexcept { return state_; }

private:
    TargetGeometrySnapshot state_{};
};

struct TargetLifecycleSnapshot {
    pipeline_contract::TargetLifecycle lifecycle =
        pipeline_contract::TargetLifecycle::None;
    std::uint64_t target_id = 0;
    bool target_present = false;
};

// Sole owner of controller-persistent target identity and lifecycle.
class TargetLifecycleReducer {
public:
    void reset() noexcept {
        state_ = {};
        next_target_id_ = 1;
    }
    void observe(bool cue_continuation, bool force_new_identity) noexcept {
        if (!state_.target_present || force_new_identity) {
            state_.target_id = next_target_id_++;
        }
        state_.target_present = true;
        state_.lifecycle = cue_continuation
            ? pipeline_contract::TargetLifecycle::CueContinuation
            : pipeline_contract::TargetLifecycle::Observed;
    }
    void clear() noexcept { state_ = {}; }
    const TargetLifecycleSnapshot& snapshot() const noexcept { return state_; }

private:
    TargetLifecycleSnapshot state_{};
    std::uint64_t next_target_id_ = 1;
};

struct DesiredPointConfig {
    float traversal_ms = 180.0f;
    float boundary_exit_ms = 50.0f;
};

struct DesiredPointSnapshot {
    pipeline_contract::Vec2f position{};
    pipeline_contract::Vec2f normalized{};
    pipeline_contract::DesiredPointSource source =
        pipeline_contract::DesiredPointSource::None;
    bool user_active = false;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_boundary_x = false;
    bool manual_boundary_y = false;
    bool manual_exit_requested = false;
};

// Sole owner of D and the bounded user-correction lifecycle inside R.
class DesiredPointReducer {
public:
    explicit DesiredPointReducer(DesiredPointConfig config = {}) noexcept
        : config_(config) {}

    void reset() noexcept;
    void adopt_geometry(
        const TargetGeometrySnapshot& geometry,
        bool cue_continuation,
        bool reset_desired_point,
        bool previous_geometry_available) noexcept;
    void reduce_manual(
        const pipeline_contract::IntentState& intent,
        const TargetGeometrySnapshot& geometry,
        bool target_present,
        bool firing_recently,
        float dt_seconds) noexcept;
    const DesiredPointSnapshot& snapshot() const noexcept { return state_; }

private:
    DesiredPointConfig config_{};
    DesiredPointSnapshot state_{};
    float boundary_seconds_x_ = 0.0f;
    float boundary_seconds_y_ = 0.0f;
};

// Sole owner of the mutually exclusive Manual/ADS/BodyLock mode.
class AimModeReducer {
public:
    void reset() noexcept {
        mode_ = pipeline_contract::ControlMode::Manual;
    }
    void transition(pipeline_contract::ControlMode next) noexcept {
        mode_ = next;
    }
    pipeline_contract::ControlMode mode() const noexcept { return mode_; }

private:
    pipeline_contract::ControlMode mode_ =
        pipeline_contract::ControlMode::Manual;
};

}  // namespace controller_native
