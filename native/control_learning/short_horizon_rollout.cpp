#include "control_learning/short_horizon_rollout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace control_learning {
namespace {
double magnitude(Vec2d value) noexcept { return std::hypot(value.x, value.y); }
Vec2d response(const ResponseMatrix2d& matrix, Vec2d stick) noexcept {
    return {matrix.values[0][0] * stick.x + matrix.values[0][1] * stick.y,
            matrix.values[1][0] * stick.x + matrix.values[1][1] * stick.y};
}
bool finite(Vec2d value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}
Vec2d scaled_final_output(Vec2d value, double scale) noexcept {
    return {
        std::clamp(value.x * scale, -1.0, 1.0),
        std::clamp(value.y * scale, -1.0, 1.0)};
}
Vec2d pending_residual(const RolloutSnapshot& snapshot) noexcept {
    return snapshot.pending_motion_valid ? snapshot.pending_total_px : Vec2d{};
}
double candidate_cost(const RolloutSnapshot& snapshot, double scale) noexcept {
    constexpr double dt = 0.020;
    constexpr int steps = 8;
    const auto pending = pending_residual(snapshot);
    Vec2d error{
        snapshot.predicted_terminal_error_px.x - pending.x,
        snapshot.predicted_terminal_error_px.y - pending.y};
    Vec2d velocity = snapshot.target_velocity_px_per_sec;
    const Vec2d stick = scaled_final_output(snapshot.final_output, scale);
    const Vec2d camera = response(snapshot.right_response, stick);
    double cost = 0.0;
    for (int step = 0; step < steps; ++step) {
        velocity.x += snapshot.target_acceleration_px_per_sec2.x * dt;
        velocity.y += snapshot.target_acceleration_px_per_sec2.y * dt;
        error.x += (velocity.x - camera.x) * dt;
        error.y += (velocity.y - camera.y) * dt;
        cost += magnitude(error) * 20.0;
    }
    cost += magnitude(error) * (snapshot.mode ==
        pipeline_contract::ControlMode::AdsAcquire ? 70.0 : 45.0);
    return cost;
}

double tracking_area_cost(const RolloutSnapshot& snapshot, double scale) noexcept {
    constexpr double dt = 0.020;
    constexpr int steps = 8;
    const auto pending = pending_residual(snapshot);
    Vec2d error{
        snapshot.predicted_terminal_error_px.x - pending.x,
        snapshot.predicted_terminal_error_px.y - pending.y};
    Vec2d velocity = snapshot.target_velocity_px_per_sec;
    const Vec2d camera = response(
        snapshot.right_response, scaled_final_output(snapshot.final_output, scale));
    double cost = 0.0;
    for (int step = 0; step < steps; ++step) {
        velocity.x += snapshot.target_acceleration_px_per_sec2.x * dt;
        velocity.y += snapshot.target_acceleration_px_per_sec2.y * dt;
        error.x += (velocity.x - camera.x) * dt;
        error.y += (velocity.y - camera.y) * dt;
        cost += magnitude(error) * 20.0;
    }
    return cost;
}
}  // namespace

bool RolloutSnapshot::operator==(const RolloutSnapshot& o) const noexcept {
    return decision_at_ns == o.decision_at_ns &&
        latest_evidence_at_ns == o.latest_evidence_at_ns && target_id == o.target_id &&
        mode == o.mode && error_px.x == o.error_px.x && error_px.y == o.error_px.y &&
        predicted_terminal_error_px.x == o.predicted_terminal_error_px.x &&
        predicted_terminal_error_px.y == o.predicted_terminal_error_px.y &&
        target_velocity_px_per_sec.x == o.target_velocity_px_per_sec.x &&
        target_velocity_px_per_sec.y == o.target_velocity_px_per_sec.y &&
        target_acceleration_px_per_sec2.x == o.target_acceleration_px_per_sec2.x &&
        target_acceleration_px_per_sec2.y == o.target_acceleration_px_per_sec2.y &&
        final_output.x == o.final_output.x && final_output.y == o.final_output.y &&
        pending_total_px.x == o.pending_total_px.x &&
        pending_total_px.y == o.pending_total_px.y &&
        right_response.values == o.right_response.values &&
        response_confidence == o.response_confidence &&
        delay_confidence == o.delay_confidence &&
        pending_motion_valid == o.pending_motion_valid &&
        has_target == o.has_target &&
        single_strong_target == o.single_strong_target;
}

bool RolloutResult::operator==(const RolloutResult& o) const noexcept {
    if (candidate_count != o.candidate_count || best_scale != o.best_scale ||
        confidence != o.confidence || used_latest_timestamp_ns != o.used_latest_timestamp_ns ||
        manual_escape != o.manual_escape || valid != o.valid) return false;
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].scale != o.candidates[i].scale ||
            candidates[i].cost != o.candidates[i].cost) return false;
    return true;
}

RolloutResult ShortHorizonRollout::evaluate(
    const RolloutSnapshot& snapshot) noexcept {
    RolloutResult result;
    result.used_latest_timestamp_ns = snapshot.latest_evidence_at_ns;
    const bool causal = snapshot.latest_evidence_at_ns <= snapshot.decision_at_ns;
    const bool confidence_ok = snapshot.response_confidence >= 0.20f &&
        snapshot.delay_confidence >= 0.05f;
    if (!causal || !snapshot.has_target || !confidence_ok ||
        !snapshot.pending_motion_valid ||
        snapshot.mode == pipeline_contract::ControlMode::Manual ||
        !finite(snapshot.error_px) || !finite(snapshot.predicted_terminal_error_px) ||
        !finite(snapshot.final_output) || !finite(snapshot.pending_total_px))
        return result;

    // Escape/ownership classification belongs to the production fuser.  This
    // shadow evaluator only compares bounded alternatives of its one final
    // pre-recoil proposal.
    result.manual_escape = false;
    constexpr std::array<float, 5> scales{0.0f, 0.70f, 0.85f, 1.00f, 1.15f};
    result.candidate_count = snapshot.single_strong_target ? 5 : 4;
    double best_cost = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < result.candidate_count; ++i) {
        result.candidates[i] = {scales[i], candidate_cost(snapshot, scales[i])};
        if (result.candidates[i].cost < best_cost - 1.0e-9) {
            best_cost = result.candidates[i].cost;
            result.best_scale = scales[i];
        }
    }
    if (result.best_scale < 1.0f &&
        tracking_area_cost(snapshot, result.best_scale) >=
            tracking_area_cost(snapshot, 1.0)) {
        result.best_scale = 1.0f;
    }
    result.confidence = std::min(
        snapshot.response_confidence, snapshot.delay_confidence);
    result.valid = true;
    return result;
}

}  // namespace control_learning
