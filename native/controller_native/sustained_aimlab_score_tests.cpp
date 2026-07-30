#include "sustained_aimlab_score.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace controller_native::sustained_aimlab;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance,
                  const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(
            message + ": expected=" + std::to_string(expected) +
            " actual=" + std::to_string(actual));
    }
}

TargetScript target_with_deadline(int deadline_ms = 300) {
    TargetScript target;
    target.id = 1;
    target.motion = MotionProfile::ConstantHorizontal;
    target.acquire_deadline_ms = deadline_ms;
    return target;
}

ScoreFrame tracking_frame(int ms, Vec2d error) {
    ScoreFrame frame;
    frame.absolute_ms = ms;
    frame.target_elapsed_ms = ms;
    frame.in_tracking_window = true;
    frame.target_observed = true;
    frame.tracker_reliable = true;
    frame.bodylock_mode = true;
    frame.target_id = 1;
    frame.error_px = error;
    return frame;
}

TargetResult score_constant_error(double error_px) {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(50);
    for (int ms = 0; ms < 1'000; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {error_px, 0.0});
        frame.final_stick = {0.05, 0.0};
        scorer.add_frame(frame);
    }
    return scorer.finish();
}

void test_acquisition_points_reward_earlier_entry() {
    TargetScorer early(target_with_deadline(), BenchmarkConfig{});
    early.mark_acquired(50);
    TargetScorer late(target_with_deadline(), BenchmarkConfig{});
    late.mark_acquired(250);
    TargetScorer miss(target_with_deadline(), BenchmarkConfig{});

    const TargetResult early_result = early.finish();
    const TargetResult late_result = late.finish();
    const TargetResult miss_result = miss.finish();
    require(early_result.acquire_points > late_result.acquire_points,
            "early acquisition must score higher than late acquisition");
    require(late_result.acquire_points > miss_result.acquire_points,
            "late acquisition must score higher than a miss");
    require_near(early_result.acquire_points, 1000.0 * 250.0 / 300.0, 1e-9,
                 "acquisition formula");
    require_near(miss_result.acquire_points, 0.0, 1e-12, "miss score");
}

void test_tracking_points_reward_center_proximity() {
    const TargetResult center = score_constant_error(0.0);
    const TargetResult middle = score_constant_error(12.0);
    const TargetResult edge = score_constant_error(22.0);
    const TargetResult outside = score_constant_error(25.0);

    require(center.tracking_points > middle.tracking_points,
            "center must outscore middle");
    require(middle.tracking_points > edge.tracking_points,
            "middle must outscore edge");
    require(edge.tracking_points > outside.tracking_points,
            "inside edge must outscore outside");
    require_near(center.tracking_points, 1000.0, 1e-9,
                 "perfect center track points");
    require_near(outside.tracking_points, 0.0, 1e-9,
                 "outside target earns no tracking points");
}

void test_smooth_zero_output_cannot_beat_useful_tracking() {
    const TargetResult useful = score_constant_error(5.0);
    TargetScorer stopped(target_with_deadline(), BenchmarkConfig{});
    stopped.mark_acquired(50);
    for (int ms = 0; ms < 1'000; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {30.0, 0.0});
        frame.target_velocity_px_per_second = {100.0, 0.0};
        stopped.add_frame(frame);
    }
    const TargetResult stopped_result = stopped.finish();
    require(useful.tracking_points > stopped_result.tracking_points,
            "useful track must beat smooth stopped output");
    require(useful.tracking_points + useful.smooth_bonus >
                stopped_result.tracking_points + stopped_result.smooth_bonus,
            "smooth bonus must not rescue stopped output");
}

void test_one_overshoot_trace_counts_once() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 10; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {6.0 - ms * 1.5, 0.0});
        frame.final_stick = {0.4, 0.0};
        scorer.add_frame(frame);
    }
    for (int ms = 10; ms < 80; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {-18.0, 0.0});
        frame.final_stick = {0.4, 0.0};
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.over_events == 1, "one crossing must count one overshoot");
}

