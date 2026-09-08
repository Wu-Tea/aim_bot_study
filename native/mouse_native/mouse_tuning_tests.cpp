#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
using namespace mouse_native;
namespace fixture = controller_native::incident_fixture;

struct Rig {
    MouseControllerFacade controller;
    MouseResponseProfile response = make_cod_default_profile({}, MouseAimMode::Ads);
    fixture::TargetSpec target{};
    std::uint64_t tick = 0;
    bool saw_ads = false, saw_body = false;
    explicit Rig(MouseControllerTuning tuning) : controller([&]() {
        MouseControllerFacadeConfig config;
        config.controller.auto_fire.enabled = false;
        config.tuning = tuning;
#ifdef MOUSE_TUNING_LEGACY_REFERENCE
        config.tuning = {1, 1};
#endif
        return config;
    }()) {
        target.observation_id = 77;
        target.selector_generation = 9;
        target.color_classified = true;
        target.has_enemy_cue = true;
        target.enemy_identity_confirmed = true;
    }
    MouseControllerTickResult step(float x, float y, MouseSourceCounts source = {},
                                   bool aiming = true, bool present = true) {
        const double now = (++tick) * 0.001;
        auto snapshot = present ? fixture::observed_snapshot(target, tick, now, x, y)
                                : fixture::empty_snapshot(target, tick, now);
        controller.submit_vision_snapshot(snapshot);
        auto out = controller.tick({source, response, now, tick, aiming, false});
        saw_ads |= controller.controller().last_ai_aim_mode() == "ads_snap";
        saw_body |= controller.controller().last_ai_aim_mode() == "body_lock";
        return out;
    }
    void acquire(MouseSourceCounts continued_source = {}) {
        for (int i = 0; i < 60; ++i) step(0, 0, continued_source);
        require(saw_body, "trigger: fixture must reach BodyLock before the measured sample");
        require(controller.controller().last_target_plan().visual_authority > 0.8f,
            "trigger: retention fixture requires strong current target evidence");
    }
};

struct Metrics {
    double ads_ratio_x = 0, ads_ratio_y = 0, body_ratio_x = 0, body_ratio_y = 0;
    int corrected_axes = 0, held_axes = 0, exact_exit_axes = 0, no_target_axes = 0, released_axes = 0;
};

double speed_ratio(bool body, int axis) {
    Rig old({1, 1}), tuned({1.5f, 4});
    if (body) { old.acquire(); tuned.acquire(); }
    std::int64_t old_counts = 0, tuned_counts = 0;
    for (int i = 0; i < 100; ++i) {
        const float error = body ? 18.0f : 80.0f;
        const auto a = old.step(axis == 0 ? error : 0, axis == 1 ? error : 0);
        const auto b = tuned.step(axis == 0 ? error : 0, axis == 1 ? error : 0);
        require(a.controller_used && b.controller_used, "trigger: both speed samples use production controller");
        old_counts += std::abs(axis ? a.actuation.dy : a.actuation.dx);
        tuned_counts += std::abs(axis ? b.actuation.dy : b.actuation.dx);
    }
    require(body ? old.saw_body && tuned.saw_body : old.saw_ads && tuned.saw_ads,
        "trigger: comparison must exercise its declared mode");
    require(old_counts > 0, "trigger: baseline must deliver nonzero speed");
    std::cout << "[Speed] body=" << body << " axis=" << axis << " counts=" << old_counts << '/' << tuned_counts << '\n';
    return static_cast<double>(tuned_counts) / old_counts;
}

