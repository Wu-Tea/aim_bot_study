#include "recoil_compensation.h"

#include <utility>

namespace recoil_native {

RecoilCompensationPolicy::RecoilCompensationPolicy(
    controller_native::GamepadRecoilConfig config)
    : compensation_(std::move(config)) {}

void RecoilCompensationPolicy::reset() {
    compensation_.reset();
}

bool RecoilCompensationPolicy::load_profile_directory(
    const std::filesystem::path& directory) {
    return compensation_.load_profile_directory(directory);
}

bool RecoilCompensationPolicy::load_calibration_directory(
    const std::filesystem::path& directory) {
    return compensation_.load_calibration_directory(directory);
}

void RecoilCompensationPolicy::set_recognizer_state_path(
    const std::filesystem::path& recognizer_state_path) {
    compensation_.set_recognizer_state_path(recognizer_state_path);
}

void RecoilCompensationPolicy::set_profile(const controller_native::RecoilProfile& profile) {
    compensation_.set_profile(profile);
}

RecoilBoundaryOutput RecoilCompensationPolicy::compute(
    const controller_native::NativeRecoilInput& input) {
    const controller_native::NativeRecoilOutput recoil = compensation_.compute(input);
    RecoilBoundaryOutput output;
    output.recoil_active = recoil.recoil_active;
    output.recoil_stick = {recoil.right_x_delta, recoil.right_y_delta};
    const RecoilVisualDisplacement visual = visual_model_.compute(
        {output.recoil_active, output.recoil_stick});
    output.visual_stick = visual.stick;
    return output;
}

}  // namespace recoil_native
