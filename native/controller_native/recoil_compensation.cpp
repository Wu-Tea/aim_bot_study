#include "recoil_compensation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace controller_native {

namespace {

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

float fallback_amount(const GamepadRecoilConfig& config) {
    const float minimum = std::max(0.0f, config.feedback_min_amount);
    const float maximum = std::max(minimum, config.feedback_max_amount);
    return std::clamp(config.feedback_amount, minimum, maximum);
}

}  // namespace

NativeRecoilCompensation::NativeRecoilCompensation(GamepadRecoilConfig config)
    : config_(std::move(config)),
      profile_directory_(config_.profile_directory),
      calibration_directory_(config_.calibration_directory),
      recognizer_state_path_(config_.recognizer_state_path) {}

void NativeRecoilCompensation::reset() {
    fire_started_at_seconds_ = 0.0;
}

bool NativeRecoilCompensation::load_profile_directory(const std::filesystem::path& directory) {
    profile_directory_ = directory;
    const std::optional<RecoilProfile> profile = load_first_recoil_profile(directory);
    if (!profile.has_value() || profile->empty()) {
        return false;
    }
    set_profile(*profile);
    return true;
}

bool NativeRecoilCompensation::load_calibration_directory(const std::filesystem::path& directory) {
    calibration_directory_ = directory;
    if (!active_profile_.has_value()) {
        active_calibration_.reset();
        return std::filesystem::exists(directory);
    }
    active_calibration_ = load_active_calibration(*active_profile_);
    return active_calibration_.has_value();
}

void NativeRecoilCompensation::set_recognizer_state_path(
    const std::filesystem::path& recognizer_state_path) {
    recognizer_state_path_ = recognizer_state_path;
}

void NativeRecoilCompensation::set_profile(const RecoilProfile& profile) {
    active_profile_ = profile;
    playback_profile_ = build_recoil_playback_profile(profile, config_);
    active_calibration_ = load_active_calibration(profile);
    reset();
}

NativeRecoilOutput NativeRecoilCompensation::compute(const NativeRecoilInput& input) {
    NativeRecoilOutput output;
    if (!config_.enabled) {
        return output;
    }
    if (!input.fire_active) {
        reset();
        return output;
    }

    output.recoil_active = true;
    if (!config_.profile_playback_enabled) {
        output.right_y_delta = -fallback_amount(config_);
        return output;
    }

    select_runtime_profile_for_context(input.aiming);
    if (!playback_profile_.has_value() || playback_profile_->empty()) {
        output.right_y_delta = -fallback_amount(config_);
        return output;
    }

    if (fire_started_at_seconds_ <= 0.0) {
        fire_started_at_seconds_ = input.now_seconds;
    }

    // timeline: advance continuously while fire is active; do not pause on target-direction yield.
    const int elapsed_ms = std::max(
        0,
        static_cast<int>(std::lround((input.now_seconds - fire_started_at_seconds_) * 1000.0)));
    const int profile_elapsed_ms =
        elapsed_ms + std::max(0, static_cast<int>(std::lround(config_.profile_lead_ms)));
    const auto [cumulative_x, cumulative_y] = cumulative_profile_values(profile_elapsed_ms);
    const auto [previous_x, previous_y] = cumulative_profile_values(
        std::max(0, profile_elapsed_ms - playback_profile_->sample_interval_ms));
    const float delta_x = cumulative_x - previous_x;
    const float delta_y = cumulative_y - previous_y;

    const float profile_scale = std::max(0.0f, config_.profile_amount);
    const float profile_x_scale = profile_scale * config_.profile_x_amount;
    float profile_stick_x = 0.0f;
    float anti_recoil_stick_y = 0.0f;
    if (active_calibration_.has_value()) {
        profile_stick_x = map_calibrated_pixels_to_stick(
            -delta_x,
            "x",
            playback_profile_->sample_interval_ms) * profile_x_scale;
        anti_recoil_stick_y = map_calibrated_pixels_to_stick(
            -delta_y,
            "y",
            playback_profile_->sample_interval_ms) * profile_scale;
    } else {
        const float mapped_delta_x = normalize_delta_for_uncalibrated_mapping(
            delta_x,
            playback_profile_->sample_interval_ms);
        const float mapped_delta_y = normalize_delta_for_uncalibrated_mapping(
            delta_y,
            playback_profile_->sample_interval_ms);
        profile_stick_x = map_pixels_to_stick(-mapped_delta_x) * profile_x_scale;
        anti_recoil_stick_y = map_pixels_to_stick(-mapped_delta_y) * profile_scale;
    }

    // profile_despike is applied by build_recoil_playback_profile; raw JSON stays untouched.
    output.right_x_delta = profile_stick_x;
    output.right_y_delta = anti_recoil_stick_y;
    return output;
}

