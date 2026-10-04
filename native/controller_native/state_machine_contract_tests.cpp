#include "incident_fixture_support.h"
#include "assist_activation_reducer.h"
#include "auto_fire_state_machine.h"
#include "ads_lifecycle_reducer.h"
#include "vision_native/selection_state_machine.h"
#include "test_support/native_test_registry.h"

namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }

void custom_curve_keeps_manual_passthrough() {
    using namespace controller_native;
    auto config = incident_fixture::base_config(50, 180);
    auto& curve = config.aim_response_curve;
    curve.algorithm = AimResponseCurveAlgorithm::CustomLut;
    curve.custom_count = 3;
    curve.custom_stick[0] = curve.custom_response[0] = 0;
    curve.custom_stick[1] = .5f; curve.custom_response[1] = .15f;
    curve.custom_stick[2] = curve.custom_response[2] = 1;
    double clock = 100;
    NativeGamepadController controller(config, &clock);
    PhysicalGamepadState physical{};
    physical.connected = true;
    for (int i = -1000; i <= 1000; ++i) {
        clock += .001;
        physical.right_x = i / 1000.f;
        physical.right_y = -i / 1000.f;
        auto output = controller.build_output(physical);
        require(output.right_x == physical.right_x && output.right_y == physical.right_y,
            "custom response curve must not remap raw manual passthrough or add a deadzone");
    }
}

void activation_transitions() {
    using namespace controller_native;
    using pipeline_contract::AssistActivation;
    AssistActivationReducer owner;
    AimScopeSnapshot scope;
    require(owner.reduce(scope, 1) == AssistActivation::Off, "initial activation");
    owner.request_until(6);
    require(owner.reduce(scope, 1) == AssistActivation::Application, "app activation requires physical ADS");
    scope.assist_active = scope.physical_ads_active = true;
    require(owner.reduce(scope, 2) == AssistActivation::Primed, "light press not primed");
    scope.physical_ads_ready = true;
    require(owner.reduce(scope, 3) == AssistActivation::Engaged, "physical ready priority");
    scope = {};
    require(owner.reduce(scope, 5) == AssistActivation::Application, "physical release erased app lease");
    require(owner.reduce(scope, 6) == AssistActivation::Off, "deadline failed to revoke");
    owner.request_until(9);
    owner.revoke();
    require(owner.reduce(scope, 7) == AssistActivation::Off, "explicit revoke ignored");
    scope.manual_fire_active = scope.assist_active = true;
    require(owner.reduce(scope, 8) == AssistActivation::Engaged && !scope.physical_ads_ready,
        "manual fire fabricated physical ADS");
    owner.reset();
    require(owner.state() == AssistActivation::Off, "reset retained activation");
    require(pipeline_contract::vision_request(AssistActivation::Off, true) ==
        pipeline_contract::VisionRequest::DetectionOnly, "mark acquired AI permission");
}

void application_activation_end_to_end() {
    using namespace controller_native;
    namespace fixture = controller_native::incident_fixture;
    auto config = fixture::base_config(50, 180);
    config.auto_fire.enabled = true;
    double clock = 100;
    NativeGamepadController controller(config, &clock);
    PhysicalGamepadState physical{};
    physical.connected = true;
    fixture::TargetSpec target;
    target.observation_id = 1;
    target.selector_generation = 7;
    target.has_enemy_cue = target.enemy_identity_confirmed = true;
    controller.request_assist_until(100.05);
    bool saw_output = false;
    for (int tick = 0; tick != 70; ++tick) {
        clock = 100 + tick * 0.001;
        controller.submit_vision_snapshot(fixture::observed_snapshot(target, tick + 1, clock, 20, 0));
        auto output = controller.build_output(physical);
        const auto& plan = controller.last_target_plan();
        require(!plan.ads_acquisition_exists && plan.target_acquisition_id == 0,
            "application request created ADS token");
        require(output.right_trigger == 0 && !output.rb, "application request fired");
        const auto& ai = controller.last_output_components().shaped_assist_stick;
        if (tick < 50) require(std::hypot(ai.x, ai.y) <= 0.100001f, "application AI exceeded vector budget");
        if (tick < 50) saw_output |= std::abs(output.right_x) > 0;
        else require(output.right_x == 0 && output.right_y == 0, "expired application output survived");
    }
    require(saw_output, "application mode did not reach real controller output");
    controller.request_assist_until(101);
    controller.revoke_application_assist();
    clock = 100.08;
    physical.right_x = 0.031f;
    auto manual = controller.build_output(physical);
    require(manual.right_x == physical.right_x, "revocation lost zero-deadzone passthrough");
    controller.request_assist_until(101);
    physical.connected = false;
    require(controller.begin_tick(physical).activation == pipeline_contract::AssistActivation::Off,
        "disconnect retained grant");
    physical.connected = true;
    require(controller.begin_tick(physical).activation == pipeline_contract::AssistActivation::Off,
        "reconnection restored stale grant");
    physical.left_trigger = 1;
    clock = 100.09;
    controller.submit_vision_snapshot(fixture::observed_snapshot(target, 100, clock, 20, 0));
    controller.build_output(physical);
    require(controller.last_target_plan().ads_acquisition_exists,
        "real ADS could not acquire after application lease");
}

