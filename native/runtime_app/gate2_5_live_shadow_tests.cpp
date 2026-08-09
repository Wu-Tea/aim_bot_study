#include "gate2_5_live_shadow.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

void require(bool value, int line) {
    if (!value) {
        std::cerr << "require failed at line " << line << '\n';
        std::abort();
    }
}

void require_near(float actual, float expected, float tolerance, int line) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "require near failed at line " << line
                  << " actual=" << actual << " expected=" << expected << '\n';
        std::abort();
    }
}

#define REQUIRE(value) require((value), __LINE__)
#define REQUIRE_NEAR(actual, expected, tolerance) \
    require_near((actual), (expected), (tolerance), __LINE__)

constexpr std::uint64_t kMs = 1'000'000ull;
constexpr std::uint64_t kDelay = 20 * kMs;

control_learning::DeliveredControlSample sample(
    std::uint64_t seq,
    std::uint64_t at_ns,
    pipeline_contract::Vec2f final,
    pipeline_contract::Vec2f left = {},
    bool saturated = false,
    bool firing = false,
    bool recoil = false,
    bool output_disabled = false) {
    control_learning::DeliveredControlSample value;
    value.sample_seq = seq;
    value.applied_at_ns = at_ns;
    value.physical_right = final;
    value.physical_left = left;
    value.manual_component = final;
    value.ai_component = {};
    value.pre_recoil = final;
    value.recoil_component = {};
    value.final_right = final;
    value.final_left = {};
    value.output_delivered = true;
    value.saturated = saturated;
    value.firing = firing;
    value.recoil_active = recoil;
    value.output_disabled = output_disabled;
    return value;
}

runtime_app::Gate25ObservationInput observation(
    std::uint64_t frame,
    std::uint64_t present_ns,
    float anchor_x,
    float anchor_y,
    std::uint64_t acquisition = 1) {
    runtime_app::Gate25ObservationInput value;
    value.source_frame_id = frame;
    value.source_observation_id = frame * 10 + 1;
    value.persistent_target_id = 99;
    value.selector_target_generation = 7;
    value.physical_ads_epoch = 3;
    value.target_acquisition_id = acquisition;
    value.viewport_sequence = 5;
    value.viewport_source_frame_id = frame;
    value.source_present_qpc = present_ns;
    value.source_present_qpc_frequency = 1'000'000'000ull;
    value.source_present_steady_ns = present_ns;
    value.source_present_calibration_id = 11;
    value.source_present_calibration_uncertainty_ns = 10;
    value.source_present_available = true;
    value.source_present_steady_available = true;
    value.captured_at_ns = present_ns - 1 * kMs;
    value.result_at_ns = present_ns - 500'000ull;
    value.controller_consume_ns = present_ns - 100'000ull;
    value.decision_ns = present_ns - 50'000ull;
    value.response_delay_ns = kDelay;
    value.response_delay_valid = true;
    value.target_anchor_screen_x = anchor_x;
    value.target_anchor_screen_y = anchor_y;
    value.stable_body_width = 50.0f;
    value.stable_body_height = 100.0f;
    value.reliability = 1.0f;
    value.target_confidence = 1.0f;
    value.motion_anchor_score = 1.0f;
    value.viewport_width = 640;
    value.viewport_height = 512;
    value.lifecycle = static_cast<std::uint8_t>(
        pipeline_contract::TargetLifecycle::Observed);
    value.motion = static_cast<std::uint8_t>(
        pipeline_contract::TargetMotion::Steady);
    value.mode = static_cast<std::uint8_t>(
        pipeline_contract::ControlMode::BodyLockFollow);
    value.controller_tick_id = frame * 100;
    value.backend_epoch = 1;
    value.backend_known = true;
    value.output_enabled = true;
    value.fresh_observed = true;
    value.strong_observation = true;
    value.stable_coordinates_valid = true;
    return value;
}

control_learning::ControlHistory<1024> neutral_and_axis_history(
    pipeline_contract::Vec2f effect,
    bool saturation = false) {
    control_learning::ControlHistory<1024> history;
    // Delayed interval [1,11] is neutral and [11,21] is the axis command.
    REQUIRE(history.push(sample(1, 1 * kMs, {})));
    REQUIRE(history.push(sample(2, 11 * kMs, effect, {}, saturation)));
    REQUIRE(history.push(sample(3, 21 * kMs, effect, {}, saturation)));
    return history;
}

std::unique_ptr<control_learning::ControlHistory<1024>>
make_neutral_and_axis_history(pipeline_contract::Vec2f effect) {
    auto history = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(history->push(sample(1, 1 * kMs, {})));
    REQUIRE(history->push(sample(2, 11 * kMs, effect)));
    REQUIRE(history->push(sample(3, 21 * kMs, effect)));
    return history;
}

void seed_baseline(
    runtime_app::Gate25LiveShadow& shadow,
    const control_learning::ControlHistory<1024>& history) {
    shadow.observe(observation(1, 21 * kMs, 100.0f, 100.0f), &history);
    shadow.observe(observation(2, 31 * kMs, 100.0f, 100.0f), &history);
}

void set_ledger_candidate(
    runtime_app::Gate25ObservationInput& current,
    const runtime_app::Gate25ObservationInput& estimate_previous) {
    current.ledger_realized_available = true;
    current.ledger_realized_x = 10.0f;
    current.ledger_realized_y = 0.0f;
    current.ledger_physical_actuator_epoch = current.backend_epoch;
    current.ledger_previous_source_frame_id =
        estimate_previous.source_frame_id;
    current.ledger_previous_source_observation_id =
        estimate_previous.source_observation_id;
    current.ledger_previous_present_steady_ns =
        estimate_previous.source_present_steady_ns;
    current.ledger_previous_present_calibration_id =
        estimate_previous.source_present_calibration_id;
    current.ledger_previous_present_qpc_frequency =
        estimate_previous.source_present_qpc_frequency;
    current.ledger_previous_target_id = estimate_previous.persistent_target_id;
    current.ledger_previous_ads_epoch = estimate_previous.physical_ads_epoch;
    current.ledger_current_source_frame_id = current.source_frame_id;
    current.ledger_current_source_observation_id =
        current.source_observation_id;
    current.ledger_current_present_steady_ns =
        current.source_present_steady_ns;
    current.ledger_current_present_calibration_id =
        current.source_present_calibration_id;
    current.ledger_current_present_qpc_frequency =
        current.source_present_qpc_frequency;
    current.ledger_current_target_id = current.persistent_target_id;
    current.ledger_current_ads_epoch = current.physical_ads_epoch;
}

void test_gate_ledger_join_uses_gate_accepted_previous() {
    auto history = neutral_and_axis_history({0.5f, 0.0f});

    auto run_case = [&](bool invalid_geometry, bool wrong_previous) {
        runtime_app::Gate25LiveShadow shadow;
        const auto first = observation(1, 21 * kMs, 100.0f, 100.0f);
        const auto accepted_prior = observation(
            2, 31 * kMs, 100.0f, 100.0f);
        shadow.observe(first, &history);
        shadow.observe(accepted_prior, &history);

        auto rejected = invalid_geometry
            ? observation(3, 35 * kMs, 100.0f, 100.0f)
            : observation(2, 35 * kMs, 100.0f, 100.0f);
        if (!invalid_geometry) rejected.source_observation_id = 22;
        if (invalid_geometry) rejected.stable_coordinates_valid = false;
        shadow.observe(rejected, &history);

        auto current = observation(3, 41 * kMs, 90.0f, 100.0f);
        current.source_observation_id = 31;
        set_ledger_candidate(
            current, wrong_previous ? rejected : accepted_prior);
        shadow.observe(current, &history);
        return shadow.last_effect();
    };

    for (const bool invalid_geometry : {false, true}) {
        const auto mismatched = run_case(invalid_geometry, true);
        REQUIRE(mismatched.valid);
        REQUIRE(!mismatched.ledger_comparison_valid);

        const auto matched = run_case(invalid_geometry, false);
        REQUIRE(matched.valid);
        REQUIRE(matched.ledger_comparison_valid);
        REQUIRE_NEAR(matched.residual_x, 0.0f, 1.0e-5f);
    }
}

