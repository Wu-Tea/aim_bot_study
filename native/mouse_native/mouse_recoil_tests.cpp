#include "mouse_native/mouse_controller_runtime_core.h"
#include "controller_native/incident_fixture_support.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void integration(int step_ms) {
    using namespace mouse_native;
    MouseControllerFacadeConfig config; config.tuning = {2,4,0.5f}; config.recoil = {true, 37.5, true};
    MouseControllerRuntimeCore runtime(config);
    // No target must not prevent recoil during intentional firing.
    int total = 0; std::uint64_t id = 0;
    for (int ms = 0; ms <= 2000; ms += step_ms) {
        auto out = runtime.tick({{1,0}, 1 + ms * .001, ++id, true, true});
        total += out.output.counts.dy;
        check(out.output.counts.dx == 1 && out.output.counts.dy == out.controller.recoil.dy,
            "recoil must compose once and preserve unrelated physical X");
    }
    check(total == 75, "elapsed-time pull must equal 75 counts at every cadence");
    auto stop = runtime.tick({{},3.001,++id,true,false});
    check(stop.output.counts.dy == 0 && !stop.controller.recoil.active, "release revokes recoil same tick");
    runtime.reset();
    auto start = runtime.tick({{},4,++id,true,true});
    check(start.controller.recoil.dy == 0, "reset cannot replay prior recoil debt");
}
void boundaries() {
    using namespace mouse_native;
    MouseRecoil recoil({true,100,true});
    check(!recoil.tick(1,false,true,true).active, "ADS required");
    check(!recoil.tick(1,true,false,true).active, "firing required");
    check(!recoil.tick(1,true,true,false).active, "calibration cannot accumulate recoil");
    recoil.tick(1,true,true,true);
    check(recoil.tick(1.01,true,true,true).dy == 1, "rate output");
    auto gap = recoil.tick(2,true,true,true);
    check(gap.dy == 0 && gap.clock_discontinuity, "stall cannot create catch-up burst");
    check(recoil.tick(1,true,true,true).clock_discontinuity, "clock reversal resets debt");
    check(!recoil.tick(std::numeric_limits<double>::quiet_NaN(),true,true,true).active, "invalid time revokes");
    MouseRecoil hip({true,100,false}); hip.tick(1,false,true,true);
    check(hip.tick(1.01,false,true,true).dy == 1, "optional hipfire");
    MouseRecoil disabled({false,100,false});
    check(!disabled.tick(1,false,true,true).active, "disabled passthrough");
}
void bodylock_and_calibration() {
    using namespace mouse_native;
    namespace f = controller_native::incident_fixture;
    MouseControllerFacadeConfig config; config.tuning={2,4,0.5f}; config.recoil={true,100,true};
    MouseControllerRuntimeCore runtime(config);
    f::TargetSpec target; target.observation_id=77; target.selector_generation=9;
    target.color_classified=target.has_enemy_cue=target.enemy_identity_confirmed=true;
    int recoil_total=0; bool bodylock=false;
    for (int i=0;i<=100;++i) {
        const double now=1+i*.001;
        runtime.submit_vision_snapshot(f::observed_snapshot(target,i+1,now,0,0));
        auto out=runtime.tick({{},now,static_cast<std::uint64_t>(i+1),true,true});
        recoil_total+=out.controller.recoil.dy;
        bodylock |= runtime.facade().controller().last_ai_aim_mode()=="body_lock";
        check(out.output.counts.dy==out.controller.aim_counts.dy+out.controller.recoil.dy,
            "BodyLock deadzone cannot swallow feed-forward, compose once");
    }
    check(bodylock && recoil_total==10, "fixture needs BodyLock and 10 downward counts");
    auto start=runtime.begin_calibration(true,1100000000);
    check(start.started, "calibration trigger");
    runtime.acknowledge_calibration_output(true,1100000000);
    auto out=runtime.tick({{},1.101,102,true,true});
    check(!out.controller.recoil.active && out.controller.recoil.dy==0, "F11 disables recoil even while firing");
}
}
int main() {
    try { for (int ms : {1,2,4,8}) integration(ms); boundaries(); bodylock_and_calibration(); }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    std::cout<<"PASS recoil duration, composition, BodyLock, calibration and release boundaries\n";
}