void ads_wait_identity_and_priority() {
    using namespace controller_native;
    AdsLifecycleReducer owner({250, 135, 220});
    owner.begin_epoch(1, 1);
    owner.begin_tick(true, 1.25);
    require(!owner.select({true, false, false, true}, 1.25), "late candidate admitted");
    owner.begin_epoch(2, 2);
    require(owner.select({true, false, false, true}, 2.01), "fresh candidate not admitted");
    const auto id = owner.snapshot().target_acquisition_id;
    require(owner.missing({true, true, true, false, true}, 2.02) == AdsTargetDisposition::Wait,
        "same identity gap did not wait");
    require(owner.snapshot().evidence_wait == AdsEvidenceWait::SameTarget &&
        owner.snapshot().task_mode == pipeline_contract::ControlMode::Manual,
        "wait retained actuation");
    require(!owner.select({true, false, false, true}, 2.03), "same target reminted token");
    owner.advance(true, true, true, 3);
    require(owner.snapshot().terminal_reason == pipeline_contract::AdsDecisionReason::Settled &&
        owner.snapshot().target_acquisition_id == id, "completion priority or identity changed");
    owner.begin_tick(false, 3.01);
    require(!owner.snapshot().epoch_active && owner.snapshot().target_acquisition_id == 0,
        "scope release retained task");
}

void fire_state_boundaries() {
    using namespace controller_native;
    for (int hz : {160, 250, 1000, 2000}) {
        FirePulseStateMachine pulse;
        ManualFireStateMachine manual;
        int starts = 0, pressed = 0, gaps = 0, guards = 0;
        for (int tick = 0; tick < hz * 10; ++tick) {
            const double now = 1 + double(tick) / hz;
            const bool hand = tick % hz >= hz / 3 && tick % hz < hz / 2;
            auto phase = manual.update(hand, true, now, 0.25);
            const bool authorized = phase == ManualFirePhase::Free && tick % hz < hz * 9 / 10;
            const auto out = pulse.update(authorized, now, 0.016, 0.055);
            require(authorized || (!out.pressed && pulse.phase() == FirePulsePhase::Idle),
                "revoked pulse retained button");
            starts += out.started; pressed += out.pressed;
            gaps += pulse.phase() == FirePulsePhase::Gap;
            guards += phase == ManualFirePhase::ResumeGuard;
        }
        require(starts && pressed && gaps && guards, "fire boundary fixture missed a phase");
        pulse.reset(); manual.reset();
        require(pulse.phase() == FirePulsePhase::Idle && manual.phase() == ManualFirePhase::Free,
            "fire reset retained phase");
    }
}

void selection_state_owns_identity() {
    using namespace vision_native;
    SelectionStateMachine<int> owner;
    require(!owner.confirm(1, false, false, 2), "unconfirmed pickup published");
    require(owner.confirmation_phase() == ConfirmationPhase::Confirming, "confirmation phase missing");
    require(owner.confirm(1, true, false, 2).value() == 1, "confirmation never completed");
    owner.commit(1, true);
    const auto generation = owner.generation();
    require(owner.changed() && owner.has_identity(), "committed identity missing");
    owner.begin_frame();
    require(owner.retain_miss(2) && owner.phase() == SelectionPhase::Missing, "miss destroyed identity");
    owner.observe(1, true);
    require(owner.phase() == SelectionPhase::CueOnly && owner.generation() == generation,
        "cue reminted identity");
    owner.observe(1, false);
    owner.commit(2, true);
    require(owner.generation() == generation + 1 && owner.active().value() == 2, "replacement not atomic");
    owner.clear();
    require(!owner.has_identity() && owner.generation() == generation + 1, "clear reused generation");
}
}  // namespace

void register_state_machine_contract_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "custom_curve_keeps_manual_passthrough", custom_curve_keeps_manual_passthrough);
    registry.add_case("BaseContracts", "activation_transitions", activation_transitions);
    registry.add_case("BaseEndToEnd", "application_activation_end_to_end", application_activation_end_to_end);
    registry.add_case("BaseAds", "ads_wait_identity_and_priority", ads_wait_identity_and_priority);
    registry.add_case("BaseContracts", "fire_state_boundaries", fire_state_boundaries);
    registry.add_case("BaseVisionSelection", "selection_state_owns_identity", selection_state_owns_identity);
}