void test_sustained_projected_lag_counts_one_undertrack() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 100; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {14.0, 0.0});
        frame.target_velocity_px_per_second = {100.0, 0.0};
        frame.final_stick = {0.01, 0.0};
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.undertrack_events == 1,
            "sustained projected lag must count once");
    require(result.undertrack_total_ms >= 40,
            "undertrack duration must be recorded");
}

void test_false_stop_requires_stable_demand_and_no_escape() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 40; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {20.0, 0.0});
        frame.target_velocity_px_per_second = {100.0, 0.0};
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.false_stop_events == 1,
            "20ms demanded zero output must count one false stop");
    require(result.false_stop_total_ms >= 20,
            "false stop duration must be recorded");

    TargetScorer escaping(target_with_deadline(), BenchmarkConfig{});
    escaping.mark_acquired(20);
    for (int ms = 0; ms < 40; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {20.0, 0.0});
        frame.target_velocity_px_per_second = {100.0, 0.0};
        frame.manual_escape = true;
        frame.bodylock_mode = false;
        escaping.add_frame(frame);
    }
    const TargetResult escaped = escaping.finish();
    require(escaped.false_stop_events == 0,
            "manual escape must suppress false-stop classification");
    require(escaped.false_mode_exit_events == 0,
            "manual escape must suppress false-mode-exit classification");
}

void test_stale_output_after_target_loss_counts_once() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 40; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {0.0, 0.0});
        frame.target_observed = false;
        frame.tracker_reliable = false;
        frame.bodylock_mode = false;
        frame.final_stick = {0.30, 0.0};
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.stale_output_after_stop_events == 1,
            "stale output after target loss must count once");
}

void test_assist_dropout_uses_shaped_assist_not_final_manual_mix() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    ScoreFrame before = tracking_frame(0, {20.0, 0.0});
    before.target_velocity_px_per_second = {100.0, 0.0};
    before.shaped_assist_stick = {0.20, 0.0};
    before.final_stick = {0.01, 0.0};
    scorer.add_frame(before);

    ScoreFrame after = tracking_frame(1, {20.0, 0.0});
    after.target_velocity_px_per_second = {100.0, 0.0};
    after.shaped_assist_stick = {0.0, 0.0};
    after.final_stick = {0.01, 0.0};
    scorer.add_frame(after);
    scorer.add_frame(after);

    const TargetResult result = scorer.finish();
    require(result.assist_dropout_events == 1,
            "assist dropout must compare shaped assist history");
}

void test_false_mode_exit_requires_bodylock_to_have_started() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 20; ++ms) {
        ScoreFrame acquiring = tracking_frame(ms, {20.0, 0.0});
        acquiring.target_velocity_px_per_second = {100.0, 0.0};
        acquiring.bodylock_mode = false;
        scorer.add_frame(acquiring);
    }
    ScoreFrame bodylock = tracking_frame(20, {10.0, 0.0});
    bodylock.target_velocity_px_per_second = {100.0, 0.0};
    bodylock.bodylock_mode = true;
    scorer.add_frame(bodylock);
    for (int ms = 21; ms < 24; ++ms) {
        ScoreFrame interrupted = tracking_frame(ms, {14.0, 0.0});
        interrupted.target_velocity_px_per_second = {100.0, 0.0};
        interrupted.bodylock_mode = false;
        scorer.add_frame(interrupted);
    }
    const TargetResult result = scorer.finish();
    require(result.false_mode_exit_events == 1,
            "only post-BodyLock exit must count as interruption");
    require(result.interruption_total_ms == 3,
            "pre-BodyLock ADS time must not count as interruption duration");
}

