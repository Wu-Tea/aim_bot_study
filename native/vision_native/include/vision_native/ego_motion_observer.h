#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace vision_native {

inline constexpr int kEgoMotionFrameWidth = 160;
inline constexpr int kEgoMotionFrameHeight = 128;
inline constexpr std::size_t kEgoMotionPixelCount =
    static_cast<std::size_t>(kEgoMotionFrameWidth) *
    static_cast<std::size_t>(kEgoMotionFrameHeight);
inline constexpr std::size_t kEgoMotionMaxMaskRects = 32;

enum class EgoMotionInvalidReason : std::uint8_t {
    None,
    NoPreviousFrame,
    InvalidInput,
    DuplicateOrOutOfOrder,
    LowBackgroundCoverage,
    LowConfidence,
    WorkerStopped,
};

struct EgoMotionMaskRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// The pointer members are borrowed only for the duration of submit_frame().
// The observer copies the bounded image and masks into its single pending slot.
struct EgoMotionFrameView {
    std::uint64_t frame_id = 0;
    std::uint64_t source_present_qpc = 0;
    std::uint64_t source_present_qpc_frequency = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    int width = 0;
    int height = 0;
    int row_pitch = 0;
    const std::uint8_t* gray = nullptr;
    const EgoMotionMaskRect* masks = nullptr;
    std::size_t mask_count = 0;
};

struct EgoMotionShadowResult {
    bool available = false;
    bool valid = false;
    EgoMotionInvalidReason invalid_reason = EgoMotionInvalidReason::None;
    std::uint64_t result_sequence = 0;
    std::uint64_t previous_frame_id = 0;
    std::uint64_t current_frame_id = 0;
    std::uint64_t previous_present_qpc = 0;
    std::uint64_t current_present_qpc = 0;
    std::uint64_t present_qpc_frequency = 0;
    std::uint64_t previous_captured_at_ns = 0;
    std::uint64_t current_captured_at_ns = 0;
    std::uint64_t previous_result_ns = 0;
    std::uint64_t current_result_ns = 0;
    float background_dx = 0.0f;
    float background_dy = 0.0f;
    // Camera displacement is the inverse of observed background displacement:
    // a background moving right means the camera moved left.
    float camera_dx = 0.0f;
    float camera_dy = 0.0f;
    float confidence = 0.0f;
    float valid_background_ratio = 0.0f;
    float residual_px = 0.0f;
    float compute_ms = 0.0f;
    std::uint32_t inlier_count = 0;
    std::uint32_t sample_count = 0;
};

struct EgoMotionObserverConfig {
    int search_radius_px = 8;
    int patch_radius_px = 1;
    int grid_stride_px = 8;
    int edge_margin_px = 6;
    int center_mask_radius_px = 10;
    float min_valid_background_ratio = 0.20f;
    float min_confidence = 0.18f;
    float max_residual_px = 3.5f;
    // Test-only delay. Production remains zero and the worker is latest-only.
    unsigned int worker_delay_ms_for_test = 0;
};

class EgoMotionObserver {
public:
    explicit EgoMotionObserver(EgoMotionObserverConfig config = {});
    ~EgoMotionObserver();

    EgoMotionObserver(const EgoMotionObserver&) = delete;
    EgoMotionObserver& operator=(const EgoMotionObserver&) = delete;

    // Returns false for malformed input or after stop. A newer submit replaces
    // an unprocessed pending frame; no queue can accumulate.
    bool submit_frame(const EgoMotionFrameView& frame) noexcept;

    // Takes each produced result at most once. Results remain independent of
    // control/tracker state and are intended only for shadow telemetry.
    bool take_latest_result(EgoMotionShadowResult* result) noexcept;

    // Clears pair history and pending/output mailboxes at a lifecycle boundary.
    void reset() noexcept;

    // Deterministic, thread-free estimator entry point used by fixtures.
    static EgoMotionShadowResult estimate_pair(
        const EgoMotionFrameView& previous,
        const EgoMotionFrameView& current,
        const EgoMotionObserverConfig& config = {}) noexcept;

private:
    struct StoredFrame {
        std::uint64_t frame_id = 0;
        std::uint64_t source_present_qpc = 0;
        std::uint64_t source_present_qpc_frequency = 0;
        std::uint64_t captured_at_ns = 0;
        std::uint64_t result_at_ns = 0;
        std::array<std::uint8_t, kEgoMotionPixelCount> gray{};
        std::array<EgoMotionMaskRect, kEgoMotionMaxMaskRects> masks{};
        std::size_t mask_count = 0;
    };

    static bool store_frame(const EgoMotionFrameView& view, StoredFrame* out) noexcept;
    static EgoMotionFrameView view_of(const StoredFrame& frame) noexcept;
    void worker_loop() noexcept;
    void stop() noexcept;

    EgoMotionObserverConfig config_{};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool stop_requested_ = false;
    bool pending_available_ = false;
    bool previous_available_ = false;
    bool result_available_ = false;
    std::uint64_t lifecycle_generation_ = 0;
    StoredFrame pending_{};
    StoredFrame previous_{};
    EgoMotionShadowResult result_{};
    std::uint64_t next_result_sequence_ = 1;
};

}  // namespace vision_native
