#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mouse_native;
namespace fixture = controller_native::incident_fixture;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
MouseControllerFacadeConfig configuration(float deadzone) {
    MouseControllerFacadeConfig c;
    c.tuning = {2.0f, 4.0f};
#ifndef MOUSE_BODYLOCK_DEADZONE_RED
    c.tuning.bodylock_deadzone = deadzone;
#else
    (void)deadzone;
#endif
    return c;
}
struct Rig {
    MouseControllerFacade controller;
    MouseResponseProfile response = make_cod_default_profile({}, MouseAimMode::Ads);
    fixture::TargetSpec target;
    std::uint64_t tick = 0;
    double now = 0;
    explicit Rig(float deadzone = 0.5f) : controller(configuration(deadzone)) {
        target.observation_id = 77; target.selector_generation = 9;
        target.color_classified = target.has_enemy_cue = target.enemy_identity_confirmed = true;
    }
    MouseControllerTickResult step(MouseSourceCounts counts = {}, bool aiming = true,
            bool present = true, double dt = 0.001, float error = 0) {
        now += dt; ++tick;
        controller.submit_vision_snapshot(present
            ? fixture::observed_snapshot(target, tick, now, error, 0)
            : fixture::empty_snapshot(target, tick, now));
        return controller.tick({counts, response, now, tick, aiming, false});
    }
    void acquire() {
        for (int i = 0; i < 60; ++i) step();
        check(controller.controller().last_ai_aim_mode() == "body_lock" &&
                controller.controller().last_target_plan().visual_authority > 0.8f,
            "fixture requires established authoritative BodyLock");
    }
};
void jitter_matches_quiet_controller() {
    for (int axis = 0; axis < 2; ++axis) {
        Rig quiet, jitter;
        quiet.acquire(); jitter.acquire();
        const auto initial = jitter.controller.controller().last_target_plan();
        for (int i = 0; i < 48; ++i) {
            const int count = (i % 2 ? -1 : 1) * (i % 4 < 2 ? 1 : 2);
            const auto a = quiet.step({}, true, true, 0.001, i < 8 ? 0 : 12);
            const auto b = jitter.step(axis ? MouseSourceCounts{0,count} : MouseSourceCounts{count,0},
                true, true, 0.001, i < 8 ? 0 : 12);
            const auto& plan = jitter.controller.controller().last_target_plan();
            if (plan.manual_correction_x || plan.manual_correction_y ||
                a.actuation.dx != b.actuation.dx || a.actuation.dy != b.actuation.dy)
                std::cout << "axis=" << axis << " sample=" << i << " M=" << count
                    << " quiet_T=" << a.actuation.dx << ',' << a.actuation.dy
                    << " jitter_T=" << b.actuation.dx << ',' << b.actuation.dy
                    << " D_edit=" << plan.manual_correction_x << ',' << plan.manual_correction_y << '\n';
            check(!plan.manual_correction_x && !plan.manual_correction_y &&
                    std::abs(plan.desired_point_normalized.x - initial.desired_point_normalized.x) < 1e-6f &&
                    std::abs(plan.desired_point_normalized.y - initial.desired_point_normalized.y) < 1e-6f,
                "RED: BodyLock jitter must not edit D or become a target-selection gesture");
            check(!b.vision_intent.valid && !b.vision_intent.has_direction,
                "BodyLock jitter must not send a selection direction to Vision");
            check(a.actuation.dx == b.actuation.dx && a.actuation.dy == b.actuation.dy,
                "jitter must leave the existing AI final counts identical to the quiet control");
        }
    }
}
void manual_boundaries() {
    for (int axis = 0; axis < 2; ++axis) {
        const MouseSourceCounts small = axis ? MouseSourceCounts{0,2} : MouseSourceCounts{2,0};
        const MouseSourceCounts intentional = axis ? MouseSourceCounts{0,3} : MouseSourceCounts{3,0};
        const auto exact = [&](const MouseControllerTickResult& out, MouseSourceCounts counts) {
            check(out.actuation.dx == counts.dx && out.actuation.dy == counts.dy,
                "released/lost/disabled/intentional manual counts must remain exact");
        };
        { Rig f; exact(f.step(small, false, false), small); }
        { Rig f; f.acquire(); exact(f.step(small, false), small); }
        { Rig f; f.acquire(); exact(f.step(small, true, false), small); }
        { Rig f(0); f.acquire(); exact(f.step(small), small);
          const auto& p = f.controller.controller().last_target_plan();
          check(axis ? p.manual_correction_y : p.manual_correction_x, "zero setting must preserve prior point editing"); }
        { Rig f; f.acquire(); exact(f.step(intentional), intentional);
          const auto& p = f.controller.controller().last_target_plan();
          check(axis ? p.manual_correction_y : p.manual_correction_x, "above-deadzone intent must still edit D"); }
        { Rig f; f.acquire(); const MouseSourceCounts flick = axis ? MouseSourceCounts{0,20} : MouseSourceCounts{20,0};
          const auto out = f.step(flick); exact(out, flick); check(out.transparent, "physical speed escape must precede deadzone"); }
        { Rig f; f.acquire(); const auto out = f.step(axis ? MouseSourceCounts{0,4} : MouseSourceCounts{4,0}, true, true, 0.002);
          check(out.actuation.dx == 0 && out.actuation.dy == 0,
              "equivalent physical rate at 2 ms must retain the same deadzone behavior"); }
        { Rig f; f.acquire(); f.now += 0.2; ++f.tick;
          exact(f.controller.tick({small, f.response, f.now, f.tick, true, false}), small);
          check(f.controller.controller().last_target_plan().target_id == 0,
              "expired source must revoke the old BodyLock deadzone in the same tick"); }
    }
    { Rig f; f.acquire(); const auto out = f.step({3,2});
      const auto& p = f.controller.controller().last_target_plan();
      check(out.actuation.dx == 3 && out.actuation.dy == 0 &&
              p.manual_correction_x && !p.manual_correction_y,
          "intentional X editing must not grant orthogonal Y jitter authority"); }
    { Rig active, disabled(0);
      for (int i = 0; i < 12; ++i) {
          const auto a = active.step({1,-1}, true, true, 0.001, 80);
          const auto b = disabled.step({1,-1}, true, true, 0.001, 80);
          check(active.controller.controller().last_ai_aim_mode() == "ads_snap" &&
                  a.actuation.dx == b.actuation.dx && a.actuation.dy == b.actuation.dy,
              "BodyLock deadzone must not alter ADS acquisition");
      } }
}
void close_target_sparse_frames_keep_ai_active() {
    for (int frame_ms : {1,16,33}) {
        Rig quiet, jitter;
        quiet.target.body_width=jitter.target.body_width=300;
        quiet.target.body_height=jitter.target.body_height=700;
        quiet.acquire(); jitter.acquire();
        int moved=0;
        for (int i=0;i<120;++i) {
            const auto advance = [&](Rig& rig, MouseSourceCounts counts) {
                rig.now+=.001; ++rig.tick;
                if(i%frame_ms==0) rig.controller.submit_vision_snapshot(
                    fixture::observed_snapshot(rig.target,rig.tick,rig.now,20,12));
                return rig.controller.tick({counts,rig.response,rig.now,rig.tick,true,false});
            };
            const auto a=advance(quiet,{});
            const auto b=advance(jitter,{i%2 ? -1:1,i%2 ? 1:-1});
            moved+=std::abs(a.actuation.dx)+std::abs(a.actuation.dy);
            check(jitter.controller.controller().last_ai_aim_mode()=="body_lock",
                "large target fixture must remain in BodyLock");
            check(a.actuation.dx==b.actuation.dx && a.actuation.dy==b.actuation.dy,
                "sparse vision jitter must not change the AI output");
        }
        check(moved>0,"off-center close target must receive actual AI movement despite sparse vision");
    }
}
}
int main() {
    try { jitter_matches_quiet_controller(); manual_boundaries(); close_target_sparse_frames_keep_ai_active(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "PASS BodyLock jitter, unchanged AI, Vision intent, D, manual boundaries and dt\n";
}
