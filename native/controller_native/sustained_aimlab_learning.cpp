#include "sustained_aimlab_learning.h"

#include "control_learning/causal_online_response_learner.h"
#include "control_learning/pending_motion_model.h"
#include "control_learning/short_horizon_rollout.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace controller_native::sustained_aimlab {
namespace {

constexpr std::uint64_t kNsPerMs = 1'000'000ull;

pipeline_contract::Vec2f to_vec2f(Vec2d value) noexcept {
    return {static_cast<float>(value.x), static_cast<float>(value.y)};
}

control_learning::Vec2d response(
    const control_learning::ResponseMatrix2d& matrix,
    Vec2d stick) noexcept {
    return {
        matrix.values[0][0] * stick.x + matrix.values[0][1] * stick.y,
        matrix.values[1][0] * stick.x + matrix.values[1][1] * stick.y};
}

double clamp_stick(double value) noexcept {
    return std::clamp(value, -1.0, 1.0);
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4)
                    << std::setfill('0') << static_cast<int>(ch) << std::dec;
            } else {
                out << ch;
            }
        }
    }
    return out.str();
}

class LearningRoundController {
public:
    LearningRoundController(
        ControllerStep base,
        control_learning::CausalOnlineResponseLearner& learner,
        control_learning::ControlHistory<1024>& history,
        std::uint64_t& sample_sequence,
        std::uint64_t round_base_ns,
        std::uint64_t target_namespace,
        bool enabled,
        LearningRoundResult& metrics)
        : base_(std::move(base)),
          learner_(learner),
          history_(history),
          sample_sequence_(sample_sequence),
          round_base_ns_(round_base_ns),
          target_namespace_(target_namespace),
          enabled_(enabled),
          metrics_(metrics) {}

