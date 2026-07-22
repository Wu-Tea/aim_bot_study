#include "causal_response_synthetic_benchmark.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::uint32_t seed = 1337;
        std::filesystem::path output;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--seed" && index + 1 < argc) {
                seed = static_cast<std::uint32_t>(std::stoul(argv[++index]));
            } else if (argument == "--output" && index + 1 < argc) {
                output = argv[++index];
            } else {
                throw std::runtime_error("unknown or incomplete argument: " + argument);
            }
        }
        controller_native::causal_response::PlantConfig config;
        const auto report =
            controller_native::causal_response::run_feedback_fixture(config, seed);
        const std::string json = controller_native::causal_response::to_json(report);
        if (!output.empty()) {
            if (!output.parent_path().empty()) {
                std::filesystem::create_directories(output.parent_path());
            }
            std::ofstream file(output, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("cannot write benchmark output");
            file << json << '\n';
        } else {
            std::cout << json << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[CausalResponseBenchmark] FAIL " << error.what() << '\n';
        return 1;
    }
}
