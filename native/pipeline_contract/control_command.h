#pragma once

#include "control_event.h"
#include "vision_observation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pipeline_contract {

struct ControlCommandHeader {
    std::uint16_t schema_version = 1;
    EventSequence sequence{};
    ControllerTickId controller_tick{};
    EventSequence cause_event{};

    bool valid() const noexcept {
        return schema_version == 1 && sequence.valid && controller_tick.valid;
    }
};

struct PreRecoilStickCommand {
    Vec2f stick{};
    EventSequence cause_event{};
    bool available = false;

    bool valid() const noexcept {
        return !available ||
            (std::isfinite(stick.x) && std::isfinite(stick.y) &&
             stick.x >= -1.0f && stick.x <= 1.0f &&
             stick.y >= -1.0f && stick.y <= 1.0f);
    }

    static PreRecoilStickCommand from_stick(
        Vec2f value,
        EventSequence cause = {}) noexcept {
        PreRecoilStickCommand result{};
        if (!std::isfinite(value.x) || !std::isfinite(value.y)) return result;
        result.stick = {
            std::clamp(value.x, -1.0f, 1.0f),
            std::clamp(value.y, -1.0f, 1.0f)};
        result.cause_event = cause;
        result.available = true;
        return result;
    }
};

struct FireCommand {
    ControlCommandHeader header{};
    // FireCommand is synthetic-only.  Physical RB/RT are seeded from the
    // sampled input and remain untouched by this command.
    bool synthetic_active = false;
    bool synthetic_rb = false;
    float synthetic_right_trigger = 0.0f;

    bool valid() const noexcept {
        return (!synthetic_active || header.valid()) &&
            std::isfinite(synthetic_right_trigger) &&
            synthetic_right_trigger >= 0.0f &&
            synthetic_right_trigger <= 1.0f;
    }
};

struct RecoilContribution {
    // Recoil has no target/manual/aim context by design.  It is a final-stage
    // contribution and is consumed only by OutputComposer.
    Vec2f stick_delta{};
    EventSequence cause_event{};
    bool active = false;

    bool valid() const noexcept {
        return std::isfinite(stick_delta.x) &&
            std::isfinite(stick_delta.y);
    }
};

struct AuxiliaryDpadCommand {
    ControlCommandHeader header{};
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;

    bool valid() const noexcept {
        const bool has_request = up || down || left || right;
        return !has_request || header.valid();
    }
};

}  // namespace pipeline_contract
