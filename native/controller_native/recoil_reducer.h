#pragma once

#include "runtime_config.h"
#include "../pipeline_contract/control_command.h"
#include "../recoil_native/recoil_compensation.h"

#include <filesystem>
#include <utility>

namespace controller_native {

// Sole recoil state owner. Its input deliberately excludes target, D/R,
// manual stick, AI proposal and pre-recoil T.
class RecoilReducer {
public:
    explicit RecoilReducer(GamepadRecoilConfig config = {})
        : policy_(std::move(config)) {}

    void reset() { policy_.reset(); }
    bool load_profile_directory(const std::filesystem::path& directory) {
        return policy_.load_profile_directory(directory);
    }
    bool load_calibration_directory(const std::filesystem::path& directory) {
        return policy_.load_calibration_directory(directory);
    }
    void set_recognizer_state_path(const std::filesystem::path& path) {
        policy_.set_recognizer_state_path(path);
    }

    pipeline_contract::RecoilContribution reduce(
        bool effective_fire,
        bool aiming,
        double now_seconds,
        pipeline_contract::EventSequence cause_event = {});

private:
    recoil_native::RecoilCompensationPolicy policy_;
};

}  // namespace controller_native
