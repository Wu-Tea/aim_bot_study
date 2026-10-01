#include "aim_response_curve_plugin.h"
#include "ds4_output_report.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
struct Result {
    float maximum_pre_recoil_y = 0;
    float maximum_error = 0;
    float maximum_tracking_error = 0;
    int firing_bodylock_ticks = 0;
    int motion_observations = 0;
    bool identity_preserved = true;
    bool release_passthrough = true;
};

// This is an assumed, compensated linear plant, not a replay of Apex ALC.
// Its independent weapon kick cancels the configured recoil contribution.
// A stationary target must therefore require no additional BodyLock pull.
Result run(float recoil_amount, float target_rate_x, float target_rate_y,
           int duration_ms, int vision_ms, unsigned seed,
           bool ds4_receipt = false,
           controller_native::AimResponseCurveAlgorithm curve =
               controller_native::AimResponseCurveAlgorithm::Linear) {
    using namespace controller_native;
    auto config = incident_fixture::base_config(75, 180);
    config.ai_aim.aim_response_initial_scale = 500;
    config.ai_aim.aim_response_learning_enabled = false;
    config.ai_aim.aim_response_effect_delay_ms = 0;
    config.ai_aim.ads_completion_fresh_frames = 2;
    config.ai_aim.visual_authority_enabled = false;
    config.ai_aim.body_lock_max_ai_force = .6f;
    config.ai_aim.body_lock_max_ai_force_y = .6f;
    config.recoil.enabled = recoil_amount > 0;
    config.recoil.feedback_min_amount = 0;
    config.recoil.feedback_max_amount = .34f;
    config.recoil.feedback_amount = recoil_amount;
    config.auto_fire.manual_fire_input = "RT";
    config.aim_response_curve.algorithm = curve;
    double now = 10;
    NativeGamepadController controller(config, &now);
    incident_fixture::TargetSpec spec;
    spec.observation_id = 61001;
    spec.selector_generation = 101;
    spec.enemy_identity_confirmed = spec.has_enemy_cue = true;
    std::mt19937 rng(seed);
    float x = 0, y = 0;
    std::uint64_t identity = 0, frame = 1;
    Result result;
    for (int tick = 0; tick < duration_ms; ++tick) {
        const bool firing = tick >= 200;
        const bool moving = tick >= 80;
        // Independent deterministic freshness gaps, including high-rate input.
        if (tick % vision_ms == 0 && (tick < 200 || rng() % 13 != 0)) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec, frame++, now, x, y, tick == 0));
        }
        auto output = controller.build_output(incident_fixture::ads_input(0, 0, firing));
        if (ds4_receipt) {
            const auto report = to_ds4_report(output);
            output.right_x = ds4_axis_value(report.bytes[2]);
            output.right_y = -ds4_axis_value(report.bytes[3]);
            // Replace the facade receipt at the same timestamp with the actual
            // byte-decoded delivered command, as the runtime publisher does.
            controller.observe_delivered_output(output, true, now);
        }
        const auto& plan = controller.last_target_plan();
        if (!identity && plan.target_id) identity = plan.target_id;
        result.identity_preserved &= plan.target_id == identity && identity != 0;
        if (firing && plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
            ++result.firing_bodylock_ticks;
            result.motion_observations += plan.bodylock_target_motion_valid;
            if (target_rate_x == 0 && target_rate_y == 0) {
                result.maximum_pre_recoil_y = std::max(result.maximum_pre_recoil_y,
                    std::fabs(controller.last_output_components().before_recoil_stick.y));
                result.maximum_error = std::max(result.maximum_error, std::hypot(x, y));
            }
            result.maximum_tracking_error = std::max(result.maximum_tracking_error, std::hypot(x, y));
        }
        const auto response = forward_aim_response_curve(
            {output.right_x, output.right_y}, config.aim_response_curve);
        const auto kick = forward_aim_response_curve(
            {0, -recoil_amount}, config.aim_response_curve);
        x += ((moving ? target_rate_x : 0) - 500 * response.x) * .001f;
        y += ((moving ? target_rate_y : 0) + 500 * (response.y - (firing ? kick.y : 0))) * .001f;
        now += .001;
    }
    auto manual = incident_fixture::ads_input(.031f, -.043f);
    manual.left_trigger = 0;
    const auto released = controller.build_output(manual);
    result.release_passthrough = released.right_x == manual.right_x && released.right_y == manual.right_y;
    return result;
}

