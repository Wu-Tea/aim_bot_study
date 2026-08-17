#pragma once

#include "../pipeline_contract/control_command.h"
#include "xinput_reader.h"

#include <cmath>

namespace controller_native {

struct SampledControlInput {
    PhysicalGamepadState physical{};
    pipeline_contract::ControllerTickId controller_tick{};
    pipeline_contract::EventSequence sample_sequence{};

    bool valid() const noexcept {
        return controller_tick.valid && sample_sequence.valid &&
            std::isfinite(physical.left_x) &&
            std::isfinite(physical.left_y) &&
            std::isfinite(physical.right_x) &&
            std::isfinite(physical.right_y) &&
            std::isfinite(physical.left_trigger) &&
            std::isfinite(physical.right_trigger);
    }
};

// One immutable physical sample plus the only commands consumed by
// OutputComposer. Reducer-internal state and observer-only event mirrors do
// not travel through the output boundary.
class ControlFrame {
public:
    ControlFrame() noexcept = default;
    explicit ControlFrame(const SampledControlInput& sampled) noexcept
        : sampled_(sampled) {}

    static ControlFrame begin(
        const PhysicalGamepadState& physical,
        pipeline_contract::ControllerTickId controller_tick,
        pipeline_contract::EventSequence sample_sequence) noexcept {
        return ControlFrame({physical, controller_tick, sample_sequence});
    }

    const SampledControlInput& sampled() const noexcept { return sampled_; }
    const PhysicalGamepadState& physical() const noexcept {
        return sampled_.physical;
    }
    pipeline_contract::ControllerTickId controller_tick() const noexcept {
        return sampled_.controller_tick;
    }
    pipeline_contract::EventSequence sample_sequence() const noexcept {
        return sampled_.sample_sequence;
    }

    pipeline_contract::PreRecoilStickCommand& pre_recoil_command() noexcept {
        return pre_recoil_command_;
    }
    const pipeline_contract::PreRecoilStickCommand& pre_recoil_command() const noexcept {
        return pre_recoil_command_;
    }
    pipeline_contract::FireCommand& fire_command() noexcept {
        return fire_command_;
    }
    const pipeline_contract::FireCommand& fire_command() const noexcept {
        return fire_command_;
    }
    pipeline_contract::RecoilContribution& recoil_contribution() noexcept {
        return recoil_contribution_;
    }
    const pipeline_contract::RecoilContribution& recoil_contribution() const noexcept {
        return recoil_contribution_;
    }
    pipeline_contract::AuxiliaryDpadCommand& auxiliary_dpad() noexcept {
        return auxiliary_dpad_;
    }
    const pipeline_contract::AuxiliaryDpadCommand& auxiliary_dpad() const noexcept {
        return auxiliary_dpad_;
    }

    bool valid() const noexcept {
        return sampled_.valid() && pre_recoil_command_.valid() &&
            fire_command_.valid() && recoil_contribution_.valid() &&
            auxiliary_dpad_.valid();
    }

private:
    const SampledControlInput sampled_{};
    pipeline_contract::PreRecoilStickCommand pre_recoil_command_{};
    pipeline_contract::FireCommand fire_command_{};
    pipeline_contract::RecoilContribution recoil_contribution_{};
    pipeline_contract::AuxiliaryDpadCommand auxiliary_dpad_{};
};

}  // namespace controller_native