void test_small_center_cross_is_visible_without_severe_overshoot() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    const double errors[] = {6.0, 3.0, 1.0, -2.0, -2.5, -2.0};
    for (int ms = 0; ms < 6; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {errors[ms], 0.0});
        frame.radial_closing_velocity_px_per_sec = 120.0;
        frame.shaped_assist_stick = {0.30, 0.0};
        frame.final_stick = {0.30, 0.0};
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.center_cross_events == 1,
            "a noise-qualified small crossing must remain observable");
    require(result.over_events == 0,
            "a 2.5px crossing must not become a severe overshoot");
    require(result.max_post_cross_error_px >= 2.5,
            "post-cross excursion must retain amplitude");
    require(result.overshoot_area_px_ms > 0.0,
            "post-cross excursion must accumulate area");
    require(result.continued_push_after_cross_ms > 0,
            "AI continuing the approach after crossing must be attributed");
    require(result.post_cross_wrong_way_output_integral > 0.0,
            "wrong-way delivered output must accumulate after crossing");
    require(result.post_cross_error_area_px_ms > 0.0,
            "V1 post-cross error area must be exported explicitly");
}

void test_vertical_cross_exports_v1_overshoot_metrics() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    const double errors[] = {-6.0, -3.0, -1.0, 2.0, 3.5, 4.0};
    for (int ms = 0; ms < 6; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {0.0, errors[ms]});
        frame.radial_closing_velocity_px_per_sec = 120.0;
        frame.final_stick = {0.0, 0.4};
        frame.shaped_assist_stick = frame.final_stick;
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.maximum_vertical_overshoot_px >= 4.0,
            "vertical post-cross amplitude must be exported explicitly");
    require(result.post_cross_wrong_way_output_integral > 0.0,
            "vertical wrong-way delivered output must be accumulated");
}

void test_fast_no_cross_capture_settles_without_false_overshoot() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 55; ++ms) {
        const double error = ms < 10 ? 12.0 - ms : 2.0;
        ScoreFrame frame = tracking_frame(ms, {error, 0.0});
        frame.radial_closing_velocity_px_per_sec = ms < 10 ? 100.0 : 0.0;
        frame.shaped_assist_stick = ms < 10 ? Vec2d{0.20, 0.0} : Vec2d{};
        frame.final_stick = frame.shaped_assist_stick;
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.center_cross_events == 0, "no-cross capture must stay no-cross");
    require(result.settled, "40ms inside the settle band must settle");
    require(result.first_entry_to_settle_ms >= 40,
            "settle latency must include the stability hold");
}

void test_stall_ring_and_handoff_diagnostics_are_exported() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    for (int ms = 0; ms < 50; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {15.0, 0.0});
        frame.radial_closing_velocity_px_per_sec = 0.0;
        frame.ads_to_bodylock_transition = ms == 5;
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(result.stall_ring_ms >= 49,
            "non-closing 10-20px residence must be exported");
    require_near(result.handoff_residual_px, 15.0, 1e-9,
                 "handoff residual must use transition frame");
    require_near(result.handoff_closing_speed_px_per_sec, 0.0, 1e-9,
                 "handoff closing speed must use transition frame");
    require(!result.settled && result.first_entry_to_settle_ms == -1,
            "a stalled target must remain explicitly unsettled");
}