Metrics measure() {
    Metrics m;
    m.ads_ratio_x = speed_ratio(false, 0); m.ads_ratio_y = speed_ratio(false, 1);
    m.body_ratio_x = speed_ratio(true, 0); m.body_ratio_y = speed_ratio(true, 1);
    for (int axis = 0; axis < 2; ++axis) {
        Rig tuned({1.5f, 4}); tuned.acquire();
        const MouseSourceCounts small{axis ? 0 : 2, axis ? 2 : 0};
        // The original post-acquisition onset is a valid edit of D, owned by
        // TargetCoordinator. Keep that trigger and require exact counts; it
        // cannot prove suppression of ordinary conflicting movement.
        const auto corrected = tuned.step(0, 0, small);
        const auto& corrected_plan = tuned.controller.controller().last_target_plan();
        const auto& corrected_components = tuned.controller.controller().last_output_components();
        const bool correction = axis ? corrected_plan.manual_correction_y : corrected_plan.manual_correction_x;
        const float correction_retention = axis ? corrected_components.mouse_manual_retention.y
                                               : corrected_components.mouse_manual_retention.x;
        const std::string correction_conflict = axis ? corrected_components.mouse_manual_conflict_y
                                                    : corrected_components.mouse_manual_conflict_x;
        std::cout << "[PointEdit] axis=" << axis << " used=" << corrected.controller_used
            << " transparent=" << corrected.transparent
            << " mode=" << tuned.controller.controller().last_ai_aim_mode()
            << " output=" << corrected.actuation.dx << ',' << corrected.actuation.dy
            << " visual=" << corrected_plan.visual_authority << " correction=" << correction
            << " conflict=" << correction_conflict << " retention=" << correction_retention << '\n';
        require(corrected.controller_used && !corrected.transparent &&
                tuned.controller.controller().last_ai_aim_mode() == "body_lock" &&
                corrected_plan.visual_authority > 0.8f && correction &&
                (axis ? corrected_components.manual_correction_y : corrected_components.manual_correction_x) &&
                corrected_plan.desired_point_source == pipeline_contract::DesiredPointSource::UserCorrected &&
                !corrected_plan.manual_exit_requested,
            "trigger: new owned-target gesture must be a protected desired-point edit in BodyLock");
        require(correction_retention == 1.0f && correction_conflict == "none",
            "valid desired-point editing must remain outside ordinary mouse conflict attenuation");
        m.corrected_axes += corrected.actuation.dx == small.dx && corrected.actuation.dy == small.dy;

        // Matched source counts, response and target: only gesture admission
        // differs. Keep the pre-target gesture active so it cannot edit D.
        Rig continued({1.5f, 4});
        const auto before_admission = continued.step(0, 0, small, true, false);
        require(before_admission.manual_input.valid && !before_admission.manual_input.transparent &&
                before_admission.vision_intent.purpose == pipeline_contract::UserAimIntentPurpose::AcquireTarget &&
                continued.controller.controller().last_target_plan().target_id == 0,
            "trigger: the conflicting gesture must begin before target admission within the physical rate envelope");
        continued.acquire(small);
        const auto continued_out = continued.step(0, 0, small);
        const auto& continued_plan = continued.controller.controller().last_target_plan();
        const auto& continued_components = continued.controller.controller().last_output_components();
        const float manual_axis = axis ? continued_out.manual_input.y : continued_out.manual_input.x;
        const float error_axis = axis ? -continued_plan.error_px.y : continued_plan.error_px.x;
        const float retention = axis ? continued_components.mouse_manual_retention.y
                                     : continued_components.mouse_manual_retention.x;
        const std::string conflict = axis ? continued_components.mouse_manual_conflict_y
                                         : continued_components.mouse_manual_conflict_x;
        // Freeze the existing 4x response units, 12 ms lookahead and 8 px
        // completion boundary; do not obtain an expected result by calling
        // the production judgment under test.
        const float predicted_error = error_axis - manual_axis * 2000.0f * 0.012f;
        std::cout << "[ContinuedHold] axis=" << axis
            << " used=" << continued_out.controller_used << " transparent=" << continued_out.transparent
            << " mode=" << continued.controller.controller().last_ai_aim_mode()
            << " output=" << continued_out.actuation.dx << ',' << continued_out.actuation.dy
            << " visual=" << continued_plan.visual_authority
            << " correction=" << (axis ? continued_plan.manual_correction_y : continued_plan.manual_correction_x)
            << " conflict=" << conflict << " retention=" << retention
            << " error=" << error_axis << " manual=" << manual_axis
            << " predicted_error=" << predicted_error << '\n';
        require(continued_out.controller_used && !continued_out.transparent &&
                continued.controller.controller().last_ai_aim_mode() == "body_lock" &&
                continued_plan.visual_authority > 0.8f &&
                !continued_plan.manual_correction_x && !continued_plan.manual_correction_y &&
                !continued_components.manual_correction_x && !continued_components.manual_correction_y &&
                !continued_plan.manual_exit_requested &&
                continued_out.vision_intent.purpose == pipeline_contract::UserAimIntentPurpose::AcquireTarget,
            "trigger: continued pre-admission gesture must reach authoritative BodyLock without gaining D-edit authority");
        require(continued_out.manual_input.valid && !continued_out.manual_input.envelope_exceeded &&
                std::abs(manual_axis) > 0.0f && std::abs(manual_axis) < 1.0f &&
                std::abs(error_axis) < 1.0e-5f && std::abs(predicted_error) > 8.0f &&
                conflict == "predicted_exit",
            "trigger: below-envelope movement must predict exit from the frozen centered hold boundary");
        require(std::abs(retention - 0.2875f) < 1.0e-6f,
            "strong current evidence must apply the existing 1/4 retention floor with its unchanged authority weight");
        m.held_axes += std::abs(axis ? continued_out.actuation.dy : continued_out.actuation.dx) < 2;
        const MouseSourceCounts large{axis ? 0 : 20, axis ? 20 : 0};
        const auto exit = tuned.step(0, 0, large);
        m.exact_exit_axes += exit.transparent && exit.actuation.dx == large.dx && exit.actuation.dy == large.dy;
        Rig no_target({1.5f, 4});
        const auto manual = no_target.step(0, 0, small, false, false);
        m.no_target_axes += manual.actuation.dx == small.dx && manual.actuation.dy == small.dy;
        Rig released({1.5f, 4}); released.acquire();
        const auto out = released.step(0, 0, small, false, false);
        m.released_axes += out.actuation.dx == small.dx && out.actuation.dy == small.dy && !out.auto_fire_active;
    }
    return m;
}

