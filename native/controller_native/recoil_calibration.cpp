#include "recoil_calibration.h"

#include "recoil_profile.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace controller_native {

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open recoil calibration: " + path.u8string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::size_t find_value_start(const std::string& text, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::size_t key_pos = text.find(quoted_key);
    if (key_pos == std::string::npos) {
        return std::string::npos;
    }
    const std::size_t colon = text.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) {
        return std::string::npos;
    }
    return colon + 1;
}

std::string parse_json_string(
    const std::string& text,
    const std::string& key,
    const std::string& fallback) {
    std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return fallback;
    }
    value = text.find('"', value);
    if (value == std::string::npos) {
        return fallback;
    }
    std::string out;
    for (std::size_t index = value + 1; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '"' && (index == 0 || text[index - 1] != '\\')) {
            return out;
        }
        out.push_back(ch);
    }
    return fallback;
}

float parse_json_float(const std::string& text, const std::string& key, float fallback) {
    const std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return fallback;
    }
    try {
        return std::stof(text.substr(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

bool calibration_matches_profile(
    const RecoilCalibration& calibration,
    const RecoilProfile& profile) {
    return calibration.game == profile.game &&
        calibration.aim_mode == profile.aim_mode &&
        calibration.stance == profile.stance;
}

}  // namespace

RecoilCalibration load_recoil_calibration(const std::filesystem::path& path) {
    const std::string text = read_file(path);
    RecoilCalibration calibration;
    calibration.game = parse_json_string(text, "game", "");
    calibration.aim_mode = parse_json_string(text, "aim_mode", "");
    calibration.stance = parse_json_string(text, "stance", "standing");
    calibration.pixels_per_full_stick_x_per_second = parse_json_float(
        text,
        "pixels_per_full_stick_x_per_second",
        0.0f);
    calibration.pixels_per_full_stick_y_per_second = parse_json_float(
        text,
        "pixels_per_full_stick_y_per_second",
        0.0f);
    calibration.created_at = parse_json_string(text, "created_at", "");
    return calibration;
}

std::optional<RecoilCalibration> load_matching_recoil_calibration(
    const std::filesystem::path& directory,
    const RecoilProfile& profile) {
    if (directory.empty() || !std::filesystem::exists(directory) ||
        profile.game.empty() || profile.aim_mode.empty() || profile.stance.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path path =
        directory / (profile.game + "-" + profile.aim_mode + "-" + profile.stance + ".json");
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    try {
        RecoilCalibration calibration = load_recoil_calibration(path);
        if (!calibration_matches_profile(calibration, profile)) {
            return std::nullopt;
        }
        return calibration;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

float calibrated_stick_from_pixels(
    float pixels,
    const std::string& axis,
    int duration_ms,
    const RecoilCalibration& calibration) {
    if (duration_ms <= 0) {
        return 0.0f;
    }
    float rate = 0.0f;
    if (axis == "x") {
        rate = calibration.pixels_per_full_stick_x_per_second;
    } else if (axis == "y") {
        rate = calibration.pixels_per_full_stick_y_per_second;
    } else {
        return 0.0f;
    }
    if (rate <= 0.0f) {
        return 0.0f;
    }
    const float seconds = static_cast<float>(duration_ms) / 1000.0f;
    return clamp_unit(pixels / (rate * seconds));
}

}  // namespace controller_native