void test_ads_bodylock_handoff_windows_measure_rebound_and_wrong_way_output() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);

    for (int ms = 0; ms < 10; ++ms) {
        ScoreFrame ads = tracking_frame(ms, {20.0 - ms, 0.0});
        ads.bodylock_mode = false;
        ads.final_stick = {0.3, 0.0};
        scorer.add_frame(ads);
    }
    for (int elapsed = 0; elapsed < 1'000; ++elapsed) {
        const int ms = 10 + elapsed;
        double error_px = 12.0;
        if (elapsed <= 10) {
            error_px = 12.0 - elapsed;
        } else if (elapsed <= 30) {
            error_px = 2.0 + (elapsed - 10) * 0.5;
        }
        ScoreFrame bodylock = tracking_frame(ms, {error_px, 0.0});
        bodylock.ads_to_bodylock_transition = elapsed == 0;
        bodylock.predicted_terminal_error_px =
            elapsed == 0 ? Vec2d{-2.0, 0.0} : Vec2d{error_px, 0.0};
        bodylock.manual_stick = elapsed < 16
            ? Vec2d{-0.25, 0.0}
            : Vec2d{};
        bodylock.requested_assist_stick = elapsed < 12
            ? Vec2d{-0.15, 0.0}
            : Vec2d{0.05, 0.0};
        bodylock.shaped_assist_stick = elapsed < 8
            ? Vec2d{-0.18, 0.0}
            : Vec2d{0.05, 0.0};
        bodylock.final_stick = elapsed < 20
            ? Vec2d{-0.20, 0.0}
            : Vec2d{0.05, 0.0};
        scorer.add_frame(bodylock);
    }

    const TargetResult result = scorer.finish();
    require(result.handoff_observed,
            "ADS->BodyLock handoff must be recorded");
    require(result.post_handoff_local_samples == 160,
            "local handoff window must contain exactly 160 one-ms samples");
    require(result.post_handoff_tail_samples == 840,
            "tail window must cover ms 160 through 999");
    require(result.post_handoff_wrong_way_ms == 20,
            "away-directed delivered output must be counted");
    require(result.handoff_predicted_crossing,
            "transition frame must preserve predicted center crossing");
    require_near(result.handoff_manual_radial, -0.25, 1e-9,
                 "handoff must preserve manual radial contribution");
    require_near(result.handoff_requested_ai_radial, -0.15, 1e-9,
                 "handoff must preserve requested AI radial contribution");
    require_near(result.handoff_shaped_ai_radial, -0.18, 1e-9,
                 "handoff must preserve shaped AI radial contribution");
    require_near(result.handoff_final_radial, -0.20, 1e-9,
                 "handoff must preserve final radial contribution");
    require(result.post_handoff_manual_wrong_way_ms == 16,
            "manual wrong-way duration must be separated");
    require(result.post_handoff_requested_ai_wrong_way_ms == 12,
            "requested AI wrong-way duration must be separated");
    require(result.post_handoff_shaped_ai_wrong_way_ms == 8,
            "shaped AI wrong-way duration must be separated");
    require(result.post_handoff_rebound_px >= 10.0,
            "error growth after a lower minimum must be measured");
    require(result.handoff_defect,
            "sustained wrong-way output or an 8px rebound must classify a defect");
}

void test_bodylock_from_spawn_is_not_a_handoff_episode() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(0);
    for (int ms = 0; ms < 1'000; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {8.0, 0.0});
        frame.final_stick = {0.05, 0.0};
        scorer.add_frame(frame);
    }
    const TargetResult result = scorer.finish();
    require(!result.handoff_observed,
            "BodyLock active from spawn is not an ADS handoff");
    require(result.post_handoff_local_samples == 0,
            "non-handoff targets must not populate the local window");
    require(result.post_handoff_tail_samples == 0,
            "non-handoff targets must not populate the tail window");
    require(!result.handoff_defect,
            "non-handoff targets cannot be classified as handoff defects");
}

void test_settle_ends_the_frozen_approach_axis() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(20);
    ScoreFrame approach = tracking_frame(0, {6.0, 0.0});
    approach.radial_closing_velocity_px_per_sec = 100.0;
    scorer.add_frame(approach);
    for (int ms = 1; ms <= 40; ++ms) {
        ScoreFrame settled = tracking_frame(ms, {0.0, 0.0});
        settled.radial_closing_velocity_px_per_sec = 0.0;
        scorer.add_frame(settled);
    }
    ScoreFrame later_motion = tracking_frame(41, {-10.0, 0.0});
    later_motion.radial_closing_velocity_px_per_sec = 80.0;
    scorer.add_frame(later_motion);
    const TargetResult result = scorer.finish();
    require(result.settled, "fixture must settle its first brake episode");
    require(result.center_cross_events == 0,
            "post-settle motion must start a new axis, not cross the old one");
    require(result.max_post_cross_error_px == 0.0,
            "post-settle motion must not inflate the old excursion");
}

