#include "aim_perf_file_logger.h"

#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace runtime_app {

namespace {

constexpr const char* kLogDirectory = "runs/native_perf";
constexpr const char* kLogPrefix = "native_aim_perf_";
constexpr const char* kLogExtension = ".jsonl";

std::string safe_c_string(const char* value, const char* fallback) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

std::string json_string(const std::string& value) {
    std::ostringstream escaped;
    escaped << '"';
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                escaped << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch))
                        << std::dec << std::setfill(' ');
            } else {
                escaped << ch;
            }
            break;
        }
    }
    escaped << '"';
    return escaped.str();
}

std::tm local_time_from(std::time_t value) {
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &value);
#else
    localtime_r(&value, &local_time);
#endif
    return local_time;
}

std::string startup_timestamp_suffix(std::chrono::system_clock::time_point started_at) {
    const std::time_t time_value = std::chrono::system_clock::to_time_t(started_at);
    const std::tm local_time = local_time_from(time_value);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        started_at.time_since_epoch()) % 1000;

    std::ostringstream suffix;
    suffix << std::put_time(&local_time, "%Y%m%d_%H%M%S")
           << '_' << std::setw(3) << std::setfill('0') << millis.count();
    return suffix.str();
}

std::filesystem::path resolved_log_directory(std::filesystem::path log_directory) {
    if (log_directory.empty()) {
        return std::filesystem::path(kLogDirectory);
    }
    return log_directory;
}

std::filesystem::path unique_startup_log_path(
    const std::filesystem::path& directory,
    std::chrono::system_clock::time_point started_at) {
    const std::string timestamp = startup_timestamp_suffix(started_at);
    const std::string base_name = std::string(kLogPrefix) + timestamp;

    std::filesystem::path candidate =
        directory / (base_name + std::string(kLogExtension));
    for (int suffix = 1; std::filesystem::exists(candidate) && suffix < 1000; ++suffix) {
        candidate = directory /
            (base_name + "_" + std::to_string(suffix) + std::string(kLogExtension));
    }
    return candidate;
}

