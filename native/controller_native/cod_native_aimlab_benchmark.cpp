#include "aimlab_benchmark.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

void print_report(const std::string& name, const controller_native::aimlab::ScoreReport& report) {
    std::cout
        << name
        << " final=" << report.final_score
        << " selection=" << report.selection_score
        << " control=" << report.control_score
        << " cooperation=" << report.cooperation_score
        << " safety=" << report.authority_safety_score
        << " wrong_ads=" << report.wrong_target_ads_snap_count
        << " sticky_wrong=" << report.sticky_wrong_frames
        << " invalid_strong=" << report.invalid_strong_frames
        << " err_snap=" << report.err_snap_frames
        << " recovery=" << report.recovery_frames
        << " stale_high=" << report.stale_high_output_frames
        << " overshoot50=" << report.overshoot_over_50px_count
        << " fight=" << report.user_fight_frames
        << " helpful=" << report.helpful_output_ratio
        << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint32_t seed = 12345;
    if (argc == 2) {
        seed = static_cast<std::uint32_t>(std::stoul(argv[1]));
    }

    for (const auto& scenario : controller_native::aimlab::default_scenarios()) {
        const auto report = controller_native::aimlab::run_scenario(scenario, seed);
        print_report(scenario, report);
    }
    std::cout << "cod_native_aimlab_benchmark PASS seed=" << seed << "\n";
    return 0;
}
