#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"
#include <fstream>
#include <iostream>

// Local invariant fixture, not a replay of the unrecorded live incident.
int main(int argc, char** argv) {
    using namespace mouse_native;
    namespace f = controller_native::incident_fixture;
    std::ostream* report = &std::cout;
    std::ofstream file;
    if (argc == 2) { file.open(argv[1]); report = &file; }
    bool pass = true;
    for (int axis = 0; axis < 2; ++axis) for (int period : {1, 2})
    for (int frame_ms : {1, 16, 33}) for (bool enabled : {true, false}) {
        MouseControllerFacadeConfig config;
        config.tuning = {2, 4, enabled ? 0.5f : 0.0f};
        MouseControllerFacade controller(config);
        auto profile = make_cod_default_profile({}, MouseAimMode::Ads);
        f::TargetSpec target;
        target.observation_id = 77; target.selector_generation = 9;
        target.body_width = 300; target.body_height = 700;
        target.color_classified = target.has_enemy_cue = target.enemy_identity_confirmed = true;
        double now = 0; int tick = 0;
        const auto step = [&](MouseSourceCounts source, int ms) {
            now += ms * 0.001; ++tick;
            if (tick == 1 || (tick * ms) % frame_ms < ms)
                controller.submit_vision_snapshot(f::observed_snapshot(target, tick, now, 0, 0));
            return controller.tick({source, profile, now, static_cast<std::uint64_t>(tick), true, false});
        };
        for (int i = 0; i < 100; ++i) step({}, 1);
        const bool bodylock = controller.controller().last_ai_aim_mode() == "body_lock";
        int total = 0, first = -1;
        for (int elapsed = 0; elapsed < 80; elapsed += period) {
            auto out = step(axis ? MouseSourceCounts{0,period} : MouseSourceCounts{period,0}, period);
            const int value = axis ? out.actuation.dy : out.actuation.dx;
            total += value;
            if (value > 0 && first < 0) first = elapsed + period;
        }
        // At most 20 counts may be withheld by the spatial noise allowance;
        // a slow 80-count deliberate drag must produce at least 60 counts.
        const bool ok = bodylock && total >= 60 && first >= 0 && first <= 20;
        pass = pass && ok;
        *report << "{\"axis\":" << axis << ",\"tick_ms\":" << period
            << ",\"frame_ms\":" << frame_ms << ",\"enabled\":" << enabled
            << ",\"bodylock\":" << bodylock << ",\"input\":80,\"output\":" << total
            << ",\"first_ms\":" << first << ",\"pass\":" << ok << "}\n";
    }
    return pass ? 0 : 1;
}
