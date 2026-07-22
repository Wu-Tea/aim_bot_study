#include "control_learning/causal_online_response_learner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace control_learning {
namespace {

double dot(const std::array<double, 2>& theta,
           const std::array<double, 2>& phi) noexcept {
    return theta[0] * phi[0] + theta[1] * phi[1];
}

bool hard_contamination(const ControlIntegral& integral) noexcept {
    return !integral.complete || integral.failed_delivery ||
        integral.output_disabled || integral.firing || integral.recoil_active ||
        integral.saturated;
}

std::uint32_t contamination_reasons(const ControlIntegral& integral) noexcept {
    std::uint32_t reasons = IdentificationReasonNone;
    if (!integral.complete) reasons |= IdentificationReasonDeliveryGap;
    if (integral.failed_delivery || integral.output_disabled)
        reasons |= IdentificationReasonDeliveryGap;
    if (integral.firing) reasons |= IdentificationReasonFiring;
    if (integral.recoil_active) reasons |= IdentificationReasonRecoil;
    if (integral.saturated) reasons |= IdentificationReasonSaturation;
    return reasons;
}

}  // namespace

std::uint64_t CausalOnlineResponseLearner::delay_ns(std::size_t index) noexcept {
    return static_cast<std::uint64_t>(kMinimumDelayMs +
        static_cast<int>(index) * kDelayStepMs) * 1'000'000ull;
}

bool CausalOnlineResponseLearner::finite_matrix(
    const ResponseMatrix2d& matrix) noexcept {
    for (const auto& row : matrix.values)
        for (double value : row) if (!std::isfinite(value)) return false;
    return true;
}

ResponseMatrix2d CausalOnlineResponseLearner::response_from(
    const Rls2& x, const Rls2& y) noexcept {
    ResponseMatrix2d result;
    result.values[0] = x.snapshot().theta;
    result.values[1] = y.snapshot().theta;
    return result;
}

void CausalOnlineResponseLearner::clear_interval_pairing() noexcept {
    has_anchor_ = false;
}

void CausalOnlineResponseLearner::reset_fast() noexcept {
    for (auto& candidate : candidates_) {
        candidate.right_fast_x.reset(); candidate.right_fast_y.reset();
        candidate.left_fast_x.reset(); candidate.left_fast_y.reset();
    }
}

void CausalOnlineResponseLearner::reset_left() noexcept {
    for (auto& candidate : candidates_) {
        candidate.left_fast_x.reset(); candidate.left_fast_y.reset();
        candidate.left_stable_x.reset(); candidate.left_stable_y.reset();
    }
}

void CausalOnlineResponseLearner::reset_session() noexcept {
    *this = CausalOnlineResponseLearner{};
}

void CausalOnlineResponseLearner::update_selection() noexcept {
    std::size_t best = selected_index_;
    double best_score = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        if (candidates_[i].accepted >= 3 &&
            candidates_[i].residual_score < best_score) {
            best_score = candidates_[i].residual_score;
            best = i;
        }
    }
    best_index_ = best;
    if (best == selected_index_) {
        pending_index_ = best;
        pending_wins_ = 0;
        switch_pending_ = false;
        return;
    }
    if (pending_index_ != best) {
        pending_index_ = best;
        pending_wins_ = 1;
    } else if (pending_wins_ < 255) {
        ++pending_wins_;
    }
    switch_pending_ = true;
    const double selected_score = candidates_[selected_index_].residual_score;
    const bool separated = best_score < selected_score * 0.92;
    if (pending_wins_ >= 5 && separated) {
        selected_index_ = best;
        pending_wins_ = 0;
        switch_pending_ = false;
    }
}

