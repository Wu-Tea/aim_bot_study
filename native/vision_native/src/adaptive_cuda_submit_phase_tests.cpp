#include "vision_native/adaptive_cuda_submit_phase.h"
#include "vision_native/cuda_submit_phase_schedule.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using Controller = vision_native::AdaptiveCudaSubmitPhaseController;

void require(bool value, const char* expression) {
    if (!value) {
        std::fprintf(stderr, "require failed: %s\n", expression);
        std::exit(19);
    }
}

#define REQUIRE(value) require((value), #value)

Controller::Config test_config() {
    auto config = Controller::default_config();
    config.candidate_phases_us.fill(0);
    config.candidate_phases_us[0] = 0;
    config.candidate_phases_us[1] = 1000;
    config.candidate_phases_us[2] = 2000;
    config.candidate_count = 3;
    config.block_samples = 3;
    config.min_cadence_samples = 3;
    return config;
}

Controller::FrameContext frame(
    std::uint64_t timestamp_ns,
    bool active = true,
    std::uint64_t context_id = 1,
    std::uint64_t regime_id = 1,
    std::uint32_t accumulated_frames = 1) {
    Controller::FrameContext value{};
    value.source_present_steady_ns = timestamp_ns;
    value.source_present_steady_available = timestamp_ns != 0;
    value.accumulated_frames = accumulated_frames;
    value.active = active;
    value.context_id = context_id;
    value.context_id_available = true;
    value.regime_id = regime_id;
    value.regime_id_available = true;
    return value;
}

Controller::FrameContext frame_qpc(
    std::uint64_t steady_timestamp_ns,
    std::uint64_t qpc_timestamp,
    std::uint64_t qpc_frequency = 10'000'000,
    std::uint32_t accumulated_frames = 1) {
    auto value = frame(
        steady_timestamp_ns, true, qpc_frequency, 1, accumulated_frames);
    value.source_present_qpc = qpc_timestamp;
    value.source_present_qpc_frequency = qpc_frequency;
    value.source_present_qpc_available = true;
    return value;
}

Controller::CompletedObservation observation(
    std::uint64_t timestamp_ns,
    std::uint32_t phase_us,
    float wait_ms,
    float output_wait_ms,
    float residual_ms = 0.0f,
    float tail_ms = 0.0f,
    bool active = true,
    std::uint64_t context_id = 1,
    std::uint64_t regime_id = 1,
    std::uint32_t accumulated_frames = 1) {
    Controller::CompletedObservation value{};
    value.source_present_steady_ns = timestamp_ns;
    value.source_present_steady_available = true;
    value.applied_phase_us = phase_us;
    value.cuda_submit_wait_ms = wait_ms;
    value.output_wait_ms = output_wait_ms;
    value.sync_queue_residual_ms = residual_ms;
    value.tail_latency_ms = tail_ms;
    value.accumulated_frames = accumulated_frames;
    value.completed = true;
    value.timing_confident = true;
    value.active = active;
    value.context_id = context_id;
    value.context_id_available = true;
    value.regime_id = regime_id;
    value.regime_id_available = true;
    return value;
}

void warm_cadence(
    Controller& controller,
    std::uint64_t* timestamp_ns,
    std::uint64_t period_ns = 5'000'000,
    std::uint32_t accumulated_frames = 1) {
    for (int i = 0; i < 5; ++i) {
        (void)controller.recommend(
            frame(*timestamp_ns, true, 1, 1, accumulated_frames));
        *timestamp_ns += period_ns;
    }
}

void complete_candidate_block(
    Controller& controller,
    std::uint64_t* timestamp_ns,
    std::uint32_t expected_phase_us,
    float wait_ms,
    float output_wait_ms,
    float residual_ms = 0.0f,
    float tail_ms = 0.0f,
    bool active = true,
    std::uint64_t context_id = 1,
    std::uint64_t regime_id = 1,
    std::uint64_t period_ns = 5'000'000,
    std::uint32_t accumulated_frames = 1) {
    for (int i = 0; i < 3; ++i) {
        const auto decision = controller.recommend(
            frame(
                *timestamp_ns,
                active,
                context_id,
                regime_id,
                accumulated_frames));
        if (decision.phase_us != expected_phase_us) {
            std::fprintf(
                stderr,
                "phase mismatch expected=%u actual=%u mode=%d reason=%d timestamp=%llu\n",
                expected_phase_us,
                decision.phase_us,
                static_cast<int>(decision.mode),
                static_cast<int>(decision.reason),
                static_cast<unsigned long long>(*timestamp_ns));
        }
        REQUIRE(decision.phase_us == expected_phase_us);
        controller.observe(observation(
            *timestamp_ns,
            decision.phase_us,
            wait_ms,
            output_wait_ms,
            residual_ms,
            tail_ms,
            active,
            context_id,
            regime_id,
            accumulated_frames));
        *timestamp_ns += period_ns;
    }
}

