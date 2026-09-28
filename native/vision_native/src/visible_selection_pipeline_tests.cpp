#include "vision_native/target_selector.h"
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <stdexcept>

namespace {
void test_flat_observation_reaches_ads_and_releases_to_raw_input() {
    double now = 1.0;
    controller_native::NativeGamepadController controller(
        controller_native::incident_fixture::base_config(50, 180), &now);
    vision_native::VisionTargetSelector selector(640, 512, 150, true, .35f, .65f);
    vision_native::Detection d;
    d.x1 = 310; d.x2 = 430; d.y1 = 220; d.y2 = 280;
    d.conf = .92f; d.color_classified = true;
    vision_native::DetectionBatch input;
    input.frame_width = 640; input.frame_height = 512;
    input.frame_id = 1; input.captured_at_ns = 995'000'000ull;
    input.detections = {d};
    selector.select(input);
    input.frame_id = 2; input.captured_at_ns = 1'000'000'000ull;
    auto result = selector.select(input);
    if (!result.has_target || !result.aim_authority || result.fire_authority)
        throw std::runtime_error("flat-body setup did not produce aim-only person evidence");
    // The engine, rather than select(), stamps these source-publication fields.
    result.frame_id = input.frame_id; result.captured_at_ns = input.captured_at_ns;
    result.result_at_ns = input.captured_at_ns; result.frame_updated = true;
    controller.submit_vision_snapshot(runtime_app::adapt_vision_result(result));
    const auto output = controller.build_output(controller_native::incident_fixture::ads_input());
    const auto& plan = controller.last_target_plan();
    if (plan.target_id == 0 || plan.mode != pipeline_contract::ControlMode::AdsAcquire ||
        plan.aim_authority <= 0 || plan.fire_authority ||
        !std::isfinite(output.right_x) || output.right_x <= 0)
        throw std::runtime_error("flat-body observation was blocked between selector and ADS output");
    auto manual = controller_native::incident_fixture::ads_input(-.95f, 0.0f);
    // Reuse the existing 160 ms region-exit contract, not a new demand for
    // immediate raw passthrough during ADS. Keep publishing fresh observations
    // so a stale-target timeout cannot accidentally satisfy the escape test.
    auto corrected = output;
    bool escaped = false;
    for (int tick = 0; tick < 32; ++tick) {
        now += .005;
        ++input.frame_id;
        input.captured_at_ns = static_cast<std::uint64_t>(now * 1e9);
        result = selector.select(input);
        result.frame_id = input.frame_id; result.captured_at_ns = input.captured_at_ns;
        result.result_at_ns = input.captured_at_ns; result.frame_updated = true;
        if (!result.has_target) throw std::runtime_error("manual escape lost its fresh target fixture");
        controller.submit_vision_snapshot(runtime_app::adapt_vision_result(result));
        corrected = controller.build_output(manual);
        if (std::isfinite(corrected.right_x) && corrected.right_x <= -.94f) {
            escaped = true;
            break;
        }
    }
    if (!escaped)
        throw std::runtime_error("flat-body ADS manual output: x=" + std::to_string(corrected.right_x) +
            " y=" + std::to_string(corrected.right_y));
    manual.left_trigger = 0; manual.right_x = .012f; manual.right_y = -.012f;
    now += .001;
    const auto released = controller.build_output(manual);
    if (released.right_x != manual.right_x || released.right_y != manual.right_y)
        throw std::runtime_error("flat-body release did not restore raw passthrough");
}
} // namespace

void register_visible_selection_pipeline_tests(native_test::Registry& registry) {
    registry.add_case("BaseEndToEnd", "flat_person_ads_manual_and_release",
        test_flat_observation_reaches_ads_and_releases_to_raw_input);
}
