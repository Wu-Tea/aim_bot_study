#pragma once

#include "../pipeline_contract/control_command.h"
#include "virtual_gamepad.h"
#include "xinput_reader.h"

#include <cstdint>

namespace controller_native {

class ControlFrame;

enum class OutputComposeStage : std::uint8_t {
    Empty = 0,
    PhysicalSeeded = 1,
    PreRecoilWritten = 2,
    FireWritten = 3,
    RecoilWritten = 4,
    AuxiliaryDpadWritten = 5,
    Finalized = 6,
};

enum class OutputComposeStatus : std::uint8_t {
    Ok = 0,
    InvalidInput = 1,
    NotSeeded = 2,
    OutOfOrder = 3,
    DuplicateStage = 4,
    AlreadyFinalized = 5,
    OutputUnavailable = 6,
};

// OutputComposer is intentionally a small, one-way state machine.  It is the
// only place in the new pipeline that combines physical passthrough, one
// target-first pre-recoil proposal, fire-only synthetic input, recoil, and
// auxiliary D-pad actions into GamepadOutputState.
class OutputComposer {
public:
    OutputComposer() noexcept = default;

    void reset() noexcept;

    OutputComposeStatus seed_physical_passthrough(
        const PhysicalGamepadState& physical) noexcept;

    OutputComposeStatus apply_pre_recoil_stick(
        const pipeline_contract::PreRecoilStickCommand& command) noexcept;

    OutputComposeStatus apply_fire_only(
        const pipeline_contract::FireCommand& command) noexcept;

    OutputComposeStatus apply_recoil(
        const pipeline_contract::RecoilContribution& contribution) noexcept;

    OutputComposeStatus merge_auxiliary_dpad(
        const pipeline_contract::AuxiliaryDpadCommand& command) noexcept;

    OutputComposeStatus finalize() noexcept;

    // Runs the complete fixed output phase for one immutable control frame.
    // This is the production entry point; the individual stage methods remain
    // public so contract tests can prove ordering and duplicate-write guards.
    OutputComposeStatus compose(const ControlFrame& frame) noexcept;

    OutputComposeStage stage() const noexcept { return stage_; }
    bool finalized() const noexcept {
        return stage_ == OutputComposeStage::Finalized;
    }

    const GamepadOutputState* finalized_output() const noexcept {
        return finalized() ? &output_ : nullptr;
    }

    // This accessor is useful for trace/tests while composing.  Callers that
    // need a deliverable output must use finalized_output().
    const GamepadOutputState& output_for_trace() const noexcept {
        return output_;
    }

private:
    OutputComposeStatus reject_if_finalized() const noexcept;

    GamepadOutputState output_{};
    OutputComposeStage stage_ = OutputComposeStage::Empty;
};

}  // namespace controller_native