SampleAssessment CausalOnlineResponseLearner::observe_vision(
    const pipeline_contract::CommittedCaptureObservation& observation,
    const ControlHistory<1024>& history) noexcept {
    SampleAssessment assessment;
    if (!pipeline_contract::valid(observation)) {
        assessment.reason_bits = IdentificationReasonInvalidObservation;
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        clear_interval_pairing();
        return assessment;
    }
    assessment.vision_quality = observation.reused_or_projected
        ? VisionSampleQuality::ReusedOrProjected : VisionSampleQuality::Normal;
    normalized_target_size_ = observation.normalized_size;

    if (has_anchor_ && observation.captured_at_ns <= anchor_.captured_at_ns) {
        assessment.reason_bits = IdentificationReasonNonFinite;
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        clear_interval_pairing();
        return assessment;
    }
    if (has_anchor_ &&
        observation.persistent_target_id != anchor_.persistent_target_id) {
        assessment.vision_quality = VisionSampleQuality::IdentityTransition;
        assessment.reason_bits = IdentificationReasonIdentityChange;
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        reset_fast();
        reset_left();
        right_prior_confidence_scale_ *= 0.70;
        clear_interval_pairing();
        return assessment;
    }
    if (has_anchor_ && observation.ads_epoch != anchor_.ads_epoch) {
        assessment.reason_bits = IdentificationReasonAdsEpochChange;
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        reset_fast();
        clear_interval_pairing();
        return assessment;
    }
    if (observation.reused_or_projected || !observation.fresh_observed) {
        assessment.reason_bits = IdentificationReasonInvalidObservation;
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        clear_interval_pairing();
        return assessment;
    }
    if (has_anchor_ &&
        observation.captured_at_ns - anchor_.captured_at_ns > 250'000'000ull) {
        assessment.reason_bits = IdentificationReasonCaptureGap;
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        clear_interval_pairing();
        return assessment;
    }
    if (!has_anchor_) {
        anchor_ = observation;
        has_anchor_ = true;
        assessment.update_outcome = IdentificationUpdateOutcome::NotAttempted;
        return assessment;
    }

    const std::array<double, 2> output{
        static_cast<double>(anchor_.stable_error_px.x - observation.stable_error_px.x),
        static_cast<double>(anchor_.stable_error_px.y - observation.stable_error_px.y)};
    bool any_hard_clean = false;
    bool any_excited = false;
    std::uint32_t hard_reasons = IdentificationReasonNone;
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        const std::uint64_t delay = delay_ns(i);
        if (anchor_.captured_at_ns <= delay || observation.captured_at_ns <= delay) {
            hard_reasons |= IdentificationReasonDeliveryGap;
            continue;
        }
        const auto integral = history.integrate(
            anchor_.captured_at_ns - delay,
            observation.captured_at_ns - delay);
        hard_reasons |= contamination_reasons(integral);
        if (hard_contamination(integral)) continue;
        any_hard_clean = true;
        const std::array<double, 2> right_phi{
            integral.final_right_stick_seconds.x,
            integral.final_right_stick_seconds.y};
        const std::array<double, 2> left_phi{
            integral.final_left_stick_seconds.x,
            integral.final_left_stick_seconds.y};
        const double excitation = std::sqrt(
            right_phi[0] * right_phi[0] + right_phi[1] * right_phi[1] +
            left_phi[0] * left_phi[0] + left_phi[1] * left_phi[1]);
        candidates_[i].excitation = excitation;
        if (excitation < 1.0e-4) continue;
        any_excited = true;

        const auto prior_x = candidates_[i].right_stable_x.snapshot().theta;
        const auto prior_y = candidates_[i].right_stable_y.snapshot().theta;
        const double residual_x = output[0] - dot(prior_x, right_phi);
        const double residual_y = output[1] - dot(prior_y, right_phi);
        const double residual = std::hypot(residual_x, residual_y);
        candidates_[i].residual_score = candidates_[i].accepted == 0
            ? residual : 0.94 * candidates_[i].residual_score + 0.06 * residual;

        const bool right_ok =
            candidates_[i].right_fast_x.update(right_phi, output[0]) &&
            candidates_[i].right_fast_y.update(right_phi, output[1]) &&
            candidates_[i].right_stable_x.update(right_phi, output[0]) &&
            candidates_[i].right_stable_y.update(right_phi, output[1]);
        const std::array<double, 2> left_output{
            output[0] - dot(candidates_[i].right_stable_x.snapshot().theta, right_phi),
            output[1] - dot(candidates_[i].right_stable_y.snapshot().theta, right_phi)};
        const bool left_excited = std::hypot(left_phi[0], left_phi[1]) >= 1.0e-4;
        bool left_ok = false;
        if (left_excited) {
            left_ok = candidates_[i].left_fast_x.update(left_phi, left_output[0]) &&
                candidates_[i].left_fast_y.update(left_phi, left_output[1]) &&
                candidates_[i].left_stable_x.update(left_phi, left_output[0]) &&
                candidates_[i].left_stable_y.update(left_phi, left_output[1]);
        }
        if (right_ok) {
            ++candidates_[i].accepted;
            ++assessment.accepted_delay_count;
            assessment.accepted_by_any_delay = true;
            (void)left_ok;
        }
    }

    if (assessment.accepted_by_any_delay) {
        assessment.update_outcome = IdentificationUpdateOutcome::Accepted;
        right_prior_confidence_scale_ = std::min(1.0,
            right_prior_confidence_scale_ + 0.005);
        update_selection();
        anchor_ = observation;
        has_anchor_ = true;
    } else if (!any_hard_clean) {
        assessment.update_outcome = IdentificationUpdateOutcome::HardRejected;
        assessment.reason_bits = hard_reasons == IdentificationReasonNone
            ? IdentificationReasonDeliveryGap : hard_reasons;
        clear_interval_pairing();
    } else if (!any_excited) {
        assessment.update_outcome = IdentificationUpdateOutcome::InsufficientExcitation;
        assessment.reason_bits = IdentificationReasonLowExcitation;
        anchor_ = observation;
        has_anchor_ = true;
    } else {
        assessment.update_outcome = IdentificationUpdateOutcome::NumericallyRejected;
        assessment.reason_bits = IdentificationReasonNonFinite;
        clear_interval_pairing();
    }
    return assessment;
}

