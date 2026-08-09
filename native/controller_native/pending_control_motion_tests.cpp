#include "pending_control_motion.h"
#include "aim_response_curve_plugin.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using controller_native::DeliveredPreRecoilSample;
using controller_native::DeliveredFinalCommandSample;
using controller_native::CausalMotionLedger;
using controller_native::CausalMotionLedgerStatus;
using controller_native::CausalMotionPhaseRequest;
using controller_native::CausalMotionPhaseEstimate;
using controller_native::CausalMotionCaptureProvenance;
using controller_native::PendingControlMotion;
using controller_native::delivered_camera_work_px;
using controller_native::remaining_work_after_delivery;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void test_integrates_delivered_pre_recoil_since_observation_capture() {
    PendingControlMotion ledger;
    require(ledger.observe({0.001, {0.0f, 1.0f}, 7, true, true}),
            "first delivered sample must be accepted");
    require(ledger.observe({0.011, {0.0f, 1.0f}, 7, true, true}),
            "second delivered sample must be accepted");
    const auto pending = ledger.estimate(0.021, 20.0f, 500.0f, 7);
    require(pending.valid, "complete delivered interval must be valid");
    for (const auto sample : pending.camera_displacement_px) {
        require_near(sample.y, 10.0f, 0.001f,
                     "20ms at full stick and 500px/s must equal 10px");
    }
}

void test_target_change_and_failed_delivery_invalidate_pending_motion() {
    PendingControlMotion ledger;
    (void)ledger.observe({0.001, {0.0f, 1.0f}, 7, true, true});
    (void)ledger.observe({0.011, {0.0f, 1.0f}, 7, true, true});
    require(!ledger.estimate(0.021, 20.0f, 500.0f, 8).valid,
            "target mismatch must invalidate history");
    require(!ledger.observe({0.021, {0.0f, 1.0f}, 7, false, true}),
            "failed delivery must be rejected");
    require(!ledger.estimate(0.031, 20.0f, 500.0f, 7).valid,
            "failed delivery must clear pending coverage");
}

void test_incomplete_time_coverage_is_not_guessed() {
    PendingControlMotion ledger;
    (void)ledger.observe({0.011, {0.0f, 1.0f}, 7, true, true});
    const auto pending = ledger.estimate(0.021, 20.0f, 500.0f, 7);
    require(!pending.valid,
            "capture older than retained history must be invalid");
}

void test_sub_tick_zero_hold_before_first_delivery_is_allowed() {
    PendingControlMotion ledger;
    (void)ledger.observe({0.001001, {1.0f, 0.0f}, 7, true, true});
    (void)ledger.observe({0.002001, {1.0f, 0.0f}, 7, true, true});
    const auto pending = ledger.estimate(0.003, 2.0f, 500.0f, 7);
    require(pending.valid,
            "sub-tick gap before first delivered sample must be a zero hold");
    require_near(
        pending.camera_displacement_px.front().x,
        0.9995f,
        0.001f,
        "only delivered time after the sub-tick zero hold may be integrated");
}

void test_overlong_accounting_window_is_dropped() {
    PendingControlMotion ledger;
    (void)ledger.observe({1.000, {1.0f, 0.0f}, 7, true, true});
    (void)ledger.observe({1.050, {1.0f, 0.0f}, 7, true, true});
    const auto pending = ledger.estimate_between(1.000, 1.101, 500.0f, 7);
    require(
        !pending.valid,
        "Remaining must drop an interval older than its realtime accounting budget");
}

void test_screen_space_remaining_work_uses_one_y_conversion() {
    const auto delivered = delivered_camera_work_px({10.0f, 6.0f});
    require_near(delivered.x, 10.0f, 0.0001f,
                 "right camera motion must complete positive X work");
    require_near(delivered.y, -6.0f, 0.0001f,
                 "up camera motion must complete negative screen-Y work");
    const auto remaining = remaining_work_after_delivery(
        {40.0f, -20.0f}, {10.0f, -5.0f});
    require_near(remaining.x, 30.0f, 0.0001f,
                 "D-P must preserve signed X subtraction");
    require_near(remaining.y, -15.0f, 0.0001f,
                 "D-P must preserve signed Y subtraction");
    const auto overshot = remaining_work_after_delivery(
        {40.0f, -20.0f}, {50.0f, 0.0f});
    require_near(overshot.x, -10.0f, 0.0001f,
                 "P>D must produce a signed negative residual");
    require_near(overshot.y, -20.0f, 0.0001f,
                 "zero P Y must leave the signed D Y unchanged");
}

