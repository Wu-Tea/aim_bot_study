#include "vertical_bodylock_defect_benchmark.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::abort(); }
}
}

void write_metric(std::ostream& out, const controller_native::vertical_defect::Metrics& metric) {
    out << "    {\n"
        << "      \"name\": \"" << metric.name << "\",\n"
        << "      \"defect_reproduced\": " << (metric.defect_reproduced ? "true" : "false") << ",\n"
        << "      \"selector_target_y\": " << metric.selector_target_y << ",\n"
        << "      \"visible_body_top\": " << metric.visible_body_top << ",\n"
        << "      \"visible_body_bottom\": " << metric.visible_body_bottom << ",\n"
        << "      \"target_distance_to_body_px\": " << metric.target_distance_to_body_px << ",\n"
        << "      \"max_overshoot_px\": " << metric.max_overshoot_px << ",\n"
        << "      \"outside_body_frames\": " << metric.outside_body_frames << ",\n"
        << "      \"ai_opposes_recovery_frames\": " << metric.ai_opposes_recovery_frames << ",\n"
        << "      \"manual_escape_frame\": " << metric.manual_escape_frame << ",\n"
        << "      \"recovery_start_frame\": " << metric.recovery_start_frame << ",\n"
        << "      \"reacquire_frame\": " << metric.reacquire_frame << "\n"
        << "    }";
}

void write_takeover_metric(
    std::ostream& out,
    const controller_native::vertical_defect::ManualTakeoverMetrics& metric) {
    out << "    {\n"
        << "      \"name\": \"" << metric.name << "\",\n"
        << "      \"defect_reproduced\": " << (metric.defect_reproduced ? "true" : "false") << ",\n"
        << "      \"manual_direction_preservation_ratio\": " << metric.manual_direction_preservation_ratio << ",\n"
        << "      \"manual_reversal_frames\": " << metric.manual_reversal_frames << ",\n"
        << "      \"max_continuous_reversal_ms\": " << metric.max_continuous_reversal_ms << ",\n"
        << "      \"manual_stall_ms\": " << metric.manual_stall_ms << ",\n"
        << "      \"manual_takeover_latency_ms\": " << metric.manual_takeover_latency_ms << ",\n"
        << "      \"old_target_resistance_integral\": " << metric.old_target_resistance_integral << ",\n"
        << "      \"mode_transitions\": " << metric.mode_transitions << "\n"
        << "    }";
}

