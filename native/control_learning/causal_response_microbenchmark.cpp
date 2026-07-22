#include "control_learning/causal_online_response_learner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

namespace {
double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

pipeline_contract::CommittedCaptureObservation observation(
    std::uint64_t frame, std::uint64_t capture_ns) {
    pipeline_contract::CommittedCaptureObservation value;
    value.source_frame_id = frame;
    value.source_observation_id = frame;
    value.persistent_target_id = 1;
    value.viewport_source_frame_id = frame;
    value.captured_at_ns = capture_ns;
    value.result_at_ns = capture_ns + 2'000'000;
    value.stable_error_px = {
        static_cast<float>(20.0 * std::sin(frame * 0.13)),
        static_cast<float>(8.0 * std::cos(frame * 0.17))};
    value.stable_body_size_px = {35, 100};
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
}

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace control_learning;
    auto history = std::make_unique<ControlHistory<1024>>();
    std::vector<double> push_us;
    push_us.reserve(5000);
    for (std::uint64_t i = 1; i <= 5000; ++i) {
        DeliveredControlSample value;
        value.sample_seq = i;
        value.applied_at_ns = i * 5'000'000;
        value.final_right = {
            static_cast<float>(0.3 * std::sin(i * 0.19)),
            static_cast<float>(0.25 * std::cos(i * 0.23))};
        value.final_left = {0.1f * value.final_right.y,
                            -0.1f * value.final_right.x};
        value.output_delivered = true;
        const auto begin = Clock::now();
        history->push(value);
        const auto end = Clock::now();
        push_us.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }

    auto learner = std::make_unique<CausalOnlineResponseLearner>();
    std::vector<double> observe_us;
    observe_us.reserve(400);
    for (std::uint64_t frame = 1; frame <= 400; ++frame) {
        const std::uint64_t capture_ns = (21'000 + frame * 10) * 1'000'000;
        const auto value = observation(frame, capture_ns);
        const auto begin = Clock::now();
        learner->observe_vision(value, *history);
        const auto estimate = learner->estimate();
        const auto end = Clock::now();
        if (!estimate.finite) return 2;
        observe_us.push_back(
            std::chrono::duration<double, std::micro>(end - begin).count());
    }
    std::cout << std::fixed << std::setprecision(3)
        << "{\"history_push_us\":{\"p50\":" << percentile(push_us, 0.50)
        << ",\"p95\":" << percentile(push_us, 0.95)
        << ",\"p99\":" << percentile(push_us, 0.99)
        << "},\"vision_observe_estimate_us\":{\"p50\":"
        << percentile(observe_us, 0.50)
        << ",\"p95\":" << percentile(observe_us, 0.95)
        << ",\"p99\":" << percentile(observe_us, 0.99) << "}}\n";
    return 0;
}
