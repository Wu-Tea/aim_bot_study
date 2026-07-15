#pragma once

#include "../common_native/screen_geometry.h"
#include "../pipeline_contract/assist_authority.h"

#include "runtime_config.h"

#include <cstdint>

namespace controller_native {

struct NativeAimAssistDynamicsInput {
    common_native::Vec2f manual;
    common_native::Vec2f requested_assist;
    pipeline_contract::AssistAuthorityState authority =
        pipeline_contract::AssistAuthorityState::Reject;
    pipeline_contract::BodylockLifecycleState lifecycle =
        pipeline_contract::BodylockLifecycleState::Inactive;
    common_native::Vec2f target_error_px;
    bool ads_snap_active = false;
    bool fresh_observation = false;
    std::uint64_t vision_sequence = 0;
    std::uint64_t selected_track_id = 0;
    double position_sigma = 0.0;
    double dt_seconds = 0.001;
    double now_seconds = 0.0;
};

struct NativeAimAssistDynamicsOutput {
    common_native::Vec2f assist;
    const char* limit_reason = "none";
};

class NativeAimAssistDynamics {
public:
    explicit NativeAimAssistDynamics(GamepadAimAssistDynamicsConfig config = {});

    void reset();
    void observe_pre_recoil_output(
        common_native::Vec2f manual,
        common_native::Vec2f pre_recoil,
        bool ads_snap_active,
        std::uint64_t selected_track_id);
    void observe_ads_snap_crossing(const NativeAimAssistDynamicsInput& input);
    [[nodiscard]] bool ads_handoff_assist(
        std::uint64_t selected_track_id,
        common_native::Vec2f* out_assist) const;
    [[nodiscard]] NativeAimAssistDynamicsOutput apply(
        const NativeAimAssistDynamicsInput& input);
    [[nodiscard]] bool ads_crossing_brake_pending(double now_seconds) const;

private:
    struct AdsAxisCrossingState {
        float previous_error = 0.0f;
        float previous_manual = 0.0f;
        bool has_previous_error = false;
        bool has_previous_manual = false;
        double brake_until_seconds = 0.0;
    };

    void reset_envelope();
    void reset_ads_crossing();
    [[nodiscard]] bool ads_axis_brake_active(
        const AdsAxisCrossingState& state,
        float manual,
        float requested_assist,
        float target_error,
        double now_seconds) const;
    [[nodiscard]] float shape_axis(
        float requested,
        float previous,
        float previous_delta,
        float step_cap,
        float jerk_cap,
        float* out_delta) const;
    [[nodiscard]] bool strong_opposing_manual(
        const NativeAimAssistDynamicsInput& input) const;

    GamepadAimAssistDynamicsConfig config_;
    common_native::Vec2f previous_assist_;
    common_native::Vec2f previous_delta_;
    bool has_history_ = false;
    AdsAxisCrossingState ads_crossing_x_;
    AdsAxisCrossingState ads_crossing_y_;
    std::uint64_t ads_target_key_ = 0;
    std::uint64_t ads_vision_sequence_ = 0;
    common_native::Vec2f ads_handoff_assist_;
    std::uint64_t ads_handoff_track_id_ = 0;
    bool has_ads_handoff_assist_ = false;
};

}  // namespace controller_native