void test_shifted_present_interval_preserves_full_2d_sign() {
    for (const auto& case_value : {
        std::pair<pipeline_contract::Vec2f, pipeline_contract::Vec2f>{
            {0.5f, 0.0f}, {-10.0f, 0.0f}},
        std::pair<pipeline_contract::Vec2f, pipeline_contract::Vec2f>{
            {0.0f, 0.5f}, {0.0f, -10.0f}},
        std::pair<pipeline_contract::Vec2f, pipeline_contract::Vec2f>{
            {0.0f, -0.5f}, {0.0f, 10.0f}}}) {
        auto history = neutral_and_axis_history(case_value.first);
        runtime_app::Gate25LiveShadow shadow;
        seed_baseline(shadow, history);
        auto current = observation(3, 41 * kMs, 100.0f + case_value.second.x,
                                   100.0f + case_value.second.y);
        shadow.observe(current, &history);
        const auto& effect = shadow.last_effect();
        REQUIRE(effect.valid);
        REQUIRE(effect.controller_tick_id == 300);
        REQUIRE(effect.delivery_first_seq != 0);
        REQUIRE(effect.delivery_last_seq >= effect.delivery_first_seq);
        REQUIRE_NEAR(effect.observed_camera_work_x, -case_value.second.x, 1.0e-5f);
        REQUIRE_NEAR(effect.observed_camera_work_y, -case_value.second.y, 1.0e-5f);
    }
}

void test_current_present_neutral_does_not_poison_baseline_for_delayed_work() {
    control_learning::ControlHistory<1024> history;
    REQUIRE(history.push(sample(1, 1 * kMs, {})));
    REQUIRE(history.push(sample(2, 11 * kMs, {0.8f, 0.0f})));
    REQUIRE(history.push(sample(3, 21 * kMs, {0.8f, 0.0f})));
    REQUIRE(history.push(sample(4, 31 * kMs, {})));
    REQUIRE(history.push(sample(5, 41 * kMs, {})));

    runtime_app::Gate25LiveShadow shadow;
    seed_baseline(shadow, history);
    // Present interval [31,41] is neutral, but the correct delayed interval
    // [11,21] still contains the earlier +X work.
    shadow.observe(observation(3, 41 * kMs, 90.0f, 100.0f), &history);
    const auto& effect = shadow.last_effect();
    REQUIRE(effect.valid);
    REQUIRE(effect.observed_camera_work_x > 9.0f);
    REQUIRE(shadow.last_effect().reason == runtime_app::Gate25Reason::None);
}

