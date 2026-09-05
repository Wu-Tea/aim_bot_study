#include "runtime_app/fusion_channel_publisher.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

int positive_int_or(const char* value, int fallback) {
    if (value == nullptr) return fallback;
    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : fallback;
}

void publish_phase(
    runtime_app::FusionChannelPublisher& publisher,
    std::uint64_t& frame_id,
    const shared_fusion::FusionFrameGeometry& geometry,
    const shared_fusion::FusionTarget& target,
    int phase_ms,
    int publish_hz) {
    const auto interval = std::chrono::microseconds(
        std::max<std::int64_t>(1, 1'000'000 / publish_hz));
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(phase_ms);
    auto next_publish = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
        publisher.publish(
            ++frame_id,
            640,
            512,
            geometry,
            target,
            nullptr,
            0);
        next_publish += interval;
        std::this_thread::sleep_until(next_publish);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* session = "cycle_probe";
    int cycles = 15;
    int phase_ms = 1000;
    int publish_hz = 120;
    int warmup_ms = 1500;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--session" && i + 1 < argc) {
            session = argv[++i];
        } else if (arg == "--cycles" && i + 1 < argc) {
            cycles = positive_int_or(argv[++i], cycles);
        } else if (arg == "--phase-ms" && i + 1 < argc) {
            phase_ms = positive_int_or(argv[++i], phase_ms);
        } else if (arg == "--publish-hz" && i + 1 < argc) {
            publish_hz = positive_int_or(argv[++i], publish_hz);
        } else if (arg == "--warmup-ms" && i + 1 < argc) {
            warmup_ms = positive_int_or(argv[++i], warmup_ms);
        }
    }

    runtime_app::FusionChannelPublisher publisher;
    if (!publisher.open(session, false)) {
        std::fprintf(stderr, "cycle publisher failed to open session=%s\n", session);
        return 1;
    }

    const int output_width = std::max(640, GetSystemMetrics(SM_CXSCREEN));
    const int output_height = std::max(512, GetSystemMetrics(SM_CYSCREEN));
    shared_fusion::FusionFrameGeometry geometry;
    geometry.output_left = 0;
    geometry.output_top = 0;
    geometry.output_width = output_width;
    geometry.output_height = output_height;
    geometry.roi_left = (output_width - 640) / 2;
    geometry.roi_top = (output_height - 512) / 2;

    shared_fusion::FusionTarget visible_target;
    visible_target.has_target = true;
    visible_target.has_body_box = true;
    visible_target.direct_observation = true;
    visible_target.enemy_identity_confirmed = true;
    visible_target.selector_target_generation = 1;
    visible_target.target_x = 0.5f;
    visible_target.target_y = 0.5f;
    visible_target.confidence = 0.9f;

    shared_fusion::FusionTarget hidden_target;
    hidden_target.selector_target_generation = 1;

    std::uint64_t frame_id = 0;
    std::printf(
        "cycle publisher ready session=%s cycles=%d phase_ms=%d publish_hz=%d\n",
        session,
        cycles,
        phase_ms,
        publish_hz);
    std::fflush(stdout);
    publish_phase(
        publisher,
        frame_id,
        geometry,
        hidden_target,
        warmup_ms,
        publish_hz);

    for (int cycle = 0; cycle < cycles; ++cycle) {
        std::printf("cycle=%d state=visible\n", cycle + 1);
        std::fflush(stdout);
        publish_phase(
            publisher,
            frame_id,
            geometry,
            visible_target,
            phase_ms,
            publish_hz);

        std::printf("cycle=%d state=hidden\n", cycle + 1);
        std::fflush(stdout);
        publish_phase(
            publisher,
            frame_id,
            geometry,
            hidden_target,
            phase_ms,
            publish_hz);
    }

    std::printf("cycle publisher complete frames=%llu\n",
        static_cast<unsigned long long>(frame_id));
    return 0;
}