int main(int argc, char** argv) {
    const auto large_vertical =
        controller_native::vertical_defect::run_large_vertical_cooperative_acquisition();
    std::cout << "large_vertical initial_px="
              << large_vertical.target_distance_to_body_px
              << " overshoot_px=" << large_vertical.max_overshoot_px
              << " obsolete_up_frames="
              << large_vertical.ai_opposes_recovery_frames
              << " recovery_frame=" << large_vertical.recovery_start_frame << '\n';
    require(large_vertical.target_distance_to_body_px >= 200.0 &&
                large_vertical.target_distance_to_body_px <= 300.0,
        "large vertical fixture must start 200-300px above the reticle");
    require(large_vertical.defect_reproduced,
        "large vertical cooperative acquisition must expose current overshoot debt");

    const auto slide_recoil =
        controller_native::vertical_defect::run_slide_recoil_dropout();
    std::cout << "slide_recoil peak_error_px="
              << slide_recoil.max_overshoot_px
              << " dropout_frames=" << slide_recoil.outside_body_frames
              << " obsolete_up_frames="
              << slide_recoil.ai_opposes_recovery_frames
              << " reacquire_frame=" << slide_recoil.reacquire_frame << '\n';
    require(slide_recoil.reacquire_frame >= 0,
        "slide/recoil fixture must publish a same-target reacquisition");
    require(slide_recoil.defect_reproduced,
        "slide plus upward recoil and dropout must expose current tracking debt");
    if (argc == 2 && std::string(argv[1]) == "--new-stress-only") {
        std::cout << "[VerticalStressDiagnosticTests] PASS defects_reproduced=2\n";
        return 0;
    }

    const auto prone = controller_native::vertical_defect::run_prone_air_lock();
    std::cout << "prone target=" << prone.selector_target_y
              << " escape=" << prone.manual_escape_frame
              << " oppose=" << prone.ai_opposes_recovery_frames << '\n';
    require(prone.visible_body_top > prone.detection_top, "prone oracle body must exclude cue/gap");
    require(prone.cue_y < prone.visible_body_top, "cue must sit above the visible body");
    require(!prone.target_outside_visible_body, "prone target must land inside visible body");
    require(prone.manual_escape_frame >= 0, "manual correction must reach prone body");
    require(prone.ai_opposes_recovery_frames <= 5,
        "body-lock may overshoot briefly but must not sustain opposition to prone correction");

    const auto stairs = controller_native::vertical_defect::run_stairs_air_lock();
    require(!stairs.target_outside_visible_body, "stairs target must land inside visible body");
    require(stairs.manual_escape_frame >= 0, "manual correction must reach stairs body");
    require(stairs.ai_opposes_recovery_frames <= 5,
        "body-lock may overshoot briefly but must not sustain opposition to stairs correction");

    const auto overshoot = controller_native::vertical_defect::run_cooperative_overshoot_occlusion();
    std::cout << "overshoot_px=" << overshoot.max_overshoot_px
              << " outside_frames=" << overshoot.outside_body_frames
              << " oppose_frames=" << overshoot.ai_opposes_recovery_frames
              << " recovery_frame=" << overshoot.recovery_start_frame
              << " reacquire_frame=" << overshoot.reacquire_frame << '\n';
    require(overshoot.ai_opposes_recovery_frames <= 5,
        "body-lock may overshoot briefly but must not sustain resistance to recovery");
    require(overshoot.max_overshoot_px <= 50.0,
        "body-lock overshoot must remain bounded even though continuous tracking is preferred");
    require(overshoot.outside_body_frames <= 140,
        "body-lock must reacquire promptly after an allowed manual overshoot");
    require(overshoot.recovery_start_frame == 160, "recovery must begin on the first reverse-input frame");
    const auto takeover = controller_native::vertical_defect::run_single_target_manual_takeover();
    const auto legacy_takeover =
        controller_native::vertical_defect::run_single_target_manual_takeover_legacy();
    const auto cooperative = controller_native::vertical_defect::run_single_target_cooperative_tracking();
    const auto noise = controller_native::vertical_defect::run_single_target_short_noise();
    const auto crossing =
        controller_native::vertical_defect::run_bodylock_crossing_continuity();
    std::cout << "takeover preservation=" << takeover.manual_direction_preservation_ratio
              << " reverse_ms=" << takeover.max_continuous_reversal_ms
              << " stall_ms=" << takeover.manual_stall_ms
              << " latency_ms=" << takeover.manual_takeover_latency_ms
              << " resistance=" << takeover.old_target_resistance_integral << '\n';
    require(takeover.manual_direction_preservation_ratio >= 0.75,
        "manual takeover must preserve at least 75% of committed user direction");
    require(takeover.max_continuous_reversal_ms <= 20.0,
        "bodylock must not reverse committed manual input for more than 20ms");
    require(takeover.manual_stall_ms <= 40.0,
        "bodylock must not stall committed manual input for more than 40ms");
    require(takeover.manual_takeover_latency_ms >= 0.0 &&
        takeover.manual_takeover_latency_ms <= 60.0,
        "manual takeover must become effective within 60ms");
    require(cooperative.cooperative_assist_preserved,
        "cooperative tracking must retain bodylock assistance");
    require(noise.short_noise_kept_body_lock,
        "short opposing stick noise must not release bodylock");
    // Keep the legacy-control measurement in the report as historical context, but do
    // not require a retired defect to remain reproducible after the shared controller
    // pipeline has removed the old brake path.
    std::cout << "crossing min_output=" << crossing.min_committed_output
              << " brake_frames=" << crossing.downstream_brake_frames
              << " carry_brake_frames=" << crossing.ads_carry_brake_frames << '\n';
    require(crossing.downstream_brake_frames == 0,
        "body-lock error crossings must not re-arm a downstream manual brake");
    require(crossing.ads_carry_brake_frames == 0,
        "ADS carry brake must stay inactive throughout body-lock tracking");
    require(crossing.min_committed_output >= 0.80,
        "body-lock must preserve continuous committed manual tracking through error crossings");
    if (argc == 3 && std::string(argv[1]) == "--report") {
        std::ofstream report(argv[2]);
        require(report.good(), "could not open benchmark report path");
        report << "{\n  \"schema_version\": 1,\n  \"scenarios\": [\n";
        write_metric(report, prone);
        report << ",\n";
        write_metric(report, stairs);
        report << ",\n";
        write_metric(report, overshoot);
        report << ",\n";
        write_takeover_metric(report, takeover);
        report << ",\n";
        write_takeover_metric(report, legacy_takeover);
        report << "\n  ]\n}\n";
    }
    std::cout << "[VerticalBodylockDefectTests] PASS defects_fixed=5 controls=2\n";
    return 0;
}
