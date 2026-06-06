#include "downward_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace runtime_app {

namespace {

bool env_flag(const char* name, bool fallback = false) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return fallback;
    }
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string env_string(const char* name, const std::string& fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    return std::string(raw);
}

int env_int(const char* name, int fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

int stick_units(float value) {
    return static_cast<int>(std::lround(
        std::max(-1.0f, std::min(1.0f, value)) * 32767.0f));
}

double ns_to_seconds(std::uint64_t ns) {
    return static_cast<double>(ns) / 1'000'000'000.0;
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

void write_json_string_or_null(std::ostream& out, const std::string& value, bool has_value) {
    if (!has_value) {
        out << "null";
        return;
    }
    out << '"' << json_escape(value) << '"';
}

bool trace_auto_fire_active(
    const std::vector<controller_native::NativeControllerStageTrace>& traces) {
    for (const auto& trace : traces) {
        if (trace.after_auto_fire_active) {
            return true;
        }
    }
    return false;
}

const controller_native::NativeControllerStageTrace* largest_downward_trace(
    const std::vector<controller_native::NativeControllerStageTrace>& traces) {
    const controller_native::NativeControllerStageTrace* best = nullptr;
    for (const auto& trace : traces) {
        if (best == nullptr || trace.delta_right_y < best->delta_right_y) {
            best = &trace;
        }
    }
    return best;
}

}  // namespace

DownwardPullDiagnostics::DownwardPullDiagnostics(DownwardPullDiagnosticsConfig config)
    : config_(std::move(config)) {}

DownwardPullDiagnostics DownwardPullDiagnostics::from_environment() {
    DownwardPullDiagnosticsConfig config;
    config.enabled = env_flag("GAMEPAD_DOWNWARD_DIAGNOSTICS");
    config.output_path = env_string(
        "GAMEPAD_DOWNWARD_DIAGNOSTICS_PATH",
        "artifacts/diagnostics/gamepad_downward_pull.jsonl");
    config.downward_delta_threshold =
        std::max(1, env_int("GAMEPAD_DOWNWARD_DIAGNOSTICS_THRESHOLD", 6000));
    return DownwardPullDiagnostics(config);
}

bool DownwardPullDiagnostics::record_if_triggered(
    const controller_native::PhysicalGamepadState& physical,
    const controller_native::GamepadOutputState& output,
    const std::vector<controller_native::NativeControllerStageTrace>& traces,
    const vision_native::VisionResult* latest_result,
    bool is_aiming) const {
    if (!config_.enabled) {
        return false;
    }

    const int manual_right_y = stick_units(physical.right_y);
    const int final_right_y = stick_units(output.right_y);
    const int system_right_y_delta = final_right_y - manual_right_y;
    const auto* largest_trace = largest_downward_trace(traces);
    const int largest_trace_delta = largest_trace == nullptr
        ? 0
        : stick_units(largest_trace->delta_right_y);
    const int threshold = -std::abs(config_.downward_delta_threshold);
    const bool triggered_by_total_delta = system_right_y_delta <= threshold;
    const bool triggered_by_plugin_delta =
        largest_trace != nullptr && largest_trace_delta <= threshold;
    if (!triggered_by_total_delta && !triggered_by_plugin_delta) {
        return false;
    }

    const std::filesystem::path parent = config_.output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream out(config_.output_path, std::ios::app | std::ios::binary);
    if (!out) {
        return false;
    }

    const bool has_result = latest_result != nullptr;
    const double target_timestamp = has_result
        ? ns_to_seconds(
              latest_result->result_at_ns != 0
                  ? latest_result->result_at_ns
                  : latest_result->captured_at_ns)
        : 0.0;

    out << std::fixed << std::setprecision(6);
    out << '{';
    out << "\"timestamp\":" << target_timestamp << ',';
    out << "\"manual_right_y\":" << manual_right_y << ',';
    out << "\"final_right_y\":" << final_right_y << ',';
    out << "\"system_right_y_delta\":" << system_right_y_delta << ',';
    out << "\"auto_fire_active\":"
        << (trace_auto_fire_active(traces) ? "true" : "false") << ',';
    out << "\"is_aiming\":" << (is_aiming ? "true" : "false") << ',';
    out << "\"auto_fire_requested\":"
        << (has_result && latest_result->auto_fire ? "true" : "false") << ',';
    out << "\"target_dx\":" << (has_result ? latest_result->dx : 0.0f) << ',';
    out << "\"target_dy\":" << (has_result ? latest_result->dy : 0.0f) << ',';
    out << "\"target_revision\":0,";
    out << "\"target_timestamp\":" << target_timestamp << ',';
    out << "\"target\":";
    if (!has_result || !latest_result->has_target) {
        out << "null";
    } else {
        out << '{';
        out << "\"aim_point_x\":" << latest_result->target_x << ',';
        out << "\"aim_point_y\":" << latest_result->target_y << ',';
        out << "\"screen_center_x\":" << latest_result->screen_center_x << ',';
        out << "\"screen_center_y\":" << latest_result->screen_center_y << ',';
        out << "\"body_box\":";
        if (!latest_result->has_body_box) {
            out << "null";
        } else {
            out << '['
                << latest_result->body_x1 << ','
                << latest_result->body_y1 << ','
                << latest_result->body_x2 << ','
                << latest_result->body_y2 << ']';
        }
        out << '}';
    }
    out << ',';
    out << "\"triggered_by_total_delta\":"
        << (triggered_by_total_delta ? "true" : "false") << ',';
    out << "\"triggered_by_plugin_delta\":"
        << (triggered_by_plugin_delta ? "true" : "false") << ',';
    out << "\"largest_downward_plugin\":";
    write_json_string_or_null(
        out,
        largest_trace == nullptr ? std::string() : largest_trace->stage_name,
        largest_trace != nullptr);
    out << ',';
    out << "\"largest_downward_plugin_delta\":" << largest_trace_delta << ',';
    out << "\"plugin_traces\":[";
    for (std::size_t index = 0; index < traces.size(); ++index) {
        const auto& trace = traces[index];
        if (index > 0) {
            out << ',';
        }
        out << '{';
        out << "\"plugin_name\":\"" << json_escape(trace.stage_name) << "\",";
        out << "\"before_right_y\":" << stick_units(trace.before_right_y) << ',';
        out << "\"after_right_y\":" << stick_units(trace.after_right_y) << ',';
        out << "\"delta_right_y\":" << stick_units(trace.delta_right_y) << ',';
        out << "\"before_auto_fire_active\":"
            << (trace.before_auto_fire_active ? "true" : "false") << ',';
        out << "\"after_auto_fire_active\":"
            << (trace.after_auto_fire_active ? "true" : "false");
        out << '}';
    }
    out << "]}";
    out << '\n';
    return true;
}

}  // namespace runtime_app