void test_manual_and_unreliable_crossings_are_geometry_not_ai_blame() {
    for (int variant = 0; variant < 2; ++variant) {
        TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
        scorer.mark_acquired(20);
        ScoreFrame before = tracking_frame(0, {6.0, 0.0});
        before.shaped_assist_stick = {0.3, 0.0};
        scorer.add_frame(before);
        ScoreFrame crossed = tracking_frame(1, {-3.0, 0.0});
        crossed.shaped_assist_stick = {0.3, 0.0};
        crossed.manual_escape = variant == 0;
        crossed.tracker_reliable = variant != 1;
        scorer.add_frame(crossed);
        const TargetResult result = scorer.finish();
        require(result.center_cross_events == 1,
                "crossing geometry must remain visible");
        require(result.continued_push_after_cross_ms == 0,
                "manual or unreliable crossing must not blame AI");
    }
}

void test_bodylock_occupancy_distinguishes_never_entered_from_interrupted() {
    TargetScorer never_entered(target_with_deadline(), BenchmarkConfig{});
    never_entered.mark_acquired(20);
    for (int ms = 0; ms < 1'000; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {10.0, 0.0});
        frame.bodylock_mode = false;
        never_entered.add_frame(frame);
    }
    const TargetResult missing = never_entered.finish();
    require(missing.bodylock_entry_failed,
            "never entering BodyLock must be an entry failure");
    require(missing.bodylock_active_ms == 0,
            "ADS frames must not count as BodyLock-active time");
    require(missing.unexpected_mode_ms == 1'000,
            "all pre-entry tracking time must remain observable");

    TargetScorer interrupted(target_with_deadline(), BenchmarkConfig{});
    interrupted.mark_acquired(20);
    for (int ms = 0; ms < 1'000; ++ms) {
        ScoreFrame frame = tracking_frame(ms, {10.0, 0.0});
        frame.bodylock_mode = ms < 800;
        interrupted.add_frame(frame);
    }
    const TargetResult occupied = interrupted.finish();
    require(!occupied.bodylock_entry_failed,
            "a confirmed BodyLock interval must clear entry failure");
    require(occupied.bodylock_entry_ms == 0,
            "first BodyLock frame must record entry time");
    require(occupied.bodylock_active_ms == 800,
            "BodyLock active milliseconds must be exact");
    require(occupied.unexpected_mode_ms == 200,
            "post-entry fallback milliseconds must be exact");
}

void test_aggregate_preserves_additive_totals_and_percentiles() {
    std::vector<TargetResult> targets;
    targets.push_back(score_constant_error(0.0));
    targets.push_back(score_constant_error(12.0));
    const BenchmarkResult result = aggregate(1337, 99, targets);
    require(result.seed == 1337 && result.script_hash == 99,
            "aggregate identity");
    require(result.targets_spawned == 2 && result.targets_acquired == 2,
            "aggregate counts");
    require(result.acquire_points > 0.0 && result.tracking_points > 0.0,
            "aggregate additive points");
    require(result.p95_error_px >= result.mean_error_px,
            "p95 error must not be below mean in this fixture");
    require(result.settled_targets == 1 && result.unsettled_targets == 1,
            "only the center-band fixture must aggregate as settled");
}