CausalResponseEstimate CausalOnlineResponseLearner::estimate() const noexcept {
    CausalResponseEstimate result;
    const Candidate& selected = candidates_[selected_index_];
    result.right_fast = response_from(selected.right_fast_x, selected.right_fast_y);
    result.right_stable = response_from(selected.right_stable_x, selected.right_stable_y);
    result.left_fast = response_from(selected.left_fast_x, selected.left_fast_y);
    result.left_stable = response_from(selected.left_stable_x, selected.left_stable_y);
    result.best_delay_ms = static_cast<float>(kMinimumDelayMs +
        static_cast<int>(best_index_) * kDelayStepMs);
    result.selected_delay_ms = static_cast<float>(kMinimumDelayMs +
        static_cast<int>(selected_index_) * kDelayStepMs);
    result.delay_switch_pending = switch_pending_;
    const double right_confidence = std::min(
        selected.right_stable_x.snapshot().confidence,
        selected.right_stable_y.snapshot().confidence) *
        right_prior_confidence_scale_;
    const double left_confidence = std::min(
        selected.left_stable_x.snapshot().confidence,
        selected.left_stable_y.snapshot().confidence);
    double separation = 0.0;
    if (selected.accepted >= 3) {
        double competing = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < candidates_.size(); ++i) {
            if (i != selected_index_ && candidates_[i].accepted >= 3)
                competing = std::min(competing, candidates_[i].residual_score);
        }
        if (std::isfinite(competing) && competing > 0.0) {
            separation = std::clamp(
                (competing - selected.residual_score) / competing, 0.0, 1.0);
        }
    }
    result.selected_delay_confidence = static_cast<float>(
        std::clamp(right_confidence * separation, 0.0, 1.0));
    result.right_confidence = static_cast<float>(
        std::clamp(right_confidence, 0.0, 1.0));
    result.left_confidence = static_cast<float>(
        std::clamp(left_confidence, 0.0, 1.0));
    const bool joint_ranked = selected.right_stable_x.snapshot().rank >= 2 &&
        selected.left_stable_x.snapshot().rank >= 2;
    result.joint_confidence = joint_ranked
        ? std::min(result.right_confidence, result.left_confidence) : 0.0f;
    result.excitation = static_cast<float>(selected.excitation);
    result.residual = static_cast<float>(selected.residual_score);
    result.normalized_target_size = normalized_target_size_;
    result.finite = finite_matrix(result.right_fast) &&
        finite_matrix(result.right_stable) && finite_matrix(result.left_fast) &&
        finite_matrix(result.left_stable) && std::isfinite(result.residual);
    return result;
}

}  // namespace control_learning