DeliveredFinalCommandSample causal_sample(
    double delivered_at_seconds,
    std::uint64_t target_id = 7,
    std::uint64_t ads_epoch = 3) {
    DeliveredFinalCommandSample sample;
    sample.delivered_at_seconds = delivered_at_seconds;
    sample.final_stick = {0.5f, 0.5f};
    sample.camera_velocity_px_per_second = {1000.0f, -500.0f};
    sample.target_id = target_id;
    sample.ads_epoch = ads_epoch;
    sample.delivered = true;
    sample.output_enabled = true;
    return sample;
}

DeliveredFinalCommandSample neutral_causal_sample(
    double delivered_at_seconds,
    std::uint64_t physical_actuator_epoch = 1) {
    auto sample = causal_sample(delivered_at_seconds);
    sample.final_stick = {0.0f, 0.0f};
    sample.camera_velocity_px_per_second = {0.0f, 0.0f};
    sample.physical_actuator_epoch = physical_actuator_epoch;
    return sample;
}

void test_causal_ledger_separates_realized_in_flight_and_scheduled() {
    CausalMotionLedger ledger;
    for (int index = 0; index <= 6; ++index) {
        require(
            ledger.observe(causal_sample(0.100 + index * 0.010)),
            "causal command sample must be accepted");
    }

    CausalMotionPhaseRequest request;
    request.previous_capture_seconds = 0.130;
    request.current_capture_seconds = 0.150;
    request.decision_seconds = 0.160;
    request.response_delay_ms = 20.0f;
    request.memory_horizon_ms = 200.0f;
    request.target_id = 7;
    request.ads_epoch = 3;
    const auto estimate = ledger.estimate(request);
    require(estimate.valid && estimate.realized_valid && estimate.pending_valid,
            "complete causal history must produce all three phases");
    require(estimate.status == CausalMotionLedgerStatus::Valid,
            "complete causal history must report valid status");
    require_near(estimate.realized_px.x, 20.0f, 0.001f,
                 "realized phase must cover only the observation interval");
    require_near(estimate.in_flight_px.x, 20.0f, 0.001f,
                 "in-flight phase must cover only the delay interval");
    require_near(estimate.scheduled_px.x, 10.0f, 0.001f,
                 "scheduled phase must cover only post-capture work");
    require_near(estimate.pending_total_px.x, 30.0f, 0.001f,
                 "pending total must exclude realized work");
    require_near(estimate.pending_total_px.y, -15.0f, 0.001f,
                 "screen-space Y must remain causal and signed once");
}