void test_aggregate_counts_only_true_handoff_episodes() {
    TargetResult clean;
    clean.handoff_observed = true;
    clean.post_handoff_rebound_px = 0.0;
    clean.post_handoff_wrong_way_output_integral = 1.0;
    clean.post_handoff_local_error_area_px_ms = 100.0;
    clean.post_handoff_tail_error_area_px_ms = 200.0;

    TargetResult defect;
    defect.handoff_observed = true;
    defect.handoff_defect = true;
    defect.post_handoff_rebound_px = 8.0;
    defect.post_handoff_wrong_way_output_integral = 3.0;
    defect.post_handoff_local_error_area_px_ms = 300.0;
    defect.post_handoff_tail_error_area_px_ms = 400.0;

    TargetResult not_a_handoff;
    not_a_handoff.handoff_defect = true;
    not_a_handoff.post_handoff_rebound_px = 100.0;

    const BenchmarkResult run = aggregate(
        1337, 99, {clean, defect, not_a_handoff});
    require(run.handoff_episodes == 2,
            "only true transitions are eligible");
    require(run.handoff_defect_episodes == 1,
            "one eligible episode is defective");
    require_near(run.handoff_defect_rate, 0.5, 1e-9,
                 "defect rate must use eligible handoffs");
    require_near(run.post_handoff_local_error_area_px_ms, 400.0, 1e-9,
                 "local burden must sum eligible handoffs");
    require_near(run.post_handoff_tail_error_area_px_ms, 600.0, 1e-9,
                 "tail burden must sum eligible handoffs");
    require_near(run.p50_post_handoff_rebound_px, 4.0, 1e-9,
                 "median rebound must be retained");
    require_near(run.p95_post_handoff_rebound_px, 7.6, 1e-9,
                 "P95 rebound must be interpolated");
    require_near(run.max_post_handoff_rebound_px, 8.0, 1e-9,
                 "worst rebound must be retained");
    require_near(run.p95_post_handoff_wrong_way_output_integral, 2.9, 1e-9,
                 "wrong-way output P95 must be interpolated");
}

void test_short_occlusion_scores_reveal_recovery_window() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(0);

    for (int ms = 0; ms < 24; ++ms) {
        ScoreFrame hidden = tracking_frame(ms, {18.0, 0.0});
        hidden.vision_occluded = true;
        hidden.fresh_vision = false;
        scorer.add_frame(hidden);
    }
    for (int ms = 24; ms < 120; ++ms) {
        const int reveal_elapsed = ms - 24;
        ScoreFrame visible = tracking_frame(
            ms, {std::max(2.0, 18.0 - reveal_elapsed), 0.0});
        visible.fresh_vision = ms == 24;
        scorer.add_frame(visible);
    }

    const TargetResult result = scorer.finish();
    require(result.occlusion_episodes == 1,
            "one contiguous hidden interval must count as one episode");
    require(result.post_occlusion_samples == 80,
            "reveal burden must use an exact 80ms window");
    require_near(result.max_post_occlusion_error_px, 18.0, 1e-9,
                 "reveal maximum must start at the first fresh frame");
    require(result.post_occlusion_error_area_px_ms > 0.0,
            "reveal error area must accumulate");
    require(result.reveal_to_stable_ms == 21,
            "12 stable ticks must complete 21ms after fresh reveal");

    const BenchmarkResult run = aggregate(1337, 99, {result});
    require(run.occlusion_episodes == 1 &&
                run.post_occlusion_samples == 80,
            "aggregate must retain occlusion episode and sample counts");
    require_near(run.post_occlusion_error_area_px_ms,
                 result.post_occlusion_error_area_px_ms, 1e-9,
                 "aggregate must retain reveal error burden");
    require_near(run.p95_reveal_to_stable_ms, 21.0, 1e-9,
                 "aggregate must retain reveal recovery latency");
}

void test_occlusion_without_fresh_reveal_does_not_start_recovery_window() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(0);

    for (int ms = 0; ms < 24; ++ms) {
        ScoreFrame hidden = tracking_frame(ms, {18.0, 0.0});
        hidden.vision_occluded = true;
        scorer.add_frame(hidden);
    }
    for (int ms = 24; ms < 80; ++ms) {
        ScoreFrame stale = tracking_frame(ms, {4.0, 0.0});
        stale.fresh_vision = false;
        scorer.add_frame(stale);
    }

    const TargetResult result = scorer.finish();
    require(result.occlusion_episodes == 1,
            "hidden interval remains observable without a reveal");
    require(result.post_occlusion_samples == 0,
            "stale carried observations must not start reveal scoring");
    require(result.reveal_to_stable_ms == -1,
            "recovery latency needs a fresh reveal");
}