void recoil_is_not_target_motion(const native_test::TestContext& context) {
    const auto firing = run(.20f, 0, 0, 1200, 5, 20261001);
    const auto no_recoil = run(0, 0, 0, 1200, 5, 20261001);
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream out(context.artifact_path("recoil_feedback.json"));
    out << "{\"plant_source\":\"assumed compensated linear plant, not Apex replay\","
        << "\"firing_bodylock_ticks\":" << firing.firing_bodylock_ticks
        << ",\"motion_observations\":" << firing.motion_observations
        << ",\"maximum_pre_recoil_y\":" << firing.maximum_pre_recoil_y
        << ",\"maximum_error_px\":" << firing.maximum_error
        << ",\"no_recoil_pre_y\":" << no_recoil.maximum_pre_recoil_y
        << ",\"identity_preserved\":" << firing.identity_preserved
        << ",\"release_passthrough\":" << firing.release_passthrough << "}\n";
    out.close();
    if (firing.firing_bodylock_ticks < 900 || firing.motion_observations < 850 ||
        !firing.identity_preserved || !firing.release_passthrough || no_recoil.maximum_error > .001f)
        native_test::invalid_fixture("recoil trigger or no-recoil control missing");
    if (firing.maximum_pre_recoil_y > .02f || firing.maximum_error > 1)
        throw std::runtime_error("compensated weapon recoil recursively becomes BodyLock pull");
}

void randomized_firing_tracking(const native_test::TestContext& context) {
    unsigned cases = 0;
    float maximum_error = 0;
    unsigned failed = 0;
    std::vector<float> per_case_error;
    for (unsigned seed : {20261001u, 8675309u}) {
        std::mt19937 rng(seed);
        for (int index = 0; index < 256; ++index) {
            const float amount = .14f + (rng() % 201) * .001f;
            const float vx = (static_cast<int>(rng() % 241) - 120);
            const float vy = (static_cast<int>(rng() % 121) - 60);
            const int vision = 2 + rng() % 9;
            for (int duration : {600, 3000}) {
                const auto result = run(amount, vx, vy, duration, vision, rng());
                maximum_error = std::max(maximum_error, result.maximum_tracking_error);
                per_case_error.push_back(result.maximum_tracking_error);
                failed += !result.identity_preserved || !result.release_passthrough ||
                    result.firing_bodylock_ticks < duration - 205 || result.maximum_tracking_error > 8;
                ++cases;
            }
        }
    }
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream out(context.artifact_path("randomized_firing_tracking.json"));
    out << "{\"cases\":" << cases << ",\"failed\":" << failed
        << ",\"maximum_tracking_error_px\":" << maximum_error
        << ",\"seeds\":[20261001,8675309],\"plant_source\":\"assumed compensated linear plant\",\"per_case_error_px\":[";
    for (unsigned index = 0; index < per_case_error.size(); ++index) {
        if (index) out << ',';
        out << per_case_error[index];
    }
    out << "]}\n";
    out.close();
    if (failed) throw std::runtime_error("firing motion tracking or manual/identity constraint failed");
}

void delivered_recoil_coordinates(const native_test::TestContext& context) {
    float maximum_pull = 0, maximum_error = 0;
    for (auto curve : {controller_native::AimResponseCurveAlgorithm::Linear,
                       controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
        const auto result = run(.20f, 0, 0, 1200, 5, 20261001, true, curve);
        maximum_pull = std::max(maximum_pull, result.maximum_pre_recoil_y);
        maximum_error = std::max(maximum_error, result.maximum_error);
        if (result.firing_bodylock_ticks < 900 || !result.identity_preserved)
            native_test::invalid_fixture("byte-decoded firing trigger missing");
    }
    const auto moving = run(.20f, 100, -50, 3000, 5, 8675309, true);
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream out(context.artifact_path("delivered_recoil_coordinates.json"));
    out << "{\"maximum_stationary_pre_y\":" << maximum_pull
        << ",\"maximum_stationary_error_px\":" << maximum_error
        << ",\"moving_error_px\":" << moving.maximum_tracking_error
        << ",\"curves\":[\"linear\",\"cod_dynamic_legacy_lut\"],\"receipt\":\"DS4 byte decoded\"}\n";
    out.close();
    if (maximum_pull > .02f || maximum_error > 1 || moving.maximum_tracking_error > 8 ||
        !moving.release_passthrough || !moving.identity_preserved)
        throw std::runtime_error("delivered recoil decomposition corrupts camera coordinates");
}
}

void register_bodylock_recoil_feedback_incident_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock", "incident_recoil_not_target_motion", recoil_is_not_target_motion);
    registry.add_context_case("BaseBodyLock", "randomized_firing_tracking", randomized_firing_tracking);
    registry.add_context_case("BaseBodyLock", "delivered_recoil_coordinates", delivered_recoil_coordinates);
}
