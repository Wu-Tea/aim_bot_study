#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace controller_native {

struct RecoilProfile;

struct RecoilCalibration {
    std::string game;
    std::string aim_mode;
    std::string stance = "standing";
    float pixels_per_full_stick_x_per_second = 0.0f;
    float pixels_per_full_stick_y_per_second = 0.0f;
    std::string created_at;
};

RecoilCalibration load_recoil_calibration(const std::filesystem::path& path);
std::optional<RecoilCalibration> load_matching_recoil_calibration(
    const std::filesystem::path& directory,
    const RecoilProfile& profile);
float calibrated_stick_from_pixels(
    float pixels,
    const std::string& axis,
    int duration_ms,
    const RecoilCalibration& calibration);

}  // namespace controller_native