void test_tuning_boundaries_and_manual_units() {
    require(valid(MouseControllerTuning{0.5f, 1}) && valid(MouseControllerTuning{3, 8}),
        "supported endpoints must be accepted");
    for (auto invalid : {MouseControllerTuning{0, 4}, MouseControllerTuning{4, 4},
            MouseControllerTuning{2, 1}, MouseControllerTuning{1, 9},
            MouseControllerTuning{std::numeric_limits<float>::quiet_NaN(), 4}}) {
        bool rejected = false;
        try { (void)make_mouse_controller_config({}, invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid tuning must be rejected before interception starts");
    }
    // Raw counts, including negative and diagonal motion, retain their units
    // for all supported tuning values when no target owns the output.
    for (auto tuning : {MouseControllerTuning{0.5f, 1}, MouseControllerTuning{1.5f, 4},
                       MouseControllerTuning{3, 8}}) {
        Rig rig(tuning);
        for (auto counts : {MouseSourceCounts{1,-1}, MouseSourceCounts{-2,3}, MouseSourceCounts{100,-90}}) {
            const auto out = rig.step(0, 0, counts, false, false);
            require(out.actuation.dx == counts.dx && out.actuation.dy == counts.dy,
                "speed and breakaway tuning must not change free manual sensitivity");
        }
    }
}

void test_autofire_readiness_keeps_physical_speed_units() {
    for (float speed : {0.10f, 0.25f}) {
        auto config = make_mouse_controller_config({}, {1.5f, 4});
        config.ai_aim.auto_fire_ready_frames = 1;
        controller_native::AutoFireGate gate(config.auto_fire, config.ai_aim);
        controller_native::AutoFireGateInput in;
        in.now_seconds = 1;
        in.aiming = true;
        in.output_right_x = speed / 4;
        auto& v = in.vision_state;
        v.has_target = v.auto_fire_requested = v.aim_authority = v.fire_authority = true;
        v.current_observed_target_present = true;
        v.target_tier = "strong";
        v.observed_at_seconds = 1;
        v.vision_sequence = 1;
        require(gate.evaluate(in).aim_ready == (speed < 6000.0f / 32767.0f),
            "new stick units cannot make AutoFire treat fast motion as settled");
    }
}

void test_mouse_judges_manual_without_counterforce() {
    controller_native::AssistControlStateMachineConfig config;
    config.direct_mouse_manual = true;
    config.bodylock_manual_weight = 0.25f;
    for (float manual : {-0.4f, 0.0f, 0.4f}) {
        controller_native::AssistControlStateMachine owner(config);
        controller_native::AssistControlStateMachineInput in;
        in.aiming = in.target_authoritative = in.fresh_observation = true;
        in.target_id = in.selector_target_generation = 9;
        in.visual_authority = 1;
        in.mode = pipeline_contract::ControlMode::BodyLockFollow;
        in.manual_stick = in.centered_manual_stick = {manual, -manual};
        in.centered_manual_available = true;
        in.ai_stick = {0.2f, -0.1f};
        auto out = owner.update(in);
        const float expected_x = manual > 0 ? manual : 0.2f + manual * 0.25f;
        const float expected_y = manual > 0 ? -manual : -0.1f - manual * 0.25f;
        require(std::abs(out.stick.x - expected_x) < 1e-6f && std::abs(out.stick.y - expected_y) < 1e-6f,
            "helpful manual stays intact; conflicting manual is reduced before bounded AI work");
        require(out.mouse_x.retention == (manual < 0 ? 0.25f : 1.0f) &&
                out.mouse_y.retention == (manual < 0 ? 0.25f : 1.0f),
            "only judged opposing axes may receive the configured attenuation");
        require(std::abs(out.mouse_x.ai_remainder) <= 0.2f && std::abs(out.mouse_y.ai_remainder) <= 0.1f,
            "AI must not grow an opposing-manual cancellation term");
        in.ai_stick = {};
        out = owner.update(in);
        require(out.stick.x == manual && out.stick.y == -manual &&
                out.mouse_x.retention == 1 && out.mouse_y.retention == 1,
            "idle axes within the motion envelope preserve original manual input");
        in.aiming = false;
        out = owner.update(in);
        require(out.stick.x == manual && out.stick.y == -manual,
            "release bypasses mouse suppression in the same tick");
    }
}
}

int main(int argc, char** argv) {
    try {
        test_tuning_boundaries_and_manual_units();
        test_autofire_readiness_keeps_physical_speed_units();
        test_mouse_judges_manual_without_counterforce();
        const auto m = measure();
        std::ofstream file;
        if (argc == 2) file.open(argv[1]);
        std::ostream& out = file.is_open() ? file : std::cout;
        out << "{\"ads_ratio_x\":" << m.ads_ratio_x << ",\"ads_ratio_y\":" << m.ads_ratio_y
            << ",\"body_ratio_x\":" << m.body_ratio_x << ",\"body_ratio_y\":" << m.body_ratio_y
            << ",\"corrected_axes\":" << m.corrected_axes
            << ",\"held_axes\":" << m.held_axes << ",\"exact_exit_axes\":" << m.exact_exit_axes
            << ",\"no_target_axes\":" << m.no_target_axes << ",\"released_axes\":" << m.released_axes << "}\n";
        require(m.ads_ratio_x >= 1.35 && m.ads_ratio_y >= 1.35 &&
                m.body_ratio_x >= 1.35 && m.body_ratio_y >= 1.35,
            "speed control must increase both ADS/BodyLock axes by at least 35 percent in fixed command scenarios");
        require(m.corrected_axes == 2, "valid desired-point edits must preserve exact physical counts on both axes");
        require(m.held_axes == 2, "below-envelope conflicting physical input must not bypass BodyLock");
        require(m.exact_exit_axes == 2 && m.no_target_axes == 2 && m.released_axes == 2,
            "breakaway, no-target and release must preserve physical counts");
    } catch (const std::exception& error) {
        std::cerr << "[MouseTuningTests] FAIL: " << error.what() << '\n'; return 1;
    }
    std::cout << "[MouseTuningTests] PASS\n";
}