double elapsed_ms(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    if (end <= start) {
        return 0.0;
    }
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double capture_transfer_ms(const vision_native::VisionResult& result) {
    return std::max(0.0f, result.wait_ms - result.capture_acquire_ms);
}

double stick_magnitude(common_native::Vec2f value) {
    return std::hypot(
        static_cast<double>(value.x),
        static_cast<double>(value.y));
}

double stick_dot(common_native::Vec2f lhs, common_native::Vec2f rhs) {
    return (static_cast<double>(lhs.x) * static_cast<double>(rhs.x)) +
        (static_cast<double>(lhs.y) * static_cast<double>(rhs.y));
}

const char* target_authority_state_name(common_native::TargetAuthorityState state) {
    switch (state) {
    case common_native::TargetAuthorityState::StrongAssist:
        return "strong_assist";
    case common_native::TargetAuthorityState::WeakAssist:
        return "weak_assist";
    case common_native::TargetAuthorityState::TrackOnly:
        return "track_only";
    case common_native::TargetAuthorityState::Yield:
        return "yield";
    case common_native::TargetAuthorityState::Reject:
    default:
        return "reject";
    }
}

void write_controller_components(
    std::ofstream& output,
    const controller_native::NativeControllerOutputComponents* components,
    const controller_native::GamepadOutputState* tracker_motion_output) {
    controller_native::NativeControllerOutputComponents zero_components;
    const controller_native::NativeControllerOutputComponents& value =
        components != nullptr ? *components : zero_components;
    controller_native::GamepadOutputState zero_tracker_output;
    const controller_native::GamepadOutputState& tracker =
        tracker_motion_output != nullptr ? *tracker_motion_output : zero_tracker_output;

    output
        << ",\"physical_right_x\":" << value.physical_stick.x
        << ",\"physical_right_y\":" << value.physical_stick.y
        << ",\"manual_pre_ai_x\":" << value.manual_stick.x
        << ",\"manual_pre_ai_y\":" << value.manual_stick.y
        << ",\"manual_x\":" << value.manual_stick.x
        << ",\"manual_y\":" << value.manual_stick.y
        << ",\"ai_aim_x\":" << value.ai_aim_stick.x
        << ",\"ai_aim_y\":" << value.ai_aim_stick.y
        << ",\"post_ai_x\":" << value.post_ai_stick.x
        << ",\"post_ai_y\":" << value.post_ai_stick.y
        << ",\"dynamic_x\":" << value.dynamic_adjustment_stick.x
        << ",\"dynamic_y\":" << value.dynamic_adjustment_stick.y
        << ",\"post_dynamic_x\":" << value.post_dynamic_stick.x
        << ",\"post_dynamic_y\":" << value.post_dynamic_stick.y
        << ",\"ads_brake_x\":" << value.ads_brake_stick.x
        << ",\"ads_brake_y\":" << value.ads_brake_stick.y
        << ",\"post_ads_brake_x\":" << value.post_ads_brake_stick.x
        << ",\"post_ads_brake_y\":" << value.post_ads_brake_stick.y
        << ",\"ads_brake_error_x\":" << value.ads_brake_error_px.x
        << ",\"ads_brake_error_y\":" << value.ads_brake_error_px.y
        << ",\"ads_carry_brake_x\":" << value.ads_carry_brake_stick.x
        << ",\"ads_carry_brake_y\":" << value.ads_carry_brake_stick.y
        << ",\"post_ads_carry_brake_x\":" << value.post_ads_carry_brake_stick.x
        << ",\"post_ads_carry_brake_y\":" << value.post_ads_carry_brake_stick.y
        << ",\"ads_carry_brake_active\":"
        << (value.ads_carry_brake_active ? "true" : "false")
        << ",\"ads_brake_active\":" << (value.ads_brake_active ? "true" : "false")
        << ",\"before_recoil_x\":" << value.before_recoil_stick.x
        << ",\"before_recoil_y\":" << value.before_recoil_stick.y
        << ",\"recoil_x\":" << value.recoil_stick.x
        << ",\"recoil_y\":" << value.recoil_stick.y
        << ",\"final_x\":" << value.final_stick.x
        << ",\"final_y\":" << value.final_stick.y
        << ",\"aim_mode\":" << json_string(value.aim_mode)
        << ",\"tracker_sample_x\":" << tracker.right_x
        << ",\"tracker_sample_y\":" << tracker.right_y
        << ",\"fire_button\":" << (value.fire_button ? "true" : "false");
}

void write_controller_vision_state(
    std::ofstream& output,
    const controller_native::NativeControllerVisionState* state) {
    controller_native::NativeControllerVisionState empty_state;
    const controller_native::NativeControllerVisionState& value =
        state != nullptr ? *state : empty_state;
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            value.has_target,
            value.aim_authority,
            value.fire_authority,
            value.target_tier);
    output
        << ",\"controller_target\":" << (value.has_target ? "true" : "false")
        << ",\"controller_aim_authority\":" << (value.aim_authority ? "true" : "false")
        << ",\"controller_fire_authority\":" << (value.fire_authority ? "true" : "false")
        << ",\"controller_authority_state\":"
        << json_string(target_authority_state_name(authority.target_authority_state))
        << ",\"controller_tier\":" << json_string(value.target_tier)
        << ",\"controller_dx\":" << value.dx
        << ",\"controller_dy\":" << value.dy
        << ",\"controller_projection\":" << (value.has_tracker_projection ? "true" : "false")
        << ",\"controller_tracker_dx\":" << value.tracker_dx
        << ",\"controller_tracker_dy\":" << value.tracker_dy;
}