void test_direction_discontinuity_attribution_keeps_pipeline_stage() {
    TargetScorer scorer(target_with_deadline(), BenchmarkConfig{});
    scorer.mark_acquired(0);
    ScoreFrame before = tracking_frame(0, {8.0, 0.0});
    before.manual_stick = {0.4, 0.0};
    before.requested_assist_stick = {0.4, 0.0};
    before.shaped_assist_stick = {0.4, 0.0};
    before.final_stick = {0.4, 0.0};
    scorer.add_frame(before);

    ScoreFrame hidden = tracking_frame(1, {8.0, 0.0});
    hidden.vision_occluded = true;
    hidden.manual_stick = {-0.4, 0.0};
    hidden.requested_assist_stick = {-0.4, 0.0};
    hidden.shaped_assist_stick = {-0.4, 0.0};
    hidden.final_stick = {-0.4, 0.0};
    scorer.add_frame(hidden);

    ScoreFrame reveal = tracking_frame(2, {8.0, 0.0});
    reveal.fresh_vision = true;
    reveal.manual_stick = {-0.4, 0.0};
    reveal.requested_assist_stick = {-0.4, 0.0};
    reveal.shaped_assist_stick = {-0.4, 0.0};
    reveal.final_stick = {0.4, 0.0};
    scorer.add_frame(reveal);

    const TargetResult result = scorer.finish();
    require(result.direction_discontinuities == 2,
            "fixture must contain two delivered discontinuities");
    require(result.occluded_direction_discontinuities == 1,
            "hidden tick must retain its attribution");
    require(result.fresh_vision_direction_discontinuities == 1,
            "fresh reveal tick must retain its attribution");
    require(result.manual_input_discontinuities == 1,
            "manual stage must be measured independently");
    require(result.manual_driven_final_discontinuities == 1,
            "delivered jumps coincident with physical jumps need attribution");
    require(result.controller_residual_discontinuities == 1,
            "only controller-added residual jumps count as controller twitch");
    require(result.controller_residual_kick_events == 1,
            "material controller residual kick must remain observable");
    require(result.requested_assist_discontinuities == 1,
            "requested assist stage must be measured independently");
    require(result.shaped_assist_discontinuities == 1,
            "shaped assist stage must be measured independently");
}

}  // namespace

int main() {
    try {
        test_acquisition_points_reward_earlier_entry();
        test_tracking_points_reward_center_proximity();
        test_smooth_zero_output_cannot_beat_useful_tracking();
        test_one_overshoot_trace_counts_once();
        test_small_center_cross_is_visible_without_severe_overshoot();
        test_vertical_cross_exports_v1_overshoot_metrics();
        test_fast_no_cross_capture_settles_without_false_overshoot();
        test_stall_ring_and_handoff_diagnostics_are_exported();
        test_ads_bodylock_handoff_windows_measure_rebound_and_wrong_way_output();
        test_bodylock_from_spawn_is_not_a_handoff_episode();
        test_settle_ends_the_frozen_approach_axis();
        test_manual_and_unreliable_crossings_are_geometry_not_ai_blame();
        test_sustained_projected_lag_counts_one_undertrack();
        test_false_stop_requires_stable_demand_and_no_escape();
        test_stale_output_after_target_loss_counts_once();
        test_assist_dropout_uses_shaped_assist_not_final_manual_mix();
        test_false_mode_exit_requires_bodylock_to_have_started();
        test_bodylock_occupancy_distinguishes_never_entered_from_interrupted();
        test_aggregate_preserves_additive_totals_and_percentiles();
        test_aggregate_counts_only_true_handoff_episodes();
        test_short_occlusion_scores_reveal_recovery_window();
        test_occlusion_without_fresh_reveal_does_not_start_recovery_window();
        test_direction_discontinuity_attribution_keeps_pipeline_stage();
        std::cout << "cod_native_sustained_aimlab_score_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_score_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
