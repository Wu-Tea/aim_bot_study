#include "ds4_output_report.h"
#include "output_composer.h"
#include "control_frame.h"
#include "test_support/native_test_registry.h"

#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void neutral_and_release() {
    controller_native::GamepadOutputState state;
    state.right_x = 1; state.right_y = -1; state.rb = true;
    state.right_trigger = 1; state.dpad_up = true;
    (void)controller_native::to_ds4_report(state);
    const auto b = controller_native::to_ds4_report({}).bytes;
    require(b[0] == 128 && b[1] == 128 && b[2] == 128 && b[3] == 128,
        "neutral/release must center all four DS4 axes at 128");
    require(b[4] == 8 && b[5] == 0 && b[6] == 0 && b[7] == 0 && b[8] == 0,
        "neutral must release hat, fire, ADS, buttons and triggers");
    for (int i = 12; i < 29; ++i)
        require(b[i] == 0, "unavailable sensors must not retain driver sample data");
    require(b[32] == 0, "no synthesized touch packets");
    for (const int i : {34, 38, 43, 47, 52, 56})
        require(b[i] == 128, "all touch contacts must be lifted");
}

void axes_and_reversal() {
    controller_native::GamepadOutputState state;
    state.left_x = -1; state.left_y = 1;
    state.right_x = 1; state.right_y = -1;
    auto b = controller_native::to_ds4_report(state).bytes;
    require(b[0] == 0 && b[1] == 0 && b[2] == 255 && b[3] == 255,
        "DS4 endpoints/Y direction must preserve native movement");
    state.right_x = -1; state.right_y = 1;
    b = controller_native::to_ds4_report(state).bytes;
    require(b[2] == 0 && b[3] == 0, "reversal must reach this report immediately");
    // Sony HID byte -> SDL signed axis -> project normalization -> DS4 byte.
    // Exhaust all codes, including the asymmetric SDL center and negative Y.
    for (int byte = 0; byte <= 255; ++byte) {
        const int sdl = byte * 257 - 32768;
        const float normalized = float(sdl) / (sdl < 0 ? 32768.0f : 32767.0f);
        state.right_x = normalized;
        state.right_y = -normalized;
        b = controller_native::to_ds4_report(state).bytes;
        require(b[2] == byte && b[3] == byte, "Sony/SDL raw axis code must round-trip");
    }
    require(controller_native::ds4_axis(0) == 128, "AI zero must encode exact neutral");
    require(controller_native::ds4_axis(2) == 255 && controller_native::ds4_axis(-2) == 0,
        "finite out-of-range output must saturate");
    require(controller_native::ds4_axis(std::numeric_limits<float>::quiet_NaN()) == 128 &&
        controller_native::ds4_trigger(std::numeric_limits<float>::infinity()) == 0,
        "invalid values must encode neutral at the device boundary");
}

void buttons_triggers_and_hat() {
    using State = controller_native::GamepadOutputState;
    bool State::* const fields[] = {&State::x, &State::a, &State::b, &State::y,
        &State::lb, &State::rb, &State::back, &State::start,
        &State::left_thumb, &State::right_thumb};
    const unsigned bits[] = {4,5,6,7,8,9,12,13,14,15};
    for (int i = 0; i < 10; ++i) {
        State state; state.*fields[i] = true;
        const auto b = controller_native::to_ds4_report(state).bytes;
        require((unsigned(b[4]) | (unsigned(b[5]) << 8)) == (8u | (1u << bits[i])),
            "button must map to the corresponding DS4 control only");
    }
    State state;
    state.left_trigger = .5f; state.right_trigger = 1;
    auto b = controller_native::to_ds4_report(state).bytes;
    require(b[7] == 128 && b[8] == 255 && b[5] == 12,
        "L2/R2 analog and digital state must agree");
    state = {}; state.guide = true;
    b = controller_native::to_ds4_report(state, 63, 0xabcd).bytes;
    require(b[6] == 253 && b[9] == 0xcd && b[10] == 0xab,
        "PS button, report counter and timestamp must use independent fields");
    const int xs[] = {0,1,1,1,0,-1,-1,-1,0};
    const int ys[] = {-1,-1,0,1,1,1,0,-1,0};
    for (int hat = 0; hat < 9; ++hat) {
        state = {};
        state.dpad_left = xs[hat] < 0; state.dpad_right = xs[hat] > 0;
        state.dpad_up = ys[hat] < 0; state.dpad_down = ys[hat] > 0;
        require(controller_native::to_ds4_report(state).bytes[4] == hat,
            "all eight hat directions and neutral must encode correctly");
    }
    state = {}; state.dpad_left = state.dpad_right = true; state.dpad_up = true;
    require(controller_native::to_ds4_report(state).bytes[4] == 0,
        "opposite horizontal inputs must cancel without dropping up");
}

void touchpad_survives_composition_and_release() {
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.touchpad = true;
    physical.start = true;
    physical.back = true;
    auto frame = controller_native::ControlFrame::begin(physical,
        pipeline_contract::ControllerTickId::from(1),
        pipeline_contract::EventSequence::from(1));
    controller_native::OutputComposer composer;
    require(composer.compose(frame) == controller_native::OutputComposeStatus::Ok,
        "physical touchpad fixture must compose successfully");
    auto b = controller_native::to_ds4_report(*composer.finalized_output(), 63).bytes;
    require((b[6] & 3) == 2 && (b[6] >> 2) == 63 && b[5] == 0x30,
        "touchpad click must survive physical/composer/DS4 chain independently of menu/share/counter");
    const auto released_frame = controller_native::ControlFrame::begin({},
        pipeline_contract::ControllerTickId::from(2),
        pipeline_contract::EventSequence::from(2));
    require(composer.compose(released_frame) == controller_native::OutputComposeStatus::Ok,
        "release fixture must compose successfully");
    b = controller_native::to_ds4_report(*composer.finalized_output()).bytes;
    require((b[6] & 3) == 0 && b[5] == 0,
        "touchpad/menu/share release must reach the next report");
}
void decoded_delivery_round_trips_every_axis_code() {
    for (unsigned int code=0;code<256;++code) {
        const float decoded=controller_native::ds4_axis_value(static_cast<std::uint8_t>(code));
        controller_native::GamepadOutputState output;
        output.right_x=decoded;
        output.right_y=-decoded;
        const auto report=controller_native::to_ds4_report(output);
        require(report.bytes[2]==code && report.bytes[3]==code,
            "delivery feedback must represent the exact report byte on both axes");
    }
}
} // namespace

void register_ds4_output_report_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "ds4_delivery_all_axis_codes", decoded_delivery_round_trips_every_axis_code);
    registry.add_case("BaseContracts", "ds4_neutral_and_release", neutral_and_release);
    registry.add_case("BaseContracts", "ds4_axes_and_reversal", axes_and_reversal);
    registry.add_case("BaseContracts", "ds4_buttons_triggers_hat", buttons_triggers_and_hat);
    registry.add_case("BaseContracts", "ds4_touchpad_composition_release", touchpad_survives_composition_and_release);
}
