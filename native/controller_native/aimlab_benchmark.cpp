#include "aimlab_benchmark.h"

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

ScoreReport run_near_side_vs_far_front() {
    controller_native::aimlab::ScoreAggregator scorer;
    for (int frame_index = 0; frame_index < 120; ++frame_index) {
        (void)frame_index;
        controller_native::aimlab::FrameScoreInput frame;
        frame.intended_target_id = 1;
        frame.selected_target_id = 2;
        frame.has_selected_target = true;
        frame.strong_snap_active = true;
        frame.aim_error_before_px = {-70.0f, 48.0f};
        frame.aim_error_after_px = {-82.0f, 55.0f};
        frame.controller_output = {0.7f, -0.2f};
        frame.user_input = {-0.6f, 0.4f};
        frame.dt_seconds = 1.0 / 120.0;
        scorer.add_frame(frame);
    }
    return scorer.report();
}

}  // namespace

std::vector<std::string> default_scenarios() {
    return {
        "multi_target_flick",
        "near_side_vs_far_front",
        "ads_diagonal_pull",
        "moving_track",
        "slide_occlusion_delay",
        "corpse_cue_loss",
        "err_target_recovery",
    };
}

ScoreReport run_scenario(const std::string& name, std::uint32_t /*seed*/) {
    if (name == "near_side_vs_far_front") {
        return run_near_side_vs_far_front();
    }
    controller_native::aimlab::ScoreAggregator scorer;
    for (int frame_index = 0; frame_index < 60; ++frame_index) {
        (void)frame_index;
        controller_native::aimlab::FrameScoreInput frame;
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

}  // namespace controller_native::aimlab
