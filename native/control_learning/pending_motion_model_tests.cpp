#include "control_learning/pending_motion_model.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) throw std::runtime_error(message);
}

control_learning::DeliveredControlSample sample(
    std::uint64_t seq, std::uint64_t time_ns, bool delivered = true) {
    control_learning::DeliveredControlSample value;
    value.sample_seq = seq;
    value.applied_at_ns = time_ns;
    value.final_right = {0.5f, -0.25f};
    value.final_left = {0.1f, 0.2f};
    value.output_delivered = delivered;
    return value;
}
}

int main() {
    try {
        using namespace control_learning;
        ControlHistory<1024> history;
        for (std::uint64_t i = 1; i <= 40; ++i)
            require(history.push(sample(i, i * 10'000'000)), "history push failed");

        PendingMotionRequest request;
        request.previous_capture_ns = 100'000'000;
        request.current_capture_ns = 150'000'000;
        request.decision_ns = 154'000'000;
        request.delay_ms = 40.0f;
        request.right_response.values = {{{1000.0, 0.0}, {0.0, 800.0}}};
        request.left_response.values = {{{100.0, 0.0}, {0.0, 50.0}}};
        request.selected_delay_confidence = 0.8f;
        request.response_confidence = 0.7f;
        const auto pending = PendingMotionModel::estimate(request, history);
        require(pending.valid, "complete history must produce pending motion");
        near(pending.realized_px.x, 25.5, 1.0e-4, "realized x mismatch");
        near(pending.realized_px.y, -9.5, 1.0e-4, "realized y mismatch");
        // RED contract: the old implementation labels the in-flight interval
        // [current-delay,current] as scheduled.  Scheduled is the distinct
        // post-capture interval [current,decision].
        near(pending.scheduled_px.x, 2.04, 1.0e-4,
             "scheduled must cover only post-capture work");
        near(pending.scheduled_px.y, -0.76, 1.0e-4,
             "scheduled y must cover only post-capture work");
        near(pending.in_flight_px.x, 20.4, 1.0e-4,
             "in-flight x must cover the delay window");
        near(pending.in_flight_px.y, -7.6, 1.0e-4,
             "in-flight y must cover the delay window");
        near(pending.pending_total_px.x,
             22.44,
             1.0e-6, "pending total must exclude realized observation work");
        near(pending.pending_total_px.x,
             pending.in_flight_px.x + pending.scheduled_px.x,
             1.0e-12, "pending total x decomposition mismatch");
        near(pending.pending_total_px.y,
             pending.in_flight_px.y + pending.scheduled_px.y,
             1.0e-12, "pending total y decomposition mismatch");
        require(std::fabs(pending.pending_total_px.x - pending.realized_px.x) >
                    1.0e-3,
                "realized work must not enter pending total");
        near(pending.confidence, 0.7, 1.0e-6, "confidence must use weakest bound");

        auto fractional_request = request;
        fractional_request.previous_capture_ns = 105'500'000;
        fractional_request.current_capture_ns = 151'500'000;
        const auto fractional = PendingMotionModel::estimate(fractional_request, history);
        require(fractional.valid, "fractional boundaries must interpolate");
        near(fractional.realized_px.x, 23.46, 1.0e-3,
             "fractional realized interpolation mismatch");

        const auto repeated = PendingMotionModel::estimate(request, history);
        near(repeated.pending_total_px.x, pending.pending_total_px.x, 1.0e-12,
             "reconstruction must not accumulate drift");
        auto switched_delay = request;
        switched_delay.delay_ms = 55.0f;
        const auto switched = PendingMotionModel::estimate(switched_delay, history);
        require(switched.valid, "delay switch must reconstruct from immutable history");
        const auto restored = PendingMotionModel::estimate(request, history);
        near(restored.pending_total_px.x, pending.pending_total_px.x, 1.0e-12,
             "delay switching must not mutate pending state");

        auto decision_at_capture = request;
        decision_at_capture.decision_ns = decision_at_capture.current_capture_ns;
        const auto boundary = PendingMotionModel::estimate(
            decision_at_capture, history);
        require(boundary.valid,
                "zero-duration post-capture interval must remain valid");
        near(boundary.scheduled_px.x, 0.0, 1.0e-12,
             "decision-time sample must not create scheduled motion");
        near(boundary.pending_total_px.x, 20.4, 1.0e-4,
             "pending total at capture must contain only in-flight work");

        auto endpoint_history = std::make_unique<ControlHistory<1024>>();
        for (std::uint64_t i = 1; i <= 15; ++i)
            require(endpoint_history->push(sample(i, i * 10'000'000)),
                    "endpoint history push failed");
        auto endpoint_sample = sample(16, 160'000'000);
        endpoint_sample.final_right = {1.0f, 1.0f};
        require(endpoint_history->push(endpoint_sample),
                "endpoint output sample push failed");
        auto endpoint_request = request;
        endpoint_request.previous_capture_ns = 110'000'000;
        endpoint_request.current_capture_ns = 150'000'000;
        endpoint_request.decision_ns = 160'000'000;
        const auto endpoint = PendingMotionModel::estimate(
            endpoint_request, *endpoint_history);
        require(endpoint.valid, "endpoint sample fixture must remain valid");
        near(endpoint.scheduled_px.x, 5.1, 1.0e-4,
             "scheduled must use held output before decision endpoint");
        near(endpoint.scheduled_px.y, -1.9, 1.0e-4,
             "scheduled y must use held output before decision endpoint");
        near(endpoint.pending_total_px.x, 25.5, 1.0e-4,
             "decision endpoint sample must not add phantom x area");
        near(endpoint.pending_total_px.y, -9.5, 1.0e-4,
             "decision endpoint sample must not add phantom y area");

        auto invalid = request;
        invalid.identity_continuous = false;
        require(!PendingMotionModel::estimate(invalid, history).valid,
                "identity reset must invalidate pending motion");
        invalid = request;
        invalid.ads_epoch_continuous = false;
        require(!PendingMotionModel::estimate(invalid, history).valid,
                "ADS reset must invalidate pending motion");
        invalid = request;
        invalid.stable_coordinates_valid = false;
        require(!PendingMotionModel::estimate(invalid, history).valid,
                "invalid coordinates must invalidate pending motion");

        ControlHistory<1024> delivery_gap;
        for (std::uint64_t i = 1; i <= 40; ++i)
            require(delivery_gap.push(sample(i, i * 10'000'000, i != 9)),
                    "gap history push failed");
        require(!PendingMotionModel::estimate(request, delivery_gap).valid,
                "delivery gap must not be extrapolated");

        auto capture_gap = request;
        capture_gap.previous_capture_ns = 20'000'000;
        require(!PendingMotionModel::estimate(capture_gap, history).valid,
                "truncated capture interval must invalidate pending motion");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[PendingMotionModelTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
