#pragma once

#include "pipeline_contract/intent_state.h"

#include <cstdint>

namespace controller_native {

// Operation-pattern model (§4.5 of docs/project/ASSIST_PERCEPTION_
// QUANTIFICATION_DESIGN_20260813.md).
//
// Every controller tick the classifier answers one question: "what is the
// user actually doing right now?" It matches a small set of context-conditioned
// operation templates (recoil pull, sustained tracking, predictive lead,
// acquisition flick, desired-point correction, handover) in priority order
// against the filtered manual stick plus the current target plan. Material
// manual input that matches none of the templates is labelled Unreliable and
// drives direction_trust down.
//
// This is the online judgement that replaces windowed statistics: a template
// exists at the instant of the gesture, so a single wrong direction is
// recognized on that tick, not after a 2-minute failure window. The degraded
// case the design calls out (user low-blood-sugar / exhausted, nearly all
// directions wrong) therefore does not depend on a trust accumulator; every
// unmatchable input is caught here as Unreliable and the assist responds with
// a bounded hold instead of compounding a bad direction.

enum class OperationClass : unsigned char {
    NoGesture = 0,   // no meaningful manual input; assist follows its plan
    AcquireFlick,    // deliberate onset gesture toward a new target
    RecoilPull,      // firing + consistent downward pull (user anti-recoil)
    LeadTrack,       // fast target, crosshair held ahead along its motion
    FollowTrack,     // target owned, crosshair near, low manual input
    CorrectTrack,    // sustained small correction of the desired point D
    HandoverIntent,  // explicit direction to switch targets
    Unreliable,      // material manual input matching no known template
};

const char* operation_class_name(OperationClass operation_class) noexcept;

struct OperationIntentInput {
    bool aiming = false;
    bool firing = false;             // firing recently (auto or manual)
    bool target_owned = false;       // plan.target_id != 0
    bool manual_correction = false;  // plan.manual_correction_x || _y

    float filtered_right_x = 0.0f;
    float filtered_right_y = 0.0f;
    float right_confidence = 0.0f;

    pipeline_contract::StickPhase right_phase =
        pipeline_contract::StickPhase::Neutral;
    pipeline_contract::UserAimIntentPurpose right_purpose =
        pipeline_contract::UserAimIntentPurpose::AcquireTarget;

    float error_px = 0.0f;               // |plan.error_px|
    float error_along_motion_px = 0.0f;  // dot(error_px, unit(velocity))
    float target_velocity_px_per_sec = 0.0f;
};

struct OperationIntentOutput {
    OperationClass operation_class = OperationClass::NoGesture;
    float class_confidence = 0.0f;   // 0..1
    float direction_trust = 0.5f;    // 0..1, §4.4 degraded-intent signal
    float recoil_pull_strength = 0.0f;  // normalized downward pull when
                                        // RecoilPull, else 0
    bool recoil_pull_onset = false;  // fire just started + downward pull
};

// Deterministic per-tick classifier. Holds only cross-tick evidence about
// direction consistency and the firing edge; nothing depends on wall-clock
// time, so every classification is reproducible from the input sequence.
class OperationIntentClassifier {
public:
    void reset() noexcept;

    OperationIntentOutput classify(const OperationIntentInput& input) noexcept;

private:
    bool was_firing_ = false;
    float last_manual_x_ = 0.0f;
    float last_manual_y_ = 0.0f;
    float direction_consistency_ = 0.5f;
};

}  // namespace controller_native