    ControllerStepResult step(const ControllerObservation& input) {
        ControllerStepResult output = base_(input);
        const std::uint64_t capture_ns = round_base_ns_ +
            static_cast<std::uint64_t>(input.now_ms) * kNsPerMs;

        if (enabled_ && input.fresh_vision && input.target_present) {
            pipeline_contract::CommittedCaptureObservation observation;
            observation.source_frame_id = target_namespace_ + input.frame_id;
            observation.source_observation_id = observation.source_frame_id;
            observation.persistent_target_id = target_namespace_ + input.target_id;
            observation.viewport_source_frame_id = observation.source_frame_id;
            observation.captured_at_ns = capture_ns;
            observation.result_at_ns = capture_ns + 2 * kNsPerMs;
            observation.stable_error_px = to_vec2f(input.observed_error_px);
            observation.stable_body_size_px = {35.0f, 100.0f};
            observation.reliability = 0.95f;
            observation.normalized_size = 0.4f;
            observation.lifecycle = pipeline_contract::TargetLifecycle::Observed;
            observation.mode = output.bodylock_mode
                ? pipeline_contract::ControlMode::BodyLockFollow
                : pipeline_contract::ControlMode::AdsAcquire;
            observation.ads_epoch = 1;
            observation.eligible_candidate_count = 1;
            observation.fresh_observed = true;
            observation.strong_observation = true;
            observation.stable_coordinates_valid = true;

            const auto assessment = learner_.observe_vision(observation, history_);
            if (assessment.accepted_by_any_delay) {
                if (metrics_.first_accepted_update_ms < 0)
                    metrics_.first_accepted_update_ms = input.now_ms;
                ++metrics_.accepted_updates;
            }
            const auto estimate = learner_.estimate();

            control_learning::Vec2d target_velocity{};
            if (has_previous_observation_ &&
                previous_target_id_ == observation.persistent_target_id &&
                capture_ns > previous_capture_ns_) {
                const double dt = static_cast<double>(
                    capture_ns - previous_capture_ns_) / 1'000'000'000.0;
                target_velocity.x =
                    (input.observed_error_px.x - previous_error_.x) / dt;
                target_velocity.y =
                    (input.observed_error_px.y - previous_error_.y) / dt;
                const auto camera = response(estimate.right_stable, last_final_);
                target_velocity.x += camera.x;
                target_velocity.y += camera.y;
            }

            control_learning::Vec2d scheduled_pending{};
            if (has_previous_observation_ &&
                previous_target_id_ == observation.persistent_target_id) {
                control_learning::PendingMotionRequest request;
                request.previous_capture_ns = previous_capture_ns_;
                request.current_capture_ns = capture_ns;
                request.decision_ns = capture_ns;
                request.delay_ms = estimate.selected_delay_ms;
                request.right_response = estimate.right_stable;
                request.left_response = estimate.left_stable;
                request.selected_delay_confidence =
                    estimate.selected_delay_confidence;
                request.response_confidence = estimate.right_confidence;
                const auto pending = control_learning::PendingMotionModel::estimate(
                    request, history_);
                if (pending.valid) scheduled_pending = pending.scheduled_px;
            }

            control_learning::RolloutSnapshot snapshot;
            snapshot.decision_at_ns = capture_ns;
            snapshot.latest_evidence_at_ns = capture_ns;
            snapshot.target_id = observation.persistent_target_id;
            snapshot.mode = observation.mode;
            snapshot.error_px = {input.observed_error_px.x,
                                 input.observed_error_px.y};
            snapshot.predicted_terminal_error_px = {
                output.predicted_terminal_error_px.x,
                output.predicted_terminal_error_px.y};
            snapshot.target_velocity_px_per_sec = target_velocity;
            snapshot.shaped_ai = {output.shaped_assist_stick.x,
                                  output.shaped_assist_stick.y};
            snapshot.manual = {input.manual_stick.x, input.manual_stick.y};
            snapshot.scheduled_pending_px = scheduled_pending;
            snapshot.right_response = estimate.right_stable;
            snapshot.response_confidence = estimate.right_confidence;
            snapshot.delay_confidence = estimate.selected_delay_confidence;
            snapshot.has_target = true;
            snapshot.single_strong_target = true;
            const auto rollout =
                control_learning::ShortHorizonRollout::evaluate(snapshot);
            if (rollout.valid) {
                active_scale_ = rollout.best_scale;
                ++metrics_.valid_rollout_decisions;
                metrics_.mean_selected_scale += active_scale_;
                if (std::fabs(active_scale_ - 1.0f) > 1.0e-6f)
                    ++metrics_.changed_scale_decisions;
            } else {
                active_scale_ = 1.0f;
            }
            previous_capture_ns_ = capture_ns;
            previous_target_id_ = observation.persistent_target_id;
            previous_error_ = input.observed_error_px;
            has_previous_observation_ = true;
        }

        if (enabled_ && input.target_present) {
            const Vec2d residual{
                output.final_stick.x - input.manual_stick.x,
                output.final_stick.y - input.manual_stick.y};
            output.final_stick = {
                clamp_stick(input.manual_stick.x + residual.x * active_scale_),
                clamp_stick(input.manual_stick.y + residual.y * active_scale_)};
        }

        control_learning::DeliveredControlSample delivered;
        delivered.sample_seq = ++sample_sequence_;
        delivered.applied_at_ns = capture_ns + kNsPerMs;
        delivered.physical_right = to_vec2f(input.manual_stick);
        delivered.manual_component = to_vec2f(input.manual_stick);
        delivered.ai_component = to_vec2f(output.shaped_assist_stick);
        delivered.pre_recoil = to_vec2f(output.final_stick);
        delivered.final_right = to_vec2f(output.final_stick);
        delivered.output_delivered = true;
        delivered.ads_epoch = 1;
        if (!history_.push(delivered)) {
            throw std::runtime_error("learning control history rejected a sample");
        }
        last_final_ = output.final_stick;
        return output;
    }

private:
    ControllerStep base_;
    control_learning::CausalOnlineResponseLearner& learner_;
    control_learning::ControlHistory<1024>& history_;
    std::uint64_t& sample_sequence_;
    std::uint64_t round_base_ns_ = 0;
    std::uint64_t target_namespace_ = 0;
    bool enabled_ = false;
    LearningRoundResult& metrics_;
    float active_scale_ = 1.0f;
    bool has_previous_observation_ = false;
    std::uint64_t previous_capture_ns_ = 0;
    std::uint64_t previous_target_id_ = 0;
    Vec2d previous_error_{};
    Vec2d last_final_{};
};

}  // namespace

