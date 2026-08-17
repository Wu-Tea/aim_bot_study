#include "output_composer.h"

#include "control_frame.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

float clamp_axis(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, -1.0f, 1.0f);
}

float clamp_trigger(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

GamepadOutputState seed_output(const PhysicalGamepadState& physical) noexcept {
    GamepadOutputState output{};
    output.left_x = clamp_axis(physical.left_x);
    output.left_y = clamp_axis(physical.left_y);
    output.right_x = clamp_axis(physical.right_x);
    output.right_y = clamp_axis(physical.right_y);
    output.left_trigger = clamp_trigger(physical.left_trigger);
    output.right_trigger = clamp_trigger(physical.right_trigger);
    output.rb = physical.rb;
    output.lb = physical.lb;
    output.a = physical.a;
    output.b = physical.b;
    output.x = physical.x;
    output.y = physical.y;
    output.back = physical.back;
    output.guide = physical.guide;
    output.start = physical.start;
    output.left_thumb = physical.left_thumb;
    output.right_thumb = physical.right_thumb;
    output.dpad_up = physical.dpad_up;
    output.dpad_down = physical.dpad_down;
    output.dpad_left = physical.dpad_left;
    output.dpad_right = physical.dpad_right;
    return output;
}

bool finite_physical(const PhysicalGamepadState& physical) noexcept {
    return std::isfinite(physical.left_x) &&
        std::isfinite(physical.left_y) &&
        std::isfinite(physical.right_x) &&
        std::isfinite(physical.right_y) &&
        std::isfinite(physical.left_trigger) &&
        std::isfinite(physical.right_trigger);
}

}  // namespace

void OutputComposer::reset() noexcept {
    output_ = {};
    stage_ = OutputComposeStage::Empty;
}

