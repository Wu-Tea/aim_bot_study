#include "aim_perf_file_logger.h"

#include <algorithm>
#include <chrono>
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
        << ",\"manual_x\":" << value.manual_stick.x
        << ",\"manual_y\":" << value.manual_stick.y
        << ",\"ai_aim_x\":" << value.ai_aim_stick.x
        << ",\"ai_aim_y\":" << value.ai_aim_stick.y
        << ",\"dynamic_x\":" << value.dynamic_adjustment_stick.x
        << ",\"dynamic_y\":" << value.dynamic_adjustment_stick.y
        << ",\"recoil_x\":" << value.recoil_stick.x
        << ",\"recoil_y\":" << value.recoil_stick.y
        << ",\"final_x\":" << value.final_stick.x
        << ",\"final_y\":" << value.final_stick.y
        << ",\"tracker_sample_x\":" << tracker.right_x
        << ",\"tracker_sample_y\":" << tracker.right_y
        << ",\"fire_button\":" << (value.fire_button ? "true" : "false");
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
            << ",\"enhance_ms\":" << result->enhance_ms
            << ",\"post_ms\":" << result->post_ms
            << ",\"age_ms\":" << result->age_ms
            << ",\"boxes_seen\":" << result->boxes_seen;
    }

    output_
        << ",\"consume_ms\":" << snapshot.consume_ms
        << ",\"out_age_ms\":" << snapshot.out_age_ms
        << ",\"ctrl_loop_ms\":" << snapshot.ctrl_loop_ms
        << ",\"ctrl_pipeline_ms\":" << snapshot.ctrl_pipeline_ms
        << ",\"vigem_update_ms\":" << snapshot.vigem_update_ms
        << ",\"fire_requested\":" << snapshot.fire_requested
        << ",\"fire_allowed\":" << snapshot.fire_allowed
        << ",\"fire_blocked\":" << snapshot.fire_blocked
        << ",\"box_samples\":" << snapshot.box_samples;
    write_controller_components(output_, output_components, tracker_motion_output);
    output_ << "}\n";
}

const std::filesystem::path& AimPerfFileLogger::log_path() const noexcept {
    return log_path_;
}

}  // namespace runtime_app
