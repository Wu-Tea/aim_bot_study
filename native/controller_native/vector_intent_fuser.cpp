#include "vector_intent_fuser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace controller_native {
namespace {

float dot(pipeline_contract::Vec2f left,
          pipeline_contract::Vec2f right) noexcept {
    return left.x * right.x + left.y * right.y;
}

float length(pipeline_contract::Vec2f value) noexcept {
    return std::hypot(value.x, value.y);
}

bool finite_vec(pipeline_contract::Vec2f value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

struct TangentInterval {
    float lower = 0.0f;
    float upper = 0.0f;
};

TangentInterval ellipse_tangent_interval(
    pipeline_contract::Vec2f radial,
    pipeline_contract::Vec2f tangent,
    float radial_value,
    float max_force_x,
    float max_force_y) noexcept {
    const float inv_x = 1.0f / std::max(1.0e-5f, max_force_x);
    const float inv_y = 1.0f / std::max(1.0e-5f, max_force_y);
    const float a = tangent.x * tangent.x * inv_x * inv_x +
        tangent.y * tangent.y * inv_y * inv_y;
    const float b = 2.0f * radial_value * (
        radial.x * tangent.x * inv_x * inv_x +
        radial.y * tangent.y * inv_y * inv_y);
    const float c = radial_value * radial_value * (
        radial.x * radial.x * inv_x * inv_x +
        radial.y * radial.y * inv_y * inv_y);
    if (a <= 1.0e-5f || c > 1.0f + 1.0e-5f) {
        return {};
    }
    const float discriminant = std::max(0.0f,
        b * b + 4.0f * a * (1.0f - c));
    const float root = std::sqrt(discriminant);
    const float lower = (-b - root) / (2.0f * a);
    const float upper = (-b + root) / (2.0f * a);
    return {std::min(lower, upper), std::max(lower, upper)};
}

float smoothstep(float value) noexcept {
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

}  // namespace

FusionWeights candidate_weights(FusionCandidate candidate) noexcept {
    switch (candidate) {
        case FusionCandidate::ExistingMix: return {1.0f, 1.0f, 1.0f};
        case FusionCandidate::ManualSupported: return {1.0f, 0.5f, 1.0f};
        case FusionCandidate::AiSupported: return {0.5f, 1.0f, 0.5f};
        case FusionCandidate::ManualOnly: return {1.0f, 0.0f, 1.0f};
        case FusionCandidate::AiOnly: return {0.0f, 1.0f, 0.0f};
        case FusionCandidate::ReducedMix: return {0.5f, 0.5f, 0.5f};
        case FusionCandidate::RadialCorrected: return {0.5f, 1.0f, 1.0f};
        case FusionCandidate::RadialReplaced: return {0.0f, 1.0f, 1.0f};
        case FusionCandidate::TangentialCorrected: return {1.0f, 1.0f, 0.5f};
        case FusionCandidate::TangentialReplaced: return {1.0f, 1.0f, 0.0f};
        case FusionCandidate::FreshVisionCounterCorrected:
            return {0.0f, 1.0f, 1.0f};
    }
    return {1.0f, 0.0f, 1.0f};
}

VectorIntentFuser::VectorIntentFuser(VectorIntentFusionConfig config)
    : config_(config) {
    config_.manual_escape_threshold = std::clamp(
        config_.manual_escape_threshold, 0.05f, 1.0f);
    config_.manual_preservation_floor = std::clamp(
        config_.manual_preservation_floor, 0.0f, 1.0f);
    config_.fresh_vision_wrong_way_manual_floor = std::clamp(
        config_.fresh_vision_wrong_way_manual_floor, 0.0f, 1.0f);
    config_.ai_priority_parallel_headroom = std::clamp(
        config_.ai_priority_parallel_headroom, 0.0f, 1.0f);
    config_.ai_priority_opposing_manual_retention = std::clamp(
        config_.ai_priority_opposing_manual_retention, 0.0f, 1.0f);
    config_.ads_same_direction_manual_scale = std::clamp(
        config_.ads_same_direction_manual_scale, 0.0f, 1.0f);
    config_.near_bodylock_same_direction_manual_scale = std::clamp(
        config_.near_bodylock_same_direction_manual_scale, 0.0f, 1.0f);
    config_.ai_priority_manual_start = std::clamp(
        config_.ai_priority_manual_start, 0.02f, 0.94f);
    config_.ai_priority_manual_full = std::clamp(
        config_.ai_priority_manual_full,
        config_.ai_priority_manual_start + 0.001f, 0.95f);
}

VectorIntentFusionDecision VectorIntentFuser::update(
    const VectorIntentFusionInput& input, float dt_seconds) noexcept {
    VectorIntentFusionDecision decision{};
    decision.candidate_costs.fill(std::numeric_limits<float>::infinity());

    auto manual_only = [&](FusionFallbackReason reason,
                           bool preserve_output_continuity) {
        const pipeline_contract::Vec2f manual_stick = finite_vec(
            input.manual_stick) ? input.manual_stick
                                 : pipeline_contract::Vec2f{};
        decision.fused_stick = manual_stick;
        decision.candidate = FusionCandidate::ManualOnly;
        decision.target_manual_weight = 1.0f;
        decision.target_ai_weight = 0.0f;
        decision.target_tangential_manual_weight = 1.0f;
        decision.applied_manual_weight = 1.0f;
        decision.applied_ai_weight = 0.0f;
        decision.applied_tangential_manual_weight = 1.0f;
        decision.reason = reason;
        decision.fallback = reason != FusionFallbackReason::None;
        decision.candidate_costs[
            static_cast<std::size_t>(FusionCandidate::ManualOnly)] = 0.0f;
        if (preserve_output_continuity && output_initialized_) {
            const pipeline_contract::Vec2f output_delta{
                manual_stick.x - previous_output_.x,
                manual_stick.y - previous_output_.y};
            const float output_delta_length = length(output_delta);
            const float maximum_release_step = std::min(
                200.0f * std::clamp(dt_seconds, 0.0001f, 0.05f),
                0.20f);
            if (output_delta_length > maximum_release_step &&
                output_delta_length > 1.0e-6f) {
                const float scale = maximum_release_step / output_delta_length;
                decision.fused_stick = {
                    previous_output_.x + output_delta.x * scale,
                    previous_output_.y + output_delta.y * scale};
            }
        }
        // Manual fallback is an output state, not a request to cold-start the
        // next AI tick. Keep the actual released output as the re-entry
        // baseline so lifecycle boundaries remain continuous.
        previous_output_ = decision.fused_stick;
        output_initialized_ = true;
        reentry_pending_ = true;
        manual_escape_pending_ = false;
        manual_escape_pending_target_id_ = 0;
        manual_escape_latched_ = false;
        manual_escape_latched_target_id_ = 0;
        const pipeline_contract::Vec2f residual_ai{
            decision.fused_stick.x - manual_stick.x,
            decision.fused_stick.y - manual_stick.y};
        const float shaped_ai_magnitude = length(input.shaped_ai_stick);
        decision.applied_ai_weight = shaped_ai_magnitude > 0.001f
            ? std::clamp(length(residual_ai) / shaped_ai_magnitude,
                         0.0f, 1.0f)
            : 0.0f;
        return decision;
    };

    if (!finite_vec(input.manual_stick) ||
        !finite_vec(input.shaped_ai_stick) ||
        !std::isfinite(input.manual_confidence) ||
        !std::isfinite(dt_seconds) ||
        !pipeline_contract::valid(input.plan)) {
        return manual_only(FusionFallbackReason::NonFinite, false);
    }
    if (input.plan.target_id == 0 ||
        input.plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        input.plan.mode == pipeline_contract::ControlMode::Manual) {
        // Retain the last nonzero identity for the rest of this LT epoch. If
        // ownership expires and a different person appears while LT is still
        // held, the first replacement tick must pass through TargetChanged
        // instead of being mistaken for an initial acquisition. reset() still
        // clears this memory on a deliberate new ADS epoch.
        return manual_only(FusionFallbackReason::NoTarget, false);
    }
    if (target_id_ != 0 && target_id_ != input.plan.target_id) {
        target_id_ = input.plan.target_id;
        return manual_only(FusionFallbackReason::TargetChanged, false);
    }
    target_id_ = input.plan.target_id;
    const float manual_magnitude = length(input.manual_stick);
    const float ai_magnitude = length(input.shaped_ai_stick);
    constexpr float kIntentDeadzone = 0.02f;
    const float commitment_span = std::max(
        0.001f, config_.manual_escape_threshold - kIntentDeadzone);
    // IntentFilter confidence already derives from stick magnitude. Using it
    // as another multiplier squares the same evidence and makes deliberate
    // 10-30% corrections look like drift. Confidence gates the path; the
    // continuous magnitude curve owns commitment strength.
    const float manual_commitment = input.manual_confidence > 0.0f
        ? smoothstep(
            (manual_magnitude - kIntentDeadzone) / commitment_span)
        : 0.0f;

    float opposing_projection = 0.0f;
    pipeline_contract::Vec2f fused_ai = input.shaped_ai_stick;
    pipeline_contract::Vec2f manual_direction{};
    if (manual_magnitude > kIntentDeadzone) {
        manual_direction = {
            input.manual_stick.x / manual_magnitude,
            input.manual_stick.y / manual_magnitude};
        opposing_projection = std::max(
            0.0f, -dot(input.shaped_ai_stick, manual_direction));
    }
    const float authority = std::clamp(
        std::min(input.plan.aim_authority, input.plan.reliability),
        0.0f, 1.0f);
    if (authority <= 0.001f) {
        // A rejected assist is an explicit manual-safe boundary. Do not let
        // a stale shaper proposal survive as a legacy additive force when
        // the active plan has withdrawn authority.
        return manual_only(FusionFallbackReason::NoAuthority, false);
    }
    const float bodylock_priority_scope =
        input.plan.mode == pipeline_contract::ControlMode::BodyLockFollow
        ? smoothstep((input.plan.normalized_size - 0.18f) / 0.06f)
        : 0.0f;
    const float assisted_priority_scope =
        config_.assisted_ai_priority_enabled ? authority *
        (input.plan.mode == pipeline_contract::ControlMode::AdsAcquire
            ? 1.0f : bodylock_priority_scope) : 0.0f;
    const float strong_manual_commitment =
        ai_magnitude > kIntentDeadzone &&
            manual_magnitude > kIntentDeadzone
        ? smoothstep(
            (manual_magnitude - config_.ai_priority_manual_start) /
            (config_.ai_priority_manual_full -
             config_.ai_priority_manual_start))
        : 0.0f;
    pipeline_contract::Vec2f ai_direction{};
    if (ai_magnitude > kIntentDeadzone) {
        ai_direction = {
            input.shaped_ai_stick.x / ai_magnitude,
            input.shaped_ai_stick.y / ai_magnitude};
    }
    const float manual_ai_alignment =
        dot(input.manual_stick, input.shaped_ai_stick);
    const bool cooperative_manual =
        ai_magnitude > kIntentDeadzone && manual_ai_alignment > 0.0f;
    pipeline_contract::Vec2f fusion_manual_stick = input.manual_stick;
    bool fresh_vision_wrong_way_policy_applied = false;
    float fresh_vision_manual_radial_scale = 1.0f;
    bool fresh_vision_ai_radial_bound_applied = false;
    float fresh_vision_ai_radial_scale = 1.0f;
    bool fresh_vision_predictive_envelope_applied = false;
    const pipeline_contract::Vec2f control_error{
        input.plan.error_px.x, -input.plan.error_px.y};
    const float control_error_length = length(control_error);
    const bool fresh_target_relative =
        input.fresh_single_target_observation &&
        input.plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        (input.plan.mode == pipeline_contract::ControlMode::AdsAcquire ||
         input.plan.mode == pipeline_contract::ControlMode::BodyLockFollow) &&
        input.plan.reliability >= 0.85f &&
        authority > 0.001f &&
        control_error_length >= 6.0f;
    // A near-full physical deflection is a candidate ownership request.
    // Same-direction input is cooperative rather than an escape request: it
    // stays on the AI-priority proposal path so high camera sensitivity cannot
    // turn manual + AI into two additive forces.
    const bool full_manual_escape =
        input.manual_confidence > 0.0f &&
        manual_magnitude >= std::max(
            0.95f, config_.manual_escape_threshold) &&
        !(cooperative_manual && assisted_priority_scope > 0.001f);
    const bool directional_manual_escape =
        manual_magnitude >= config_.manual_escape_threshold &&
        opposing_projection > 0.001f &&
        manual_magnitude >= ai_magnitude &&
        opposing_projection >= ai_magnitude * 0.995f &&
        assisted_priority_scope <= 0.001f;
    const bool manual_escape_candidate =
        full_manual_escape || directional_manual_escape;
    const bool manual_escape_hold =
        input.manual_confidence > 0.0f &&
        manual_magnitude >= config_.manual_escape_threshold;
    bool deliberate_manual_escape = false;
    if (manual_escape_latched_) {
        // Once confirmed, ownership is a physical hold state. Do not let a
        // shaped-AI direction change or a lifecycle/mode-dependent candidate
        // recomputation alternate ownership while the same stick remains held.
        if (manual_escape_hold &&
            manual_escape_latched_target_id_ == input.plan.target_id) {
            deliberate_manual_escape = true;
        } else {
            manual_escape_latched_ = false;
            manual_escape_latched_target_id_ = 0;
            manual_escape_pending_ = false;
            manual_escape_pending_target_id_ = 0;
        }
    }
    if (!deliberate_manual_escape && manual_escape_candidate) {
        const bool same_pending =
            manual_escape_pending_ &&
            manual_escape_pending_target_id_ == input.plan.target_id;
        deliberate_manual_escape = same_pending;
        if (deliberate_manual_escape) {
            manual_escape_latched_ = true;
            manual_escape_latched_target_id_ = input.plan.target_id;
            manual_escape_pending_ = false;
            manual_escape_pending_target_id_ = 0;
        } else {
            manual_escape_pending_ = true;
            manual_escape_pending_target_id_ = input.plan.target_id;
        }
    } else {
        if (!deliberate_manual_escape) {
            // Fresh positional evidence bounds the first candidate tick. An
            // explicit escape can still reassert after a consecutive
            // candidate, preserving a continuous, observable exit channel.
            manual_escape_pending_ = false;
            manual_escape_pending_target_id_ = 0;
        }
    }
    if (deliberate_manual_escape) {
        decision.fused_stick = input.manual_stick;
        decision.candidate = FusionCandidate::ManualOnly;
        decision.target_manual_weight = 1.0f;
        decision.target_ai_weight = 0.0f;
        decision.target_tangential_manual_weight = 1.0f;
        decision.applied_manual_weight = 1.0f;
        decision.applied_ai_weight = 0.0f;
        decision.applied_tangential_manual_weight = 1.0f;
        decision.reason = FusionFallbackReason::ManualEscape;
        decision.fallback = false;
        decision.manual_escape = true;
        decision.candidate_costs[
            static_cast<std::size_t>(FusionCandidate::ManualOnly)] = 0.0f;
        previous_output_ = input.manual_stick;
        output_initialized_ = true;
        reentry_pending_ = true;
        decision.manual_escape_latched = manual_escape_latched_;
        return decision;
    }
    // Classify intent and escape from the raw physical stick above, then
    // normalize only the proposal that is actually allowed to cooperate with
    // AI. This keeps the 0.45..0.70 ownership transition stable instead of
    // accidentally disabling it by scaling the evidence first.
    float desired_same_direction_scale = 1.0f;
    if (input.plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        desired_same_direction_scale =
            config_.ads_same_direction_manual_scale;
    } else if (input.plan.mode ==
               pipeline_contract::ControlMode::BodyLockFollow) {
        desired_same_direction_scale =
            config_.near_bodylock_same_direction_manual_scale;
    }
    const float manual_normalization_commitment =
        config_.contextual_manual_normalization_enabled
        ? assisted_priority_scope * strong_manual_commitment : 0.0f;
    const float same_direction_manual_scale = manual_ai_alignment > 0.0f
        ? 1.0f - manual_normalization_commitment *
            (1.0f - desired_same_direction_scale)
        : 1.0f;
    if (!fresh_target_relative &&
        manual_ai_alignment > 0.0f && ai_magnitude > kIntentDeadzone) {
        const float raw_manual_parallel =
            dot(input.manual_stick, ai_direction);
        const float removed_parallel = std::max(
            0.0f,
            raw_manual_parallel * (1.0f - same_direction_manual_scale));
        fusion_manual_stick.x -= ai_direction.x * removed_parallel;
        fusion_manual_stick.y -= ai_direction.y * removed_parallel;
    }
    // Same-target Reacquiring means that the coordinator has fresh evidence
    // for the existing owner. The shaper may continue its normal request
    // slew; only TargetChanged/None/Manual owns a hard admission boundary.
    // Outside assisted AI-priority scope the accepted legacy ownership curve
    // remains intact. Inside it, physical input is an alternative proposal;
    // full escape and hard identity boundaries still preserve it exactly.
    if (!fresh_target_relative && opposing_projection > 0.001f &&
        !fresh_vision_wrong_way_policy_applied &&
        !fresh_vision_ai_radial_bound_applied) {
        // AI may brake a wrong manual push, but its opposing component may
        // never consume more than the configured share of physical intent.
        // Interpolate into that bound over the whole commitment range so the
        // brake cannot disappear and then reassert at the old threshold.
        const float bounded_brake = manual_magnitude *
            (1.0f - config_.manual_preservation_floor);
        const float retained_opposing_ai =
            opposing_projection * (1.0f - manual_commitment) +
            bounded_brake * manual_commitment;
        const float opposing_retention = std::clamp(
            retained_opposing_ai / opposing_projection, 0.0f, 1.0f);
        // Decompose in the user's actual 2-D direction. Only the conflicting
        // projection is bounded; useful orthogonal target-follow motion is
        // untouched. This is rotationally invariant and avoids X/Y gates.
        const pipeline_contract::Vec2f opposing_component{
            -manual_direction.x * opposing_projection,
            -manual_direction.y * opposing_projection};
        fused_ai.x += opposing_component.x * (opposing_retention - 1.0f);
        fused_ai.y += opposing_component.y * (opposing_retention - 1.0f);
    }
    if (!fresh_target_relative && manual_magnitude > kIntentDeadzone) {
        const float cooperative_projection = std::max(
            0.0f, dot(fused_ai, manual_direction));
        const float combined_parallel =
            manual_magnitude + cooperative_projection;
        // Live close-target failures appeared in the largest-size quartile,
        // starting around normalized size 0.22, and only after same-direction
        // parallel demand exceeded ~0.32.
        // Smooth both boundaries so ordinary/far cooperation remains exactly
        // unchanged and no new threshold impulse is introduced.
        const float proximity_commitment = smoothstep(
            (input.plan.normalized_size - 0.22f) / 0.02f);
        const float stack_commitment = smoothstep(
            (combined_parallel - 0.32f) / 0.08f);
        const float cooperative_commitment =
            manual_commitment * proximity_commitment * stack_commitment;
        if (cooperative_projection > 0.001f &&
            cooperative_commitment > 0.0f) {
            // Manual and AI are control proposals, not additive forces. Keep
            // the stronger parallel proposal and bounded headroom from the
            // weaker one; preserve the complete orthogonal follow vector.
            const float cooperative_overlap =
                1.0f - config_.manual_preservation_floor;
            const float dominant_parallel = std::max(
                manual_magnitude, cooperative_projection);
            const float weaker_parallel = std::min(
                manual_magnitude, cooperative_projection);
            const float bounded_combined_parallel =
                dominant_parallel +
                weaker_parallel * cooperative_overlap;
            const float retained_cooperative_ai = std::max(
                0.0f,
                bounded_combined_parallel - manual_magnitude);
            const float cooperative_reduction =
                (retained_cooperative_ai - cooperative_projection) *
                cooperative_commitment;
            fused_ai.x += manual_direction.x * cooperative_reduction;
            fused_ai.y += manual_direction.y * cooperative_reduction;
        }
    }
    const float fused_ai_magnitude = length(fused_ai);
    const float legacy_ai_weight = ai_magnitude > 0.001f
        ? std::clamp(fused_ai_magnitude / ai_magnitude, 0.0f, 1.0f)
        : 0.0f;
    pipeline_contract::Vec2f fused_target{
        fusion_manual_stick.x + fused_ai.x,
        fusion_manual_stick.y + fused_ai.y};
    float applied_manual_weight = same_direction_manual_scale;
    float applied_ai_weight = legacy_ai_weight;
    float ai_priority_commitment = 0.0f;
    if (!fresh_target_relative && ai_magnitude > kIntentDeadzone &&
        manual_magnitude > kIntentDeadzone &&
        assisted_priority_scope > 0.0f &&
        !fresh_vision_ai_radial_bound_applied) {
        ai_priority_commitment =
            assisted_priority_scope * strong_manual_commitment;
        if (ai_priority_commitment > 0.0f) {
            const float manual_parallel =
                dot(fusion_manual_stick, ai_direction);
            const pipeline_contract::Vec2f manual_tangent{
                fusion_manual_stick.x - ai_direction.x * manual_parallel,
                fusion_manual_stick.y - ai_direction.y * manual_parallel};
            float priority_parallel = ai_magnitude;
            float priority_manual_weight =
                config_.ai_priority_opposing_manual_retention;
            if (manual_parallel >= 0.0f) {
                // AI remains the primary absolute proposal. A bounded share
                // of the normalized manual proposal always participates, so
                // the old asymmetry (manual kept, AI decayed) is not replaced
                // by the opposite asymmetry (manual ignored below AI).
                priority_parallel += manual_parallel *
                    config_.ai_priority_parallel_headroom;
                priority_manual_weight =
                    config_.ai_priority_parallel_headroom *
                    same_direction_manual_scale;
            } else {
                priority_parallel += manual_parallel *
                    config_.ai_priority_opposing_manual_retention;
            }
            pipeline_contract::Vec2f priority_target{
                ai_direction.x * priority_parallel + manual_tangent.x,
                ai_direction.y * priority_parallel + manual_tangent.y};
            // A candidate escape is confirmed on the next consecutive tick
            // above. Until then both proposals remain inside this shared
            // predicted-result envelope; a single near-full sample must not
            // jump straight to exact raw-manual ownership.
            constexpr float escape_commitment = 0.0f;
            priority_target = {
                priority_target.x * (1.0f - escape_commitment) +
                    input.manual_stick.x * escape_commitment,
                priority_target.y * (1.0f - escape_commitment) +
                    input.manual_stick.y * escape_commitment};
            priority_manual_weight +=
                (1.0f - priority_manual_weight) * escape_commitment;
            fused_target = {
                fused_target.x * (1.0f - ai_priority_commitment) +
                    priority_target.x * ai_priority_commitment,
                fused_target.y * (1.0f - ai_priority_commitment) +
                    priority_target.y * ai_priority_commitment};
            applied_manual_weight =
                applied_manual_weight * (1.0f - ai_priority_commitment) +
                priority_manual_weight * ai_priority_commitment;
            applied_ai_weight = legacy_ai_weight +
                ai_priority_commitment * (1.0f - legacy_ai_weight);
        }
    }
    if (fresh_target_relative && !deliberate_manual_escape) {
        const pipeline_contract::Vec2f radial_direction{
            control_error.x / control_error_length,
            control_error.y / control_error_length};
        const pipeline_contract::Vec2f tangent_direction{
            -radial_direction.y, radial_direction.x};
        const float manual_radial = dot(input.manual_stick, radial_direction);
        // shaped_ai_stick is already the controller's shaped proposal.  The
        // fuser must not apply plan authority a second time; only the model
        // demand reconstructed below applies the active authority once.
        const float ai_radial = dot(input.shaped_ai_stick, radial_direction);
        const float manual_tangent = dot(input.manual_stick, tangent_direction);
        const float ai_tangent = dot(input.shaped_ai_stick, tangent_direction);
        // A proposal that points away from the authoritative error is not a
        // candidate force. It may retain tangent intent, but it contributes
        // no radial demand. The final command is selected from one valid
        // proposal, never assembled from manual + AI quantities.
        const float positive_manual_radial = std::max(0.0f, manual_radial);
        const float positive_ai_radial = std::max(0.0f, ai_radial);
        const float strongest_valid_radial = std::max(
            positive_manual_radial, positive_ai_radial);

        // TargetPlan does not own the active arrival envelope. Production
        // callers provide the actual ADS/BodyLock horizon and force ellipse;
        // the fallback is test/low-evidence only and is observable.
        constexpr float kLowEvidenceHorizonSeconds = 0.100f;
        constexpr float kFallbackResponsePxPerStickSecond = 500.0f;
        const bool active_envelope =
            input.response_envelope_valid &&
            std::isfinite(input.response_horizon_seconds) &&
            std::isfinite(input.response_horizon_y_seconds) &&
            input.response_horizon_seconds >= 0.005f &&
            input.response_horizon_y_seconds >= 0.005f &&
            std::isfinite(input.response_max_force.x) &&
            std::isfinite(input.response_max_force.y) &&
            input.response_max_force.x > 0.0f &&
            input.response_max_force.y > 0.0f;
        const float horizon_x = active_envelope
            ? input.response_horizon_seconds : kLowEvidenceHorizonSeconds;
        const float horizon_y = active_envelope
            ? input.response_horizon_y_seconds : kLowEvidenceHorizonSeconds;
        const float max_force_x = active_envelope
            ? input.response_max_force.x : 1.0f;
        const float max_force_y = active_envelope
            ? input.response_max_force.y : 1.0f;
        const bool response_evidence =
            input.plan.response_confidence >= 0.25f &&
            input.plan.response_scale >= 50.0f &&
            std::isfinite(input.plan.response_scale);
        const float envelope_response = response_evidence
            ? input.plan.response_scale
            : kFallbackResponsePxPerStickSecond;
        float available_radial_px = control_error_length;
        const char* envelope_reason = active_envelope
            ? (response_evidence ? "active_response_envelope"
                                  : "active_envelope_low_response_fallback")
            : "low_response_envelope_fallback";
        if (response_evidence) {
            const pipeline_contract::Vec2f predicted_control_error{
                input.plan.predicted_terminal_error_px.x,
                -input.plan.predicted_terminal_error_px.y};
            const float predicted_radial = dot(
                predicted_control_error, radial_direction);
            available_radial_px = predicted_radial > 0.0f
                ? std::min(control_error_length, predicted_radial)
                : 0.0f;
            if (predicted_radial <= 0.0f) {
                envelope_reason = "predicted_arrival_stop";
            }
        }
        const float linear_response_demand_radial = authority *
            available_radial_px * (
            radial_direction.x * radial_direction.x /
                (horizon_x * envelope_response) +
            radial_direction.y * radial_direction.y /
                (horizon_y * envelope_response));
        const pipeline_contract::Vec2f curved_response_target =
            inverse_aim_response_curve(
                {radial_direction.x * linear_response_demand_radial,
                 radial_direction.y * linear_response_demand_radial},
                input.response_curve);
        const float response_demand_radial = std::max(
            0.0f, dot(curved_response_target, radial_direction));
        const float force_denominator = std::sqrt(
            std::pow(radial_direction.x / max_force_x, 2.0f) +
            std::pow(radial_direction.y / max_force_y, 2.0f));
        const float force_radial_limit = force_denominator > 1.0e-5f
            ? 1.0f / force_denominator : 1.0f;
        const float permitted_radial = std::min(
            std::max(0.0f, response_demand_radial), force_radial_limit);
        const float final_radial = std::min(
            strongest_valid_radial, permitted_radial);
        const pipeline_contract::Vec2f validated_manual{
            radial_direction.x * positive_manual_radial +
                tangent_direction.x * manual_tangent,
            radial_direction.y * positive_manual_radial +
                tangent_direction.y * manual_tangent};
        const pipeline_contract::Vec2f validated_ai{
            radial_direction.x * positive_ai_radial +
                tangent_direction.x * ai_tangent,
            radial_direction.y * positive_ai_radial +
                tangent_direction.y * ai_tangent};
        const float manual_demand = length(validated_manual);
        const float ai_demand = length(validated_ai);
        const bool choose_manual = manual_demand >= ai_demand;
        const float strongest_valid_demand = std::max(
            manual_demand, ai_demand);
        const float selected_tangent = choose_manual
            ? manual_tangent : ai_tangent;
        const float tangent_demand_limit = std::sqrt(std::max(
            0.0f, strongest_valid_demand * strongest_valid_demand -
                final_radial * final_radial));
        const TangentInterval tangent_force_interval =
            ellipse_tangent_interval(
            radial_direction,
            tangent_direction,
            final_radial,
            max_force_x,
            max_force_y);
        const float tangent_lower = std::max(
            -tangent_demand_limit, tangent_force_interval.lower);
        const float tangent_upper = std::min(
            tangent_demand_limit, tangent_force_interval.upper);
        const float final_tangent = std::clamp(
            selected_tangent, tangent_lower, tangent_upper);
        fused_target = {
            radial_direction.x * final_radial +
                tangent_direction.x * final_tangent,
            radial_direction.y * final_radial +
                tangent_direction.y * final_tangent};

        // Keep the legacy field useful by reporting the validated manual
        // proposal itself. It is not an output contribution and is never
        // paired with an invented AI share.
        fresh_vision_wrong_way_policy_applied =
            manual_radial < -0.02f && positive_manual_radial <= 0.0f;
        fresh_vision_manual_radial_scale =
            std::fabs(manual_radial) > 0.02f
            ? (positive_manual_radial > 0.0f ? 1.0f : 0.0f)
            : 1.0f;
        fresh_vision_ai_radial_bound_applied = ai_radial < -0.02f;
        fresh_vision_ai_radial_scale =
            std::fabs(ai_radial) > 0.02f
            ? (positive_ai_radial > 0.0f ? 1.0f : 0.0f)
            : 1.0f;
        fresh_vision_predictive_envelope_applied = true;
        decision.fresh_vision_validated_manual_proposal = validated_manual;
        decision.fresh_vision_validated_ai_proposal = validated_ai;
        decision.fresh_vision_manual_radial = manual_radial;
        decision.fresh_vision_proposed_radial = strongest_valid_radial;
        decision.fresh_vision_raw_ai_radial = ai_radial;
        decision.fresh_vision_strongest_valid_radial = strongest_valid_radial;
        decision.fresh_vision_strongest_valid_demand =
            strongest_valid_demand;
        decision.fresh_vision_permitted_radial = permitted_radial;
        decision.fresh_vision_stopping_radial = std::max(
            0.0f, response_demand_radial);
        decision.fresh_vision_pre_slew_radial = final_radial;
        decision.fresh_vision_final_radial = final_radial;
        decision.fresh_vision_envelope_horizon_seconds =
            horizon_x;
        decision.fresh_vision_envelope_horizon_y_seconds = horizon_y;
        decision.fresh_vision_envelope_max_force = {
            max_force_x, max_force_y};
        decision.fresh_vision_envelope_target_stick = fused_target;
        decision.fresh_vision_authoritative_error_px = input.plan.error_px;
        decision.fresh_vision_predicted_error_px =
            input.plan.predicted_terminal_error_px;
        decision.fresh_vision_envelope_source =
            input.response_envelope_source;
        decision.fresh_vision_envelope_reason = envelope_reason;
        applied_manual_weight = manual_magnitude > 0.001f
            ? std::clamp(length(validated_manual) / manual_magnitude, 0.0f, 1.0f)
            : 0.0f;
        applied_ai_weight = ai_magnitude > 0.001f
            ? (choose_manual ? 0.0f : 1.0f)
            : 0.0f;
    }
    decision.fused_stick = {
        std::clamp(fused_target.x, -1.0f, 1.0f),
        std::clamp(fused_target.y, -1.0f, 1.0f),
    };
    // This is the only final-output memory in the aim chain. It limits the
    // vector result itself (manual + bounded AI), so neither manual mistakes
    // nor fresh AI observations can create a one-tick camera impulse. At
    // Yielding toward deliberate manual input is fast; AI reassertion is
    // slower. This avoids making escape feel sticky without allowing a fresh
    // observation to snap ownership back in one tick.
    if (!output_initialized_) {
        previous_output_ = decision.fused_stick;
        output_initialized_ = true;
    }
    const pipeline_contract::Vec2f output_delta{
        decision.fused_stick.x - previous_output_.x,
        decision.fused_stick.y - previous_output_.y};
    const float output_delta_length = length(output_delta);
    const bool yielding_to_manual =
        !fresh_target_relative &&
        !reentry_pending_ &&
        manual_magnitude > kIntentDeadzone &&
        dot(output_delta, manual_direction) > 0.0f;
    const float output_slew_rate = yielding_to_manual ? 200.0f : 80.0f;
    const float maximum_output_step = std::min(
        output_slew_rate * std::clamp(dt_seconds, 0.0001f, 0.05f),
        yielding_to_manual ? 0.20f : 0.08f);
    if (output_delta_length > maximum_output_step &&
        output_delta_length > 1.0e-6f) {
        const float scale = maximum_output_step / output_delta_length;
        decision.fused_stick = {
            previous_output_.x + output_delta.x * scale,
            previous_output_.y + output_delta.y * scale};
    }
    if (fresh_target_relative && !deliberate_manual_escape) {
        // The previous-output slew is stateful. Re-project only the
        // target-relative radial component here so a prior target direction
        // cannot remain an obsolete command for several ticks. Tangent is
        // preserved; this is the same fresh radial safety, not a generic
        // final XY clamp or a second owner.
        const pipeline_contract::Vec2f radial_direction{
            control_error.x / control_error_length,
            control_error.y / control_error_length};
        const pipeline_contract::Vec2f tangent_direction{
            -radial_direction.y, radial_direction.x};
        const float actual_radial = dot(
            decision.fused_stick, radial_direction);
        const float maximum_radial = std::min(
            decision.fresh_vision_strongest_valid_radial,
            decision.fresh_vision_permitted_radial);
        const float bounded_radial = std::clamp(
            actual_radial, 0.0f, maximum_radial);
        float tangent = dot(decision.fused_stick, tangent_direction);
        const float tangent_demand_limit = std::sqrt(std::max(
            0.0f,
            decision.fresh_vision_strongest_valid_demand *
                decision.fresh_vision_strongest_valid_demand -
                bounded_radial * bounded_radial));
        const TangentInterval tangent_force_interval =
            ellipse_tangent_interval(
                radial_direction,
                tangent_direction,
                bounded_radial,
                decision.fresh_vision_envelope_max_force.x,
                decision.fresh_vision_envelope_max_force.y);
        const float tangent_lower = std::max(
            -tangent_demand_limit, tangent_force_interval.lower);
        const float tangent_upper = std::min(
            tangent_demand_limit, tangent_force_interval.upper);
        const float bounded_tangent = std::clamp(
            tangent, tangent_lower, tangent_upper);
        if (std::fabs(bounded_radial - actual_radial) > 1.0e-5f ||
            std::fabs(bounded_tangent - tangent) > 1.0e-5f) {
            tangent = bounded_tangent;
            decision.fused_stick = {
                radial_direction.x * bounded_radial +
                    tangent_direction.x * tangent,
                radial_direction.y * bounded_radial +
                    tangent_direction.y * tangent};
            decision.fresh_vision_envelope_reason =
                "post_slew_fresh_target_relative_envelope";
        }
        decision.fresh_vision_final_radial = dot(
            decision.fused_stick, radial_direction);
    }
    previous_output_ = decision.fused_stick;
    reentry_pending_ = false;
    decision.target_manual_weight = applied_manual_weight;
    decision.target_ai_weight = applied_ai_weight;
    decision.target_tangential_manual_weight = 1.0f;
    decision.applied_manual_weight = applied_manual_weight;
    decision.applied_ai_weight = applied_ai_weight;
    decision.applied_tangential_manual_weight = 1.0f;
    decision.manual_escape = deliberate_manual_escape;
    decision.fresh_vision_wrong_way_policy_applied =
        fresh_vision_wrong_way_policy_applied;
    decision.fresh_vision_manual_radial_scale =
        fresh_vision_manual_radial_scale;
    decision.fresh_vision_ai_radial_bound_applied =
        fresh_vision_ai_radial_bound_applied;
    decision.fresh_vision_ai_radial_scale = fresh_vision_ai_radial_scale;
    decision.fresh_vision_predictive_envelope_applied =
        fresh_vision_predictive_envelope_applied;
    decision.manual_escape_latched = manual_escape_latched_;
    decision.fallback = false;
    decision.reason = FusionFallbackReason::None;
    if (applied_ai_weight <= 0.001f) {
        decision.candidate = FusionCandidate::ManualOnly;
    } else if (ai_priority_commitment > 0.001f) {
        decision.candidate = FusionCandidate::RadialCorrected;
    } else if (applied_ai_weight >= 0.999f) {
        decision.candidate = FusionCandidate::ExistingMix;
    } else {
        decision.candidate = FusionCandidate::ManualSupported;
    }
    decision.candidate_costs[
        static_cast<std::size_t>(decision.candidate)] = 0.0f;
    return decision;
}

void VectorIntentFuser::reset() noexcept {
    target_id_ = 0;
    previous_output_ = {};
    output_initialized_ = false;
    reentry_pending_ = false;
    manual_escape_pending_ = false;
    manual_escape_pending_target_id_ = 0;
    manual_escape_latched_ = false;
    manual_escape_latched_target_id_ = 0;
}

}  // namespace controller_native