void test_invalid_and_unstable_cadence_fall_back() {
    Controller controller(test_config());
    auto missing = controller.recommend(frame(0));
    REQUIRE(missing.phase_us == 0);
    REQUIRE(missing.reason == Controller::DecisionReason::MissingSourceTimestamp);

    std::uint64_t timestamp = 1'000'000'000;
    (void)controller.recommend(frame(timestamp));
    timestamp += 5'000'000;
    (void)controller.recommend(frame(timestamp));
    timestamp += 8'000'000;
    const auto unstable = controller.recommend(frame(timestamp));
    REQUIRE(unstable.phase_us == 0);
    timestamp += 5'000'000;
    const auto still_unstable = controller.recommend(frame(timestamp));
    REQUIRE(still_unstable.phase_us == 0);
}

void test_stable_200_hz_phase_is_bounded_by_next_frame_guard() {
    Controller controller;
    std::uint64_t timestamp = 2'000'000'000;
    bool saw_stable = false;
    for (int i = 0; i < 24; ++i) {
        const auto decision = controller.recommend(frame(timestamp));
        if (decision.cadence_stable) {
            saw_stable = true;
            REQUIRE(decision.estimated_period_us == 5000);
            REQUIRE(decision.phase_us <= 4500);
            REQUIRE(decision.phase_us <= 5000);
        }
        timestamp += 5'000'000;
    }
    REQUIRE(saw_stable);
}

void test_nonzero_optimum_is_learned_and_held() {
    Controller controller(test_config());
    std::uint64_t timestamp = 3'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 0.40f);

    for (int i = 0; i < 20; ++i) {
        const auto decision = controller.recommend(frame(timestamp));
        REQUIRE(decision.mode == Controller::Mode::Held);
        REQUIRE(decision.reason == Controller::DecisionReason::HeldCandidate);
        REQUIRE(decision.phase_us == 2000);
        timestamp += 5'000'000;
    }
}

void test_accumulated_frames_normalize_underlying_cadence() {
    Controller controller(test_config());
    std::uint64_t timestamp = 3'500'000'000;
    (void)controller.recommend(frame(timestamp));
    constexpr std::uint32_t kAccumulatedPattern[] = {2, 1, 3, 2, 1, 3};
    bool saw_stable = false;
    for (const std::uint32_t accumulated : kAccumulatedPattern) {
        timestamp += static_cast<std::uint64_t>(accumulated) * 5'000'000ull;
        const auto decision = controller.recommend(
            frame(timestamp, true, 1, 1, accumulated));
        if (decision.cadence_stable) {
            saw_stable = true;
            REQUIRE(decision.estimated_period_us == 5000);
            REQUIRE(decision.mode == Controller::Mode::Exploring);
        }
    }
    REQUIRE(saw_stable);
    REQUIRE(controller.snapshot().cadence_stable);
    REQUIRE(controller.snapshot().estimated_period_us == 5000);
}

