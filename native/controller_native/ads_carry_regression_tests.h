#pragma once
#include "incident_fixture_support.h"

inline void ads_carry_does_not_become_correction_on_timeout() {
    using namespace controller_native;
    for (bool vertical : {false, true}) for (float sign : {-1.f, 1.f})
    for (bool firing : {false, true}) {
        auto config = incident_fixture::base_config(40, 180);
        config.ai_aim.normalized_assist_parameters = true;
        config.ai_aim.ads_completion_fresh_frames = 1000;
        config.ai_aim.ads_extension_budget_ms = 100;
        config.recoil.enabled = firing;
        config.recoil.feedback_amount = .2f;
        config.auto_fire.manual_fire_input = "RB";
        double now = 10;
        NativeGamepadController controller(config, &now);
        incident_fixture::TargetSpec spec;
        spec.observation_id = 1; spec.selector_generation = 33;
        unsigned checked = 0, corrected = 0;
        for (int tick = 0; tick < 750; ++tick) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec, tick + 1, now, vertical ? 0 : -sign * 40,
                vertical ? sign * 40 : 0));
            const float manual = tick >= 500 && tick < 540 ? 0 : sign * .4f;
            auto physical = incident_fixture::ads_input(vertical ? 0 : manual, vertical ? manual : 0);
            physical.rb = firing;
            controller.build_output(physical);
            const auto& plan = controller.last_target_plan();
            const auto c = controller.last_output_components();
            const float pre = vertical ? c.before_recoil_stick.y : c.before_recoil_stick.x;
            if (tick >= 350 && tick < 490) {
                if (plan.mode != pipeline_contract::ControlMode::AdsAcquire ||
                    (vertical ? plan.manual_correction_y : plan.manual_correction_x))
                    throw std::runtime_error("carry fixture lost unfinished ADS or reclassified intent");
                const bool protected_down = firing && vertical && sign < 0;
                if (!protected_down && pre * sign >= 0)
                    throw std::runtime_error("ADS timeout let carried acquisition input veto correction");
                if (protected_down && std::fabs(pre - manual) > 1e-5f)
                    throw std::runtime_error("firing downward authority lost");
                ++checked;
            }
            if (tick >= 560) {
                if (!(vertical ? plan.manual_correction_y : plan.manual_correction_x) ||
                    std::fabs(pre - manual) > 1e-5f)
                    throw std::runtime_error("fresh correction authority lost after carry fix");
                ++corrected;
            }
            now += .001;
        }
        if (checked != 140 || corrected != 190) throw std::runtime_error("incomplete carry regression");
    }
}
