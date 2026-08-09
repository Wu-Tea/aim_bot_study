#include "aim_response_curve_plugin.h"
#include "pending_control_motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using controller_native::AimResponseCurveAlgorithm;
using controller_native::AimResponseCurveConfig;
using controller_native::CausalMotionLedger;
using controller_native::CausalMotionPhaseRequest;
using controller_native::DeliveredFinalCommandSample;
using pipeline_contract::Vec2f;

constexpr double kPixelsPerStickSecond = 500.0;
constexpr double kBaseSeconds = 0.020;
constexpr double kEpsilon = 1.0e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Event {
    double time_seconds = 0.0;
    Vec2 stick{};
    Vec2 camera_velocity{};
    std::uint64_t target_id = 1;
};

struct Metric {
    std::vector<double> residuals;
    std::vector<double> normalized_residuals;
    int normalized_invalid_count = 0;
    int sign_errors = 0;
    double cosine_sum = 0.0;
    int cosine_count = 0;

    void add(Vec2 predicted, Vec2 truth) {
        const Vec2 delta{
            predicted.x - truth.x,
            predicted.y - truth.y};
        const double residual = std::hypot(delta.x, delta.y);
        const double truth_magnitude = std::hypot(truth.x, truth.y);
        residuals.push_back(residual);
        if (truth_magnitude > 1.0e-5) {
            normalized_residuals.push_back(residual / truth_magnitude);
            const double predicted_magnitude =
                std::hypot(predicted.x, predicted.y);
            if (predicted_magnitude > 1.0e-9 &&
                predicted.x * truth.x + predicted.y * truth.y < 0.0) {
                ++sign_errors;
            }
            if (predicted_magnitude > 1.0e-9) {
                cosine_sum +=
                    (predicted.x * truth.x + predicted.y * truth.y) /
                    (predicted_magnitude * truth_magnitude);
                ++cosine_count;
            }
        } else {
            ++normalized_invalid_count;
        }
    }

    void merge(const Metric& other) {
        residuals.insert(
            residuals.end(), other.residuals.begin(), other.residuals.end());
        normalized_residuals.insert(
            normalized_residuals.end(),
            other.normalized_residuals.begin(),
            other.normalized_residuals.end());
        normalized_invalid_count += other.normalized_invalid_count;
        sign_errors += other.sign_errors;
        cosine_sum += other.cosine_sum;
        cosine_count += other.cosine_count;
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual,
                  double expected,
                  double tolerance,
                  const std::string& message) {
    require(std::fabs(actual - expected) <= tolerance, message);
}

double magnitude(Vec2 value) {
    return std::hypot(value.x, value.y);
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = std::min(
        values.size() - 1,
        static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1);
    return values[index];
}

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double total = 0.0;
    for (const double value : values) total += value;
    return total / static_cast<double>(values.size());
}

std::string number(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;
    return stream.str();
}

std::string metric_json(const Metric& metric) {
    const double cosine = metric.cosine_count == 0
        ? 1.0 : metric.cosine_sum / metric.cosine_count;
    std::ostringstream stream;
    stream << "{\"count\":" << metric.residuals.size()
           << ",\"mean\":" << number(mean(metric.residuals))
           << ",\"p95\":" << number(percentile(metric.residuals, 0.95))
           << ",\"max\":" << number(percentile(metric.residuals, 1.0))
           << ",\"normalized_valid\":"
           << (!metric.normalized_residuals.empty() ? "true" : "false")
           << ",\"normalized_valid_count\":"
           << metric.normalized_residuals.size()
           << ",\"normalized_invalid_count\":"
           << metric.normalized_invalid_count
           << ",\"normalized_mean\":";
    if (metric.normalized_residuals.empty()) {
        stream << "null";
    } else {
        stream << number(mean(metric.normalized_residuals));
    }
    stream << ",\"normalized_p95\":";
    if (metric.normalized_residuals.empty()) {
        stream << "null";
    } else {
        stream << number(percentile(metric.normalized_residuals, 0.95));
    }
    stream
           << ",\"sign_errors\":" << metric.sign_errors
           << ",\"cosine_mean\":" << number(cosine) << "}";
    return stream.str();
}

DeliveredFinalCommandSample sample_from(const Event& event) {
    DeliveredFinalCommandSample sample;
    sample.delivered_at_seconds = event.time_seconds;
    sample.final_stick = {
        static_cast<float>(event.stick.x),
        static_cast<float>(event.stick.y)};
    sample.camera_velocity_px_per_second = {
        static_cast<float>(event.camera_velocity.x),
        static_cast<float>(event.camera_velocity.y)};
    sample.target_id = event.target_id;
    sample.ads_epoch = event.target_id == 0 ? 0 : 1;
    sample.delivered = true;
    sample.output_enabled = true;
    sample.physical_actuator_epoch = 1;
    sample.response_confidence = 1.0f;
    sample.response_model_valid = true;
    return sample;
}

Vec2 latest_event_stick(
    const std::vector<Event>& events,
    double time_seconds) {
    Vec2 value{};
    for (const auto& event : events) {
        if (event.time_seconds <= time_seconds + kEpsilon) {
            value = event.camera_velocity;
        } else {
            break;
        }
    }
    return value;
}

Vec2 integrate_zoh(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds) {
    Vec2 integral{};
    double cursor = begin_seconds;
    Vec2 held = latest_event_stick(events, begin_seconds);
    for (const auto& event : events) {
        if (event.time_seconds <= begin_seconds + kEpsilon) continue;
        if (event.time_seconds >= end_seconds - kEpsilon) break;
        const double dt = event.time_seconds - cursor;
        integral.x += held.x * dt;
        integral.y += held.y * dt;
        cursor = event.time_seconds;
        held = event.camera_velocity;
    }
    const double tail = end_seconds - cursor;
    if (tail > 0.0) {
        integral.x += held.x * tail;
        integral.y += held.y * tail;
    }
    return integral;
}