void write_diagnostics(
    std::ofstream& output,
    const vision_native::VisionResult* result,
    const controller_native::NativeControllerVisionState* state,
    const controller_native::NativeControllerOutputComponents* components) {
    controller_native::NativeControllerVisionState empty_state;
    const controller_native::NativeControllerVisionState& vision_state =
        state != nullptr ? *state : empty_state;
    controller_native::NativeControllerOutputComponents empty_components;
    const controller_native::NativeControllerOutputComponents& sticks =
        components != nullptr ? *components : empty_components;

    const double manual_magnitude = stick_magnitude(sticks.manual_stick);
    const double ai_magnitude = stick_magnitude(sticks.ai_aim_stick);
    const double final_magnitude = stick_magnitude(sticks.final_stick);
    const double target_error_px = vision_state.has_target
        ? std::hypot(
              static_cast<double>(vision_state.dx),
              static_cast<double>(vision_state.dy))
        : (result != nullptr && result->has_target
              ? std::hypot(static_cast<double>(result->dx), static_cast<double>(result->dy))
              : 0.0);
    const double vision_age_ms = result != nullptr ? result->age_ms : 0.0;
    constexpr double kManualFightThreshold = 0.22;
    constexpr double kAiFightThreshold = 0.22;
    constexpr double kFightDotThreshold = -0.05;
    constexpr double kNearTargetPx = 60.0;
    constexpr double kHighOutputThreshold = 0.45;
    constexpr double kStaleTargetMs = 80.0;
    const bool manual_ai_fight =
        manual_magnitude >= kManualFightThreshold &&
        ai_magnitude >= kAiFightThreshold &&
        stick_dot(sticks.manual_stick, sticks.ai_aim_stick) <= kFightDotThreshold;
    const bool manual_final_fight =
        manual_magnitude >= kManualFightThreshold &&
        final_magnitude >= kAiFightThreshold &&
        stick_dot(sticks.manual_stick, sticks.final_stick) <= kFightDotThreshold;
    const bool near_target =
        vision_state.has_target && target_error_px > 0.0 && target_error_px <= kNearTargetPx;
    const bool high_output = final_magnitude >= kHighOutputThreshold;
    const bool stale_target = vision_state.has_target && vision_age_ms >= kStaleTargetMs;
    const bool projected_high_output =
        vision_state.has_tracker_projection && high_output;
    const bool authority_without_fire =
        vision_state.has_target && vision_state.aim_authority && !vision_state.fire_authority;

    output
        << ",\"diagnostic_manual_magnitude\":" << manual_magnitude
        << ",\"diagnostic_ai_magnitude\":" << ai_magnitude
        << ",\"diagnostic_final_magnitude\":" << final_magnitude
        << ",\"diagnostic_target_error_px\":" << target_error_px
        << ",\"diagnostic_manual_ai_fight\":"
        << (manual_ai_fight ? "true" : "false")
        << ",\"diagnostic_manual_final_fight\":"
        << (manual_final_fight ? "true" : "false")
        << ",\"diagnostic_near_target\":" << (near_target ? "true" : "false")
        << ",\"diagnostic_near_high_output\":"
        << (near_target && high_output ? "true" : "false")
        << ",\"diagnostic_stale_target\":"
        << (stale_target ? "true" : "false")
        << ",\"diagnostic_tracker_projection\":"
        << (vision_state.has_tracker_projection ? "true" : "false")
        << ",\"diagnostic_projected_high_output\":"
        << (projected_high_output ? "true" : "false")
        << ",\"diagnostic_authority_without_fire\":"
        << (authority_without_fire ? "true" : "false");
}

}  // namespace

AimPerfFileLogger::AimPerfFileLogger(
    bool enabled,
    std::filesystem::path log_directory,
    unsigned int log_interval_ticks)
    : enabled_(enabled),
      log_interval_ticks_(std::max(1u, log_interval_ticks)),
      started_at_(std::chrono::steady_clock::now()),
      log_directory_(resolved_log_directory(std::move(log_directory))) {
    if (!enabled_) {
        return;
    }

    try {
        std::filesystem::create_directories(log_directory_);
        log_path_ = unique_startup_log_path(log_directory_, std::chrono::system_clock::now());
        output_.open(log_path_, std::ios::out | std::ios::trunc);
        if (!output_.is_open()) {
            enabled_ = false;
            std::cerr << "[Perf][CPP][File][Warn] failed to open aim perf log "
                      << log_path_.string() << '\n';
            return;
        }
        std::cout << "[Perf][CPP][File] aim perf log=" << log_path_.string() << '\n';
    } catch (const std::exception& exc) {
        enabled_ = false;
        std::cerr << "[Perf][CPP][File][Warn] failed to initialize aim perf log: "
                  << exc.what() << '\n';
    }
}

