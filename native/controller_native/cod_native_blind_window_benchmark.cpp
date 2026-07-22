#include "blind_window_baseline.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::filesystem::path output;
    std::string revision = "unknown";
    bool dirty = false;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--output" && index + 1 < argc) {
            options.output = argv[++index];
        } else if (argument == "--revision" && index + 1 < argc) {
            options.revision = argv[++index];
        } else if (argument == "--dirty") {
            options.dirty = true;
        } else if (argument == "--help") {
            std::cout << "Usage: cod_native_blind_window_benchmark "
                         "--output PATH [--revision HASH] [--dirty]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::runtime_error(
                "unknown or incomplete argument: " + argument);
        }
    }
    if (options.output.empty()) {
        throw std::runtime_error("--output is required");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const auto summary =
            controller_native::blind_window::run_k1_baseline_matrix();
        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(options.output.parent_path());
        }
        std::ofstream output(options.output, std::ios::out | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output path");
        output << controller_native::blind_window::serialize_k1_baseline_json(
            summary, options.revision, options.dirty);
        output.close();
        std::cout << "episodes=" << summary.episodes.size()
                  << " baseline_discriminating="
                  << (summary.baseline_discriminating ? "true" : "false")
                  << " output=" << options.output.string() << '\n';
        return summary.baseline_discriminating ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "blind-window benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
