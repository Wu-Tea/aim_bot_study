#include "blind_window_baseline.h"
#include "blind_window_native_adapter.h"
#include "runtime_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::filesystem::path output;
    std::filesystem::path config = "config.toml";
    std::string revision = "unknown";
    std::string policy = "fixture";
    double assist_scale = 1.0;
    bool dirty = false;
};

std::uint64_t file_fingerprint(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot fingerprint config: " + path.string());
    }
    std::uint64_t hash = 1469598103934665603ULL;
    char byte = 0;
    while (input.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--output" && index + 1 < argc) {
            options.output = argv[++index];
        } else if (argument == "--config" && index + 1 < argc) {
            options.config = argv[++index];
        } else if (argument == "--policy" && index + 1 < argc) {
            options.policy = argv[++index];
        } else if (argument == "--assist-scale" && index + 1 < argc) {
            options.assist_scale = std::stod(argv[++index]);
        } else if (argument == "--revision" && index + 1 < argc) {
            options.revision = argv[++index];
        } else if (argument == "--dirty") {
            options.dirty = true;
        } else if (argument == "--help") {
            std::cout << "Usage: cod_native_blind_window_benchmark "
                         "--output PATH [--policy fixture|production] "
                         "[--assist-scale 0..1] [--config PATH] "
                         "[--revision HASH] [--dirty]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::runtime_error(
                "unknown or incomplete argument: " + argument);
        }
    }
    if (options.output.empty()) {
        throw std::runtime_error("--output is required");
    }
    if (options.policy != "fixture" && options.policy != "production") {
        throw std::runtime_error("--policy must be fixture or production");
    }
    if (options.assist_scale < 0.0 || options.assist_scale > 1.0) {
        throw std::runtime_error("--assist-scale must be within 0..1");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const bool production = options.policy == "production";
        const auto summary = production
            ? controller_native::blind_window::run_k1_baseline_matrix(
                controller_native::blind_window::native_bodylock_controller_factory(
                    controller_native::load_runtime_config(options.config).gamepad,
                    options.assist_scale))
            : controller_native::blind_window::run_k1_baseline_matrix();
        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(options.output.parent_path());
        }
        std::ofstream output(options.output, std::ios::out | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output path");
        output << controller_native::blind_window::serialize_k1_baseline_json(
            summary,
            options.revision,
            options.dirty,
            production ? "native_gamepad_controller_causal_vector_scale_" +
                std::to_string(options.assist_scale)
                       : "stale_proportional_fixture_baseline",
            production,
            production ? options.config.string() : std::string{},
            production ? file_fingerprint(options.config) : 0u);
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
