// CPU arithmetic microbenchmark only; not whole-runtime or live-game latency.
#include "controller_native/game_stick_transfer.h"
#include <array>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

using namespace controller_native;
using Clock = std::chrono::steady_clock;

__declspec(noinline) double sample(const GameStickTransferConfig& config,
    const std::vector<pipeline_contract::Vec2f>& inputs, bool decode) {
    volatile float checksum = 0;
    const auto start = Clock::now();
    for (const auto input : inputs) {
        const auto result = transfer_game_stick(input, config, decode);
        checksum = checksum + result.x + result.y;
    }
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / inputs.size();
}

int main() {
    std::mt19937 random(29092026u);
    std::uniform_real_distribution<float> axis(-1, 1);
    std::vector<pipeline_contract::Vec2f> inputs(262144);
    for (auto& input : inputs) input = {axis(random), axis(random)};
    const std::array<GameStickTransferConfig, 4> configs{{
        {false, false, .16f, 1}, {true, false, .16f, 1},
        {true, false, .16f, 1}, {true, false, .16f, 2}}};
    const char* names[] = {"disabled", "bo3_encode", "bo3_decode", "power_encode"};
    std::array<std::array<double, 9>, 4> samples{};
    for (int run = 0; run != 10; ++run) {
        for (int mode = 0; mode != 4; ++mode) {
            const auto ns = sample(configs[mode], inputs, mode == 2);
            if (run > 0) samples[mode][run - 1] = ns;
        }
    }
    for (int mode = 0; mode != 4; ++mode) {
        std::sort(samples[mode].begin(), samples[mode].end());
        std::cout << "{\"mode\":\"" << names[mode] << "\",\"p50_ns\":" << samples[mode][4]
            << ",\"p90_ns\":" << samples[mode][8] << ",\"samples_per_run\":" << inputs.size() << "}\n";
    }
}
