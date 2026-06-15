#pragma once

#include "recoil_calibration.h"
#include "recoil_profile.h"
#include "runtime_config.h"

#include <filesystem>
#include <optional>
#include <string>

namespace controller_native {

// Recoil is feed-forward playback. It must not consume target, tracker, or
// controller correction errors; those belong to aim/controller stages.
struct NativeRecoilInput {
    bool fire_active = false;
    bool aiming = false;
    double now_seconds = 0.0;
};

struct NativeRecoilOutput {
    bool recoil_active = false;
    float right_x_delta = 0.0f;
    float right_y_delta = 0.0f;
};

class NativeRecoilCompensation {
public:
    explicit NativeRecoilCompensation(GamepadRecoilConfig config = {});

    void reset();
    bool load_profile_directory(const std::filesystem::path& directory);
    bool load_calibration_directory(const std::filesystem::path& directory);
    void set_recognizer_state_path(const std::filesystem::path& recognizer_state_path);
    void set_profile(const RecoilProfile& profile);
    NativeRecoilOutput compute(const NativeRecoilInput& input);

private:
    void select_runtime_profile_for_context(bool aiming);
    void log_profile_selection(const std::string& aim_mode, const RecoilProfile* profile);
    std::pair<float, float> cumulative_profile_values(int elapsed_ms) const;
    std::optional<RecoilCalibration> load_active_calibration(const RecoilProfile& profile) const;
    float map_calibrated_pixels_to_stick(float pixels, const std::string& axis, int duration_ms) const;
    float normalize_delta_for_uncalibrated_mapping(float delta, int sample_interval_ms) const;
    float map_pixels_to_stick(float delta) const;

    GamepadRecoilConfig config_;
    std::optional<RecoilProfile> active_profile_;
    std::optional<RecoilProfile> playback_profile_;
    std::optional<RecoilCalibration> active_calibration_;
    std::filesystem::path profile_directory_;
    std::filesystem::path calibration_directory_;
    std::filesystem::path recognizer_state_path_;
    std::string active_aim_mode_;
    std::string last_logged_aim_mode_;
    std::string last_logged_profile_id_;
    bool has_logged_selection_ = false;
    double fire_started_at_seconds_ = 0.0;
};

}  // namespace controller_native
