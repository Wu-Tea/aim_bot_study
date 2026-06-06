#pragma once

#include "runtime_config.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace controller_native {

struct RecoilProfile {
    std::string profile_id;
    std::string canonical_weapon_id;
    std::string game;
    std::string stance = "standing";
    std::string aim_mode;
    float confidence = 0.0f;
    int sample_interval_ms = 16;
    int duration_ms = 0;
    int initial_delay_ms = 0;
    std::vector<float> samples_x;
    std::vector<float> samples_y;

    bool empty() const;
    std::size_t sample_count() const;
};

RecoilProfile load_recoil_profile(const std::filesystem::path& path);
std::optional<RecoilProfile> load_first_recoil_profile(const std::filesystem::path& directory);
std::optional<RecoilProfile> load_matching_recoil_profile(
    const std::filesystem::path& directory,
    const std::filesystem::path& recognizer_state_path,
    const std::string& aim_mode);
RecoilProfile build_recoil_playback_profile(
    const RecoilProfile& profile,
    const GamepadRecoilConfig& config);

}  // namespace controller_native