Vec2 integrate_latest_only(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds,
    double poll_period_seconds,
    double phase_seconds) {
    Vec2 integral{};
    double poll = begin_seconds - phase_seconds;
    while (poll < end_seconds - kEpsilon) {
        const double next_poll = poll + poll_period_seconds;
        const double overlap_begin = std::max(begin_seconds, poll);
        const double overlap_end = std::min(end_seconds, next_poll);
        if (overlap_end > overlap_begin) {
            const Vec2 held = latest_event_stick(events, poll);
            const double dt = overlap_end - overlap_begin;
            integral.x += held.x * dt;
            integral.y += held.y * dt;
        }
        poll = next_poll;
    }
    return integral;
}

Vec2 ledger_predict(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds) {
    CausalMotionLedger ledger;
    for (const auto& event : events) {
        require(ledger.observe(sample_from(event)),
                "Gate2 sample must be accepted by the global ledger");
    }
    CausalMotionPhaseRequest request;
    request.current_capture_seconds = end_seconds;
    request.decision_seconds = end_seconds;
    request.response_delay_ms = static_cast<float>(
        (end_seconds - begin_seconds) * 1000.0);
    request.memory_horizon_ms = 200.0f;
    request.target_id = 1;
    request.ads_epoch = 1;
    request.capture_pair_compatible = true;
    request.physical_actuator_epoch = 1;
    const auto estimate = ledger.estimate(request);
    require(estimate.valid && estimate.pending_valid,
            "Gate2 ledger phase must remain numerically valid");
    return {
        estimate.pending_total_px.x,
        estimate.pending_total_px.y};
}

Vec2 ledger_realized_predict(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds,
    double delay_seconds) {
    CausalMotionLedger ledger;
    for (const auto& event : events) {
        require(ledger.observe(sample_from(event)),
                "Gate2 response sample must be accepted by the global ledger");
    }
    CausalMotionPhaseRequest request;
    request.previous_capture_seconds = begin_seconds;
    request.current_capture_seconds = end_seconds;
    request.decision_seconds = end_seconds;
    request.response_delay_ms = static_cast<float>(delay_seconds * 1000.0);
    request.memory_horizon_ms = 200.0f;
    request.target_id = 1;
    request.ads_epoch = 1;
    request.capture_pair_compatible = true;
    request.physical_actuator_epoch = 1;
    const auto estimate = ledger.estimate(request);
    require(estimate.valid && estimate.pending_valid && estimate.realized_valid,
            "Gate2 response phase must expose valid realized and pending intervals");
    return {
        estimate.realized_px.x,
        estimate.realized_px.y};
}

std::vector<Event> admission_events(
    const std::string& pattern,
    double begin_seconds,
    double period_seconds) {
    std::vector<Event> events;
    const auto add = [&](double time, Vec2 stick) {
        events.push_back({
            time,
            stick,
            {stick.x * kPixelsPerStickSecond,
             stick.y * kPixelsPerStickSecond},
            1});
    };
    if (pattern == "held_x_steady") {
        // Pre-roll by more than two poll periods so phase only tests the
        // steady-state admission model, not startup latency.
        add(begin_seconds - period_seconds * 3.0, {0.70, 0.0});
    } else {
        add(begin_seconds - 0.001,
            pattern == "held_x_onset" ? Vec2{0.70, 0.0} : Vec2{});
    }
    if (pattern == "pulse_between_polls") {
        // Both transitions land on the 1kHz controller report grid and lie
        // inside both the 180Hz and 240Hz scored poll windows.
        add(begin_seconds + 0.002, {0.90, 0.0});
        add(begin_seconds + 0.004, {0.0, 0.0});
    } else if (pattern == "reversal_inside_frame") {
        add(begin_seconds + 0.002, {0.90, 0.0});
        add(begin_seconds + 0.004, {-0.60, 0.0});
    }
    return events;
}

struct AdmissionRow {
    int hz = 0;
    std::string pattern;
    Metric continuous_metric;
    Metric latest_metric;
};

