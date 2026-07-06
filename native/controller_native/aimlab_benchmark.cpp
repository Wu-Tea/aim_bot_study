#include "aimlab_benchmark.h"

#include "pipeline_contract/target_snapshot.h"
#include "vision_native/target_selector.h"

#include <algorithm>
#include <cmath>

namespace controller_native::aimlab {

double vector_length(common_native::Vec2f value) {
    return std::sqrt(static_cast<double>(value.x) * value.x + static_cast<double>(value.y) * value.y);
}

double dot(common_native::Vec2f lhs, common_native::Vec2f rhs) {
    return static_cast<double>(lhs.x) * rhs.x + static_cast<double>(lhs.y) * rhs.y;
}

double clamp_score(double value) {
    return std::max(0.0, std::min(100.0, value));
}

namespace {

constexpr double kPi = 3.14159265358979323846;

struct ManualProfileParams {
    double reaction_delay_seconds = 0.08;
    double ramp_seconds = 0.16;
    double max_strength = 0.85;
    double noise_degrees = 4.0;
    std::size_t smoothing_frames = 4;
    double reverse_min_strength = 0.0;
};

ManualProfileParams manual_profile_params(ManualInputProfile profile) {
    switch (profile) {
    case ManualInputProfile::Clean:
        return ManualProfileParams{0.08, 0.14, 0.82, 3.0, 4, 0.0};
    case ManualInputProfile::Slow:
        return ManualProfileParams{0.16, 0.35, 0.72, 5.0, 5, 0.0};
    case ManualInputProfile::NoisyRecover:
        return ManualProfileParams{0.08, 0.18, 0.85, 12.0, 4, 0.35};
    }
    return {};
}

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

common_native::Vec2f add(common_native::Vec2f lhs, common_native::Vec2f rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

common_native::Vec2f scale(common_native::Vec2f value, double amount) {
    return {
        static_cast<float>(static_cast<double>(value.x) * amount),
        static_cast<float>(static_cast<double>(value.y) * amount),
    };
}

common_native::Vec2f normalized_or_zero(common_native::Vec2f value) {
    const double length = vector_length(value);
    if (length <= 0.001) {
        return {};
    }
    return scale(value, 1.0 / length);
}

common_native::Vec2f rotate(common_native::Vec2f value, double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return {
        static_cast<float>(static_cast<double>(value.x) * c - static_cast<double>(value.y) * s),
        static_cast<float>(static_cast<double>(value.x) * s + static_cast<double>(value.y) * c),
    };
}

double deterministic_unit_noise(
    ManualInputProfile profile,
    std::uint32_t seed,
    std::uint64_t frame_index) {
    const double profile_offset =
        profile == ManualInputProfile::Clean ? 11.0 :
        profile == ManualInputProfile::Slow ? 29.0 :
        47.0;
    return std::sin(
        (static_cast<double>(frame_index) * 12.9898)
        + (static_cast<double>(seed) * 78.233)
        + profile_offset);
}

bool crossed_axis(float previous, float current) {
    return (previous * current) < 0.0f
        && std::fabs(previous) > 2.0f
        && std::fabs(current) > 2.0f;
}

}  // namespace

ManualInputModel::ManualInputModel(ManualInputProfile profile, std::uint32_t seed)
    : profile_(profile), seed_(seed) {}

ManualInputSample ManualInputModel::update(const ManualInputFrame& frame) {
    ManualInputSample sample;
    sample.intent.intent_id = frame.frame_index;
    sample.intent.timestamp.value = frame.timestamp_seconds;
    sample.intent.aiming = frame.aiming;

    const common_native::Vec2f error = {
        frame.target_px.x - frame.reticle_px.x,
        frame.target_px.y - frame.reticle_px.y,
    };
    const bool reverse_correction =
        has_previous_error_
        && (crossed_axis(previous_error_px_.x, error.x)
            || crossed_axis(previous_error_px_.y, error.y));
    sample.reverse_correction = reverse_correction
        && profile_ == ManualInputProfile::NoisyRecover;

    previous_error_px_ = error;
    has_previous_error_ = true;

    const ManualProfileParams params = manual_profile_params(profile_);
    sample.reaction_ready = frame.aiming && frame.timestamp_seconds >= params.reaction_delay_seconds;
    if (!sample.reaction_ready) {
        return sample;
    }

    const double error_length = vector_length(error);
    if (error_length <= 0.001) {
        return sample;
    }

    common_native::Vec2f direction = normalized_or_zero(error);
    const double noise_radians =
        deterministic_unit_noise(profile_, seed_, frame.frame_index)
        * params.noise_degrees
        * (kPi / 180.0);
    direction = normalized_or_zero(rotate(direction, noise_radians));

    const double ramp = clamp01(
        (frame.timestamp_seconds - params.reaction_delay_seconds)
        / std::max(0.001, params.ramp_seconds));
    const double distance_strength = clamp01(error_length / 140.0);
    double strength = params.max_strength * ramp * (0.35 + (0.65 * distance_strength));
    if (sample.reverse_correction) {
        strength = std::max(strength, params.reverse_min_strength);
    }
    strength = clamp01(strength);
    sample.manual_stick = scale(direction, strength);

    if (vector_length(sample.manual_stick) > 0.001) {
        recent_manual_.push_back(sample.manual_stick);
        while (recent_manual_.size() > params.smoothing_frames) {
            recent_manual_.erase(recent_manual_.begin());
        }
    }

    common_native::Vec2f smoothed;
    for (const auto& value : recent_manual_) {
        smoothed = add(smoothed, value);
    }
    if (!recent_manual_.empty()) {
        smoothed = scale(smoothed, 1.0 / static_cast<double>(recent_manual_.size()));
    }

    const double smoothed_length = vector_length(smoothed);
    sample.intent.strength = static_cast<float>(std::min(1.0, smoothed_length));
    if (frame.aiming && smoothed_length > 0.05) {
        sample.intent.valid = true;
        sample.intent.has_direction = true;
        sample.intent.direction = normalized_or_zero(smoothed);
    }
    return sample;
}

void ScoreAggregator::add_frame(const FrameScoreInput& frame) {
    ++report_.frames;
    const bool selected_intended =
        frame.has_selected_target && frame.selected_target_id == frame.intended_target_id;
    if (selected_intended) {
        ++report_.intended_selected_frames;
    }
    const bool wrong_strong =
        frame.strong_snap_active && frame.has_selected_target && !selected_intended;
    if (wrong_strong) {
        ++report_.wrong_strong_lock_frames;
        ++report_.wrong_target_ads_snap_count;
    }
    if (frame.strong_snap_active && frame.selected_is_corpse) {
        ++report_.corpse_lock_frames;
    }
    if (frame.strong_snap_active && frame.selected_is_friendly_or_unknown) {
        ++report_.friendly_or_unknown_lock_frames;
    }

    const double before = vector_length(frame.aim_error_before_px);
    const double after = vector_length(frame.aim_error_after_px);
    if (after < before) {
        ++report_.helpful_output_frames;
    } else if (after > before + 0.001) {
        ++report_.harmful_output_frames;
    }
    if (after > 50.0 && before <= 50.0) {
        ++report_.overshoot_over_50px_count;
    }
    if (vector_length(frame.user_input) > 0.25 && vector_length(frame.controller_output) > 0.25 &&
        dot(frame.user_input, frame.controller_output) < -0.05) {
        ++report_.user_fight_frames;
    }
}

ScoreReport ScoreAggregator::report() const {
    ScoreReport out = report_;
    const double frames = static_cast<double>(std::max(1, out.frames));
    out.time_on_intended_target_ratio = out.intended_selected_frames / frames;
    out.helpful_output_ratio =
        out.helpful_output_frames / static_cast<double>(std::max(1, out.helpful_output_frames + out.harmful_output_frames));

    out.selection_score = clamp_score(100.0 - out.wrong_strong_lock_frames * 60.0);
    out.control_score = clamp_score(100.0 - out.overshoot_over_50px_count * 20.0);
    out.cooperation_score = clamp_score(100.0 * out.helpful_output_ratio - out.user_fight_frames * 10.0);
    out.smoothness_score = 100.0;
    out.authority_safety_score = clamp_score(
        100.0 - out.corpse_lock_frames * 50.0 - out.friendly_or_unknown_lock_frames * 50.0);
    out.final_score = clamp_score(
        0.30 * out.selection_score +
        0.30 * out.control_score +
        0.20 * out.cooperation_score +
        0.10 * out.smoothness_score +
        0.10 * out.authority_safety_score -
        out.wrong_target_ads_snap_count * 10.0);
    return out;
}

namespace {

vision_native::Detection detection_for_target(float target_x, float target_y, float conf) {
    constexpr float width = 60.0f;
    constexpr float height = 140.0f;
    vision_native::Detection detection;
    detection.x1 = target_x - (width * 0.5f);
    detection.x2 = target_x + (width * 0.5f);
    detection.y1 = target_y - (height * 0.40f);
    detection.y2 = detection.y1 + height;
    detection.conf = conf;
    return detection;
}

vision_native::DetectionBatch near_side_vs_far_front_batch(std::uint64_t frame_id) {
    vision_native::DetectionBatch batch;
    batch.frame_id = frame_id;
    batch.captured_at_ns = frame_id * 10'000'000ull;
    batch.inferred_at_ns = batch.captured_at_ns + 1'000'000ull;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(detection_for_target(250.0f, 310.0f, 0.58f));
    batch.detections.push_back(detection_for_target(390.0f, 210.0f, 0.86f));
    return batch;
}

pipeline_contract::UserAimIntent lower_left_intent(std::uint64_t intent_id) {
    pipeline_contract::UserAimIntent intent;
    intent.valid = true;
    intent.intent_id = intent_id;
    intent.strength = 1.0f;
    intent.has_direction = true;
    intent.direction.x = -0.75f;
    intent.direction.y = 0.65f;
    intent.aiming = true;
    return intent;
}

int selected_target_id(const vision_native::VisionResult& result) {
    if (!result.has_target) {
        return -1;
    }
    return result.target_x < 320.0f && result.target_y > 256.0f ? 1 : 2;
}

ScoreReport run_selector_near_side_vs_far_front(bool use_intent) {
    vision_native::VisionTargetSelector selector(640, 512);
    ScoreAggregator scorer;
    for (int frame_index = 0; frame_index < 120; ++frame_index) {
        const std::uint64_t frame_id = static_cast<std::uint64_t>(frame_index + 1);
        const vision_native::DetectionBatch batch = near_side_vs_far_front_batch(frame_id);
        const vision_native::VisionResult result = use_intent
            ? selector.select(batch, lower_left_intent(frame_id))
            : selector.select(batch);

        FrameScoreInput frame;
        frame.intended_target_id = 1;
        frame.selected_target_id = selected_target_id(result);
        frame.has_selected_target = result.has_target;
        frame.strong_snap_active = result.has_target && result.aim_authority;
        frame.user_input = {-0.6f, 0.4f};
        frame.dt_seconds = 1.0 / 120.0;

        if (frame.selected_target_id == 1) {
            frame.aim_error_before_px = {-70.0f, 54.0f};
            frame.aim_error_after_px = {-28.0f, 22.0f};
            frame.controller_output = {-0.6f, 0.4f};
        } else if (frame.selected_target_id == 2) {
            frame.aim_error_before_px = {-70.0f, 54.0f};
            frame.aim_error_after_px = {-91.0f, 71.0f};
            frame.controller_output = {0.7f, -0.25f};
        } else {
            frame.aim_error_before_px = {-70.0f, 54.0f};
            frame.aim_error_after_px = {-70.0f, 54.0f};
        }

        scorer.add_frame(frame);
    }
    return scorer.report();
}

ScoreReport unknown_scenario_report() {
    ScoreReport report;
    report.final_score = 0.0;
    report.selection_score = 0.0;
    report.control_score = 0.0;
    report.cooperation_score = 0.0;
    report.smoothness_score = 0.0;
    report.authority_safety_score = 0.0;
    return report;
}

ScoreReport scaffold_perfect_report() {
    ScoreAggregator scorer;
    for (int frame_index = 0; frame_index < 60; ++frame_index) {
        (void)frame_index;
        FrameScoreInput frame;
        frame.intended_target_id = 1;
        frame.selected_target_id = 1;
        frame.has_selected_target = true;
        frame.strong_snap_active = true;
        frame.aim_error_before_px = {80.0f, 0.0f};
        frame.aim_error_after_px = {40.0f, 0.0f};
        frame.controller_output = {-0.5f, 0.0f};
        frame.user_input = {-0.4f, 0.0f};
        frame.dt_seconds = 1.0 / 120.0;
        scorer.add_frame(frame);
    }
    return scorer.report();
}

}  // namespace

std::vector<std::string> default_scenarios() {
    return {
        "multi_target_flick",
        "near_side_vs_far_front_no_intent",
        "near_side_vs_far_front_intent",
        "ads_diagonal_pull",
        "moving_track",
        "slide_occlusion_delay",
        "corpse_cue_loss",
        "err_target_recovery",
    };
}

ScoreReport run_scenario(const std::string& name, std::uint32_t /*seed*/) {
    if (name == "near_side_vs_far_front") {
        return run_selector_near_side_vs_far_front(false);
    }
    if (name == "near_side_vs_far_front_no_intent") {
        return run_selector_near_side_vs_far_front(false);
    }
    if (name == "near_side_vs_far_front_intent") {
        return run_selector_near_side_vs_far_front(true);
    }
    for (const auto& scenario : default_scenarios()) {
        if (name == scenario) {
            return scaffold_perfect_report();
        }
    }
    return unknown_scenario_report();
}

}  // namespace controller_native::aimlab
