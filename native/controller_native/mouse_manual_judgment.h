#pragma once
#include <algorithm>
#include <cmath>

namespace controller_native {
enum class MouseManualConflict { None, OpposesAim, PredictedExit, Deadzone };
inline const char* mouse_manual_conflict_name(MouseManualConflict value) noexcept {
    switch (value) {
    case MouseManualConflict::OpposesAim: return "opposes_aim";
    case MouseManualConflict::PredictedExit: return "predicted_exit";
    case MouseManualConflict::Deadzone: return "deadzone";
    default: return "none";
    }
}
struct MouseManualAxisDecision {
    float retention = 1;
    float retained_manual = 0;
    float ai_remainder = 0;
    float final = 0;
    MouseManualConflict conflict = MouseManualConflict::None;
};

// Caller owns target validity, lifecycle and protected manual corrections.
// Inputs are finite normalized mouse rates; desired_total is the existing
// solver's TOTAL target command, not an additive correction.
inline MouseManualAxisDecision judge_mouse_manual_axis(
    float manual, float desired_total, float error_px,
    float visual_authority, float minimum_retention,
    float hold_radius_px, float response_px_per_second,
    float material) noexcept {
    MouseManualAxisDecision result;
    const bool has_goal = std::fabs(desired_total) > material;
    if (std::fabs(manual) > material) {
        // A short constant-velocity prediction, in the same linear response
        // units as ADS/BodyLock. Reuse the 12 ms stopping lookahead and the
        // existing completion radius; this is an estimate, not game evidence.
        const float predicted_error = error_px - manual * response_px_per_second * 0.012f;
        if (has_goal && manual * desired_total < 0) {
            result.conflict = MouseManualConflict::OpposesAim;
        } else if (std::isfinite(error_px) && std::isfinite(predicted_error) &&
                   std::fabs(predicted_error) > std::max(std::fabs(error_px), hold_radius_px)) {
            result.conflict = MouseManualConflict::PredictedExit;
        }
    }
    if (result.conflict != MouseManualConflict::None) {
        result.retention = 1 - (1 - std::clamp(minimum_retention, 0.0f, 1.0f)) *
            std::clamp(visual_authority, 0.0f, 1.0f);
    }
    result.retained_manual = manual * result.retention;
    if (has_goal) {
        // Helpful manual work is already part of the requested total. Fill
        // only its deficit, never add M + T. For opposing work the AI retains
        // its own target budget; do not invent an extra -M cancellation term.
        const float completed = result.retained_manual * desired_total > 0
            ? std::min(std::fabs(result.retained_manual), std::fabs(desired_total)) : 0;
        result.ai_remainder = std::copysign(std::fabs(desired_total) - completed, desired_total);
    }
    result.final = result.retained_manual + result.ai_remainder;
    return result;
}
} // namespace controller_native
