#include "vision_native/vision_engine.h"

#include <chrono>

namespace vision_native {
namespace {

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

float ns_to_ms(uint64_t delta_ns) {
    return static_cast<float>(delta_ns) / 1'000'000.0f;
}

} // namespace

VisionEngine::VisionEngine(
    int width,
    int height,
    int adapter_index,
    int output_index,
    int timeout_ms)
    : capture_(width, height, adapter_index, output_index, timeout_ms),
      width_(width),
      height_(height) {}

VisionEngine::~VisionEngine() = default;

void VisionEngine::set_aiming(bool aiming) {
    aiming_.store(aiming, std::memory_order_relaxed);
}

void VisionEngine::reset() {
    aiming_.store(false, std::memory_order_relaxed);
}

VisionResult VisionEngine::poll_once() {
    VisionResult result;
    result.screen_center_x = static_cast<float>(width_) * 0.5f;
    result.screen_center_y = static_cast<float>(height_) * 0.5f;

    if (!aiming_.load(std::memory_order_relaxed)) {
        result.result_at_ns = now_ns();
        result.age_ms = 0.0f;
        return result;
    }

    const DxgiCaptureMetadata metadata = capture_.grab();
    result.frame_id = metadata.frame.frame_id;
    result.captured_at_ns = metadata.frame.captured_at_ns;
    result.inferred_at_ns = metadata.frame.captured_at_ns;
    result.result_at_ns = now_ns();
    result.wait_ms = metadata.acquire_ms;
    result.post_ms = metadata.copy_ms;
    result.target_x = result.screen_center_x;
    result.target_y = result.screen_center_y;
    result.target_source = metadata.updated ? "observed" : "predicted";
    result.boxes_seen = 0.0f;
    if (result.captured_at_ns != 0) {
        result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
    }
    return result;
}

int VisionEngine::width() const {
    return width_;
}

int VisionEngine::height() const {
    return height_;
}

} // namespace vision_native