void test_rollover_carries_neutral_baseline() {
    control_learning::ControlHistory<1024> history;
    REQUIRE(history.push(sample(1, 1 * kMs, {})));
    REQUIRE(history.push(sample(2, 11 * kMs, {0.5f, 0.0f})));
    REQUIRE(history.push(sample(3, 21 * kMs, {0.5f, 0.0f})));
    REQUIRE(history.push(sample(4, 31 * kMs, {0.5f, 0.0f})));
    const std::uint64_t far_delivery = 5'000 * kMs + 31 * kMs;
    REQUIRE(history.push(sample(5, far_delivery, {0.5f, 0.0f})));

    runtime_app::Gate25LiveShadow shadow;
    seed_baseline(shadow, history);
    shadow.observe(observation(3, 41 * kMs, 90.0f, 100.0f), &history);
    const auto before = shadow.last_effect();
    REQUIRE(before.valid);
    auto rollover = observation(4, 5'000 * kMs + 41 * kMs, 80.0f, 100.0f);
    shadow.observe(rollover, &history);
    REQUIRE(shadow.last_effect().valid);
    REQUIRE(shadow.last_effect().observed_camera_work_x > 9.0f);
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(shadow.take_due_summary(summary));
    REQUIRE(summary.neutral_baseline_pairs >= 1);
    REQUIRE(summary.effect_valid >= 1);
}

void test_exogenous_is_rejected_before_baseline_and_saturation_is_separate() {
    // Rebuild with a left-stick average of 0.5 over the delayed interval.
    auto exogenous_history = std::make_unique<
        control_learning::ControlHistory<1024>>();
    REQUIRE(exogenous_history->push(sample(1, 1 * kMs, {})));
    REQUIRE(exogenous_history->push(sample(
        2, 11 * kMs, {0.5f, 0.0f}, {0.5f, 0.0f})));
    REQUIRE(exogenous_history->push(sample(
        3, 21 * kMs, {0.5f, 0.0f}, {0.5f, 0.0f})));
    auto exogenous = std::make_unique<runtime_app::Gate25LiveShadow>();
    exogenous->observe(observation(1, 31 * kMs, 100.0f, 100.0f),
                       exogenous_history.get());
    exogenous->observe(observation(2, 41 * kMs, 90.0f, 100.0f),
                       exogenous_history.get());
    const auto exogenous_summary = [&] {
        runtime_app::Gate25AggregateSnapshot value;
        exogenous->flush_summary(value);
        return value;
    }();
    REQUIRE(exogenous_summary.exogenous_rejected == 1);
    REQUIRE(exogenous_summary.missing_neutral_baseline == 0);

    auto saturated_history = neutral_and_axis_history({0.5f, 0.0f}, true);
    runtime_app::Gate25LiveShadow saturated;
    seed_baseline(saturated, saturated_history);
    saturated.observe(observation(3, 41 * kMs, 90.0f, 100.0f), &saturated_history);
    runtime_app::Gate25AggregateSnapshot saturated_summary;
    REQUIRE(saturated.flush_summary(saturated_summary));
    REQUIRE(saturated_summary.saturation_rows == 1);
    REQUIRE(saturated_summary.exogenous_rejected == 0);
}

void test_missing_delay_and_identity_boundaries_are_explicit() {
    auto history = neutral_and_axis_history({0.5f, 0.0f});
    runtime_app::Gate25LiveShadow shadow;
    auto first = observation(1, 21 * kMs, 100.0f, 100.0f);
    first.response_delay_valid = false;
    first.response_delay_ns = 0;
    shadow.observe(first, &history);
    auto second = observation(2, 31 * kMs, 100.0f, 100.0f);
    shadow.observe(second, &history);
    REQUIRE(shadow.last_effect().reason == runtime_app::Gate25Reason::ResponseDelayBoundary);

    auto third = observation(3, 41 * kMs, 100.0f, 100.0f, 2);
    shadow.observe(third, &history);
    REQUIRE(shadow.last_effect().reason == runtime_app::Gate25Reason::TargetAcquisitionBoundary);

    auto invalid_viewport = observation(4, 51 * kMs, 100.0f, 100.0f, 2);
    invalid_viewport.viewport_source_frame_id = 3;
    shadow.observe(invalid_viewport, &history);
    REQUIRE(shadow.last_effect().reason == runtime_app::Gate25Reason::InvalidGeometry);
}

void test_lifecycle_viewport_and_backend_boundaries_are_explicit() {
    auto history = neutral_and_axis_history({0.5f, 0.0f});

    runtime_app::Gate25LiveShadow moving;
    seed_baseline(moving, history);
    auto moving_frame = observation(3, 41 * kMs, 90.0f, 100.0f);
    moving_frame.motion = static_cast<std::uint8_t>(
        pipeline_contract::TargetMotion::Strafe);
    moving.observe(moving_frame, &history);
    REQUIRE(moving.last_effect().reason ==
            runtime_app::Gate25Reason::TargetMotionContamination);

    runtime_app::Gate25LiveShadow viewport;
    seed_baseline(viewport, history);
    auto changed_viewport = observation(3, 41 * kMs, 90.0f, 100.0f);
    changed_viewport.viewport_width = 800;
    viewport.observe(changed_viewport, &history);
    REQUIRE(viewport.last_effect().reason ==
            runtime_app::Gate25Reason::ViewportBoundary);

    runtime_app::Gate25LiveShadow backend;
    auto disabled = observation(1, 21 * kMs, 100.0f, 100.0f);
    disabled.output_enabled = false;
    backend.observe(disabled, &history);
    auto reenabled = observation(2, 31 * kMs, 100.0f, 100.0f);
    backend.observe(reenabled, &history);
    REQUIRE(backend.last_effect().reason ==
            runtime_app::Gate25Reason::BackendBoundary);

    runtime_app::Gate25LiveShadow no_acquisition;
    auto zero_acquisition = observation(1, 21 * kMs, 100.0f, 100.0f, 0);
    no_acquisition.observe(zero_acquisition, &history);
    REQUIRE(no_acquisition.last_effect().reason ==
            runtime_app::Gate25Reason::NoTargetIdentity);
}

void test_rollover_precedes_no_unsigned_backward_rollover() {
    auto history = neutral_and_axis_history({0.5f, 0.0f});
    runtime_app::Gate25LiveShadow shadow;
    shadow.observe(observation(1, 100 * kMs, 100.0f, 100.0f), &history);
    auto backward = observation(2, 50 * kMs, 100.0f, 100.0f);
    shadow.observe(backward, &history);
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(!shadow.take_due_summary(summary));
    REQUIRE(shadow.last_effect().reason == runtime_app::Gate25Reason::BackwardPresent);
}

void test_source_ordering_rejects_without_advancing_and_recovers_once() {
    auto history = make_neutral_and_axis_history({0.5f, 0.0f});
    auto shadow = std::make_unique<runtime_app::Gate25LiveShadow>();
    auto first = observation(1, 21 * kMs, 100.0f, 100.0f);
    shadow->observe(first, history.get());

    shadow->observe(first, history.get());
    REQUIRE(shadow->last_effect().reason ==
            runtime_app::Gate25Reason::DuplicateObservation);

    auto same_present = observation(2, 21 * kMs, 100.0f, 100.0f);
    shadow->observe(same_present, history.get());
    REQUIRE(shadow->last_effect().reason ==
            runtime_app::Gate25Reason::SamePresentEndpoint);

    auto accepted = observation(2, 31 * kMs, 100.0f, 100.0f);
    shadow->observe(accepted, history.get());
    REQUIRE(shadow->last_effect().reason == runtime_app::Gate25Reason::None);

    auto duplicate_id = observation(3, 41 * kMs, 90.0f, 100.0f);
    duplicate_id.source_observation_id = accepted.source_observation_id;
    shadow->observe(duplicate_id, history.get());
    REQUIRE(shadow->last_effect().reason ==
            runtime_app::Gate25Reason::DuplicateObservation);

    auto stale = observation(1, 41 * kMs, 90.0f, 100.0f);
    shadow->observe(stale, history.get());
    REQUIRE(shadow->last_effect().reason == runtime_app::Gate25Reason::StaleObservation);

    auto backward = observation(3, 25 * kMs, 90.0f, 100.0f);
    shadow->observe(backward, history.get());
    REQUIRE(shadow->last_effect().reason ==
            runtime_app::Gate25Reason::BackwardPresent);

    auto recovered = observation(3, 41 * kMs, 90.0f, 100.0f);
    shadow->observe(recovered, history.get());
    REQUIRE(shadow->last_effect().valid);
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(shadow->flush_summary(summary));
    REQUIRE(summary.effect_valid == 1);
}

void test_calibration_ids_are_endpoint_provenance_not_boundaries() {
    auto history = make_neutral_and_axis_history({0.5f, 0.0f});
    auto shadow = std::make_unique<runtime_app::Gate25LiveShadow>();
    auto first = observation(1, 21 * kMs, 100.0f, 100.0f);
    first.source_present_calibration_id = 100;
    first.source_present_calibration_uncertainty_ns = 10;
    shadow->observe(first, history.get());
    auto second = observation(2, 31 * kMs, 100.0f, 100.0f);
    second.source_present_calibration_id = 101;
    second.source_present_calibration_uncertainty_ns = 20;
    shadow->observe(second, history.get());
    REQUIRE(shadow->last_effect().reason == runtime_app::Gate25Reason::None);
    auto effect = observation(3, 41 * kMs, 90.0f, 100.0f);
    effect.source_present_calibration_id = 102;
    effect.source_present_calibration_uncertainty_ns = 30;
    shadow->observe(effect, history.get());
    REQUIRE(shadow->last_effect().valid);

    auto frequency = std::make_unique<runtime_app::Gate25LiveShadow>();
    first = observation(1, 21 * kMs, 100.0f, 100.0f);
    frequency->observe(first, history.get());
    second = observation(2, 31 * kMs, 100.0f, 100.0f);
    second.source_present_qpc_frequency += 1;
    frequency->observe(second, history.get());
    REQUIRE(frequency->last_effect().reason ==
            runtime_app::Gate25Reason::PresentCalibrationBoundary);

    auto uncertainty = std::make_unique<runtime_app::Gate25LiveShadow>();
    first = observation(1, 21 * kMs, 100.0f, 100.0f);
    uncertainty->observe(first, history.get());
    second = observation(2, 31 * kMs, 100.0f, 100.0f);
    second.source_present_calibration_uncertainty_ns = 3'000'000;
    uncertainty->observe(second, history.get());
    REQUIRE(uncertainty->last_effect().reason ==
            runtime_app::Gate25Reason::PresentCalibrationBoundary);
}

void test_capture_gap_and_profile_provenance_are_explicit() {
    auto history = make_neutral_and_axis_history({0.5f, 0.0f});
    auto gap = std::make_unique<runtime_app::Gate25LiveShadow>();
    gap->observe(observation(1, 21 * kMs, 100.0f, 100.0f), history.get());
    auto jumped = observation(3, 31 * kMs, 100.0f, 100.0f);
    jumped.accumulated_frames = 2;
    gap->observe(jumped, history.get());
    REQUIRE(gap->last_effect().reason == runtime_app::Gate25Reason::CaptureGap);

    auto profile = std::make_unique<runtime_app::Gate25LiveShadow>();
    auto first = observation(1, 21 * kMs, 100.0f, 100.0f);
    profile->observe(first, history.get());
    auto neutral = observation(2, 31 * kMs, 100.0f, 100.0f);
    profile->observe(neutral, history.get());
    auto effect = observation(3, 41 * kMs, 90.0f, 100.0f);
    effect.response_label_ms = 260;
    effect.response_model_available = true;
    profile->observe(effect, history.get());
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(profile->flush_summary(summary));
    REQUIRE(summary.response_260ms_rows == 0);
    REQUIRE(summary.profile_identity_unavailable == 1);
}

void test_exact_passive_output_magnitude_bins() {
    const std::array<float, 5> values{0.01f, 0.02f, 0.03f, 0.05f, 0.10f};
    for (std::size_t expected_bin = 0; expected_bin < values.size(); ++expected_bin) {
        auto history = make_neutral_and_axis_history(
            {values[expected_bin], 0.0f});
        auto shadow = std::make_unique<runtime_app::Gate25LiveShadow>();
        seed_baseline(*shadow, *history);
        shadow->observe(observation(3, 41 * kMs, 90.0f, 100.0f), history.get());
        runtime_app::Gate25AggregateSnapshot summary;
        REQUIRE(shadow->flush_summary(summary));
        const std::size_t mode = static_cast<std::size_t>(
            static_cast<std::uint8_t>(
                pipeline_contract::ControlMode::BodyLockFollow));
        const auto& cell = summary.cohort_grid[
            (mode * runtime_app::kGate25AxisCapacity) *
                runtime_app::kGate25CommandBinCapacity +
            expected_bin];
        REQUIRE(cell.pair_count == 1);
        REQUIRE(cell.valid_count == 1);
        REQUIRE(cell.invalid_count == 0);
        REQUIRE(cell.below_noise_count == 0);
        REQUIRE(cell.attempted_delivered_count == 1);
        REQUIRE(summary.output_magnitude_bins[expected_bin] == 1);
        REQUIRE(summary.output_axis_bins[0] == 1);
    }

    auto diagonal_history = make_neutral_and_axis_history({0.10f, 0.10f});
    auto diagonal = std::make_unique<runtime_app::Gate25LiveShadow>();
    seed_baseline(*diagonal, *diagonal_history);
    diagonal->observe(observation(3, 41 * kMs, 90.0f, 90.0f), diagonal_history.get());
    runtime_app::Gate25AggregateSnapshot diagonal_summary;
    REQUIRE(diagonal->flush_summary(diagonal_summary));
    REQUIRE(diagonal_summary.output_axis_bins[2] == 1);
    REQUIRE(diagonal_summary.output_magnitude_bins[5] == 1);
}

void test_interval_begin_flags_are_not_lost_at_first_boundary() {
    auto contaminated = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(contaminated->push(sample(1, 1 * kMs, {})));
    REQUIRE(contaminated->push(sample(
        2, 11 * kMs, {0.5f, 0.0f}, {0.5f, 0.0f}, true, true, true)));
    REQUIRE(contaminated->push(sample(3, 12 * kMs, {0.5f, 0.0f})));
    REQUIRE(contaminated->push(sample(4, 21 * kMs, {0.5f, 0.0f})));
    const auto integral = contaminated->integrate(11 * kMs, 21 * kMs);
    REQUIRE(integral.firing);
    REQUIRE(integral.recoil_active);
    REQUIRE(integral.saturated);

    auto exogenous = std::make_unique<runtime_app::Gate25LiveShadow>();
    exogenous->observe(observation(1, 21 * kMs, 100.0f, 100.0f),
                       contaminated.get());
    exogenous->observe(observation(2, 31 * kMs, 100.0f, 100.0f),
                       contaminated.get());
    exogenous->observe(observation(3, 41 * kMs, 90.0f, 100.0f),
                       contaminated.get());
    REQUIRE(exogenous->last_effect().reason ==
            runtime_app::Gate25Reason::ExogenousMotion);

    auto saturated_history = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(saturated_history->push(sample(1, 1 * kMs, {})));
    REQUIRE(saturated_history->push(sample(
        2, 11 * kMs, {0.5f, 0.0f}, {}, true)));
    REQUIRE(saturated_history->push(sample(3, 12 * kMs, {0.5f, 0.0f})));
    REQUIRE(saturated_history->push(sample(4, 21 * kMs, {0.5f, 0.0f})));
    auto saturated = std::make_unique<runtime_app::Gate25LiveShadow>();
    saturated->observe(observation(1, 21 * kMs, 100.0f, 100.0f),
                       saturated_history.get());
    saturated->observe(observation(2, 31 * kMs, 100.0f, 100.0f),
                       saturated_history.get());
    saturated->observe(observation(3, 41 * kMs, 90.0f, 100.0f),
                       saturated_history.get());
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(saturated->flush_summary(summary));
    REQUIRE(summary.saturation_rows == 1);
}

void test_control_history_coverage_and_flags_are_boundary_invariant() {
    auto no_boundary = std::make_unique<control_learning::ControlHistory<16>>();
    REQUIRE(no_boundary->push(sample(1, 1 * kMs, {0.5f, 0.0f}, {}, false, true)));
    REQUIRE(no_boundary->push(sample(2, 21 * kMs, {0.5f, 0.0f})));
    const auto short_integral = no_boundary->integrate(5 * kMs, 15 * kMs);
    REQUIRE(short_integral.complete);
    REQUIRE(short_integral.firing);
    REQUIRE_NEAR(short_integral.final_right_stick_seconds.x, 5.0e-3f, 1.0e-6f);

    auto with_boundary = std::make_unique<control_learning::ControlHistory<16>>();
    REQUIRE(with_boundary->push(sample(1, 1 * kMs, {0.5f, 0.0f}, {}, false, true)));
    REQUIRE(with_boundary->push(sample(2, 10 * kMs, {0.5f, 0.0f})));
    REQUIRE(with_boundary->push(sample(3, 21 * kMs, {0.5f, 0.0f})));
    const auto boundary_integral = with_boundary->integrate(5 * kMs, 15 * kMs);
    REQUIRE(boundary_integral.complete);
    REQUIRE(boundary_integral.firing);
    REQUIRE_NEAR(boundary_integral.final_right_stick_seconds.x, 5.0e-3f, 1.0e-6f);
    REQUIRE(!with_boundary->integrate(5 * kMs, 22 * kMs).complete);
}

void test_reversal_exposure_never_seeds_neutral_baseline() {
    auto equal = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(equal->push(sample(1, 1 * kMs, {})));
    REQUIRE(equal->push(sample(2, 11 * kMs, {0.5f, 0.0f})));
    REQUIRE(equal->push(sample(3, 16 * kMs, {-0.5f, 0.0f})));
    REQUIRE(equal->push(sample(4, 21 * kMs, {-0.5f, 0.0f})));
    const auto equal_integral = equal->integrate(11 * kMs, 21 * kMs);
    REQUIRE(equal_integral.complete);
    REQUIRE(equal_integral.final_right_reversal);
    REQUIRE_NEAR(equal_integral.final_right_stick_seconds.x, 0.0f, 1.0e-6f);
    REQUIRE(equal_integral.final_right_stick_abs_seconds > 0.004f);

    runtime_app::Gate25LiveShadow equal_gate;
    equal_gate.observe(observation(1, 21 * kMs, 100.0f, 100.0f), equal.get());
    equal_gate.observe(observation(2, 31 * kMs, 100.0f, 100.0f), equal.get());
    equal_gate.observe(observation(3, 41 * kMs, 100.0f, 100.0f), equal.get());
    REQUIRE(equal_gate.last_effect().reason ==
            runtime_app::Gate25Reason::CancellationOrReversal);
    runtime_app::Gate25AggregateSnapshot equal_summary;
    REQUIRE(equal_gate.flush_summary(equal_summary));
    REQUIRE(equal_summary.neutral_baseline_pairs == 1);
    REQUIRE(equal_summary.cancellation_or_reversal == 1);

    auto unequal = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(unequal->push(sample(1, 1 * kMs, {})));
    REQUIRE(unequal->push(sample(2, 11 * kMs, {0.5f, 0.0f})));
    REQUIRE(unequal->push(sample(3, 18 * kMs, {-0.5f, 0.0f})));
    REQUIRE(unequal->push(sample(4, 21 * kMs, {-0.5f, 0.0f})));
    runtime_app::Gate25LiveShadow unequal_gate;
    unequal_gate.observe(observation(1, 21 * kMs, 100.0f, 100.0f), unequal.get());
    unequal_gate.observe(observation(2, 31 * kMs, 100.0f, 100.0f), unequal.get());
    unequal_gate.observe(observation(3, 41 * kMs, 100.0f, 100.0f), unequal.get());
    REQUIRE(unequal_gate.last_effect().reason ==
            runtime_app::Gate25Reason::CancellationOrReversal);

    auto neutral = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(neutral->push(sample(1, 1 * kMs, {})));
    REQUIRE(neutral->push(sample(2, 11 * kMs, {})));
    REQUIRE(neutral->push(sample(3, 21 * kMs, {})));
    runtime_app::Gate25LiveShadow neutral_gate;
    neutral_gate.observe(observation(1, 21 * kMs, 100.0f, 100.0f), neutral.get());
    neutral_gate.observe(observation(2, 31 * kMs, 100.0f, 100.0f), neutral.get());
    REQUIRE(neutral_gate.last_effect().reason == runtime_app::Gate25Reason::None);
    runtime_app::Gate25AggregateSnapshot neutral_summary;
    REQUIRE(neutral_gate.flush_summary(neutral_summary));
    REQUIRE(neutral_summary.neutral_baseline_pairs == 1);
    REQUIRE(neutral_summary.cancellation_or_reversal == 0);

    // Counterfactual: a steady same-sign command over the same delayed
    // window is valid evidence and must not be classified as cancellation.
    auto steady = make_neutral_and_axis_history({0.5f, 0.0f});
    runtime_app::Gate25LiveShadow steady_gate;
    seed_baseline(steady_gate, *steady);
    steady_gate.observe(
        observation(3, 41 * kMs, 99.0f, 100.0f), steady.get());
    REQUIRE(steady_gate.last_effect().valid);
    REQUIRE(steady_gate.last_effect().reason == runtime_app::Gate25Reason::None);
    runtime_app::Gate25AggregateSnapshot steady_summary;
    REQUIRE(steady_gate.flush_summary(steady_summary));
    REQUIRE(steady_summary.effect_valid == 1);
    REQUIRE(steady_summary.cancellation_or_reversal == 0);
    REQUIRE(steady_summary.neutral_baseline_pairs == 1);
    std::cout << "gate25_reversal_counterfactual_effect_valid="
              << steady_summary.effect_valid
              << " cancellation=" << steady_summary.cancellation_or_reversal
              << " baseline=" << steady_summary.neutral_baseline_pairs << '\n';
}

void test_directional_sufficient_statistics_survive_signed_cancellation() {
    control_learning::ControlHistory<1024> history;
    REQUIRE(history.push(sample(1, 1 * kMs, {})));
    REQUIRE(history.push(sample(2, 11 * kMs, {})));
    REQUIRE(history.push(sample(3, 21 * kMs, {})));
    REQUIRE(history.push(sample(4, 31 * kMs, {0.5f, 0.0f})));
    REQUIRE(history.push(sample(5, 41 * kMs, {0.5f, 0.0f})));
    REQUIRE(history.push(sample(6, 51 * kMs, {0.5f, 0.0f})));
    REQUIRE(history.push(sample(7, 61 * kMs, {})));
    REQUIRE(history.push(sample(8, 71 * kMs, {})));
    REQUIRE(history.push(sample(9, 81 * kMs, {-0.5f, 0.0f})));
    REQUIRE(history.push(sample(10, 91 * kMs, {-0.5f, 0.0f})));
    REQUIRE(history.push(sample(11, 101 * kMs, {-0.5f, 0.0f})));
    REQUIRE(history.push(sample(12, 111 * kMs, {})));
    REQUIRE(history.push(sample(13, 121 * kMs, {})));

    runtime_app::Gate25LiveShadow shadow;
    const std::array<float, 12> anchors{
        100.0f, 100.0f, 100.0f, 100.0f, 99.0f, 98.0f, 97.0f,
        97.0f, 97.0f, 98.0f, 99.0f, 100.0f};
    for (std::uint64_t frame = 1; frame <= anchors.size(); ++frame) {
        shadow.observe(observation(
            frame, (11 + frame * 10) * kMs, anchors[frame - 1], 100.0f),
            &history);
    }
    REQUIRE(shadow.last_effect().valid);
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(shadow.flush_summary(summary));
    // The positive and negative rows share the same X/>10% cell. Signed sums
    // cancel, but energy, absolute exposure, dot and sign evidence remain.
    const auto& cell = summary.cohort_grid[53]; // BodyLock, X, >10%.
    REQUIRE(cell.valid_count >= 4);
    REQUIRE(std::fabs(cell.delivered_final_sum_x) < 1.0e-5f);
    REQUIRE(cell.delivered_final_abs_sum > 0.01f);
    REQUIRE(cell.delivered_final_energy_sum > 0.00001f);
    REQUIRE(cell.observed_camera_energy_sum > 1.0f);
    REQUIRE(cell.delivered_observed_dot_sum > 0.001f);
    REQUIRE(cell.component_sign_agree_count >= 4);
    REQUIRE(cell.component_sign_disagree_count == 0);

    auto wrong_history = make_neutral_and_axis_history({0.5f, 0.0f});
    runtime_app::Gate25LiveShadow wrong_sign;
    seed_baseline(wrong_sign, *wrong_history);
    wrong_sign.observe(observation(3, 41 * kMs, 101.0f, 100.0f),
                       wrong_history.get());
    runtime_app::Gate25AggregateSnapshot wrong_summary;
    REQUIRE(wrong_sign.flush_summary(wrong_summary));
    const auto& wrong_cell = wrong_summary.cohort_grid[53];
    REQUIRE(wrong_cell.valid_count == 1);
    REQUIRE(wrong_cell.component_sign_disagree_count >= 1);
}

void test_gate25_clock_domain_transitions_preserve_summary_labels() {
    runtime_app::Gate25LiveShadow shadow;
    auto source = observation(1, 1'000'000'000ull, 100.0f, 100.0f);
    source.persistent_target_id = 0;
    source.target_acquisition_id = 0;
    shadow.observe(source, nullptr);

    auto missing = observation(2, 0, 100.0f, 100.0f);
    missing.source_present_available = false;
    missing.source_present_steady_available = false;
    missing.source_present_qpc = 0;
    missing.source_present_qpc_frequency = 0;
    missing.source_present_calibration_id = 0;
    missing.source_present_steady_ns = 0;
    missing.controller_consume_ns = 2'000'000'000ull;
    shadow.observe(missing, nullptr);
    runtime_app::Gate25AggregateSnapshot first;
    REQUIRE(shadow.take_due_summary(first));
    REQUIRE(first.window_clock_domain ==
            runtime_app::Gate25WindowClockDomain::SourcePresentSteady);
    REQUIRE(first.observations == 1);

    auto recovered = observation(3, 3'000'000'000ull, 100.0f, 100.0f);
    shadow.observe(recovered, nullptr);
    runtime_app::Gate25AggregateSnapshot diagnostic;
    REQUIRE(shadow.take_due_summary(diagnostic));
    REQUIRE(diagnostic.window_clock_domain ==
            runtime_app::Gate25WindowClockDomain::CollectorMonotonicDiagnostic);
    REQUIRE(diagnostic.observations == 1);
    runtime_app::Gate25AggregateSnapshot source_again;
    REQUIRE(shadow.flush_summary(source_again));
    REQUIRE(source_again.window_clock_domain ==
            runtime_app::Gate25WindowClockDomain::SourcePresentSteady);
    REQUIRE(source_again.observations == 1);
}

void test_gate25_anomaly_ring_overflow_is_explicit_and_bounded() {
    runtime_app::Gate25LiveShadow shadow;
    const auto input = observation(1, 21 * kMs, 100.0f, 100.0f);
    shadow.observe(input, nullptr);
    for (std::size_t i = 0; i < 300; ++i) shadow.observe(input, nullptr);
    REQUIRE(shadow.pending_anomaly_count() ==
            runtime_app::Gate25LiveShadow::kAnomalyCapacity);
    runtime_app::Gate25AggregateSnapshot summary;
    REQUIRE(shadow.flush_summary(summary));
    REQUIRE(summary.anomaly_count == 300);
    REQUIRE(summary.anomaly_dropped == 44);
    std::size_t retained = 0;
    runtime_app::Gate25Anomaly anomaly;
    while (shadow.pop_anomaly(anomaly)) ++retained;
    REQUIRE(retained == runtime_app::Gate25LiveShadow::kAnomalyCapacity);
    REQUIRE(!shadow.pop_anomaly(anomaly));
}

void test_mode_transitions_are_explicit_boundaries() {
    auto history = make_neutral_and_axis_history({0.5f, 0.0f});
    runtime_app::Gate25LiveShadow snap_to_body;
    auto snap = observation(1, 21 * kMs, 100.0f, 100.0f);
    snap.mode = static_cast<std::uint8_t>(pipeline_contract::ControlMode::AdsAcquire);
    auto body = observation(2, 31 * kMs, 100.0f, 100.0f);
    body.mode = static_cast<std::uint8_t>(pipeline_contract::ControlMode::BodyLockFollow);
    snap_to_body.observe(snap, history.get());
    snap_to_body.observe(body, history.get());
    REQUIRE(snap_to_body.last_effect().reason == runtime_app::Gate25Reason::ModeBoundary);
    runtime_app::Gate25AggregateSnapshot snap_summary;
    REQUIRE(snap_to_body.flush_summary(snap_summary));
    REQUIRE(snap_summary.mode_boundary == 1);
    REQUIRE(snap_summary.effect_valid == 0);

    runtime_app::Gate25LiveShadow body_to_snap;
    auto body_first = observation(1, 21 * kMs, 100.0f, 100.0f);
    body_first.mode = static_cast<std::uint8_t>(pipeline_contract::ControlMode::BodyLockFollow);
    auto snap_second = observation(2, 31 * kMs, 100.0f, 100.0f);
    snap_second.mode = static_cast<std::uint8_t>(pipeline_contract::ControlMode::AdsAcquire);
    body_to_snap.observe(body_first, history.get());
    body_to_snap.observe(snap_second, history.get());
    REQUIRE(body_to_snap.last_effect().reason == runtime_app::Gate25Reason::ModeBoundary);

    runtime_app::Gate25LiveShadow same_mode;
    auto first = observation(1, 21 * kMs, 100.0f, 100.0f);
    first.mode = static_cast<std::uint8_t>(pipeline_contract::ControlMode::BodyLockFollow);
    auto second = observation(2, 31 * kMs, 100.0f, 100.0f);
    second.mode = first.mode;
    auto third = observation(3, 41 * kMs, 90.0f, 100.0f);
    third.mode = first.mode;
    same_mode.observe(first, history.get());
    same_mode.observe(second, history.get());
    same_mode.observe(third, history.get());
    REQUIRE(same_mode.last_effect().valid);
}

void test_control_history_requires_explicit_cold_start_coverage() {
    auto nonneutral_first = std::make_unique<control_learning::ControlHistory<16>>();
    REQUIRE(nonneutral_first->push(sample(1, 10 * kMs, {0.5f, 0.0f})));
    REQUIRE(nonneutral_first->push(sample(2, 20 * kMs, {0.5f, 0.0f})));
    REQUIRE(!nonneutral_first->integrate(5 * kMs, 15 * kMs).complete);

    auto neutral_first = std::make_unique<control_learning::ControlHistory<16>>();
    REQUIRE(neutral_first->push(sample(1, 10 * kMs, {})));
    REQUIRE(neutral_first->push(sample(2, 20 * kMs, {})));
    REQUIRE(!neutral_first->integrate(5 * kMs, 15 * kMs).complete);

    auto anchored = std::make_unique<control_learning::ControlHistory<16>>();
    REQUIRE(anchored->push(sample(1, 1 * kMs, {})));
    REQUIRE(anchored->push(sample(2, 10 * kMs, {0.5f, 0.0f})));
    REQUIRE(anchored->push(sample(3, 20 * kMs, {0.5f, 0.0f})));
    const auto anchored_integral = anchored->integrate(5 * kMs, 15 * kMs);
    REQUIRE(anchored_integral.complete);
    REQUIRE_NEAR(anchored_integral.final_right_stick_seconds.x, 2.5e-3f, 1.0e-6f);
}

void test_gate_rejects_output_disabled_intervals_without_mixing_labels() {
    auto held_disabled = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(held_disabled->push(sample(1, 1 * kMs, {0.5f, 0.0f}, {}, false, false, false, true)));
    REQUIRE(held_disabled->push(sample(2, 11 * kMs, {0.5f, 0.0f})));
    REQUIRE(held_disabled->push(sample(3, 21 * kMs, {0.5f, 0.0f})));
    const auto held_integral = held_disabled->integrate(1 * kMs, 11 * kMs);
    REQUIRE(held_integral.complete);
    REQUIRE(held_integral.output_disabled);

    auto short_disabled = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(short_disabled->push(sample(
        1, 1 * kMs, {0.5f, 0.0f}, {}, false, false, false, true)));
    REQUIRE(short_disabled->push(sample(2, 21 * kMs, {0.5f, 0.0f})));
    const auto short_disabled_integral = short_disabled->integrate(5 * kMs, 15 * kMs);
    REQUIRE(short_disabled_integral.complete);
    REQUIRE(short_disabled_integral.output_disabled);

    runtime_app::Gate25LiveShadow short_gate;
    short_gate.observe(observation(1, 25 * kMs, 100.0f, 100.0f), short_disabled.get());
    short_gate.observe(observation(2, 35 * kMs, 100.0f, 100.0f), short_disabled.get());
    REQUIRE(short_gate.last_effect().reason == runtime_app::Gate25Reason::BackendBoundary);

    runtime_app::Gate25LiveShadow held_gate;
    held_gate.observe(observation(1, 21 * kMs, 100.0f, 100.0f), held_disabled.get());
    held_gate.observe(observation(2, 31 * kMs, 100.0f, 100.0f), held_disabled.get());
    REQUIRE(held_gate.last_effect().reason == runtime_app::Gate25Reason::BackendBoundary);

    auto in_interval_disabled = std::make_unique<control_learning::ControlHistory<1024>>();
    REQUIRE(in_interval_disabled->push(sample(1, 1 * kMs, {})));
    REQUIRE(in_interval_disabled->push(sample(
        2, 11 * kMs, {0.5f, 0.0f}, {}, false, false, false, true)));
    REQUIRE(in_interval_disabled->push(sample(3, 12 * kMs, {0.5f, 0.0f})));
    REQUIRE(in_interval_disabled->push(sample(4, 21 * kMs, {0.5f, 0.0f})));
    const auto interval_integral = in_interval_disabled->integrate(11 * kMs, 21 * kMs);
    REQUIRE(interval_integral.complete);
    REQUIRE(interval_integral.output_disabled);

    runtime_app::Gate25LiveShadow interval_gate;
    interval_gate.observe(observation(1, 21 * kMs, 100.0f, 100.0f), in_interval_disabled.get());
    interval_gate.observe(observation(2, 31 * kMs, 100.0f, 100.0f), in_interval_disabled.get());
    interval_gate.observe(observation(3, 41 * kMs, 90.0f, 100.0f), in_interval_disabled.get());
    REQUIRE(interval_gate.last_effect().reason == runtime_app::Gate25Reason::BackendBoundary);
    runtime_app::Gate25AggregateSnapshot interval_summary;
    REQUIRE(interval_gate.flush_summary(interval_summary));
    REQUIRE(interval_summary.exogenous_rejected == 0);
    REQUIRE(interval_summary.saturation_rows == 0);
    REQUIRE(interval_summary.backend_boundary >= 1);

    auto failed = std::make_unique<control_learning::ControlHistory<1024>>();
    auto failed_sample = sample(1, 1 * kMs, {});
    failed_sample.output_delivered = false;
    REQUIRE(failed->push(failed_sample));
    REQUIRE(failed->push(sample(2, 11 * kMs, {0.5f, 0.0f})));
    REQUIRE(failed->push(sample(3, 21 * kMs, {0.5f, 0.0f})));
    const auto failed_integral = failed->integrate(1 * kMs, 11 * kMs);
    REQUIRE(!failed_integral.complete);
    runtime_app::Gate25LiveShadow failed_gate;
    failed_gate.observe(observation(1, 21 * kMs, 100.0f, 100.0f), failed.get());
    failed_gate.observe(observation(2, 31 * kMs, 100.0f, 100.0f), failed.get());
    REQUIRE(failed_gate.last_effect().reason ==
            runtime_app::Gate25Reason::IncompleteDeliveryHistory);
}

void test_present_anchor_effect_is_invariant_to_copy_complete_offset() {
    auto history = make_neutral_and_axis_history({0.5f, 0.0f});
    auto present_anchored = std::make_unique<runtime_app::Gate25LiveShadow>();
    auto shifted_copy = std::make_unique<runtime_app::Gate25LiveShadow>();
    for (std::uint64_t frame = 1; frame <= 2; ++frame) {
        auto a = observation(frame, (11 + frame * 10) * kMs, 100.0f, 100.0f);
        auto b = a;
        b.captured_at_ns += (frame == 1 ? 0 : 6 * kMs);
        present_anchored->observe(a, history.get());
        shifted_copy->observe(b, history.get());
    }
    auto a = observation(3, 41 * kMs, 90.0f, 100.0f);
    auto b = a;
    b.captured_at_ns += 6 * kMs;
    present_anchored->observe(a, history.get());
    shifted_copy->observe(b, history.get());
    REQUIRE(present_anchored->last_effect().valid);
    REQUIRE(shifted_copy->last_effect().valid);
    REQUIRE_NEAR(present_anchored->last_effect().observed_camera_work_x,
                 shifted_copy->last_effect().observed_camera_work_x, 1.0e-5f);
    REQUIRE_NEAR(present_anchored->last_effect().observed_camera_work_y,
                 shifted_copy->last_effect().observed_camera_work_y, 1.0e-5f);
    // The Gate effect join intentionally uses calibrated source-present
    // endpoints; legacy capture_time_seconds is kept as copy-complete timing
    // and is not allowed to replace this independent W5 clock.
}

void test_invalid_geometry_and_missing_clock_keep_diagnostic_windows() {
    runtime_app::Gate25LiveShadow invalid_geometry;
    for (std::uint64_t frame = 1; frame <= 899; ++frame) {
        auto value = observation(
            frame, frame * 5'555'556ull, 100.0f, 100.0f);
        value.stable_coordinates_valid = false;
        invalid_geometry.observe(value, nullptr);
    }
    runtime_app::Gate25AggregateSnapshot geometry_summary;
    REQUIRE(invalid_geometry.flush_summary(geometry_summary));
    REQUIRE(geometry_summary.observations == 899);
    REQUIRE(geometry_summary.invalid_geometry == 899);
    REQUIRE(geometry_summary.effect_valid == 0);
    REQUIRE(geometry_summary.window_clock_domain ==
            runtime_app::Gate25WindowClockDomain::SourcePresentSteady);

    runtime_app::Gate25LiveShadow missing_clock;
    for (std::uint64_t frame = 1; frame <= 899; ++frame) {
        auto value = observation(
            frame, frame * 5'555'556ull, 100.0f, 100.0f);
        value.source_present_available = false;
        value.source_present_steady_available = false;
        value.source_present_qpc = 0;
        value.source_present_steady_ns = 0;
        value.controller_consume_ns = frame * 5'555'556ull;
        value.decision_ns = value.controller_consume_ns;
        missing_clock.observe(value, nullptr);
    }
    runtime_app::Gate25AggregateSnapshot missing_summary;
    REQUIRE(missing_clock.flush_summary(missing_summary));
    REQUIRE(missing_summary.observations == 899);
    REQUIRE(missing_summary.missing_present_clock == 899);
    REQUIRE(missing_summary.effect_valid == 0);
    REQUIRE(missing_summary.window_clock_domain ==
            runtime_app::Gate25WindowClockDomain::CollectorMonotonicDiagnostic);
    runtime_app::Gate25Anomaly anomaly;
    REQUIRE(missing_clock.pop_anomaly(anomaly));
    REQUIRE(anomaly.reason == runtime_app::Gate25Reason::MissingPresentClock);
    REQUIRE(!missing_clock.pop_anomaly(anomaly));
}

void test_gate25_release_hot_path_is_measured() {
    struct Stats {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double max = 0.0;
        std::uint64_t observed_calls = 0;
        std::uint64_t valid_pairs = 0;
        std::uint64_t neutral_pairs = 0;
        std::uint64_t incomplete_pairs = 0;
        std::uint64_t backward_pairs = 0;
        std::uint64_t delivery_pushes = 0;
    };
    constexpr std::size_t kWarmupTicks = 1'000;
    constexpr std::size_t kMeasuredDirectTicks = 10'000;
    constexpr std::size_t kMeasuredMixedTicks = 100'000;
    constexpr std::size_t kBlockTicks = 500;
    constexpr std::uint64_t kHistoryEndMs = 911;

    const auto percentile = [](std::vector<std::uint64_t>& values,
                               std::size_t rank) {
        std::sort(values.begin(), values.end());
        return static_cast<double>(values[rank]) / 1000.0;
    };
    const auto finish_timing = [&](std::vector<std::uint64_t>& elapsed,
                                   Stats stats) {
        REQUIRE(!elapsed.empty());
        stats.p50 = percentile(elapsed, elapsed.size() / 2);
        stats.p95 = percentile(elapsed, (elapsed.size() * 95) / 100);
        stats.p99 = percentile(elapsed, (elapsed.size() * 99) / 100);
        stats.max = static_cast<double>(
            *std::max_element(elapsed.begin(), elapsed.end())) / 1000.0;
        return stats;
    };
    const auto print = [](const char* name, const Stats& stats) {
        std::cout << "gate25_perf_" << name
                  << "_us_p50=" << stats.p50
                  << " p95=" << stats.p95
                  << " p99=" << stats.p99
                  << " max=" << stats.max
                  << " observer_calls=" << stats.observed_calls
                  << " valid_pairs=" << stats.valid_pairs
                  << " neutral_pairs=" << stats.neutral_pairs
                  << " incomplete=" << stats.incomplete_pairs
                  << " backward=" << stats.backward_pairs
                  << " delivery_pushes=" << stats.delivery_pushes << '\n';
    };

    const auto make_history = [](bool commanded, std::uint64_t end_ms) {
        auto history = std::make_unique<
            control_learning::ControlHistory<1024>>();
        for (std::uint64_t ms = 1; ms <= end_ms; ++ms) {
            const pipeline_contract::Vec2f output =
                commanded && ms >= 412 ? pipeline_contract::Vec2f{0.20f, 0.0f}
                                        : pipeline_contract::Vec2f{};
            REQUIRE(history->push(sample(ms, ms * kMs, output)));
        }
        return history;
    };
    const auto seed_block = [](runtime_app::Gate25LiveShadow& shadow,
                               const control_learning::ControlHistory<1024>& history) {
        shadow.reset();
        shadow.observe(observation(1, 421 * kMs, 100.0f, 100.0f), &history);
        shadow.observe(observation(2, 431 * kMs, 100.0f, 100.0f), &history);
    };
    const auto account_summary = [](const runtime_app::Gate25AggregateSnapshot& summary,
                                    Stats& stats) {
        stats.valid_pairs += summary.effect_valid;
        stats.neutral_pairs += summary.neutral_baseline_pairs;
        stats.incomplete_pairs += summary.incomplete_delivery_history;
        stats.backward_pairs += summary.backward_present;
    };

    const auto run_direct = [&](bool commanded) {
        auto history = make_history(commanded, kHistoryEndMs);
        runtime_app::Gate25LiveShadow shadow;
        std::vector<std::uint64_t> elapsed;
        elapsed.reserve(kMeasuredDirectTicks);
        Stats stats;
        const std::size_t total_ticks = kWarmupTicks + kMeasuredDirectTicks;
        for (std::size_t global = 0; global < total_ticks; ++global) {
            const std::size_t block = global / kBlockTicks;
            const std::size_t in_block = global % kBlockTicks;
            if (in_block == 0) {
                seed_block(shadow, *history);
            }
            const bool measure = global >= kWarmupTicks;
            const auto begin = std::chrono::steady_clock::now();
            auto input = observation(
                3 + in_block, (432 + in_block) * kMs,
                commanded
                    ? (in_block == 0
                        ? 100.0f
                        : 99.0f - static_cast<float>(in_block - 1))
                    : 100.0f,
                100.0f);
            shadow.observe(input, history.get());
            const auto end = std::chrono::steady_clock::now();
            if (measure) {
                elapsed.push_back(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - begin).count()));
                ++stats.observed_calls;
            }
            if (in_block + 1 == kBlockTicks) {
                runtime_app::Gate25AggregateSnapshot summary;
                REQUIRE(shadow.flush_summary(summary));
                if (measure || global + 1 > kWarmupTicks)
                    account_summary(summary, stats);
            }
            (void)block;
        }
        return finish_timing(elapsed, stats);
    };

    const auto run_no_target = [&]() {
        runtime_app::Gate25LiveShadow shadow;
        std::vector<std::uint64_t> elapsed;
        elapsed.reserve(kMeasuredDirectTicks);
        Stats stats;
        const std::size_t total_ticks = kWarmupTicks + kMeasuredDirectTicks;
        for (std::size_t global = 0; global < total_ticks; ++global) {
            const std::size_t in_block = global % kBlockTicks;
            if (in_block == 0) shadow.reset();
            const bool measure = global >= kWarmupTicks;
            const auto begin = std::chrono::steady_clock::now();
            auto input = observation(3 + in_block, (432 + in_block) * kMs,
                                     100.0f, 100.0f);
            input.source_frame_id = 0;
            input.source_observation_id = 0;
            input.persistent_target_id = 0;
            input.target_acquisition_id = 0;
            shadow.observe(input, nullptr);
            const auto end = std::chrono::steady_clock::now();
            if (measure) {
                elapsed.push_back(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - begin).count()));
                ++stats.observed_calls;
            }
        }
        return finish_timing(elapsed, stats);
    };

    const auto run_mixed = [&]() {
        auto history = make_history(false, 411);
        runtime_app::Gate25LiveShadow shadow;
        std::vector<std::uint64_t> elapsed;
        elapsed.reserve(kMeasuredMixedTicks);
        Stats stats;
        std::uint64_t phase = 0;
        std::size_t observation_count = 0;
        const std::size_t total_ticks = kWarmupTicks + kMeasuredMixedTicks;
        for (std::size_t global = 0; global < total_ticks; ++global) {
            const std::size_t in_block = global % kBlockTicks;
            if (in_block == 0) {
                history = make_history(false, 411);
                seed_block(shadow, *history);
                phase = 0;
                observation_count = 0;
            }
            const bool measure = global >= kWarmupTicks;
            const auto begin = std::chrono::steady_clock::now();
            const std::uint64_t delivery_at_ns = (412 + in_block) * kMs;
            REQUIRE(history->push(sample(
                412 + in_block, delivery_at_ns, {0.20f, 0.0f})));
            ++stats.delivery_pushes;
            phase += 180;
            if (phase >= 1000) {
                phase -= 1000;
                auto input = observation(
                    3 + observation_count,
                    (432 + in_block) * kMs,
                    99.0f - static_cast<float>(observation_count), 100.0f);
                shadow.observe(input, history.get());
                ++stats.observed_calls;
                ++observation_count;
            }
            const auto end = std::chrono::steady_clock::now();
            if (measure) {
                elapsed.push_back(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - begin).count()));
            }
            if (in_block + 1 == kBlockTicks) {
                runtime_app::Gate25AggregateSnapshot summary;
                REQUIRE(shadow.flush_summary(summary));
                if (global + 1 > kWarmupTicks) account_summary(summary, stats);
            }
        }
        stats.delivery_pushes -= kWarmupTicks;
        stats.observed_calls -= 180ull * (kWarmupTicks / 1000);
        return finish_timing(elapsed, stats);
    };

    const auto no_target_stats = run_no_target();
    const auto neutral_stats = run_direct(false);
    const auto valid_stats = run_direct(true);
    const auto mixed_stats = run_mixed();
    print("no_target", no_target_stats);
    print("neutral_baseline", neutral_stats);
    print("valid_effect", valid_stats);
    print("mixed_1000hz_180hz_end_to_end", mixed_stats);
    REQUIRE(valid_stats.valid_pairs > 0);
    REQUIRE(valid_stats.incomplete_pairs == 0);
    REQUIRE(valid_stats.backward_pairs == 0);
    REQUIRE(mixed_stats.delivery_pushes == kMeasuredMixedTicks);
    REQUIRE(mixed_stats.observed_calls > 0);
    REQUIRE(mixed_stats.valid_pairs > 0);
    REQUIRE(mixed_stats.incomplete_pairs == 0);
    REQUIRE(mixed_stats.backward_pairs == 0);
    REQUIRE(mixed_stats.p95 <= 0.5);
    REQUIRE(mixed_stats.p99 <= 1.0);
}