void NativeRecoilCompensation::select_runtime_profile_for_context(bool aiming) {
    if (profile_directory_.empty() || recognizer_state_path_.empty()) {
        return;
    }
    const std::string aim_mode = aiming ? "ads" : "hipfire";
    const std::optional<RecoilProfile> selected =
        load_matching_recoil_profile(profile_directory_, recognizer_state_path_, aim_mode);
    if (!selected.has_value() || selected->empty()) {
        active_profile_.reset();
        playback_profile_.reset();
        active_calibration_.reset();
        active_aim_mode_.clear();
        reset();
        log_profile_selection(aim_mode, nullptr);
        return;
    }
    if (active_profile_.has_value() &&
        active_profile_->profile_id == selected->profile_id &&
        active_aim_mode_ == aim_mode) {
        log_profile_selection(aim_mode, &(*active_profile_));
        return;
    }
    set_profile(*selected);
    active_aim_mode_ = aim_mode;
    log_profile_selection(aim_mode, &(*active_profile_));
}

void NativeRecoilCompensation::log_profile_selection(
    const std::string& aim_mode,
    const RecoilProfile* profile) {
    if (!config_.selection_log_enabled) {
        return;
    }
    const std::string profile_id = profile == nullptr ? "none" : profile->profile_id;
    if (has_logged_selection_ &&
        last_logged_aim_mode_ == aim_mode &&
        last_logged_profile_id_ == profile_id) {
        return;
    }
    has_logged_selection_ = true;
    last_logged_aim_mode_ = aim_mode;
    last_logged_profile_id_ = profile_id;

    std::ostringstream line;
    if (profile == nullptr) {
        const int fallback_percent = static_cast<int>(
            std::lround(std::max(0.0f, config_.feedback_amount) * 100.0f));
        line
            << "[Recoil] active_profile aim=" << aim_mode
            << " profile=none fallback=" << fallback_percent << "%";
    } else {
        line
            << std::fixed
            << "[Recoil] active_profile aim=" << aim_mode
            << " profile=" << profile->profile_id
            << " confidence=" << std::setprecision(3) << profile->confidence
            << " profile_amount=" << std::setprecision(2) << config_.profile_amount
            << " profile_x=" << std::setprecision(2) << config_.profile_x_amount
            << " feedback=" << std::setprecision(2) << config_.feedback_amount
            << " lead_ms=" << std::max(0, static_cast<int>(std::lround(config_.profile_lead_ms)))
            << " velocity_ref_ms="
            << std::max(1, static_cast<int>(std::lround(config_.profile_velocity_reference_ms)));
    }
    std::cout << line.str() << '\n';
}

std::pair<float, float> NativeRecoilCompensation::cumulative_profile_values(int elapsed_ms) const {
    if (!playback_profile_.has_value() || playback_profile_->empty() ||
        elapsed_ms < playback_profile_->initial_delay_ms) {
        return {0.0f, 0.0f};
    }
    const int sample_index = std::max(
        0,
        std::min(
            static_cast<int>(playback_profile_->sample_count()) - 1,
            (elapsed_ms - playback_profile_->initial_delay_ms) /
                std::max(1, playback_profile_->sample_interval_ms)));
    return {
        playback_profile_->samples_x[static_cast<std::size_t>(sample_index)],
        playback_profile_->samples_y[static_cast<std::size_t>(sample_index)],
    };
}

std::optional<RecoilCalibration> NativeRecoilCompensation::load_active_calibration(
    const RecoilProfile& profile) const {
    return load_matching_recoil_calibration(calibration_directory_, profile);
}

float NativeRecoilCompensation::map_calibrated_pixels_to_stick(
    float pixels,
    const std::string& axis,
    int duration_ms) const {
    if (!active_calibration_.has_value()) {
        return 0.0f;
    }
    return calibrated_stick_from_pixels(pixels, axis, duration_ms, *active_calibration_);
}

float NativeRecoilCompensation::normalize_delta_for_uncalibrated_mapping(
    float delta,
    int sample_interval_ms) const {
    const int interval_ms = std::max(1, sample_interval_ms);
    const int reference_ms = std::max(
        1,
        static_cast<int>(std::lround(config_.profile_velocity_reference_ms)));
    return delta * (static_cast<float>(reference_ms) / static_cast<float>(interval_ms));
}

float NativeRecoilCompensation::map_pixels_to_stick(float delta) const {
    const float abs_delta = std::fabs(delta);
    const float sign = delta < 0.0f ? -1.0f : 1.0f;
    const float mid_pixels = config_.piecewise_mid_pixels_y;
    const float max_pixels = config_.piecewise_max_pixels_y;
    const float mid_ratio = config_.piecewise_mid_ratio_y;
    if (mid_pixels <= 0.0f || max_pixels <= mid_pixels || mid_ratio <= 0.0f || mid_ratio >= 1.0f) {
        return max_pixels > 0.0f ? clamp_unit(delta / max_pixels) : 0.0f;
    }
    if (abs_delta >= max_pixels) {
        return sign;
    }
    if (abs_delta <= mid_pixels) {
        return sign * mid_ratio * (abs_delta / mid_pixels);
    }
    const float progress = (abs_delta - mid_pixels) / (max_pixels - mid_pixels);
    return sign * (mid_ratio + ((1.0f - mid_ratio) * progress));
}

}  // namespace controller_native
