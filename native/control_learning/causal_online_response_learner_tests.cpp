#include "control_learning/causal_online_response_learner.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

pipeline_contract::CommittedCaptureObservation observation(
    std::uint64_t id, std::uint64_t target, std::uint64_t time_ns,
    pipeline_contract::Vec2f error) {
    pipeline_contract::CommittedCaptureObservation value;
    value.source_frame_id = id;
    value.source_observation_id = id;
    value.persistent_target_id = target;
    value.viewport_source_frame_id = id;
    value.captured_at_ns = time_ns;
    value.result_at_ns = time_ns + 2'000'000;
    value.controller_consume_ns = time_ns + 3'000'000;
    value.stable_error_px = error;
    value.stable_body_size_px = {35.0f, 100.0f};
    value.reliability = 0.9f;
    value.normalized_size = 0.4f;
    value.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    value.mode = pipeline_contract::ControlMode::BodyLockFollow;
    value.ads_epoch = 1;
    value.eligible_candidate_count = 1;
    value.fresh_observed = true;
    value.strong_observation = true;
    value.stable_coordinates_valid = true;
    return value;
}

control_learning::DeliveredControlSample control(
    std::uint64_t seq, std::uint64_t time_ns, float x, float y) {
    control_learning::DeliveredControlSample value;
    value.sample_seq = seq;
    value.applied_at_ns = time_ns;
    value.final_right = {x, y};
    value.final_left = {0.25f * y, -0.18f * x};
    value.output_delivered = true;
    value.ads_epoch = 1;
    return value;
}
}

int main() {
    try {
        using namespace control_learning;
        auto history = std::make_unique<ControlHistory<1024>>();
        for (std::uint64_t i = 1; i <= 150; ++i) {
            const float x = static_cast<float>(0.32 * std::sin(i * 0.37));
            const float y = static_cast<float>(0.27 * std::cos(i * 0.23));
            require(history->push(control(i, i * 5'000'000, x, y)),
                    "history fixture push failed");
        }

        auto learner = std::make_unique<CausalOnlineResponseLearner>();
        std::uint64_t frame = 1;
        for (std::uint64_t capture_ms = 160; capture_ms <= 700; capture_ms += 20) {
            const auto cumulative = history->integrate(
                5'000'000, (capture_ms - 45) * 1'000'000);
            pipeline_contract::Vec2f error{
                120.0f - 900.0f * cumulative.final_right_stick_seconds.x -
                    40.0f * cumulative.final_right_stick_seconds.y,
                -35.0f + 20.0f * cumulative.final_right_stick_seconds.x -
                    760.0f * cumulative.final_right_stick_seconds.y};
            learner->observe_vision(observation(frame++, 41, capture_ms * 1'000'000, error), *history);
        }
        const auto estimate = learner->estimate();
        require(estimate.finite, "learner state must remain finite");
        require(estimate.selected_delay_ms >= 35.0f && estimate.selected_delay_ms <= 55.0f,
                "delay bank must recover the planted neighborhood");
        require(estimate.best_delay_ms >= 35.0f && estimate.best_delay_ms <= 55.0f,
                "best-delay evidence must recover the planted neighborhood");
        require(estimate.selected_delay_confidence >= 0.0f &&
                    estimate.selected_delay_confidence <= 1.0f,
                "selected delay confidence must be bounded");
        require(estimate.right_confidence > 0.0f,
                "excited evidence must build right-response confidence");

        const auto stable_before_identity = estimate.right_stable;
        auto switched = observation(frame++, 99, 720'000'000, {10.0f, 5.0f});
        const auto identity_assessment = learner->observe_vision(switched, *history);
        require((identity_assessment.reason_bits & IdentificationReasonIdentityChange) != 0,
                "identity transition reason missing");
        require(learner->estimate().right_stable.values == stable_before_identity.values,
                "stable right prior must survive identity change");
        require(learner->estimate().left_confidence == 0.0f,
                "left response must reset on target identity change");
        require(!learner->pairing_active(),
                "hard lifecycle transition must clear interval pairing");

        auto low_excitation = std::make_unique<CausalOnlineResponseLearner>();
        auto quiet = std::make_unique<ControlHistory<1024>>();
        for (std::uint64_t i = 1; i <= 80; ++i) {
            require(quiet->push(control(i, i * 5'000'000, 1.0e-5f, -1.0e-5f)),
                    "quiet history push failed");
        }
        low_excitation->observe_vision(observation(1, 7, 200'000'000, {8, 3}), *quiet);
        const auto quiet_result = low_excitation->observe_vision(
            observation(2, 7, 220'000'000, {8, 3}), *quiet);
        require(quiet_result.vision_quality == VisionSampleQuality::Normal,
                "low excitation is not a visual defect");
        require(quiet_result.update_outcome ==
                    IdentificationUpdateOutcome::InsufficientExcitation,
                "low excitation must be classified separately");
        require(!quiet_result.accepted_by_any_delay,
                "all-delay rejection must not report accepted evidence");

        auto contaminated = std::make_unique<CausalOnlineResponseLearner>();
        auto firing_history = std::make_unique<ControlHistory<1024>>();
        for (std::uint64_t i = 1; i <= 80; ++i) {
            auto sample = control(i, i * 5'000'000, 0.2f, -0.1f);
            sample.firing = i >= 16 && i <= 50;
            require(firing_history->push(sample), "firing history push failed");
        }
        contaminated->observe_vision(
            observation(1, 8, 200'000'000, {12, 4}), *firing_history);
        const auto contaminated_result = contaminated->observe_vision(
            observation(2, 8, 240'000'000, {8, 3}), *firing_history);
        require(contaminated_result.update_outcome ==
                    IdentificationUpdateOutcome::HardRejected,
                "firing interval must hard reject identification");
        require(!contaminated->pairing_active(),
                "hard rejection must clear interval pairing");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[CausalOnlineResponseLearnerTests] FAIL "
                  << error.what() << '\n';
        return 1;
    }
}
