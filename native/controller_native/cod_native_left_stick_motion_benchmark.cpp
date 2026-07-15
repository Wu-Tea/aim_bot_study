#include "left_stick_motion_defect_benchmark.h"

#include <fstream>
#include <iostream>
#include <string>

namespace {

struct Options {
    std::string output_path;
    bool require_fixed = false;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--output" && index + 1 < argc) {
            options.output_path = argv[++index];
        } else if (argument == "--require-fixed") {
            options.require_fixed = true;
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    const auto report = controller_native::left_stick_defect::run_benchmark();

    std::string validation_error;
    if (!controller_native::left_stick_defect::validate_report(
            report,
            &validation_error)) {
        std::cerr << "[LeftStickMotionBenchmark] invalid report: "
                  << validation_error << '\n';
        return 1;
    }

    if (!options.output_path.empty()) {
        std::ofstream output(options.output_path);
        if (!output) {
            std::cerr << "[LeftStickMotionBenchmark] could not open output: "
                      << options.output_path << '\n';
            return 1;
        }
        controller_native::left_stick_defect::write_json(output, report);
    }

    if (options.require_fixed && !report.desired_gate_pass) {
        std::cerr << "[LeftStickMotionBenchmark] RED desired behavior gate failed\n";
        return 2;
    }

    std::cout << "[LeftStickMotionBenchmark] PASS harness scenarios="
              << report.scenarios.size() << " defects=" << report.defect_count
              << '\n';
    return 0;
}