void test_accumulated_frames_can_complete_exploration_and_hold() {
    Controller controller(test_config());
    std::uint64_t timestamp = 3'750'000'000;
    constexpr std::uint64_t kCapturePeriodNs = 10'000'000;
    constexpr std::uint32_t kAccumulatedFrames = 2;
    warm_cadence(
        controller, &timestamp, kCapturePeriodNs, kAccumulatedFrames);
    complete_candidate_block(
        controller, &timestamp, 0, 0.0f, 2.0f, 0.0f, 0.0f,
        true, 1, 1, kCapturePeriodNs, kAccumulatedFrames);
    complete_candidate_block(
        controller, &timestamp, 1000, 0.0f, 1.0f, 0.0f, 0.0f,
        true, 1, 1, kCapturePeriodNs, kAccumulatedFrames);
    complete_candidate_block(
        controller, &timestamp, 2000, 0.0f, 0.40f, 0.0f, 0.0f,
        true, 1, 1, kCapturePeriodNs, kAccumulatedFrames);

    const auto held = controller.recommend(
        frame(timestamp, true, 1, 1, kAccumulatedFrames));
    REQUIRE(held.mode == Controller::Mode::Held);
    REQUIRE(held.phase_us == 2000);
    REQUIRE(held.estimated_period_us == 5000);
}

void test_raw_qpc_cadence_ignores_per_frame_steady_mapping_jitter() {
    Controller controller(test_config());
    std::uint64_t steady_timestamp = 3'900'000'000;
    std::uint64_t qpc_timestamp = 39'000'000;
    constexpr std::uint64_t kSteadyDeltasNs[] = {
        4'100'000, 5'900'000, 4'350'000, 5'650'000,
        4'500'000, 5'500'000, 4'250'000, 5'750'000};
    bool saw_stable = false;
    for (const auto steady_delta : kSteadyDeltasNs) {
        const auto decision = controller.recommend(
            frame_qpc(steady_timestamp, qpc_timestamp));
        if (decision.cadence_stable) {
            saw_stable = true;
            REQUIRE(decision.estimated_period_us == 5000);
        }
        steady_timestamp += steady_delta;
        qpc_timestamp += 50'000;
    }
    REQUIRE(saw_stable);
    REQUIRE(controller.snapshot().cadence_stable);
    REQUIRE(controller.snapshot().estimated_period_us == 5000);
}

void test_robust_cadence_accepts_one_outlier_in_live_like_window() {
    Controller controller(test_config());
    std::uint64_t timestamp = 4'200'000'000;
    (void)controller.recommend(frame(timestamp));
    constexpr std::uint64_t kIntervalsNs[] = {
        4'900'000, 5'000'000, 5'200'000, 5'300'000,
        5'400'000, 5'500'000, 5'800'000, 6'100'000};
    bool saw_stable = false;
    for (const auto interval : kIntervalsNs) {
        timestamp += interval;
        const auto decision = controller.recommend(frame(timestamp));
        saw_stable = saw_stable || decision.cadence_stable;
    }
    REQUIRE(saw_stable);
    REQUIRE(controller.snapshot().cadence_stable);
    REQUIRE(controller.snapshot().estimated_period_us >= 5200);
    REQUIRE(controller.snapshot().estimated_period_us <= 5500);
}

void test_transient_unstable_window_preserves_candidate_progress() {
    Controller controller(test_config());
    std::uint64_t timestamp = 4'500'000'000;
    warm_cadence(controller, &timestamp);
    auto first = controller.recommend(frame(timestamp));
    REQUIRE(first.mode == Controller::Mode::Exploring);
    controller.observe(observation(
        timestamp, first.phase_us, 0.0f, 2.0f));
    const auto before = controller.snapshot();
    REQUIRE(before.active_candidate_samples == 1);

    constexpr std::uint64_t kTransientIntervalsNs[] = {
        7'000'000, 5'000'000, 7'000'000, 5'000'000, 7'000'000};
    bool saw_unstable = false;
    for (const auto interval : kTransientIntervalsNs) {
        timestamp += interval;
        const auto decision = controller.recommend(frame(timestamp));
        saw_unstable = saw_unstable ||
            decision.reason == Controller::DecisionReason::UnstableCadence;
    }
    REQUIRE(saw_unstable);
    REQUIRE(controller.snapshot().adaptation_epoch == before.adaptation_epoch);
    REQUIRE(controller.snapshot().active_candidate_samples == 1);

    bool resumed = false;
    for (int i = 0; i < 6; ++i) {
        timestamp += 5'000'000;
        const auto decision = controller.recommend(frame(timestamp));
        resumed = resumed || decision.cadence_stable;
    }
    REQUIRE(resumed);
    REQUIRE(controller.snapshot().adaptation_epoch == before.adaptation_epoch);
    REQUIRE(controller.snapshot().active_candidate_samples == 1);
}