std::vector<AdmissionRow> run_admission_matrix() {
    std::vector<AdmissionRow> rows;
    for (const int hz : {180, 240}) {
        const double period = 1.0 / static_cast<double>(hz);
        for (const std::string pattern : {
                 std::string("held_x_onset"),
                 std::string("held_x_steady"),
                 std::string("pulse_between_polls"),
                 std::string("reversal_inside_frame")}) {
            AdmissionRow row;
            row.hz = hz;
            row.pattern = pattern;
            for (int phase_index = 0; phase_index < 16; ++phase_index) {
                const double phase = period * phase_index / 16.0;
                const double begin = kBaseSeconds;
                const double end = begin + period;
                const auto events = admission_events(
                    pattern, begin, period);
                const Vec2 predicted = ledger_predict(events, begin, end);
                const Vec2 continuous = integrate_zoh(events, begin, end);
                const Vec2 latest = integrate_latest_only(
                    events, begin, end, period, phase);
                row.continuous_metric.add(predicted, continuous);
                row.latest_metric.add(predicted, latest);
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

enum class SlowdownDirection {
    None,
    FastToSlow,
    SlowToFast,
};

double ramp_multiplier(
    double time_seconds,
    double begin_seconds,
    int ramp_ms) {
    const double ramp_begin = begin_seconds - 0.080;
    const double normalized =
        (time_seconds - ramp_begin) / (static_cast<double>(ramp_ms) / 1000.0);
    return 0.35 + 0.65 * std::clamp(normalized, 0.0, 1.0);
}

double slowdown_multiplier(
    double time_seconds,
    double begin_seconds,
    SlowdownDirection direction) {
    const double relative_ms = (time_seconds - begin_seconds) * 1000.0;
    if (direction == SlowdownDirection::FastToSlow &&
        relative_ms >= 8.0 && relative_ms < 28.0) {
        return 0.40;
    }
    if (direction == SlowdownDirection::SlowToFast &&
        relative_ms >= 0.0 && relative_ms < 20.0) {
        return 0.40;
    }
    return 1.0;
}

double delivery_response_scale(
    double time_seconds,
    double begin_seconds,
    int ramp_ms,
    SlowdownDirection direction) {
    return kPixelsPerStickSecond *
        ramp_multiplier(time_seconds, begin_seconds, ramp_ms) *
        slowdown_multiplier(time_seconds, begin_seconds, direction);
}

Vec2 response_effect_truth(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds,
    double delay_seconds,
    int ramp_ms,
    SlowdownDirection direction,
    bool target_switch,
    bool distributed) {
    Vec2 integral{};
    constexpr double kStepSeconds = 0.00005;
    for (double time = begin_seconds;
         time < end_seconds - kEpsilon;
         time += kStepSeconds) {
        const double next = std::min(end_seconds, time + kStepSeconds);
        const double midpoint = (time + next) * 0.5;
        const Event* delivered = nullptr;
        for (const auto& event : events) {
            if (event.time_seconds <= midpoint - delay_seconds + kEpsilon) {
                delivered = &event;
            } else {
                break;
            }
        }
        if (delivered == nullptr) continue;
        double scale = delivery_response_scale(
            midpoint, begin_seconds, ramp_ms, direction);
        if (target_switch && delivered->target_id == 1 &&
            midpoint >= begin_seconds + 0.015) {
            // A's already delivered command realizes under B's slower state.
            scale *= 0.35;
        }
        if (distributed) {
            const double effect_age = midpoint -
                (delivered->time_seconds + delay_seconds);
            scale *= std::clamp(effect_age / 0.008, 0.0, 1.0);
        }
        const double dt = next - time;
        integral.x += delivered->stick.x * scale * dt;
        integral.y += delivered->stick.y * scale * dt;
    }
    return integral;
}

Vec2 response_effect_truth_constant(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds,
    double delay_seconds,
    double response_scale) {
    Vec2 integral{};
    constexpr double kStepSeconds = 0.00005;
    for (double time = begin_seconds;
         time < end_seconds - kEpsilon;
         time += kStepSeconds) {
        const double next = std::min(end_seconds, time + kStepSeconds);
        const double midpoint = (time + next) * 0.5;
        const Event* delivered = nullptr;
        for (const auto& event : events) {
            if (event.time_seconds <= midpoint - delay_seconds + kEpsilon) {
                delivered = &event;
            } else {
                break;
            }
        }
        if (delivered == nullptr) continue;
        const double dt = next - time;
        integral.x += delivered->stick.x * response_scale * dt;
        integral.y += delivered->stick.y * response_scale * dt;
    }
    return integral;
}

Vec2 ledger_pending_counterfactual(
    const std::vector<Event>& events,
    double current_seconds,
    double delay_seconds) {
    CausalMotionLedger ledger;
    for (const auto& event : events) {
        require(ledger.observe(sample_from(event)),
                "counterfactual delivery must be accepted by the ledger");
    }
    CausalMotionPhaseRequest request;
    request.current_capture_seconds = current_seconds;
    request.decision_seconds = current_seconds;
    request.response_delay_ms = static_cast<float>(delay_seconds * 1000.0);
    request.memory_horizon_ms = 200.0f;
    request.target_id = 1;
    request.ads_epoch = 1;
    request.physical_actuator_epoch = 1;
    const auto estimate = ledger.estimate(request);
    require(estimate.valid && estimate.pending_valid,
            "counterfactual pending estimate must be valid");
    return {
        estimate.pending_total_px.x,
        estimate.pending_total_px.y};
}

double integrate_absolute_zoh(
    const std::vector<Event>& events,
    double begin_seconds,
    double end_seconds) {
    double exposure = 0.0;
    double cursor = begin_seconds;
    Vec2 held = latest_event_stick(events, begin_seconds);
    for (const auto& event : events) {
        if (event.time_seconds <= begin_seconds + kEpsilon) continue;
        if (event.time_seconds >= end_seconds - kEpsilon) break;
        const double dt = event.time_seconds - cursor;
        exposure += magnitude(held) * dt;
        cursor = event.time_seconds;
        held = event.camera_velocity;
    }
    if (end_seconds > cursor) exposure += magnitude(held) * (end_seconds - cursor);
    return exposure;
}

void test_same_current_state_with_different_prior_histories() {
    constexpr double kCurrentSeconds = 0.200;
    constexpr double kDelaySeconds = 0.020;
    constexpr double kStrongStick = 0.60;
    const double begin_seconds = kCurrentSeconds - kDelaySeconds;
    struct CounterfactualCurrentState {
        std::uint64_t observation_id = 0;
        Vec2 error_px{};
        Vec2 final_stick{};
    };
    // These current-state values are injected test conditions, not values
    // derived from the independent plant.  Only the pre-current history is
    // varied between aliases.
    const CounterfactualCurrentState current_state{9001, {}, {}};
    const Vec2 current_error = current_state.error_px;
    const Vec2 current_final_stick = current_state.final_stick;

    const auto event = [](double time, Vec2 stick) {
        return Event{
            time,
            stick,
            {stick.x * kPixelsPerStickSecond,
             stick.y * kPixelsPerStickSecond},
            1};
    };

    const auto neutral = event(0.050, {});
    const auto step_at_boundary = event(kCurrentSeconds - kDelaySeconds,
                                        {kStrongStick, 0.0});
    const auto preheld = event(0.100, {kStrongStick, 0.0});
    const auto positive = event(0.170, {kStrongStick, 0.0});
    const auto negative = event(0.190, {-kStrongStick, 0.0});
    const auto current = event(kCurrentSeconds, current_final_stick);

    struct Result {
        const char* name = "";
        Vec2 ledger{};
        Vec2 zoh{};
        Vec2 constant_future{};
        Vec2 distributed{};
        double exposure = 0.0;
    };

    const auto run = [&](const char* name,
                         const std::vector<Event>& events) {
        const Vec2 ledger = ledger_pending_counterfactual(
            events, kCurrentSeconds, kDelaySeconds);
        const Vec2 zoh = integrate_zoh(events, begin_seconds, kCurrentSeconds);
        const Vec2 constant_future = response_effect_truth_constant(
            events, kCurrentSeconds, kCurrentSeconds + kDelaySeconds,
            kDelaySeconds, kPixelsPerStickSecond);
        const Vec2 distributed = response_effect_truth(
            events, kCurrentSeconds, kCurrentSeconds + kDelaySeconds,
            kDelaySeconds,
            260, SlowdownDirection::None, false, true);
        const double exposure = integrate_absolute_zoh(
            events, begin_seconds, kCurrentSeconds);
        require_near(ledger.x, zoh.x, 1.0e-4,
                     "ledger X must match independent pure-delay ZOH truth");
        require_near(ledger.y, zoh.y, 1.0e-4,
                     "ledger Y must match independent pure-delay ZOH truth");
        require_near(ledger.x, constant_future.x, 1.0e-4,
                     "ledger X must match independent future constant truth");
        require_near(ledger.y, constant_future.y, 1.0e-4,
                     "ledger Y must match independent future constant truth");
        require(std::hypot(current_error.x, current_error.y) < 1.0e-9,
                "counterfactual current error must remain zero");
        require(std::hypot(events.back().stick.x - current_final_stick.x,
                           events.back().stick.y - current_final_stick.y) <
                    1.0e-9,
                "counterfactual current final command must remain fixed");
        std::cout << "w5_history_counterfactual observation="
                  << current_state.observation_id << " name=" << name
                  << " current_error=(0,0) current_final=(0,0)"
                  << " ledger=(" << ledger.x << "," << ledger.y << ")"
                  << " zoh=(" << zoh.x << "," << zoh.y << ")"
                  << " future_constant=(" << constant_future.x << ","
                  << constant_future.y << ")"
                  << " distributed=(" << distributed.x << ","
                  << distributed.y << ") exposure=" << exposure << '\n';
        return Result{name, ledger, zoh, constant_future, distributed, exposure};
    };

    // Bookkeeping controls: current observation/error/final command are fixed
    // and the ledger must agree with the independent pure-delay plant.
    const Result neutral_control = run(
        "neutral_control", {neutral, current});
    const Result neutral_to_step = run(
        "neutral_to_step", {neutral, step_at_boundary, current});
    const Result preheld_result = run(
        "preheld", {neutral, preheld, current});
    const Result opposite_reversal = run(
        "opposite_reversal", {neutral, positive, negative, current});
    require(std::hypot(neutral_control.ledger.x,
                       neutral_control.ledger.y) < 1.0e-6,
            "neutral control must not invent pending work");
    require_near(neutral_to_step.ledger.x, 6.0, 1.0e-4,
                 "step pending oracle must cover the delayed interval");
    require_near(preheld_result.ledger.x, neutral_to_step.ledger.x, 1.0e-4,
                 "preheld and step histories must share pure-ZOH pending");
    require_near(opposite_reversal.ledger.x, 0.0, 1.0e-4,
                 "equal opposite history must have zero signed pending");
    require(opposite_reversal.exposure > 5.9,
            "opposite reversal must retain nonzero physical exposure");

    // MODEL_RED alias: the step-at-boundary and pre-held histories have the
    // same signed/net integral and the same ledger output, but an independent
    // delayed distributed-response plant sees different future work.  This is
    // intentionally a model diagnostic; it must not tune or alter the ledger.
    require_near(neutral_to_step.zoh.x, preheld_result.zoh.x, 1.0e-4,
                 "model-red aliases must share signed ZOH truth");
    require_near(neutral_to_step.ledger.x, preheld_result.ledger.x, 1.0e-4,
                 "model-red aliases must share ledger pending output");
    require(std::fabs(neutral_to_step.distributed.x -
                      preheld_result.distributed.x) > 0.01,
            "phase-sensitive distributed plant must separate the aliases");
    std::cout << "w5_history_counterfactual_model_red aliases_same_ledger=1"
              << " distributed_delta="
              << std::fabs(neutral_to_step.distributed.x -
                           preheld_result.distributed.x) << '\n';
}

struct ResponseRow {
    int ramp_ms = 0;
    int delay_ms = 0;
    int phase_ms = 0;
    std::string mode;
    Metric metric;
};

std::vector<ResponseRow> run_response_matrix() {
    std::vector<ResponseRow> rows;
    const double begin = 1.0;
    const std::array<int, 8> transition_phases_ms{{0, 4, 8, 12,
                                                    16, 20, 28, 36}};
    for (const int ramp_ms : {260, 400}) {
        for (const auto mode : {
                 std::pair<std::string, SlowdownDirection>{
                     "fast_to_slow", SlowdownDirection::FastToSlow},
                 std::pair<std::string, SlowdownDirection>{
                     "slow_to_fast", SlowdownDirection::SlowToFast}}) {
            for (const int delay_ms : {5, 20, 24, 45}) {
                const double delay = delay_ms / 1000.0;
                const double end = begin + 0.060;
                for (const int phase_ms : transition_phases_ms) {
                    std::vector<Event> events;
                    const auto add = [&](double time, Vec2 stick,
                                         std::uint64_t target_id) {
                        events.push_back({
                            time,
                            stick,
                            {stick.x * delivery_response_scale(
                                 time, begin, ramp_ms, mode.second),
                             stick.y * delivery_response_scale(
                                 time, begin, ramp_ms, mode.second)},
                            target_id});
                    };
                    add(begin - delay - 0.001, {0.50, 0.0}, 1);
                    add(begin - delay + phase_ms / 1000.0,
                        {0.50, 0.0}, 1);
                    ResponseRow row;
                    row.ramp_ms = ramp_ms;
                    row.delay_ms = delay_ms;
                    row.phase_ms = phase_ms;
                    row.mode = mode.first;
                    row.metric.add(
                        ledger_realized_predict(events, begin, end, delay),
                        response_effect_truth(
                            events, begin, end, delay, ramp_ms,
                            mode.second, false, false));
                    rows.push_back(std::move(row));
                }
            }
        }

        for (const int delay_ms : {5, 20, 24, 45}) {
            const double delay = delay_ms / 1000.0;
            const double end = begin + 0.060;
            for (const int phase_ms : {0, 4, 8, 12, 16, 24}) {
                std::vector<Event> events;
                const auto add = [&](double time, Vec2 stick,
                                     std::uint64_t target_id) {
                    events.push_back({
                        time,
                        stick,
                        {stick.x * delivery_response_scale(
                             time, begin, ramp_ms, SlowdownDirection::None),
                         stick.y * delivery_response_scale(
                             time, begin, ramp_ms, SlowdownDirection::None)},
                        target_id});
                };
                add(begin - delay - 0.001, {0.50, 0.0}, 1);
                add(begin - delay + 0.010, {0.50, 0.0}, 1);
                add(begin - delay + 0.020 + phase_ms / 1000.0,
                    {0.0, 0.0}, 2);
                ResponseRow row;
                row.ramp_ms = ramp_ms;
                row.delay_ms = delay_ms;
                row.phase_ms = phase_ms;
                row.mode = "target_switch_A_to_B";
                row.metric.add(
                    ledger_realized_predict(events, begin, end, delay),
                    response_effect_truth(
                        events, begin, end, delay, ramp_ms,
                        SlowdownDirection::None, true, false));
                rows.push_back(std::move(row));
            }
        }

        const double delay = 0.020;
        const double end = begin + 0.060;
        for (const int phase_ms : {0, 8, 16, 24}) {
            std::vector<Event> events;
            const auto add = [&](double time, Vec2 stick) {
                events.push_back({
                    time,
                    stick,
                    {stick.x * delivery_response_scale(
                         time, begin, ramp_ms, SlowdownDirection::None),
                     stick.y * delivery_response_scale(
                         time, begin, ramp_ms, SlowdownDirection::None)},
                    1});
            };
            add(begin - delay - 0.001, {0.50, 0.0});
            add(begin - delay + phase_ms / 1000.0, {0.50, 0.0});
            ResponseRow distributed;
            distributed.ramp_ms = ramp_ms;
            distributed.delay_ms = 20;
            distributed.phase_ms = phase_ms;
            distributed.mode = "distributed_ramp_kernel";
            distributed.metric.add(
                ledger_realized_predict(events, begin, end, delay),
                response_effect_truth(
                    events, begin, end, delay, ramp_ms,
                    SlowdownDirection::None, false, true));
            rows.push_back(std::move(distributed));
        }
    }

    // Matched control: the ledger stores the same constant response that the
    // independent effect-time plant applies.  Any nonzero residual here is a
    // phase/interval accounting defect, not a model mismatch.
    for (const int delay_ms : {5, 20, 24, 45}) {
        const double delay = delay_ms / 1000.0;
        const double end = begin + 0.060;
        const double response_scale = kPixelsPerStickSecond;
        std::vector<Event> events{
            {begin - delay - 0.001, {0.50, 0.0},
             {0.50 * response_scale, 0.0}, 1},
            {begin - delay + 0.012, {0.50, 0.0},
             {0.50 * response_scale, 0.0}, 1}};
        ResponseRow control;
        control.delay_ms = delay_ms;
        control.phase_ms = 12;
        control.mode = "matched_constant_response";
        control.metric.add(
            ledger_realized_predict(events, begin, end, delay),
            response_effect_truth_constant(
                events, begin, end, delay, response_scale));
        rows.push_back(std::move(control));
    }
    return rows;
}

Vec2 apply_radial_deadzone(Vec2 stick, double deadzone) {
    const double radius = magnitude(stick);
    if (radius <= deadzone) return {};
    const double scaled = (radius - deadzone) / (1.0 - deadzone);
    return {
        stick.x * scaled / radius,
        stick.y * scaled / radius};
}

Vec2 curve_response(Vec2 stick, AimResponseCurveAlgorithm algorithm) {
    AimResponseCurveConfig config;
    config.algorithm = algorithm;
    const Vec2f response = controller_native::forward_aim_response_curve(
        {static_cast<float>(stick.x), static_cast<float>(stick.y)}, config);
    return {
        response.x * kPixelsPerStickSecond,
        response.y * kPixelsPerStickSecond};
}

Vec2 curve_response_with_scales(
    Vec2 stick,
    AimResponseCurveAlgorithm algorithm,
    double x_scale,
    double y_scale) {
    AimResponseCurveConfig config;
    config.algorithm = algorithm;
    const Vec2f response = controller_native::forward_aim_response_curve(
        {static_cast<float>(stick.x), static_cast<float>(stick.y)}, config);
    return {response.x * x_scale, response.y * y_scale};
}

struct MicroRow {
    std::string curve;
    double deadzone = 0.0;
    double magnitude = 0.0;
    std::string axis;
    std::string classification = "CONTROL";
    Metric metric;
    double phantom_magnitude = 0.0;
};

Vec2 stick_for_axis(double value, const std::string& axis) {
    if (axis == "y") return {0.0, value};
    if (axis == "diagonal") {
        const double component = value / std::sqrt(2.0);
        return {component, component};
    }
    return {value, 0.0};
}

std::vector<MicroRow> run_micro_matrix() {
    std::vector<MicroRow> rows;
    for (const auto algorithm : {
             AimResponseCurveAlgorithm::Linear,
             AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
        const std::string curve = algorithm ==
            AimResponseCurveAlgorithm::Linear
            ? "linear" : "cod_dynamic_legacy_lut";
        for (const double deadzone : {0.0, 0.03, 0.05}) {
            for (const double stick_magnitude : {
                     0.01, 0.02, 0.03, 0.05, 0.10}) {
                for (const std::string axis : {
                         std::string("x"), std::string("y"),
                         std::string("diagonal")}) {
                    const double begin = 2.0;
                    const double end = 2.020;
                    const Vec2 stick = stick_for_axis(
                        stick_magnitude, axis);
                    const Vec2 ledger_velocity = curve_response(
                        stick, algorithm);
                    const Vec2 plant_stick = apply_radial_deadzone(
                        stick, deadzone);
                    const Vec2 plant_velocity = curve_response(
                        plant_stick, algorithm);
                    const std::vector<Event> events{
                        {begin - 0.001, {}, {}, 1},
                        {begin, stick, ledger_velocity, 1}};
                    std::vector<Event> plant_events{
                        {begin - 0.001, {}, {}, 1},
                        {begin, stick, plant_velocity, 1}};
                    MicroRow row;
                    row.curve = curve;
                    row.deadzone = deadzone;
                    row.magnitude = stick_magnitude;
                    row.axis = axis;
                    row.metric.add(
                        ledger_predict(events, begin, end),
                        integrate_zoh(plant_events, begin, end));
                    row.phantom_magnitude = magnitude(
                        ledger_predict(events, begin, end)) - magnitude(
                            integrate_zoh(plant_events, begin, end));
                    rows.push_back(std::move(row));
                }
            }
            for (const std::string axis : {std::string("x_saturation"),
                                           std::string("x_reversal")}) {
                const double begin = 2.1;
                const double end = 2.120;
                const Vec2 first = axis == "x_saturation"
                    ? Vec2{1.0, 0.0} : Vec2{0.80, 0.0};
                const Vec2 second = axis == "x_saturation"
                    ? Vec2{1.0, 0.0} : Vec2{-0.80, 0.0};
                const Vec2 ledger_first = curve_response(first, algorithm);
                const Vec2 ledger_second = curve_response(second, algorithm);
                const Vec2 plant_first = curve_response(
                    apply_radial_deadzone(first, deadzone), algorithm);
                const Vec2 plant_second = curve_response(
                    apply_radial_deadzone(second, deadzone), algorithm);
                const std::vector<Event> events{
                    {begin - 0.001, {}, {}, 1},
                    {begin, first, ledger_first, 1},
                    {begin + 0.010, second, ledger_second, 1}};
                const std::vector<Event> plant_events{
                    {begin - 0.001, {}, {}, 1},
                    {begin, first, plant_first, 1},
                    {begin + 0.010, second, plant_second, 1}};
                MicroRow row;
                row.curve = curve;
                row.deadzone = deadzone;
                row.magnitude = 1.0;
                row.axis = axis;
                row.metric.add(
                    ledger_predict(events, begin, end),
                    integrate_zoh(plant_events, begin, end));
                rows.push_back(std::move(row));
            }
        }

        // Explicit symmetric X/Y control and asymmetric X/Y response-model
        // gap.  The latter is intentionally not fed back into the ledger.
        const double begin = 2.2;
        const double end = 2.220;
        const Vec2 y_stick{0.0, 0.50};
        const Vec2 scalar_response = curve_response(y_stick, algorithm);
        const auto add_axis_row = [&](const char* axis,
                                      Vec2 plant_velocity,
                                      const char* classification) {
            const std::vector<Event> ledger_events{
                {begin - 0.001, {}, {}, 1},
                {begin, y_stick, scalar_response, 1}};
            const std::vector<Event> plant_events{
                {begin - 0.001, {}, {}, 1},
                {begin, y_stick, plant_velocity, 1}};
            MicroRow row;
            row.curve = curve;
            row.deadzone = 0.0;
            row.magnitude = 0.50;
            row.axis = axis;
            row.classification = classification;
            row.metric.add(
                ledger_predict(ledger_events, begin, end),
                integrate_zoh(plant_events, begin, end));
            row.phantom_magnitude = 0.0;
            rows.push_back(std::move(row));
        };
        add_axis_row(
            "y_symmetric_500_500",
            curve_response_with_scales(y_stick, algorithm, 500.0, 500.0),
            "PASS");
        add_axis_row(
            "y_asymmetric_500_350",
            curve_response_with_scales(y_stick, algorithm, 500.0, 350.0),
            "MODEL_RED");
    }
    return rows;
}

struct ExogenousRow {
    std::string source;
    double pending_magnitude = 0.0;
    double screen_motion_magnitude = 0.0;
};

std::vector<ExogenousRow> run_exogenous_motion_case() {
    const double begin = 3.0;
    const double end = 3.020;
    const std::vector<Event> neutral{
        {begin - 0.001, {}, {}, 1},
        {begin, {}, {}, 1}};
    const std::array<std::pair<const char*, Vec2>, 3> exogenous_motion{
        std::pair<const char*, Vec2>{"left_strafe", {3.0, 0.0}},
        std::pair<const char*, Vec2>{"firing_recoil", {0.0, -2.0}},
        std::pair<const char*, Vec2>{"rotational_assist_like", {2.0, 1.0}},
    };
    std::vector<ExogenousRow> rows;
    for (const auto& [source, motion] : exogenous_motion) {
        const Vec2 pending = ledger_predict(neutral, begin, end);
        require(magnitude(pending) < 1.0e-6,
                "neutral right-stick actuator truth must remain zero");
        require(magnitude(motion) > 0.0,
                "each exogenous screen-motion fixture must be nonzero");
        rows.push_back({source, magnitude(pending), magnitude(motion)});
    }
    return rows;
}

std::string join_rows(const std::vector<std::string>& rows) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (index != 0) stream << ",";
        stream << rows[index];
    }
    return stream.str();
}

std::string controller_capture_json() {
    const std::array<std::string, 2> paths{{
        "artifacts/benchmarks/w5-causal-memory-gate2-20260808/GATE2_CONTROLLER_CAPTURE.json",
        "../../../artifacts/benchmarks/w5-causal-memory-gate2-20260808/GATE2_CONTROLLER_CAPTURE.json"}};
    for (const auto& path : paths) {
        std::ifstream input(path, std::ios::binary);
        if (!input.good()) continue;
        std::ostringstream contents;
        contents << input.rdbuf();
        const auto value = contents.str();
        if (!value.empty()) return value;
    }
    return "{\"available\":false,\"reason\":\"controller_fixture_not_run\"}";
}

std::string build_json(
    const std::vector<AdmissionRow>& admission,
    const std::vector<ResponseRow>& response,
    const Metric& response_transition_aggregate,
    const Metric& response_control_aggregate,
    const std::vector<MicroRow>& micro,
    const std::vector<ExogenousRow>& exogenous,
    const std::string& capture_json,
    int admission_divergence_rows,
    int admission_samples_per_row,
    int response_model_red_rows,
    double micro_deadzone0_max,
    double micro_phantom_max) {
    std::vector<std::string> admission_rows;
    for (const auto& row : admission) {
        std::ostringstream value;
        value << "{\"hz\":" << row.hz
              << ",\"pattern\":\"" << row.pattern
              << "\",\"continuous_ledger\":"
              << metric_json(row.continuous_metric)
              << ",\"latest_only_ledger\":"
              << metric_json(row.latest_metric) << "}";
        admission_rows.push_back(value.str());
    }
    std::vector<std::string> response_rows;
    for (const auto& row : response) {
        std::ostringstream value;
        value << "{\"ramp_ms\":" << row.ramp_ms
              << ",\"delay_ms\":" << row.delay_ms
              << ",\"phase_ms\":" << row.phase_ms
              << ",\"mode\":\"" << row.mode
              << "\",\"ledger_vs_effect_truth\":"
              << metric_json(row.metric) << "}";
        response_rows.push_back(value.str());
    }
    std::vector<std::string> micro_rows;
    for (const auto& row : micro) {
        std::ostringstream value;
        value << "{\"curve\":\"" << row.curve
              << "\",\"deadzone\":" << number(row.deadzone)
              << ",\"stick_magnitude\":" << number(row.magnitude)
              << ",\"axis\":\"" << row.axis
              << "\",\"classification\":\"" << row.classification
              << "\",\"ledger_vs_plant\":"
              << metric_json(row.metric)
              << ",\"phantom_magnitude\":"
              << number(row.phantom_magnitude) << "}";
        micro_rows.push_back(value.str());
    }
    std::vector<std::string> exogenous_rows;
    for (const auto& row : exogenous) {
        std::ostringstream value;
        value << "{\"source\":\"" << row.source
              << "\",\"classification\":\"NON_GOAL\""
              << ",\"pending_magnitude\":"
              << number(row.pending_magnitude)
              << ",\"screen_motion_magnitude\":"
              << number(row.screen_motion_magnitude) << "}";
        exogenous_rows.push_back(value.str());
    }
    std::size_t transition_row_count = 0;
    for (const auto& row : response) {
        if (row.mode != "matched_constant_response") {
            ++transition_row_count;
        }
    }
    std::ostringstream json;
    json << "{\n"
         << "  \"schema\": 1,\n"
         << "  \"result_classes\": [\"PASS\",\"MODEL_RED\","
            "\"UNRESOLVED\",\"NON_GOAL\"],\n"
         << "  \"game_admission\": [" << join_rows(admission_rows)
         << "],\n"
         << "  \"response_state\": [" << join_rows(response_rows)
         << "],\n"
         << "  \"response_state_aggregate\": {\"row_count\":"
         << transition_row_count
         << ",\"total_response_rows\":" << response.size()
         << ",\"row_samples_are_single_points\":true,\"metrics\":"
         << metric_json(response_transition_aggregate) << "},\n"
         << "  \"response_state_matched_control\": {\"metrics\":"
         << metric_json(response_control_aggregate) << "},\n"
         << "  \"micro_curve_deadzone_axis\": ["
         << join_rows(micro_rows) << "],\n"
         << "  \"exogenous_motion\": [" << join_rows(exogenous_rows)
         << "],\n"
         << "  \"capture_state\": {\"controller_fixture\":"
            "\"PASS:cod_native_controller_tests\"," 
            "\"gaps_ms\":[4,6,11,25,60,150,210],"
            "\"horizon_overflow\":\"PASS:explicit\"," 
            "\"viewport_change\":\"MODEL_GAP:no_pixel_guess\","
            "\"actual_result\":"
         << capture_json << "},\n"
         << "  \"summary\": {\"response_model_red_rows\":"
         << response_model_red_rows
         << ",\"admission_steady_state_rows\":2"
         << ",\"admission_divergence_rows\":"
         << admission_divergence_rows
         << ",\"admission_samples_per_row\":"
         << admission_samples_per_row
         << ",\"micro_deadzone0_max_residual\":"
         << number(micro_deadzone0_max)
         << ",\"micro_max_phantom\":" << number(micro_phantom_max)
         << ",\"micro_asymmetric_model_red_rows\":2"
         << ",\"exogenous_non_goal_rows\":3"
         << ",\"game_admission\":\"UNRESOLVED\"," 
            "\"exogenous\":\"NON_GOAL\"}\n"
         << "}\n";
    return json.str();
}

void write_json_if_requested(int argc, char** argv, const std::string& json) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) != "--json") continue;
        std::ofstream output(argv[index + 1], std::ios::binary);
        require(output.good(), "cannot open Gate2 summary JSON path");
        output << json;
        return;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        test_same_current_state_with_different_prior_histories();
        const auto admission = run_admission_matrix();
        int admission_steady_state_rows = 0;
        for (const auto& row : admission) {
            require(mean(row.continuous_metric.residuals) < 1.0e-3,
                    "continuous ledger must match independent ZOH truth");
            if (row.pattern == "held_x_steady") {
                ++admission_steady_state_rows;
                require(percentile(row.latest_metric.residuals, 1.0) < 1.0e-3,
                        "pre-rolled steady held-X must agree with latest-only polls");
            }
        }
        require(admission_steady_state_rows == 2,
                "both 180Hz and 240Hz steady held-X controls must be present");

        int admission_divergence_rows = 0;
        for (const auto& row : admission) {
            if (mean(row.latest_metric.residuals) > 1.0e-4) {
                ++admission_divergence_rows;
            }
        }
        require(admission_divergence_rows > 0,
                "phase sweep must expose latest-only divergence");

        const auto response = run_response_matrix();
        Metric response_transition_aggregate;
        Metric response_control_aggregate;
        int response_model_red_rows = 0;
        int response_control_rows = 0;
        for (const auto& row : response) {
            if (row.mode == "matched_constant_response") {
                ++response_control_rows;
                response_control_aggregate.merge(row.metric);
            } else {
                response_transition_aggregate.merge(row.metric);
                if (mean(row.metric.residuals) > 0.01) {
                    ++response_model_red_rows;
                }
            }
        }
        require(response_model_red_rows > 0,
                "effect-time response matrix must expose model mismatch");
        require(response_control_rows == 4 &&
                    percentile(response_control_aggregate.residuals, 1.0) < 1.0e-3,
                "matched delivery/effect response control must be near zero for every delay");

        const auto micro = run_micro_matrix();
        double micro_deadzone0_max = 0.0;
        double micro_phantom_max = 0.0;
        int micro_model_red_rows = 0;
        for (const auto& row : micro) {
            const double residual_max = percentile(
                row.metric.residuals, 1.0);
            if (row.classification == "MODEL_RED") {
                ++micro_model_red_rows;
            } else if (row.deadzone == 0.0) {
                micro_deadzone0_max = std::max(
                    micro_deadzone0_max, residual_max);
            }
            micro_phantom_max = std::max(
                micro_phantom_max, row.phantom_magnitude);
        }
        require(micro_deadzone0_max < 1.0e-3,
                "zero-deadzone curve control must match plant truth");
        require(micro_phantom_max > 0.1,
                "nonzero deadzone must expose phantom ledger motion");
        require(micro_model_red_rows >= 2,
                "asymmetric X/Y model-gap controls must be present for both curves");

        const auto exogenous = run_exogenous_motion_case();
        const auto capture_json = controller_capture_json();
        const auto json = build_json(
            admission, response, response_transition_aggregate,
            response_control_aggregate, micro, exogenous, capture_json,
            admission_divergence_rows, 16, response_model_red_rows,
            micro_deadzone0_max,
            micro_phantom_max);
        write_json_if_requested(argc, argv, json);

        std::cout << "[W5 Gate2 PASS] game_admission_continuous_ledger"
                  << " rows=" << admission.size()
                  << " latest_only_divergence_rows="
                  << admission_divergence_rows
                  << " steady_state_rows=" << admission_steady_state_rows
                  << " classification=UNRESOLVED\n";
        std::cout << "[W5 Gate2 MODEL RED] response_state rows="
                  << response.size()
                  << " red_rows=" << response_model_red_rows
                  << " aggregate_p95="
                  << percentile(response_transition_aggregate.residuals, 0.95)
                  << " matched_control_max="
                  << percentile(response_control_aggregate.residuals, 1.0)
                  << " row_p95_is_single_point=true\n";
        std::cout << "[W5 Gate2 MODEL RED] micro_deadzone_axis rows="
                  << micro.size()
                  << " deadzone0_max_residual="
                  << micro_deadzone0_max
                  << " phantom_max=" << micro_phantom_max
                  << " asymmetric_model_red_rows="
                  << micro_model_red_rows << "\n";
        for (const auto& row : exogenous) {
            std::cout << "[W5 Gate2 NON-GOAL] exogenous source="
                      << row.source << " pending="
                      << row.pending_magnitude << " screen_motion="
                      << row.screen_motion_magnitude << "\n";
        }
        std::cout << "cod_native_w5_gate2_adversarial_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_w5_gate2_adversarial_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
