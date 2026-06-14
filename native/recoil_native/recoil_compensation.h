#pragma once

#include "recoil_debug.h"
#include "recoil_visual_model.h"

#include "../controller_native/recoil_compensation.h"

#include <filesystem>

namespace recoil_native {

struct RecoilBoundaryOutput {
    bool recoil_active = false;
    common_native::Vec2f recoil_stick;
    common_native::Vec2f visual_stick;
};

class RecoilCompensationPolicy {
public:
    explicit RecoilCompensationPolicy(controller_native::GamepadRecoilConfig config = {});

    void reset();
    bool load_profile_directory(const std::filesystem::path& directory);
    bool load_calibration_directory(const std::filesystem::path& directory);
    void set_recognizer_state_path(const std::filesystem::path& recognizer_state_path);
    void set_profile(const controller_native::RecoilProfile& profile);
    RecoilBoundaryOutput compute(const controller_native::NativeRecoilInput& input);

private:
    controller_native::NativeRecoilCompensation compensation_;
    DisabledRecoilVisualModel visual_model_;
};

}  // namespace recoil_native