void test_unreached_nonzero_phase_does_not_advance_candidate() {
    Controller controller(test_config());
    std::uint64_t timestamp = 4'750'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    REQUIRE(controller.snapshot().active_candidate_index == 1);

    for (int i = 0; i < 3; ++i) {
        const auto decision = controller.recommend(frame(timestamp));
        REQUIRE(decision.phase_us == 1000);
        auto missed = observation(
            timestamp, decision.phase_us, 0.0f, 0.25f);
        missed.phase_target_reached = false;
        controller.observe(missed);
        timestamp += 5'000'000;
    }
    REQUIRE(controller.snapshot().active_candidate_index == 1);
    REQUIRE(controller.snapshot().active_candidate_samples == 0);
}

void test_phase_schedule_wraps_only_adaptive_nonzero_targets() {
    constexpr std::uint64_t kSourceNs = 1'000'000'000;
    constexpr std::uint64_t kNowNs = kSourceNs + 3'200'000;
    constexpr std::uint64_t kMaxWaitNs = 5'000'000;

    const auto same_cycle = vision_native::schedule_cuda_submit_phase(
        kSourceNs, kNowNs, 4000, 5000, true, kMaxWaitNs);
    REQUIRE(same_cycle.should_wait);
    REQUIRE(!same_cycle.cycle_wrapped);
    REQUIRE(same_cycle.deadline_ns == kSourceNs + 4'000'000);
    REQUIRE(same_cycle.wait_ns == 800'000);

    const auto wrapped = vision_native::schedule_cuda_submit_phase(
        kSourceNs, kNowNs, 1000, 5000, true, kMaxWaitNs);
    REQUIRE(wrapped.should_wait);
    REQUIRE(wrapped.cycle_wrapped);
    REQUIRE(wrapped.deadline_ns == kSourceNs + 6'000'000);
    REQUIRE(wrapped.wait_ns == 2'800'000);

    const auto fixed_missed = vision_native::schedule_cuda_submit_phase(
        kSourceNs, kNowNs, 1000, 5000, false, kMaxWaitNs);
    REQUIRE(!fixed_missed.should_wait);
    REQUIRE(!fixed_missed.cycle_wrapped);
    REQUIRE(fixed_missed.deadline_ns == kSourceNs + 1'000'000);

    const auto natural = vision_native::schedule_cuda_submit_phase(
        kSourceNs, kNowNs, 0, 5000, true, kMaxWaitNs);
    REQUIRE(!natural.should_wait);
    REQUIRE(!natural.cycle_wrapped);
    REQUIRE(natural.deadline_ns == 0);

    constexpr std::uint64_t kQpcFrequency = 10'000'000;
    constexpr std::uint64_t kSourceQpc = 10'000'000;
    constexpr std::uint64_t kNowQpc = kSourceQpc + 32'000;
    const auto wrapped_qpc =
        vision_native::schedule_cuda_submit_phase_ticks(
            kSourceQpc,
            kNowQpc,
            kQpcFrequency,
            1000,
            5000,
            true,
            kMaxWaitNs);
    REQUIRE(wrapped_qpc.should_wait);
    REQUIRE(wrapped_qpc.cycle_wrapped);
    REQUIRE(wrapped_qpc.deadline_tick == kSourceQpc + 60'000);
    REQUIRE(wrapped_qpc.wait_ticks == 28'000);
    REQUIRE(wrapped_qpc.wait_ns == 2'800'000);
}

void test_explicit_wait_outweighs_reduced_residual_and_keeps_zero() {
    Controller controller(test_config());
    std::uint64_t timestamp = 4'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 0.20f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 1.40f, 0.20f, 0.0f);
    complete_candidate_block(controller, &timestamp, 2000, 1.40f, 0.20f, 0.0f);
    const auto decision = controller.recommend(frame(timestamp));
    REQUIRE(decision.phase_us == 0);
    REQUIRE(decision.mode == Controller::Mode::Fallback);
}

void test_cadence_change_resets_and_reenters_exploration() {
    Controller controller(test_config());
    std::uint64_t timestamp = 5'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 0.40f);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Held);

    const std::uint64_t epoch_before = controller.snapshot().adaptation_epoch;
    bool saw_changed = false;
    for (int i = 0; i < 10; ++i) {
        timestamp += 8'000'000;
        const auto changed = controller.recommend(frame(timestamp));
        saw_changed = saw_changed ||
            changed.reason == Controller::DecisionReason::CadenceChanged;
    }
    REQUIRE(saw_changed);
    REQUIRE(controller.snapshot().adaptation_epoch == epoch_before + 1);
    REQUIRE(controller.snapshot().estimated_period_us == 8000);
}

void test_short_idle_freezes_without_reset() {
    Controller controller(test_config());
    std::uint64_t timestamp = 6'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 0.40f);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Held);
    const auto epoch_before = controller.snapshot().adaptation_epoch;
    const auto idle = controller.recommend(frame(timestamp, false));
    REQUIRE(idle.phase_us == 0);
    REQUIRE(idle.reason == Controller::DecisionReason::IdleFrozen);
    REQUIRE(controller.snapshot().adaptation_epoch == epoch_before);
    REQUIRE(controller.snapshot().held_phase_us == 2000);
    controller.observe(observation(
        timestamp, 0, 0.0f, 99.0f, 0.0f, 0.0f, false));
    REQUIRE(controller.snapshot().adaptation_epoch == epoch_before);
    timestamp += 5'000'000;
    const auto resumed = controller.recommend(frame(timestamp, true));
    REQUIRE(resumed.mode == Controller::Mode::Held);
    REQUIRE(resumed.phase_us == 2000);
    REQUIRE(controller.snapshot().adaptation_epoch == epoch_before);
}

void test_slow_idle_keepwarm_rebases_without_erasing_held_phase() {
    Controller controller(test_config());
    std::uint64_t timestamp = 6'250'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 0.40f);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Held);
    const auto epoch_before = controller.snapshot().adaptation_epoch;

    for (int i = 0; i < 4; ++i) {
        const auto idle = controller.recommend(
            frame(timestamp, false, 1, 1, 10));
        REQUIRE(idle.phase_us == 0);
        REQUIRE(idle.reason == Controller::DecisionReason::IdleFrozen);
        REQUIRE(controller.snapshot().adaptation_epoch == epoch_before);
        timestamp += 50'000'000;
    }

    // The next active acquire follows one 5 ms source period, not the full
    // 155 ms since the last active workload sample.
    timestamp -= 45'000'000;
    const auto resumed = controller.recommend(frame(timestamp, true, 1, 1, 1));
    REQUIRE(resumed.mode == Controller::Mode::Held);
    REQUIRE(resumed.phase_us == 2000);
    REQUIRE(resumed.estimated_period_us == 5000);
    REQUIRE(controller.snapshot().adaptation_epoch == epoch_before);
}

void test_long_idle_resets_and_reenters_exploration() {
    auto config = test_config();
    config.idle_reset_after_us = 15'000;
    Controller controller(config);
    std::uint64_t timestamp = 6'500'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 0.40f);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Held);
    const auto epoch_before = controller.snapshot().adaptation_epoch;

    for (int i = 0; i < 4; ++i) {
        const auto idle = controller.recommend(frame(timestamp, false));
        REQUIRE(idle.phase_us == 0);
        timestamp += 5'000'000;
    }
    REQUIRE(controller.snapshot().adaptation_epoch > epoch_before);
    REQUIRE(controller.snapshot().held_phase_us == 0);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Fallback);

    const auto resumed = controller.recommend(frame(timestamp, true));
    REQUIRE(resumed.phase_us == 0);
    REQUIRE(resumed.mode == Controller::Mode::Exploring);
    REQUIRE(resumed.reason == Controller::DecisionReason::Exploring);
}