LearningExperimentResult run_learning_experiment(
    const LearningExperimentConfig& config,
    std::uint32_t first_seed,
    ManualProfile manual_profile,
    BenchmarkCohort cohort,
    RoundControllerFactory controller_factory) {
    if (config.rounds <= 0 || config.round_duration_ms <= 0 ||
        config.control_response_delay_ms < 0 || !controller_factory) {
        throw std::invalid_argument("invalid learning experiment config");
    }
    LearningExperimentResult result;
    result.policy = config.policy;
    result.round_duration_ms = config.round_duration_ms;
    result.plant_delay_ms = config.control_response_delay_ms;
    result.camera_response_px_per_stick_second =
        config.camera_response_px_per_stick_second;
    result.manual_profile = manual_profile;
    result.cohort = cohort;
    auto learner = std::make_unique<control_learning::CausalOnlineResponseLearner>();
    auto history = std::make_unique<control_learning::ControlHistory<1024>>();
    std::uint64_t sample_sequence = 0;
    std::uint64_t round_base_ns = 1'000'000'000ull;

    for (int round = 0; round < config.rounds; ++round) {
        if (config.policy == LearningPolicy::ResetEachRound) {
            learner->reset_session();
        }
        history->clear();
        LearningRoundResult metrics;
        metrics.round_index = round + 1;
        metrics.seed = first_seed + static_cast<std::uint32_t>(round * 7919);
        metrics.started_with_response_confidence =
            learner->estimate().right_confidence;
        metrics.mean_selected_scale = 0.0;

        BenchmarkConfig benchmark;
        benchmark.duration_ms = config.round_duration_ms;
        benchmark.control_response_delay_ms = config.control_response_delay_ms;
        benchmark.camera_response_px_per_stick_second =
            config.camera_response_px_per_stick_second;
        const ScenarioScript script = generate_script(metrics.seed, benchmark);
        LearningRoundController learning_controller(
            controller_factory(), *learner, *history, sample_sequence,
            round_base_ns,
            static_cast<std::uint64_t>(round + 1) * 1'000'000ull,
            config.policy != LearningPolicy::Baseline, metrics);
        const BenchmarkResult score = run_simulation(
            script, manual_profile,
            [&](const ControllerObservation& input) {
                return learning_controller.step(input);
            },
            cohort);
        metrics.acquire_points = score.acquire_points;
        metrics.script_hash = score.script_hash;
        metrics.tracking_points = score.tracking_points;
        metrics.smooth_bonus = score.smooth_bonus;
        metrics.total_points = score.acquire_points + score.tracking_points +
            score.smooth_bonus;
        const auto estimate = learner->estimate();
        metrics.ended_with_response_confidence = estimate.right_confidence;
        metrics.ended_with_delay_confidence =
            estimate.selected_delay_confidence;
        metrics.selected_delay_ms = estimate.selected_delay_ms;
        if (metrics.valid_rollout_decisions != 0) {
            metrics.mean_selected_scale /=
                static_cast<double>(metrics.valid_rollout_decisions);
        } else {
            metrics.mean_selected_scale = 1.0;
        }
        result.rounds.push_back(metrics);
        round_base_ns += static_cast<std::uint64_t>(
            config.round_duration_ms + 200) * kNsPerMs;
    }
    return result;
}

std::string learning_experiment_to_json(
    const LearningExperimentResult& result) {
    const char* policy = "baseline";
    if (result.policy == LearningPolicy::ResetEachRound) policy = "reset";
    if (result.policy == LearningPolicy::RetainAcrossRounds) policy = "retain";
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "{\"schema\":\"sustained_aimlab_learning_v2\""
        << ",\"policy\":\"" << policy << "\""
        << ",\"ground_truth_used_for_policy\":"
        << (result.ground_truth_used_for_policy ? "true" : "false")
        << ",\"control_history_reset_each_round\":"
        << (result.control_history_reset_each_round ? "true" : "false")
        << ",\"round_duration_ms\":" << result.round_duration_ms
        << ",\"plant_delay_ms\":" << result.plant_delay_ms
        << ",\"camera_response_px_per_stick_second\":"
        << result.camera_response_px_per_stick_second
        << ",\"manual_profile\":\""
        << (result.manual_profile == ManualProfile::Mixed ? "mixed" : "pure")
        << "\",\"cohort\":\""
        << (result.cohort == BenchmarkCohort::BodyLockFollow
                ? "bodylock" : "ads")
        << "\",\"revision\":\"" << json_escape(result.revision)
        << "\",\"dirty\":" << (result.dirty ? "true" : "false")
        << ",\"config_path\":\"" << json_escape(result.config_path)
        << "\",\"config_fingerprint_fnv1a64\":\""
        << result.config_fingerprint_fnv1a64 << "\""
        << ",\"rounds\":[";
    for (std::size_t index = 0; index < result.rounds.size(); ++index) {
        if (index) out << ',';
        const auto& round = result.rounds[index];
        out << "{\"round\":" << round.round_index
            << ",\"seed\":" << round.seed
            << ",\"script_hash\":\"" << round.script_hash << "\""
            << ",\"total_points\":" << round.total_points
            << ",\"acquire_points\":" << round.acquire_points
            << ",\"tracking_points\":" << round.tracking_points
            << ",\"smooth_bonus\":" << round.smooth_bonus
            << ",\"start_response_confidence\":"
            << round.started_with_response_confidence
            << ",\"end_response_confidence\":"
            << round.ended_with_response_confidence
            << ",\"delay_confidence\":"
            << round.ended_with_delay_confidence
            << ",\"selected_delay_ms\":" << round.selected_delay_ms
            << ",\"accepted_updates\":" << round.accepted_updates
            << ",\"first_accepted_update_ms\":"
            << round.first_accepted_update_ms
            << ",\"valid_rollout_decisions\":"
            << round.valid_rollout_decisions
            << ",\"changed_scale_decisions\":"
            << round.changed_scale_decisions
            << ",\"mean_selected_scale\":"
            << round.mean_selected_scale << '}';
    }
    out << "]}";
    return out.str();
}

}  // namespace controller_native::sustained_aimlab