OutputComposeStatus OutputComposer::reject_if_finalized() const noexcept {
    return finalized()
        ? OutputComposeStatus::AlreadyFinalized
        : OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::seed_physical_passthrough(
    const PhysicalGamepadState& physical) noexcept {
    const auto final_status = reject_if_finalized();
    if (final_status != OutputComposeStatus::Ok) return final_status;
    if (stage_ != OutputComposeStage::Empty) {
        return OutputComposeStatus::DuplicateStage;
    }
    if (!finite_physical(physical)) return OutputComposeStatus::InvalidInput;

    output_ = seed_output(physical);
    stage_ = OutputComposeStage::PhysicalSeeded;
    return OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::apply_pre_recoil_stick(
    const pipeline_contract::PreRecoilStickCommand& command) noexcept {
    const auto final_status = reject_if_finalized();
    if (final_status != OutputComposeStatus::Ok) return final_status;
    if (stage_ != OutputComposeStage::PhysicalSeeded) {
        return stage_ == OutputComposeStage::Empty
            ? OutputComposeStatus::NotSeeded
            : OutputComposeStatus::DuplicateStage;
    }
    if (!command.valid()) return OutputComposeStatus::InvalidInput;

    if (command.available) {
        output_.right_x = clamp_axis(command.stick.x);
        output_.right_y = clamp_axis(command.stick.y);
    }
    stage_ = OutputComposeStage::PreRecoilWritten;
    return OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::apply_fire_only(
    const pipeline_contract::FireCommand& command) noexcept {
    const auto final_status = reject_if_finalized();
    if (final_status != OutputComposeStatus::Ok) return final_status;
    if (stage_ != OutputComposeStage::PreRecoilWritten) {
        if (stage_ == OutputComposeStage::Empty) {
            return OutputComposeStatus::NotSeeded;
        }
        return stage_ >= OutputComposeStage::FireWritten
            ? OutputComposeStatus::DuplicateStage
            : OutputComposeStatus::OutOfOrder;
    }
    if (!command.valid()) return OutputComposeStatus::InvalidInput;

    if (command.synthetic_active) {
        output_.rb = output_.rb || command.synthetic_rb;
        output_.right_trigger = clamp_trigger(std::max(
            output_.right_trigger, command.synthetic_right_trigger));
    }
    stage_ = OutputComposeStage::FireWritten;
    return OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::apply_recoil(
    const pipeline_contract::RecoilContribution& contribution) noexcept {
    const auto final_status = reject_if_finalized();
    if (final_status != OutputComposeStatus::Ok) return final_status;
    if (stage_ != OutputComposeStage::FireWritten) {
        if (stage_ == OutputComposeStage::Empty) {
            return OutputComposeStatus::NotSeeded;
        }
        return stage_ >= OutputComposeStage::RecoilWritten
            ? OutputComposeStatus::DuplicateStage
            : OutputComposeStatus::OutOfOrder;
    }
    if (!contribution.valid()) return OutputComposeStatus::InvalidInput;

    if (contribution.active) {
        // This is deliberately after the pre-recoil write.  Recoil cannot
        // feed back into target/aim arbitration or consume its proposal.
        output_.right_x = clamp_axis(
            output_.right_x + contribution.stick_delta.x);
        output_.right_y = clamp_axis(
            output_.right_y + contribution.stick_delta.y);
    }
    stage_ = OutputComposeStage::RecoilWritten;
    return OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::merge_auxiliary_dpad(
    const pipeline_contract::AuxiliaryDpadCommand& command) noexcept {
    const auto final_status = reject_if_finalized();
    if (final_status != OutputComposeStatus::Ok) return final_status;
    if (stage_ != OutputComposeStage::RecoilWritten) {
        if (stage_ == OutputComposeStage::Empty) {
            return OutputComposeStatus::NotSeeded;
        }
        return stage_ >= OutputComposeStage::AuxiliaryDpadWritten
            ? OutputComposeStatus::DuplicateStage
            : OutputComposeStatus::OutOfOrder;
    }
    if (!command.valid()) return OutputComposeStatus::InvalidInput;

    if (command.up) output_.dpad_up = true;
    if (command.down) output_.dpad_down = true;
    if (command.left) output_.dpad_left = true;
    if (command.right) output_.dpad_right = true;
    stage_ = OutputComposeStage::AuxiliaryDpadWritten;
    return OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::finalize() noexcept {
    if (finalized()) return OutputComposeStatus::AlreadyFinalized;
    if (stage_ != OutputComposeStage::AuxiliaryDpadWritten) {
        return stage_ == OutputComposeStage::Empty
            ? OutputComposeStatus::NotSeeded
            : OutputComposeStatus::OutOfOrder;
    }
    output_.left_x = clamp_axis(output_.left_x);
    output_.left_y = clamp_axis(output_.left_y);
    output_.right_x = clamp_axis(output_.right_x);
    output_.right_y = clamp_axis(output_.right_y);
    output_.left_trigger = clamp_trigger(output_.left_trigger);
    output_.right_trigger = clamp_trigger(output_.right_trigger);
    stage_ = OutputComposeStage::Finalized;
    return OutputComposeStatus::Ok;
}

OutputComposeStatus OutputComposer::compose(const ControlFrame& frame) noexcept {
    reset();
    if (!frame.valid()) return OutputComposeStatus::InvalidInput;
    auto status = seed_physical_passthrough(frame.physical());
    if (status != OutputComposeStatus::Ok) return status;
    status = apply_pre_recoil_stick(frame.pre_recoil_command());
    if (status != OutputComposeStatus::Ok) return status;
    status = apply_fire_only(frame.fire_command());
    if (status != OutputComposeStatus::Ok) return status;
    status = apply_recoil(frame.recoil_contribution());
    if (status != OutputComposeStatus::Ok) return status;
    status = merge_auxiliary_dpad(frame.auxiliary_dpad());
    if (status != OutputComposeStatus::Ok) return status;
    return finalize();
}

}  // namespace controller_native
