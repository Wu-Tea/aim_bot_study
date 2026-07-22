#include "blind_window_benchmark.h"
#include "blind_window_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <stdexcept>

namespace controller_native::blind_window {
namespace {

constexpr int kPlantTickUs = 1'000;

struct CapturedFrame {
    int captured_at_us = 0;
    int result_at_us = 0;
    Vec2d error_px{};
};

struct PendingControl {
    int applies_at_us = 0;
    Vec2d final_stick{};
};

std::uint32_t xorshift32(std::uint32_t& state) noexcept {
    if (state == 0) state = 0x9e3779b9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int result_latency_us(
    const BlindTimingProfile& timing,
    std::uint32_t& jitter_state) noexcept {
    if (timing.jitter_seed == 0) return std::max(0, timing.result_latency_us);
    return 8'000 + static_cast<int>(xorshift32(jitter_state) % 24'001u);
}

int align_to_plant_tick(int timestamp_us) noexcept {
    return ((timestamp_us + kPlantTickUs - 1) / kPlantTickUs) * kPlantTickUs;
}

Vec2d apply_response(
    const std::array<double, 4>& matrix,
    Vec2d stick) noexcept {
    return {
        matrix[0] * stick.x + matrix[1] * stick.y,
        matrix[2] * stick.x + matrix[3] * stick.y,
    };
}

}  // namespace

const BlindTraceFrame& BlindWindowRun::frame_at_us(int timestamp_us) const {
    const auto found = std::find_if(
        trace.frames.begin(), trace.frames.end(),
        [=](const BlindTraceFrame& frame) {
            return frame.now_us == timestamp_us;
        });
    if (found == trace.frames.end()) {
        throw std::out_of_range("blind-window trace timestamp not found");
    }
    return *found;
}

BlindSchedule build_blind_schedule(
    const BlindTimingProfile& timing,
    int capture_at_us,
    int /*duration_us*/) noexcept {
    BlindSchedule schedule;
    schedule.capture_at_us = capture_at_us;
    schedule.event_at_us = capture_at_us +
        timing.vision_period_us * timing.event_phase_per_mille / 1'000;
    schedule.next_capture_at_us = align_to_plant_tick(
        capture_at_us + timing.vision_period_us);
    schedule.next_result_at_us = schedule.next_capture_at_us +
        timing.result_latency_us;
    return schedule;
}

BlindWindowRun run_blind_fixture(
    const BlindFixture& fixture,
    const BlindControllerStep& controller_step) {
    BlindWindowRun run;
    run.schedule = build_blind_schedule(
        fixture.timing, 0, fixture.duration_us);
    if (!controller_step || fixture.duration_us <= 0 ||
        fixture.timing.vision_period_us <= 0) {
        return run;
    }

    Vec2d true_error = fixture.initial_error_px;
    Vec2d target_velocity = fixture.target_velocity_px_per_second;
    Vec2d applied_stick{};
    Vec2d latest_observed_error{};
    std::int64_t latest_capture_us = -1;
    std::int64_t latest_result_us = -1;
    bool has_published_observation = false;
    bool warm_start_pending = false;
    std::deque<CapturedFrame> captured;
    std::deque<PendingControl> pending_controls;
    std::uint32_t jitter_state = fixture.timing.jitter_seed;
    int next_capture_due_us = 0;

    if (fixture.warm_start_observation) {
        latest_observed_error = fixture.initial_error_px;
        latest_capture_us = -fixture.timing.vision_period_us;
        latest_result_us = 0;
        has_published_observation = true;
        warm_start_pending = true;
        applied_stick = fixture.preloaded_final_stick;
        for (int applies_at_us = 0;
             applies_at_us < fixture.timing.response_delay_us;
             applies_at_us += 1'000) {
            pending_controls.push_back({
                applies_at_us, fixture.preloaded_final_stick});
        }
    }

    constexpr int kTickUs = kPlantTickUs;
    constexpr double kTickSeconds = 0.001;
    run.trace.frames.reserve(
        static_cast<std::size_t>(fixture.duration_us / kTickUs + 1));

    for (int now_us = 0; now_us < fixture.duration_us; now_us += kTickUs) {
        while (!pending_controls.empty() &&
               pending_controls.front().applies_at_us <= now_us) {
            applied_stick = pending_controls.front().final_stick;
            pending_controls.pop_front();
        }

        const Vec2d reticle_velocity = apply_response(
            fixture.right_response_px_per_stick_second, applied_stick);
        true_error.x += (target_velocity.x - reticle_velocity.x) * kTickSeconds;
        true_error.y += (target_velocity.y - reticle_velocity.y) * kTickSeconds;
        target_velocity.x += fixture.target_acceleration_px_per_sec2.x *
            kTickSeconds;
        target_velocity.y += fixture.target_acceleration_px_per_sec2.y *
            kTickSeconds;

        if (now_us >= next_capture_due_us) {
            const int latency = result_latency_us(fixture.timing, jitter_state);
            captured.push_back({now_us, now_us + latency, true_error});
            do {
                next_capture_due_us += fixture.timing.vision_period_us;
            } while (next_capture_due_us <= now_us);
        }

        bool fresh_vision = false;
        while (!captured.empty() && captured.front().result_at_us <= now_us) {
            latest_capture_us = captured.front().captured_at_us;
            latest_result_us = captured.front().result_at_us;
            latest_observed_error = captured.front().error_px;
            captured.pop_front();
            fresh_vision = true;
            has_published_observation = true;
        }

        BlindControllerObservation observation;
        observation.now_us = now_us;
        observation.target_present = has_published_observation;
        observation.fresh_vision = fresh_vision || warm_start_pending;
        observation.captured_at_us = latest_capture_us;
        observation.result_at_us = latest_result_us;
        observation.observed_error_px = latest_observed_error;
        const BlindControllerOutput output = controller_step(observation);
        warm_start_pending = false;
        pending_controls.push_back({
            now_us + std::max(0, fixture.timing.response_delay_us),
            output.final_stick});

        Vec2d scheduled_pending{};
        for (const PendingControl& pending : pending_controls) {
            const Vec2d velocity = apply_response(
                fixture.right_response_px_per_stick_second,
                pending.final_stick);
            scheduled_pending.x += velocity.x * kTickSeconds;
            scheduled_pending.y += velocity.y * kTickSeconds;
        }

        BlindTraceFrame frame;
        frame.now_us = now_us;
        frame.true_error_px = true_error;
        frame.ai_stick = output.ai_stick;
        frame.final_stick = output.final_stick;
        frame.response_applied_reticle_px_per_sec = reticle_velocity;
        frame.scheduled_pending_reticle_motion_px = scheduled_pending;
        frame.max_controller_source_time_us = latest_capture_us;
        run.trace.frames.push_back(frame);
    }
    run.metrics = evaluate_blind_window(
        fixture, run.trace, run.schedule);
    return run;
}

}  // namespace controller_native::blind_window
