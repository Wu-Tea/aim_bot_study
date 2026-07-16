#include "io_recovery_policy.h"
#include "native_gamepad_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

controller_native::ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    std::uint64_t observation_id,
    double now) {
    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selected_observation_id = observation_id;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.dx = 0.0f;
    snapshot.state.dy = -100.0f;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_x = 320.0f;
    snapshot.state.target_y = 156.0f;
    snapshot.state.target_tier = "observed_strong";
    snapshot.state.observed_at_seconds = now;

    tracking_native::TrackerDetection detection;
    detection.id = observation_id;
    detection.body_box_px = {285.0f, 110.0f, 70.0f, 180.0f};
    detection.aim_point_px = {320.0f, 156.0f};
    detection.has_aim_point = true;
    detection.confidence = 0.95f;
    detection.target_tier = "observed_strong";
    snapshot.tracker_detections.push_back(detection);
    return snapshot;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path output_path =
            "native_live_failure_benchmark.json";
        for (int index = 1; index < argc; ++index) {
            const std::string arg = argv[index];
            if (arg == "--output" && index + 1 < argc) {
                output_path = argv[++index];
            }
        }

        double now = 80.000;
        controller_native::GamepadRuntimeConfig config;
        config.recoil.enabled = false;
        config.ai_aim.max_pixels = 100.0f;
        config.ai_aim.max_ai_force = 1.0f;
        config.ai_aim.max_ai_force_y = 1.0f;
        config.ai_aim.target_max_age_ms = 96.0f;
        config.ai_aim.target_projection_max_age_ms = 96.0f;
        config.aim_assist_dynamics.enabled = false;
        controller_native::NativeGamepadController controller(
            config, [&now]() { return now; });

        controller_native::PhysicalGamepadState physical;
        physical.connected = true;
        physical.left_trigger = 1.0f;
        physical.right_y = 0.22f;

        controller.submit_vision_snapshot(observed_snapshot(800, 8001, now));
        now += 0.010;
        controller.submit_vision_snapshot(observed_snapshot(801, 8011, now));
        controller.build_output(physical);
        const float observed_ai_y =
            std::fabs(controller.last_output_components().ai_aim_stick.y);

        now += 0.010;
        controller_native::ControllerVisionSnapshot miss;
        miss.frame_updated = true;
        miss.selector_identity_protocol = true;
        miss.frame_id = 802;
        miss.capture_time_seconds = now;
        miss.ready_time_seconds = now;
        miss.state.screen_center_x = 320.0f;
        miss.state.screen_center_y = 256.0f;
        controller.submit_vision_snapshot(miss);

        int continuity_frames = 0;
        float tracker_only_ai_y_peak = 0.0f;
        float manual_error_peak = 0.0f;
        float continuity_ai_y_first = -1.0f;
        float continuity_ai_y_last = 0.0f;
        float continuity_max_tick_delta = 0.0f;
        float previous_ai_y = 0.0f;
        bool has_previous_ai_y = false;
        int continuity_reversals = 0;
        for (int tick = 0; tick < 50; ++tick) {
            now += 0.001;
            const controller_native::GamepadOutputState output =
                controller.build_output(physical);
            const auto& state = controller.last_frame_vision_state();
            if (state.assist_authority_state ==
                pipeline_contract::AssistAuthorityState::Continuity) {
                ++continuity_frames;
            }
            tracker_only_ai_y_peak = std::max(
                tracker_only_ai_y_peak,
                std::fabs(controller.last_output_components().ai_aim_stick.y));
            const float ai_y = controller.last_output_components().ai_aim_stick.y;
            if (continuity_ai_y_first < 0.0f) continuity_ai_y_first = std::fabs(ai_y);
            continuity_ai_y_last = std::fabs(ai_y);
            if (has_previous_ai_y) {
                continuity_max_tick_delta = std::max(
                    continuity_max_tick_delta, std::fabs(ai_y - previous_ai_y));
                if (ai_y * previous_ai_y < 0.0f) ++continuity_reversals;
            }
            previous_ai_y = ai_y;
            has_previous_ai_y = true;
            manual_error_peak = std::max(
                manual_error_peak,
                std::fabs(output.right_y - physical.right_y));
        }

        const std::vector<controller_native::SdlJoystickDevice> reconnect_devices{
            {0, "Xbox 360 Controller", 6, 15, 1, true},
            {1, "DualSense Wireless Controller", 6, 15, 0, true},
        };
        const int recovered_index = controller_native::select_sdl_reconnect_device(
            reconnect_devices, "DualSense Wireless Controller", 6, 15);
        const int virtual_only_index = controller_native::select_sdl_reconnect_device(
            reconnect_devices, "Missing Physical Controller", 6, 15);

        const bool passed = observed_ai_y > 0.05f && continuity_frames > 0 &&
            tracker_only_ai_y_peak <= observed_ai_y + 0.001f &&
            continuity_ai_y_last < continuity_ai_y_first &&
            continuity_max_tick_delta <= 0.03f && continuity_reversals == 0 &&
            recovered_index == 1 && virtual_only_index == -1;
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output(output_path, std::ios::trunc);
        output << "{\n"
               << "  \"observed_ai_y\": " << observed_ai_y << ",\n"
               << "  \"tracker_continuity_frames\": " << continuity_frames << ",\n"
               << "  \"tracker_only_ai_y_peak\": " << tracker_only_ai_y_peak << ",\n"
               << "  \"tracker_only_manual_error_peak\": " << manual_error_peak << ",\n"
               << "  \"continuity_ai_y_first\": " << continuity_ai_y_first << ",\n"
               << "  \"continuity_ai_y_last\": " << continuity_ai_y_last << ",\n"
               << "  \"continuity_max_tick_delta\": " << continuity_max_tick_delta << ",\n"
               << "  \"continuity_reversals\": " << continuity_reversals << ",\n"
               << "  \"recovered_physical_index\": " << recovered_index << ",\n"
               << "  \"virtual_only_selection\": " << virtual_only_index << ",\n"
               << "  \"passed\": " << (passed ? "true" : "false") << "\n"
               << "}\n";
        output.close();
        std::cout << "[NativeLiveFailureBenchmark] observed_ai_y=" << observed_ai_y
                  << " continuity_frames=" << continuity_frames
                  << " tracker_only_ai_y_peak=" << tracker_only_ai_y_peak
                  << " manual_error_peak=" << manual_error_peak
                  << " continuity_last=" << continuity_ai_y_last
                  << " max_tick_delta=" << continuity_max_tick_delta
                  << " recovered_index=" << recovered_index
                  << " passed=" << (passed ? 1 : 0) << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[NativeLiveFailureBenchmark][FAIL] " << error.what() << '\n';
        return 1;
    }
}