void test_ties_prefer_natural_and_low_confidence_resets() {
    Controller controller(test_config());
    std::uint64_t timestamp = 7'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 1.0f);
    REQUIRE(controller.recommend(frame(timestamp)).phase_us == 0);

    Controller low_confidence(test_config());
    timestamp = 8'000'000'000;
    warm_cadence(low_confidence, &timestamp);
    auto decision = low_confidence.recommend(frame(timestamp));
    auto invalid = observation(timestamp, decision.phase_us, 0.0f, 1.0f);
    invalid.timing_confident = false;
    low_confidence.observe(invalid);
    timestamp += 5'000'000;
    decision = low_confidence.recommend(frame(timestamp));
    REQUIRE(decision.phase_us == 0);
    REQUIRE(decision.mode != Controller::Mode::Held);
}

void test_dynamic_period_guard_clips_candidate_grid() {
    Controller controller(test_config());
    std::uint64_t timestamp = 8'500'000'000;
    constexpr std::uint64_t kPeriodNs = 2'000'000;
    warm_cadence(controller, &timestamp, kPeriodNs);
    complete_candidate_block(
        controller, &timestamp, 0, 0.0f, 2.0f, 0.0f, 0.0f, true, 1, 1,
        kPeriodNs);
    complete_candidate_block(
        controller, &timestamp, 1000, 0.0f, 1.0f, 0.0f, 0.0f, true, 1, 1,
        kPeriodNs);
    const auto clipped = controller.recommend(frame(timestamp));
    REQUIRE(clipped.estimated_period_us == 2000);
    // The robust cadence window reserves both its 250 us minimum tolerance
    // and the configured 500 us next-frame guard.
    REQUIRE(clipped.phase_us == 1250);
    REQUIRE(clipped.phase_us <= 2000 - controller.config().next_frame_guard_us);
}

