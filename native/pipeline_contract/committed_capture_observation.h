#pragma once

#include "pipeline_contract/target_plan.h"

#include <cmath>
#include <cstdint>

namespace pipeline_contract {

struct CommittedCaptureObservation {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t viewport_sequence = 0;
    std::uint64_t viewport_source_frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    // The controller consume timestamp belongs to this exact source frame;
    // zero is unavailable and therefore cannot be treated as a commit.
    std::uint64_t controller_consume_ns = 0;
    Vec2f stable_error_px{};
    Vec2f stable_body_size_px{};
    common_native::Box2f raw_body_box_px{};
    Vec2f motion_anchor_px{};
    Vec2f viewport_offset_px{};
    Vec2f target_acceleration_px_per_sec2{};
    float reliability = 0.0f;
    float normalized_size = 0.0f;
    float motion_anchor_score = 0.0f;
    TargetLifecycle lifecycle = TargetLifecycle::None;
    TargetMotion motion = TargetMotion::Ambiguous;
    ControlMode mode = ControlMode::Manual;
    std::uint64_t ads_epoch = 0;
    std::uint16_t eligible_candidate_count = 0;
    bool fresh_observed = false;
    bool strong_observation = false;
    bool stable_coordinates_valid = false;
    bool has_motion_anchor = false;
};

inline bool valid(const CommittedCaptureObservation& value) noexcept {
    const bool body_size_valid = finite(value.stable_body_size_px) &&
        value.stable_body_size_px.x >= 0.0f &&
        value.stable_body_size_px.y >= 0.0f;
    const bool anchor_valid = !value.has_motion_anchor ||
        (finite(value.motion_anchor_px) &&
         unit_interval(value.motion_anchor_score));
    const bool identity_valid = value.source_frame_id != 0 &&
        value.source_observation_id != 0 &&
        value.persistent_target_id != 0;
    return identity_valid && value.captured_at_ns != 0 &&
        value.controller_consume_ns != 0 &&
        value.controller_consume_ns >= value.result_at_ns &&
        value.result_at_ns >= value.captured_at_ns &&
        value.viewport_source_frame_id == value.source_frame_id &&
        finite(value.stable_error_px) && body_size_valid &&
        finite(value.viewport_offset_px) && anchor_valid &&
        finite(value.target_acceleration_px_per_sec2) &&
        unit_interval(value.reliability) &&
        unit_interval(value.normalized_size) &&
        value.stable_coordinates_valid &&
        (!value.fresh_observed ||
         (value.persistent_target_id != 0 &&
          value.source_observation_id != 0 &&
          value.eligible_candidate_count > 0)) &&
        (!value.strong_observation || value.fresh_observed);
}

inline bool single_strong_target(
    const CommittedCaptureObservation& value) noexcept {
    return valid(value) && value.eligible_candidate_count == 1 &&
        value.strong_observation;
}

}  // namespace pipeline_contract