void AimPerfFileLogger::record_aim_sample(
    unsigned int tick_count,
    bool aiming,
    const PerfSnapshot& snapshot,
    const vision_native::VisionResult* result,
    const controller_native::NativeControllerVisionState* controller_vision_state,
    const controller_native::NativeControllerOutputComponents* output_components,
    const controller_native::GamepadOutputState* tracker_motion_output) {
    if (!enabled_ || !aiming || !output_.is_open()) {
        return;
    }
    if (tick_count % log_interval_ticks_ != 0u) {
        return;
    }

    const double relative_ms = elapsed_ms(started_at_, std::chrono::steady_clock::now());
    output_
        << '{'
        << "\"tick\":" << tick_count
        << ",\"relative_ms\":" << relative_ms
        << ",\"aiming\":" << (aiming ? "true" : "false");

    if (result == nullptr) {
        output_
            << ",\"frame_updated\":false"
            << ",\"frame_id\":0"
            << ",\"target\":false"
            << ",\"tier\":\"none\""
            << ",\"source\":\"none\""
            << ",\"stage\":\"none\""
            << ",\"aim_authority\":false"
            << ",\"fire_authority\":false"
            << ",\"service_freshness\":\"none\""
            << ",\"service_source_state\":\"unknown\""
            << ",\"service_sequence\":0"
            << ",\"service_controller_aiming\":false"
            << ",\"service_engine_aiming\":false"
            << ",\"confidence\":0"
            << ",\"dx\":0"
            << ",\"dy\":0"
            << ",\"capture_ms\":0"
            << ",\"copy_ms\":0"
            << ",\"capture_transfer_ms\":0"
            << ",\"cuda_map_ms\":0"
            << ",\"preprocess_mode\":\"none\""
            << ",\"preprocess_ms\":0"
            << ",\"infer_ms\":0"
            << ",\"gpu_total_ms\":0"
            << ",\"output_wait_ms\":0"
            << ",\"decode_ms\":0"
            << ",\"selector_ms\":0"
            << ",\"enhance_ms\":0"
            << ",\"post_ms\":0"
            << ",\"age_ms\":0"
            << ",\"vision_age_ms\":0"
            << ",\"boxes_seen\":0";
    } else {
        output_
            << ",\"frame_updated\":" << (result->frame_updated ? "true" : "false")
            << ",\"frame_id\":" << result->frame_id
            << ",\"target\":" << (result->has_target ? "true" : "false")
            << ",\"tier\":" << json_string(safe_c_string(result->target_tier, "none"))
            << ",\"source\":" << json_string(safe_c_string(result->target_source, "none"))
            << ",\"stage\":" << json_string(safe_c_string(result->association_stage, "none"))
            << ",\"aim_authority\":" << (result->aim_authority ? "true" : "false")
            << ",\"fire_authority\":" << (result->fire_authority ? "true" : "false")
            << ",\"service_freshness\":"
            << json_string(safe_c_string(result->service_freshness, "none"))
            << ",\"service_source_state\":"
            << json_string(safe_c_string(result->service_source_state, "unknown"))
            << ",\"service_sequence\":" << result->service_sequence
            << ",\"service_controller_aiming\":"
            << (result->service_controller_aiming ? "true" : "false")
            << ",\"service_engine_aiming\":"
            << (result->service_engine_aiming ? "true" : "false")
            << ",\"confidence\":" << result->target_confidence
            << ",\"dx\":" << result->dx
            << ",\"dy\":" << result->dy
            << ",\"capture_ms\":" << result->capture_acquire_ms
            << ",\"copy_ms\":" << result->capture_copy_ms
            << ",\"capture_transfer_ms\":" << capture_transfer_ms(*result)
            << ",\"cuda_map_ms\":" << result->cuda_map_ms
            << ",\"preprocess_mode\":" << json_string(
                vision_native::preprocess_mode_name(result->preprocess_mode))
            << ",\"preprocess_ms\":" << result->preprocess_ms
            << ",\"infer_ms\":" << result->infer_ms
            << ",\"gpu_total_ms\":" << result->gpu_total_ms
            << ",\"output_wait_ms\":" << result->output_wait_ms
            << ",\"decode_ms\":" << result->decode_ms
            << ",\"selector_ms\":" << result->selector_ms
            << ",\"color_copy_required\":" << (result->color_copy_required ? "true" : "false")
            << ",\"color_copy_bytes\":" << result->color_copy_bytes
            << ",\"color_copy_region_ratio\":" << result->color_copy_region_ratio
            << ",\"color_readback_mode\":" << json_string(result->color_readback_mode)
            << ",\"color_classify_ms\":" << result->color_classify_ms
            << ",\"color_candidate_count\":" << result->color_candidate_count
            << ",\"enhance_ms\":" << result->enhance_ms
            << ",\"post_ms\":" << result->post_ms
            << ",\"age_ms\":" << result->age_ms
            << ",\"vision_age_ms\":" << result->age_ms
            << ",\"boxes_seen\":" << result->boxes_seen;
    }

    output_
        << ",\"consume_ms\":" << snapshot.consume_ms
        << ",\"out_age_ms\":" << snapshot.out_age_ms
        << ",\"output_age_ms\":" << snapshot.out_age_ms
        << ",\"ctrl_loop_ms\":" << snapshot.ctrl_loop_ms
        << ",\"ctrl_pipeline_ms\":" << snapshot.ctrl_pipeline_ms
        << ",\"vigem_update_ms\":" << snapshot.vigem_update_ms
        << ",\"fire_requested\":" << snapshot.fire_requested
        << ",\"fire_allowed\":" << snapshot.fire_allowed
        << ",\"fire_blocked\":" << snapshot.fire_blocked
        << ",\"box_samples\":" << snapshot.box_samples;
    write_controller_components(output_, output_components, tracker_motion_output);
    write_controller_vision_state(output_, controller_vision_state);
    write_diagnostics(output_, result, controller_vision_state, output_components);
    output_ << "}\n";
}

const std::filesystem::path& AimPerfFileLogger::log_path() const noexcept {
    return log_path_;
}

}  // namespace runtime_app