void test_backlog_penalty_is_bounded_and_not_sole_signal() {
    const auto config = Controller::default_config();
    auto no_backlog = observation(1, 0, 1.0f, 2.0f, 0.0f, 0.0f, true, 1, 1, 1);
    auto one_queued = no_backlog;
    one_queued.accumulated_frames = 1;
    auto backlog = no_backlog;
    backlog.accumulated_frames = 3;
    auto pathological = no_backlog;
    pathological.accumulated_frames = 10'000;
    const float baseline = Controller::objective_cost(no_backlog, config);
    REQUIRE(std::fabs(Controller::objective_cost(one_queued, config) - baseline) <
        1.0e-5f);
    REQUIRE(Controller::objective_cost(backlog, config) > baseline);
    REQUIRE(Controller::objective_cost(pathological, config) - baseline <=
        config.backlog_cap_ms + 1.0e-5f);
}

void test_same_cadence_reprobe_can_follow_migrated_optimum() {
    Controller controller(test_config());
    REQUIRE(controller.config().held_reprobe_after_blocks == 100);
    std::uint64_t timestamp = 10'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.0f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 0.40f);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Held);
    REQUIRE(controller.snapshot().held_phase_us == 2000);

    const auto held_samples = static_cast<int>(
        controller.config().held_reprobe_after_blocks) *
        controller.config().block_samples;
    for (int i = 0; i < held_samples; ++i) {
        const auto held = controller.recommend(frame(timestamp));
        REQUIRE(held.mode == Controller::Mode::Held);
        REQUIRE(held.phase_us == 2000);
        controller.observe(observation(timestamp, held.phase_us, 0.0f, 0.40f));
        timestamp += 5'000'000;
    }
    REQUIRE(controller.snapshot().mode == Controller::Mode::Fallback);
    const auto reprobe = controller.recommend(frame(timestamp));
    REQUIRE(reprobe.mode == Controller::Mode::Exploring);
    REQUIRE(reprobe.phase_us == 0);
    timestamp += 5'000'000;

    complete_candidate_block(controller, &timestamp, 0, 0.0f, 2.0f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 0.30f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 1.10f);
    const auto migrated = controller.recommend(frame(timestamp));
    REQUIRE(migrated.mode == Controller::Mode::Held);
    REQUIRE(migrated.phase_us == 1000);
}

