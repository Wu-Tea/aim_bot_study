#include "incident_fixture_support.h"
#include "ads_acquisition_controller.h"
#include "bodylock_follow_controller.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace {

float axis(pipeline_contract::Vec2f v, int a) { return a == 0 ? v.x : v.y; }
pipeline_contract::Vec2f vector_axis(float v, int a) {
    return a == 0 ? pipeline_contract::Vec2f{v, 0} : pipeline_contract::Vec2f{0, v};
}

void ads_zero_crossing(const native_test::TestContext& context) {
    // Frozen owner-level covariates, not a replay of unlogged FOV/response.
    // A 0.02 px crossing must not create a >0.01 stick impulse. Both signs,
    // axes, close/normal horizons, response beliefs and inverse curves matter.
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("ads-zero-crossing.json"));
    report << std::setprecision(9) << "{\"cases\":[";
    float max_step = 0, max_zero = 0, max_stationary_step = 0;
    int cases = 0;
    for (int a : {0, 1}) for (float sign : {-1.0f, 1.0f})
    for (float response : {300.0f, 500.0f, 900.0f})
    for (float size : {0.1f, 0.42f})
    for (auto curve : {controller_native::AimResponseCurveAlgorithm::Linear,
                       controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
        controller_native::AdsAcquisitionControllerConfig config;
        config.arrival_horizon_seconds = 0.135f;
        config.response_curve.algorithm = curve;
        controller_native::AdsAcquisitionController controller(config);
        pipeline_contract::TargetPlan plan;
        plan.mode = pipeline_contract::ControlMode::AdsAcquire;
        plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
        plan.aim_authority = plan.reliability = plan.response_confidence = 1.0f;
        plan.response_scale = response;
        plan.normalized_size = size;
        plan.error_rate_px_per_sec = vector_axis(sign * 1400.0f, a);
        float values[3]{};
        int index = 0;
        for (float e : {-0.01f, 0.0f, 0.01f}) {
            plan.error_px = vector_axis(e, a);
            values[index++] = axis(controller.compute(plan, {}, 0.001f), a);
        }
        const float step = std::max(std::fabs(values[1] - values[0]),
                                    std::fabs(values[2] - values[1]));
        max_step = std::max(max_step, step);
        max_zero = std::max(max_zero, std::fabs(values[1]));
        plan.error_rate_px_per_sec = {};
        plan.error_px = vector_axis(-0.01f, a);
        const float stationary_before = axis(controller.compute(plan, {}, 0.001f), a);
        plan.error_px = vector_axis(0.01f, a);
        const float stationary_after = axis(controller.compute(plan, {}, 0.001f), a);
        max_stationary_step = std::max(max_stationary_step,
            std::fabs(stationary_after - stationary_before));
        report << (cases++ ? "," : "") << "{\"axis\":" << a
               << ",\"velocity\":" << sign * 1400 << ",\"response\":" << response
               << ",\"size\":" << size << ",\"curve\":" << static_cast<int>(curve)
               << ",\"before\":" << values[0] << ",\"at_zero\":" << values[1]
               << ",\"after\":" << values[2] << ",\"step\":" << step << "}";
    }
    // Counterfactual: actual sustaining target motion in BodyLock remains work
    // even with zero position error. Do not cure ADS by disabling all motion.
    pipeline_contract::TargetPlan moving;
    moving.mode = pipeline_contract::ControlMode::BodyLockFollow;
    moving.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    moving.aim_authority = moving.reliability = moving.response_confidence = 1;
    moving.response_scale = 500;
    moving.bodylock_target_motion_valid = true;
    moving.bodylock_target_motion_px_per_sec = {100, -100};
    const auto follow = controller_native::BodylockFollowController().compute(moving, {}, .001f);
    const bool controls = max_stationary_step < .01f &&
        std::fabs(follow.x - .2f) < 1e-5f && std::fabs(follow.y - .2f) < 1e-5f;
    report << "],\"case_count\":" << cases << ",\"max_step\":" << max_step
           << ",\"max_at_zero\":" << max_zero
           << ",\"stationary_max_step\":" << max_stationary_step
           << ",\"bodylock_centered_motion\":[" << follow.x << "," << follow.y
           << "],\"counterfactuals_pass\":" << (controls ? "true" : "false") << "}\n";
    report.close();
    if (cases != 48 || !controls) native_test::invalid_fixture("ADS crossing counterfactual failed");
    if (max_step > .01f || max_zero > 1e-5f)
        throw std::runtime_error("ADS source-error crossing injected a motion impulse");
}

