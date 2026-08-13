#include "operation_intent.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

// Below this filtered magnitude the stick is treated as noise / deadzone.
constexpr float kMaterialFloor = 0.15f;
// An acquisition gesture is a deliberate, high-magnitude onset push.
constexpr float kAcquireFlickMinMagnitude = 0.40f;
// A genuine anti-recoil pull keeps a clear downward filtered component.
constexpr float kRecoilPullMinDownwardY = 0.05f;
// With target owned and manual below this, the assist is doing the work.
constexpr float kFollowTrackMaxMagnitude = 0.12f;
// A desired-point correction stays bounded; anything larger is a flick.
constexpr float kCorrectTrackMaxMagnitude = 0.45f;
// Predictive lead only applies to targets actually moving at speed.
constexpr float kLeadTrackMinVelocityPxPerSec = 110.0f;
// Crosshair must lead the aim point by at least this much along the motion.
constexpr float kLeadTrackMinErrorAlongMotionPx = 6.0f;
// Direction-consistency smoothing; a reversal halves the running estimate.
constexpr float kDirectionConsistencyAlpha = 0.08f;

}  // namespace

const char* operation_class_name(OperationClass operation_class) noexcept {
    switch (operation_class) {
    case OperationClass::NoGesture:
        return "no_gesture";
    case OperationClass::AcquireFlick:
        return "acquire_flick";
    case OperationClass::RecoilPull:
        return "recoil_pull";
    case OperationClass::LeadTrack:
        return "lead_track";
    case OperationClass::FollowTrack:
        return "follow_track";
    case OperationClass::CorrectTrack:
        return "correct_track";
    case OperationClass::HandoverIntent:
        return "handover_intent";
    case OperationClass::Unreliable:
        return "unreliable";
    }
    return "no_gesture";
}

void OperationIntentClassifier::reset() noexcept {
    was_firing_ = false;
    last_manual_x_ = 0.0f;
    last_manual_y_ = 0.0f;
    direction_consistency_ = 0.5f;
}

OperationIntentOutput OperationIntentClassifier::classify(
    const OperationIntentInput& input) noexcept {
    OperationIntentOutput output;
    const float magnitude =
        std::hypot(input.filtered_right_x, input.filtered_right_y);

    const bool fire_started = input.firing && !was_firing_;
    was_firing_ = input.firing;

    if (magnitude > kMaterialFloor) {
        const float previous_magnitude =
            std::hypot(last_manual_x_, last_manual_y_);
        if (previous_magnitude > kMaterialFloor) {
            const float dot =
                (last_manual_x_ * input.filtered_right_x +
                 last_manual_y_ * input.filtered_right_y) /
                (magnitude * previous_magnitude);
            if (dot > 0.5f) {
                direction_consistency_ +=
                    kDirectionConsistencyAlpha *
                    (1.0f - direction_consistency_);
            } else if (dot < -0.5f) {
                direction_consistency_ *= 0.5f;
            }
        }
        last_manual_x_ = input.filtered_right_x;
        last_manual_y_ = input.filtered_right_y;
    }

    const float downward = std::max(0.0f, -input.filtered_right_y);

    // Priority cascade: most specific, safest templates first.
    if (input.right_purpose ==
        pipeline_contract::UserAimIntentPurpose::HandoverTarget) {
        output.operation_class = OperationClass::HandoverIntent;
        output.class_confidence = input.right_confidence;
    } else if (input.firing && downward > kRecoilPullMinDownwardY) {
        output.operation_class = OperationClass::RecoilPull;
        output.recoil_pull_strength = std::min(1.0f, downward);
        output.recoil_pull_onset = fire_started;
        output.class_confidence =
            0.5f + 0.5f * std::clamp(input.right_confidence, 0.0f, 1.0f);
    } else if (
        input.right_purpose ==
            pipeline_contract::UserAimIntentPurpose::AcquireTarget &&
        (input.right_phase == pipeline_contract::StickPhase::Onset ||
         input.right_phase == pipeline_contract::StickPhase::Sustained) &&
        magnitude > kAcquireFlickMinMagnitude) {
        output.operation_class = OperationClass::AcquireFlick;
        output.class_confidence = input.right_confidence;
    } else if (
        input.target_owned &&
        input.target_velocity_px_per_sec > kLeadTrackMinVelocityPxPerSec &&
        input.error_along_motion_px > kLeadTrackMinErrorAlongMotionPx &&
        magnitude < kFollowTrackMaxMagnitude) {
        output.operation_class = OperationClass::LeadTrack;
        output.class_confidence = 0.7f;
    } else if (input.target_owned &&
               magnitude < kFollowTrackMaxMagnitude) {
        output.operation_class = OperationClass::FollowTrack;
        output.class_confidence = 0.8f;
    } else if (
        input.target_owned && input.manual_correction &&
        magnitude > kMaterialFloor &&
        magnitude < kCorrectTrackMaxMagnitude) {
        output.operation_class = OperationClass::CorrectTrack;
        output.class_confidence = input.right_confidence;
    } else if (
        (input.target_owned || input.firing) &&
        magnitude > kMaterialFloor) {
        // Unreliable is a signal about the user's input *during active
        // assistance*: it matches no known operation template. Free look with
        // no target and no fire is not assistance and is not degraded intent;
        // it stays NoGesture so telemetry does not count it as a failure.
        output.operation_class = OperationClass::Unreliable;
        output.class_confidence = 1.0f;
    }

    // direction_trust (§4.4): high when the manual input matches a known
    // template, low when it matches none. NoGesture stays neutral so the
    // assist keeps following its own plan without extra inhibition.
    switch (output.operation_class) {
    case OperationClass::Unreliable:
        output.direction_trust = 0.10f;
        break;
    case OperationClass::NoGesture:
        output.direction_trust = 0.50f;
        break;
    default:
        output.direction_trust = std::clamp(
            0.72f + 0.15f * std::clamp(input.right_confidence, 0.0f, 1.0f) +
                0.10f * direction_consistency_,
            0.0f,
            0.97f);
        break;
    }
    return output;
}

}  // namespace controller_native