void test_realized_join_rejects_wrong_previous_with_same_current() {
    const CausalMotionCaptureProvenance previous_a{
        10, 110, 100'000'000, 7, 1'000'000'000, 42, 3};
    const CausalMotionCaptureProvenance previous_b{
        11, 111, 105'000'000, 8, 1'000'000'000, 42, 3};
    const CausalMotionCaptureProvenance current{
        12, 112, 120'000'000, 9, 1'000'000'000, 42, 3};
    CausalMotionPhaseEstimate estimate;
    estimate.realized_valid = true;
    estimate.physical_actuator_epoch = 4;
    estimate.previous_source_frame_id = previous_a.source_frame_id;
    estimate.previous_source_observation_id =
        previous_a.source_observation_id;
    estimate.previous_present_steady_ns = previous_a.present_steady_ns;
    estimate.previous_present_calibration_id =
        previous_a.present_calibration_id;
    estimate.previous_present_qpc_frequency =
        previous_a.present_qpc_frequency;
    estimate.previous_target_id = previous_a.target_id;
    estimate.previous_ads_epoch = previous_a.ads_epoch;
    estimate.source_frame_id = current.source_frame_id;
    estimate.source_observation_id = current.source_observation_id;
    estimate.current_present_steady_ns = current.present_steady_ns;
    estimate.present_calibration_id = current.present_calibration_id;
    estimate.present_qpc_frequency = current.present_qpc_frequency;
    estimate.current_target_id = current.target_id;
    estimate.current_ads_epoch = current.ads_epoch;
    estimate.present_time_valid = true;

    require(
        controller_native::causal_motion_estimate_matches_capture_pair(
            estimate, previous_a, current, 4),
        "matching previous/current provenance must admit realized join");
    require(
        !controller_native::causal_motion_estimate_matches_capture_pair(
            estimate, previous_b, current, 4),
        "same current with a different previous must reject realized join");
}

void test_causal_ledger_preserves_global_history_and_invalidates_backend() {
    CausalMotionLedger ledger;
    require(ledger.observe(causal_sample(0.100)),
            "initial lifecycle sample must be accepted");
    require(ledger.observe(causal_sample(0.110, 8, 3)),
            "new target sample must remain in global history");
    require(ledger.size() == 2,
            "target replacement must not discard global actuator work");

    CausalMotionPhaseRequest request;
    request.previous_capture_seconds = 0.090;
    request.current_capture_seconds = 0.110;
    request.decision_seconds = 0.115;
    request.target_id = 7;
    request.ads_epoch = 3;
    request.capture_pair_compatible = false;
    request.response_delay_ms = 10.0f;
    const auto replacement = ledger.estimate(request);
    require(replacement.valid && replacement.pending_valid &&
                !replacement.realized_valid &&
                replacement.realized_status ==
                    CausalMotionLedgerStatus::CapturePairIncompatible,
            "replacement must keep pending while invalidating only realized pair");

    auto failed = causal_sample(0.120, 8, 3);
    failed.delivered = false;
    require(!ledger.observe(failed),
            "failed ViGEm delivery must invalidate physical history");
    require(ledger.size() == 0,
            "failed delivery must clear all pending work");
    const auto invalid = ledger.estimate(request);
    require(!invalid.valid &&
                invalid.status == CausalMotionLedgerStatus::BackendStateUnknown,
            "failed delivery must expose backend-state unknown reason");

    auto recovered = causal_sample(0.130, 8, 3);
    recovered.physical_actuator_epoch = 2;
    require(ledger.observe(recovered),
            "new physical epoch must re-establish a fresh history");
    require(ledger.physical_actuator_epoch() == 2,
            "recovered history must expose its physical actuator epoch");

    CausalMotionLedger clock_invalidated;
    require(clock_invalidated.observe(causal_sample(0.200)),
            "clock invalidation fixture must accept its first sample");
    auto non_monotonic = causal_sample(0.190);
    require(!clock_invalidated.observe(non_monotonic),
            "non-monotonic delivery must invalidate clock history");
    auto same_epoch = causal_sample(0.210);
    require(!clock_invalidated.observe(same_epoch),
            "same-epoch delivery must not recover invalid clock history");
    auto new_epoch = causal_sample(0.210);
    new_epoch.physical_actuator_epoch = 2;
    require(clock_invalidated.observe(new_epoch),
            "new actuator epoch may recover invalid clock history");

    CausalMotionLedger sample_invalidated;
    require(sample_invalidated.observe(causal_sample(0.300)),
            "invalid-sample fixture must accept its first sample");
    auto invalid_sample = causal_sample(0.310);
    invalid_sample.final_stick.x = std::numeric_limits<float>::quiet_NaN();
    require(!sample_invalidated.observe(invalid_sample),
            "non-finite final sample must invalidate history");
    auto same_epoch_after_invalid = causal_sample(0.320);
    require(!sample_invalidated.observe(same_epoch_after_invalid),
            "same-epoch delivery must not recover invalid sample history");
    auto new_epoch_after_invalid = causal_sample(0.320);
    new_epoch_after_invalid.physical_actuator_epoch = 2;
    require(sample_invalidated.observe(new_epoch_after_invalid),
            "new actuator epoch may recover invalid sample history");

    CausalMotionLedger response_invalidated;
    auto response_invalid = causal_sample(0.400);
    response_invalid.response_model_valid = false;
    response_invalid.response_confidence = 0.0f;
    require(response_invalidated.observe(response_invalid),
            "delivery remains recordable when response model is invalid");
    auto response_hold = causal_sample(0.410);
    response_hold.response_model_valid = false;
    response_hold.response_confidence = 0.0f;
    require(response_invalidated.observe(response_hold),
            "invalid response state must remain observable in the ring");
    CausalMotionPhaseRequest response_request;
    response_request.current_capture_seconds = 0.420;
    response_request.decision_seconds = 0.420;
    response_request.response_delay_ms = 10.0f;
    const auto response_estimate = response_invalidated.estimate(response_request);
    require(!response_estimate.valid &&
                response_estimate.status ==
                    CausalMotionLedgerStatus::InvalidResponseModel,
            "invalid response model must expose an explicit estimate reason");

    CausalMotionLedger low_confidence;
    auto low_confidence_sample = causal_sample(0.500);
    low_confidence_sample.response_confidence = 0.25f;
    require(low_confidence.observe(low_confidence_sample),
            "low-confidence delivery must remain physically recordable");
    auto low_confidence_hold = causal_sample(0.510);
    low_confidence_hold.response_confidence = 0.25f;
    require(low_confidence.observe(low_confidence_hold),
            "low-confidence hold must remain numerically recordable");
    CausalMotionPhaseRequest low_confidence_request;
    low_confidence_request.current_capture_seconds = 0.510;
    low_confidence_request.decision_seconds = 0.510;
    low_confidence_request.response_delay_ms = 10.0f;
    const auto low_confidence_estimate =
        low_confidence.estimate(low_confidence_request);
    require(low_confidence_estimate.pending_valid &&
                low_confidence_estimate.pending_response_confidence_valid &&
                std::fabs(low_confidence_estimate.pending_response_confidence -
                          0.25f) < 1.0e-6f,
            "pending must expose low response confidence without dropping motion");
}

void test_causal_ledger_requires_known_history_coverage() {
    // A: the in-flight window begins before the first non-neutral delivery.
    // The old implementation treated that missing prefix as zero.
    CausalMotionLedger first_non_neutral;
    require(first_non_neutral.observe(causal_sample(0.020)),
            "first non-neutral sample must be recordable");
    CausalMotionPhaseRequest before_first;
    before_first.current_capture_seconds = 0.020;
    before_first.decision_seconds = 0.020;
    before_first.response_delay_ms = 10.0f;
    before_first.memory_horizon_ms = 200.0f;
    const auto missing_prefix = first_non_neutral.estimate(before_first);
    require(!missing_prefix.valid &&
                missing_prefix.status ==
                    CausalMotionLedgerStatus::IncompleteHistory,
            "pre-first non-neutral interval must not be guessed as zero");

    // B: a successful neutral is an explicit known-zero anchor before the
    // window.  The later non-neutral state is then integrated normally.
    CausalMotionLedger anchored;
    require(anchored.observe(neutral_causal_sample(0.001)),
            "explicit neutral anchor must be recordable");
    require(anchored.observe(causal_sample(0.015)),
            "post-anchor command must be recordable");
    CausalMotionPhaseRequest anchored_request;
    anchored_request.current_capture_seconds = 0.020;
    anchored_request.decision_seconds = 0.020;
    anchored_request.response_delay_ms = 10.0f;
    anchored_request.memory_horizon_ms = 200.0f;
    const auto anchored_estimate = anchored.estimate(anchored_request);
    require(anchored_estimate.valid && anchored_estimate.pending_valid,
            "neutral anchor must make the covered pending interval valid");
    require_near(anchored_estimate.pending_total_px.x, 5.0f, 0.001f,
                 "anchored interval must include only post-command work");

    // C: a new physical epoch with one later non-neutral sample must not
    // inherit a zero prefix.  It becomes usable after a complete interval
    // beginning at the first post-epoch known state.
    CausalMotionLedger new_epoch;
    require(new_epoch.observe(causal_sample(0.050, 7, 3)),
            "old epoch sample must be recordable");
    auto epoch_sample = causal_sample(0.100, 7, 3);
    epoch_sample.physical_actuator_epoch = 2;
    require(new_epoch.observe(epoch_sample),
            "new epoch sample must replace old physical history");
    CausalMotionPhaseRequest epoch_prefix;
    epoch_prefix.current_capture_seconds = 0.110;
    epoch_prefix.decision_seconds = 0.110;
    epoch_prefix.response_delay_ms = 20.0f;
    epoch_prefix.memory_horizon_ms = 200.0f;
    const auto epoch_missing = new_epoch.estimate(epoch_prefix);
    require(!epoch_missing.valid &&
                epoch_missing.status ==
                    CausalMotionLedgerStatus::IncompleteHistory,
            "new epoch must not synthesize pre-first zero work");

    CausalMotionPhaseRequest epoch_complete;
    epoch_complete.current_capture_seconds = 0.120;
    epoch_complete.decision_seconds = 0.120;
    epoch_complete.response_delay_ms = 10.0f;
    epoch_complete.memory_horizon_ms = 200.0f;
    const auto epoch_post_sample = new_epoch.estimate(epoch_complete);
    require(epoch_post_sample.valid && epoch_post_sample.pending_valid,
            "post-epoch interval beginning at known sample must be valid");
}

void test_global_history_preserves_recoil_tail_across_loss_and_replacement() {
    CausalMotionLedger ledger;
    require(ledger.observe(neutral_causal_sample(0.080)),
            "lifecycle fixture neutral anchor must be accepted");

    auto older_ai = causal_sample(0.100, 101, 1);
    older_ai.final_stick = {0.40f, 0.0f};
    older_ai.camera_velocity_px_per_second = {200.0f, 0.0f};
    require(ledger.observe(older_ai),
            "older horizontal AI final output must be recorded");

    auto post_recoil = causal_sample(0.105, 101, 1);
    post_recoil.final_stick = {0.0f, -0.50f};
    // The final post-recoil stick is negative control-Y, which is positive
    // screen-space camera Y in the ledger contract.
    post_recoil.camera_velocity_px_per_second = {0.0f, 250.0f};
    require(ledger.observe(post_recoil),
            "post-recoil downward final output must be recorded");
    require(ledger.observe(neutral_causal_sample(0.110)),
            "target loss neutral must be recorded as a sample");
    require(ledger.observe(neutral_causal_sample(0.115)),
            "immediate replacement neutral must be recorded as a sample");

    CausalMotionPhaseRequest request;
    request.previous_capture_seconds = 0.090;
    request.current_capture_seconds = 0.120;
    request.decision_seconds = 0.120;
    request.response_delay_ms = 20.0f;
    request.memory_horizon_ms = 200.0f;
    request.target_id = 202;
    request.ads_epoch = 2;
    request.capture_pair_compatible = false;
    request.physical_actuator_epoch = 1;
    const auto estimate = ledger.estimate(request);
    require(estimate.pending_valid && !estimate.realized_valid,
            "loss/replacement must keep global pending and invalidate only realized pair");
    require_near(estimate.pending_total_px.x, 1.0f, 0.0001f,
                 "older horizontal work must be counted exactly once");
    require_near(estimate.pending_total_px.y, 1.25f, 0.0001f,
                 "post-recoil downward tail must preserve screen-Y sign");
    std::cout << "[W5 PASS PhaseB] loss_replacement_preserves_ai_x_and_recoil_y"
              << " pending=(" << estimate.pending_total_px.x << ","
              << estimate.pending_total_px.y << ")\n";
}

void test_causal_p_is_observation_relative_and_not_reintegrated() {
    CausalMotionLedger ledger;
    require(ledger.observe(neutral_causal_sample(0.600)),
            "P fixture needs a known neutral anchor");
    auto step = causal_sample(0.700);
    step.camera_velocity_px_per_second = {1000.0f, 0.0f};
    require(ledger.observe(step), "P fixture step must be delivered");
    require(ledger.observe(neutral_causal_sample(0.710)),
            "P fixture neutral release must be delivered");

    CausalMotionPhaseRequest request;
    request.current_capture_seconds = 0.830;
    request.decision_seconds = 0.830;
    request.response_delay_ms = 20.0f;
    request.memory_horizon_ms = 200.0f;
    const auto first = ledger.estimate(request);
    const auto repeated = ledger.estimate(request);
    require(first.pending_valid && repeated.pending_valid,
            "observation-relative P must be available from delivery history");
    require_near(first.pending_total_px.x, 0.0f, 0.001f,
                 "already-realized pre-observation work must not become P");
    require_near(repeated.pending_total_px.x,
                 first.pending_total_px.x, 0.0001f,
                 "repeating a Vision query must not consume history twice");

    const pipeline_contract::Vec2f same_d{40.0f, 0.0f};
    const auto remaining_with_step = remaining_work_after_delivery(
        same_d, first.pending_total_px);
    require_near(remaining_with_step.x, 40.0f, 0.001f,
                 "same D with recent output history must produce D-P");

    // The same observation may be queried later while scheduled output is
    // neutral.  The delayed step remains pending exactly once; it is not
    // re-integrated merely because the controller made another decision.
    request.decision_seconds = 0.850;
    const auto repeated_later = ledger.estimate(request);
    require(repeated_later.pending_valid,
            "same observation must remain queryable between Vision frames");
    require_near(repeated_later.pending_total_px.x, 0.0f, 0.001f,
                 "a step completed before the observation must stay absent from P");

    // Positive control: a delivery after the observation but within the
    // response delay belongs to P.
    CausalMotionLedger not_yet_visible;
    require(not_yet_visible.observe(neutral_causal_sample(0.600)),
            "positive P fixture needs a known neutral anchor");
    auto pending_step = causal_sample(0.820);
    pending_step.camera_velocity_px_per_second = {1000.0f, 0.0f};
    require(not_yet_visible.observe(pending_step),
            "not-yet-visible step must be delivered");
    require(not_yet_visible.observe(neutral_causal_sample(0.830)),
            "positive P fixture neutral tail must be delivered");
    request.decision_seconds = 0.830;
    const auto positive = not_yet_visible.estimate(request);
    const auto positive_repeat = not_yet_visible.estimate(request);
    require(positive.pending_valid && positive_repeat.pending_valid,
            "not-yet-visible action must produce valid P");
    require_near(positive.pending_total_px.x, 10.0f, 0.001f,
                 "only post-observation in-flight work belongs to P");
    require_near(positive_repeat.pending_total_px.x,
                 positive.pending_total_px.x, 0.0001f,
                 "repeated observation must not double-count P");

    CausalMotionLedger reversed;
    require(reversed.observe(neutral_causal_sample(0.600)),
            "reversal fixture needs a known neutral anchor");
    auto positive_reversal = causal_sample(0.700);
    positive_reversal.camera_velocity_px_per_second = {1000.0f, 0.0f};
    require(reversed.observe(positive_reversal),
            "positive reversal command must deliver");
    auto negative = causal_sample(0.710);
    negative.camera_velocity_px_per_second = {-1000.0f, 0.0f};
    require(reversed.observe(negative), "negative reversal command must deliver");
    require(reversed.observe(neutral_causal_sample(0.720)),
            "reversal fixture neutral tail must deliver");
    request.current_capture_seconds = 0.830;
    request.decision_seconds = 0.830;
    const auto cancelled = reversed.estimate(request);
    require(cancelled.pending_valid,
            "reversal P must remain numerically available");
    require_near(cancelled.pending_total_px.x, 0.0f, 0.001f,
                 "opposite/return-to-neutral output must not accumulate old P");
}

void test_dynamic_curve_forward_mapping_round_trips_controller_target() {
    controller_native::AimResponseCurveConfig config;
    config.algorithm =
        controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    config.calibration_reference_stick = 0.50f;
    const pipeline_contract::Vec2f target{0.48f, -0.64f};
    const auto delivered =
        controller_native::inverse_aim_response_curve(target, config);
    const auto recovered =
        controller_native::forward_aim_response_curve(delivered, config);
    require_near(recovered.x, target.x, 0.0001f,
                 "dynamic curve X must round-trip into response space");
    require_near(recovered.y, target.y, 0.0001f,
                 "dynamic curve Y must round-trip into response space");
}

}  // namespace

int main() {
    try {
        test_integrates_delivered_pre_recoil_since_observation_capture();
        test_target_change_and_failed_delivery_invalidate_pending_motion();
        test_incomplete_time_coverage_is_not_guessed();
        test_sub_tick_zero_hold_before_first_delivery_is_allowed();
        test_overlong_accounting_window_is_dropped();
        test_screen_space_remaining_work_uses_one_y_conversion();
        test_causal_ledger_separates_realized_in_flight_and_scheduled();
        test_realized_join_rejects_wrong_previous_with_same_current();
        test_causal_ledger_preserves_global_history_and_invalidates_backend();
        test_causal_ledger_requires_known_history_coverage();
        test_global_history_preserves_recoil_tail_across_loss_and_replacement();
        test_causal_p_is_observation_relative_and_not_reintegrated();
        test_dynamic_curve_forward_mapping_round_trips_controller_target();
        std::cout << "cod_native_pending_control_motion_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_pending_control_motion_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