void test_natural_fallback_is_periodically_reprobed() {
    auto config = test_config();
    config.held_reprobe_after_blocks = 2;
    Controller controller(config);
    std::uint64_t timestamp = 11'000'000'000;
    warm_cadence(controller, &timestamp);
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 0.30f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 1.20f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 1.50f);
    REQUIRE(controller.snapshot().mode == Controller::Mode::Fallback);
    const auto settled_epoch = controller.snapshot().adaptation_epoch;

    const int settled_samples = static_cast<int>(
        controller.config().held_reprobe_after_blocks) *
        controller.config().block_samples;
    for (int i = 0; i < settled_samples; ++i) {
        const auto natural = controller.recommend(frame(timestamp));
        REQUIRE(natural.mode == Controller::Mode::Fallback);
        REQUIRE(natural.phase_us == 0);
        controller.observe(observation(timestamp, 0, 0.0f, 0.30f));
        timestamp += 5'000'000;
    }
    REQUIRE(controller.snapshot().adaptation_epoch > settled_epoch);

    const auto reprobe = controller.recommend(frame(timestamp));
    REQUIRE(reprobe.mode == Controller::Mode::Exploring);
    REQUIRE(reprobe.phase_us == 0);
    timestamp += 5'000'000;
    complete_candidate_block(controller, &timestamp, 0, 0.0f, 1.40f);
    complete_candidate_block(controller, &timestamp, 1000, 0.0f, 0.30f);
    complete_candidate_block(controller, &timestamp, 2000, 0.0f, 1.00f);
    const auto migrated = controller.recommend(frame(timestamp));
    REQUIRE(migrated.mode == Controller::Mode::Held);
    REQUIRE(migrated.phase_us == 1000);
}

void test_regime_change_is_block_bounded_not_per_frame_oscillation() {
    Controller controller(test_config());
    std::uint64_t timestamp = 9'000'000'000;
    warm_cadence(controller, &timestamp);
    const auto first = controller.recommend(frame(timestamp));
    REQUIRE(first.phase_us == 0);
    for (int i = 0; i < 2; ++i) {
        controller.observe(observation(timestamp, first.phase_us, 0.0f, 2.0f));
        timestamp += 5'000'000;
        const auto same_block = controller.recommend(frame(timestamp));
        REQUIRE(same_block.phase_us == 0);
    }
    controller.observe(observation(timestamp, 0, 0.0f, 2.0f));
    timestamp += 5'000'000;
    const auto next_block = controller.recommend(frame(timestamp));
    REQUIRE(next_block.phase_us == 1000);

    controller.observe(observation(timestamp, next_block.phase_us, 0.0f, 1.0f));
    timestamp += 5'000'000;
    const auto changed = controller.recommend(frame(timestamp, true, 1, 2));
    REQUIRE(changed.phase_us == 0);
    REQUIRE(changed.mode != Controller::Mode::Held);
}

void test_objective_includes_explicit_wait_and_bounded_penalties() {
    const auto config = Controller::default_config();
    auto sample = observation(1, 0, 1.5f, 2.0f, 20.0f, 20.0f);
    const float cost = Controller::objective_cost(sample, config);
    REQUIRE(std::fabs(cost - (1.5f + 2.0f + 0.5f * 4.0f + 0.25f * 8.0f)) < 1.0e-5f);
}

}  // namespace

int main() {
    test_invalid_and_unstable_cadence_fall_back();
    test_stable_200_hz_phase_is_bounded_by_next_frame_guard();
    test_nonzero_optimum_is_learned_and_held();
    test_accumulated_frames_normalize_underlying_cadence();
    test_accumulated_frames_can_complete_exploration_and_hold();
    test_raw_qpc_cadence_ignores_per_frame_steady_mapping_jitter();
    test_robust_cadence_accepts_one_outlier_in_live_like_window();
    test_transient_unstable_window_preserves_candidate_progress();
    test_unreached_nonzero_phase_does_not_advance_candidate();
    test_phase_schedule_wraps_only_adaptive_nonzero_targets();
    test_explicit_wait_outweighs_reduced_residual_and_keeps_zero();
    test_cadence_change_resets_and_reenters_exploration();
    test_short_idle_freezes_without_reset();
    test_slow_idle_keepwarm_rebases_without_erasing_held_phase();
    test_long_idle_resets_and_reenters_exploration();
    test_ties_prefer_natural_and_low_confidence_resets();
    test_dynamic_period_guard_clips_candidate_grid();
    test_backlog_penalty_is_bounded_and_not_sole_signal();
    test_same_cadence_reprobe_can_follow_migrated_optimum();
    test_natural_fallback_is_periodically_reprobed();
    test_regime_change_is_block_bounded_not_per_frame_oscillation();
    test_objective_includes_explicit_wait_and_bounded_penalties();
    std::printf("AdaptiveCudaSubmitPhaseTests passed\n");
    return 0;
}
