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

int main(int argc, char** argv) {
    const auto prone = controller_native::vertical_defect::run_prone_air_lock();
    std::cout << "prone target=" << prone.selector_target_y
              << " escape=" << prone.manual_escape_frame
              << " oppose=" << prone.ai_opposes_recovery_frames << '\n';
    require(prone.visible_body_top > prone.detection_top, "prone oracle body must exclude cue/gap");
    require(prone.cue_y < prone.visible_body_top, "cue must sit above the visible body");
    require(!prone.target_outside_visible_body, "prone target must land inside visible body");
    require(prone.manual_escape_frame >= 0, "manual correction must reach prone body");
    require(prone.ai_opposes_recovery_frames <= 2, "AI must not sustain opposition to prone correction");

    const auto stairs = controller_native::vertical_defect::run_stairs_air_lock();
    require(!stairs.target_outside_visible_body, "stairs target must land inside visible body");
    require(stairs.manual_escape_frame >= 0, "manual correction must reach stairs body");
    require(stairs.ai_opposes_recovery_frames <= 2, "AI must not sustain opposition to stairs correction");

    const auto overshoot = controller_native::vertical_defect::run_cooperative_overshoot_occlusion();
    std::cout << "overshoot_px=" << overshoot.max_overshoot_px
              << " outside_frames=" << overshoot.outside_body_frames
              << " oppose_frames=" << overshoot.ai_opposes_recovery_frames
              << " recovery_frame=" << overshoot.recovery_start_frame
              << " reacquire_frame=" << overshoot.reacquire_frame << '\n';
    require(overshoot.ai_opposes_recovery_frames <= 2, "AI must not resist overshoot recovery");
    require(overshoot.recovery_start_frame == 160, "recovery must begin on the first reverse-input frame");
    if (argc == 3 && std::string(argv[1]) == "--report") {
        std::ofstream report(argv[2]);
        require(report.good(), "could not open benchmark report path");
        report << "{\n  \"schema_version\": 1,\n  \"scenarios\": [\n";
        write_metric(report, prone);
        report << ",\n";
        write_metric(report, stairs);
        report << ",\n";
        write_metric(report, overshoot);
        report << "\n  ]\n}\n";
    }
    std::cout << "[VerticalBodylockDefectTests] PASS defects_fixed=3\n";
    return 0;
}