void bodylock_released_carry(const native_test::TestContext& context) {
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("bodylock-released-carry.json"));
    report << std::setprecision(9) << "{\"samples\":[";
    int records = 0, released_samples = 0, held_samples = 0;
    float max_release_loss = 0, max_held_loss = 0;
    bool valid = true;
    for (int a : {0, 1}) for (float sign : {-1.0f, 1.0f})
    for (bool release : {false, true}) {
        auto config = controller_native::incident_fixture::base_config(50, 180);
        config.ai_aim.ads_completion_fresh_frames = 2;
        config.ai_aim.body_lock_activation_box_px = 120;
        config.ai_aim.body_lock_max_ai_force = .60f;
        config.ai_aim.body_lock_max_ai_force_y = .66f;
        config.ai_aim.visual_authority_enabled = true;
        double now = 10;
        controller_native::NativeGamepadController controller(config, &now);
        controller_native::incident_fixture::TargetSpec spec;
        spec.observation_id = 172;
        spec.selector_generation = 8;
        spec.has_enemy_cue = spec.enemy_identity_confirmed = true;
        spec.confidence = 1;
        std::uint64_t target_id = 0;
        for (int tick = 0; tick <= 130; ++tick) {
            now = 10 + tick * .001;
            const float manual = sign * ((release && tick >= 80) ? .01f : .12f);
            const auto m = vector_axis(manual, a);
            auto physical = controller_native::incident_fixture::ads_input(m.x, m.y);
            if (tick >= 5 && tick % 5 == 0) {
                const float distance = tick <= 15 ? 0.0f : std::min(36.0f, (tick - 15) * 1.0f);
                const auto error = vector_axis(-sign * distance * (a == 1 ? -1.0f : 1.0f), a);
                controller.submit_vision_snapshot(controller_native::incident_fixture::observed_snapshot(
                    spec, tick / 5, now, error.x, error.y, tick == 5));
            }
            const auto output = controller.build_output(physical);
            if (tick < 90) continue;
            const auto& plan = controller.last_target_plan();
            const auto& d = controller.last_output_components();
            const float shaped = a == 0 ? d.shaped_assist_stick.x : d.shaped_assist_stick.y;
            const float final = a == 0 ? output.right_x : output.right_y;
            const float filtered = a == 0 ? d.filtered_manual_stick.x : d.filtered_manual_stick.y;
            if (target_id == 0) target_id = plan.target_id;
            valid = valid && target_id != 0 && target_id == plan.target_id &&
                plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
                plan.ads_acquisition_exists && !plan.ads_acquisition_active &&
                plan.aim_authority > .65f && !plan.manual_exit_requested &&
                !plan.manual_correction_x && !plan.manual_correction_y &&
                shaped * manual < 0 && std::fabs(shaped) > .1f;
            if (release) {
                valid = valid && filtered == 0;
                ++released_samples;
                max_release_loss = std::max(max_release_loss, std::fabs(final - shaped));
            } else {
                valid = valid && filtered != 0;
                ++held_samples;
                max_held_loss = std::max(max_held_loss, std::fabs(final - manual));
            }
            report << (records++ ? "," : "") << "{\"axis\":" << a
                   << ",\"tick_ms\":" << tick << ",\"release\":" << (release ? "true" : "false")
                   << ",\"manual\":" << manual << ",\"filtered\":" << filtered
                   << ",\"shaped\":" << shaped << ",\"final\":" << final
                   << ",\"mode\":" << static_cast<int>(plan.mode)
                   << ",\"authority\":" << plan.aim_authority
                   << ",\"correction\":" << (plan.manual_correction_x || plan.manual_correction_y)
                   << "}";
        }
        // No authority after LT release, independent of residual gesture.
        now += .001;
        const auto physical = controller_native::incident_fixture::ads_input(.12f, -.12f);
        auto unscoped = physical;
        unscoped.left_trigger = 0;
        const auto output = controller.build_output(unscoped);
        valid = valid && std::fabs(output.right_x - physical.right_x) < 1e-6f &&
            std::fabs(output.right_y - physical.right_y) < 1e-6f;
    }
    report << "],\"trigger_valid\":" << (valid ? "true" : "false")
           << ",\"released_samples\":" << released_samples
           << ",\"held_samples\":" << held_samples
           << ",\"max_release_loss\":" << max_release_loss
           << ",\"max_held_loss\":" << max_held_loss << "}\n";
    report.close();
    if (!valid || released_samples != 164 || held_samples != 164)
        native_test::invalid_fixture("BodyLock carry/release lifecycle trigger failed");
    if (max_release_loss > .01f || max_held_loss > 1e-5f)
        throw std::runtime_error("BodyLock loses target output after gesture release or reverses held input");
}

}  // namespace

void register_ads_bodylock_september_incidents(native_test::Registry& registry) {
    registry.add_context_case("BaseAds", "september_ads_zero_crossing", ads_zero_crossing);
    registry.add_context_case("BaseEndToEnd", "september_bodylock_released_carry", bodylock_released_carry);
}