void test_gate25b_observer_object_stays_bounded() {
    // The complete Gate-only budget is measured through the real
    // TelemetryCollectors/RuntimeTelemetry seam. This focused core target
    // only guards the observer object itself; the historical 725,520-byte
    // Gate2.5A RED is preserved in the Phase B artifact, not as a permanent
    // failing executable assertion.
    REQUIRE(sizeof(runtime_app::Gate25LiveShadow) < 128u * 1024u);
}

}  // namespace

int main() {
    test_shifted_present_interval_preserves_full_2d_sign();
    test_current_present_neutral_does_not_poison_baseline_for_delayed_work();
    test_rollover_carries_neutral_baseline();
    test_exogenous_is_rejected_before_baseline_and_saturation_is_separate();
    test_missing_delay_and_identity_boundaries_are_explicit();
    test_lifecycle_viewport_and_backend_boundaries_are_explicit();
    test_rollover_precedes_no_unsigned_backward_rollover();
    test_source_ordering_rejects_without_advancing_and_recovers_once();
    test_calibration_ids_are_endpoint_provenance_not_boundaries();
    test_capture_gap_and_profile_provenance_are_explicit();
    test_exact_passive_output_magnitude_bins();
    test_interval_begin_flags_are_not_lost_at_first_boundary();
    test_control_history_coverage_and_flags_are_boundary_invariant();
    test_control_history_requires_explicit_cold_start_coverage();
    test_reversal_exposure_never_seeds_neutral_baseline();
    test_directional_sufficient_statistics_survive_signed_cancellation();
    test_mode_transitions_are_explicit_boundaries();
    test_gate_rejects_output_disabled_intervals_without_mixing_labels();
    test_present_anchor_effect_is_invariant_to_copy_complete_offset();
    test_invalid_geometry_and_missing_clock_keep_diagnostic_windows();
    test_gate25_clock_domain_transitions_preserve_summary_labels();
    test_gate25_anomaly_ring_overflow_is_explicit_and_bounded();
    test_gate_ledger_join_uses_gate_accepted_previous();
    test_gate25_release_hot_path_is_measured();
    test_gate25b_observer_object_stays_bounded();
    return 0;
}
