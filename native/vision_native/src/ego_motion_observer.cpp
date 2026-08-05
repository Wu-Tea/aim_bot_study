#include "vision_native/ego_motion_observer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace vision_native {
namespace {

constexpr std::size_t kMaxSamples = 512;

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

bool finite_frame(const EgoMotionFrameView& frame) noexcept {
    return frame.frame_id != 0 && frame.gray != nullptr &&
        frame.width > 0 && frame.width <= kEgoMotionFrameWidth &&
        frame.height > 0 && frame.height <= kEgoMotionFrameHeight &&
        frame.row_pitch >= frame.width;
}

bool in_rect(const EgoMotionMaskRect& rect, int x, int y, int inflate) noexcept {
    return x >= rect.left - inflate && x < rect.right + inflate &&
        y >= rect.top - inflate && y < rect.bottom + inflate;
}

bool is_masked(const EgoMotionFrameView& frame, int x, int y, const EgoMotionObserverConfig& config) noexcept {
    if (x < config.edge_margin_px || y < config.edge_margin_px ||
        x >= frame.width - config.edge_margin_px ||
        y >= frame.height - config.edge_margin_px ||
        y >= (frame.height * 4) / 5) {
        return true;
    }
    const int cx = frame.width / 2;
    const int cy = frame.height / 2;
    const int dx = x - cx;
    const int dy = y - cy;
    if ((dx * dx) + (dy * dy) <=
        config.center_mask_radius_px * config.center_mask_radius_px) {
        return true;
    }
    const std::size_t count = std::min(frame.mask_count, kEgoMotionMaxMaskRects);
    for (std::size_t i = 0; i < count; ++i) {
        if (in_rect(frame.masks[i], x, y, 3)) return true;
    }
    return false;
}

std::uint8_t pixel(const EgoMotionFrameView& frame, int x, int y) noexcept {
    return frame.gray[(y * frame.row_pitch) + x];
}

std::uint64_t steady_now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

EgoMotionObserver::EgoMotionObserver(EgoMotionObserverConfig config)
    : config_(config), worker_(&EgoMotionObserver::worker_loop, this) {}

EgoMotionObserver::~EgoMotionObserver() {
    stop();
}

bool EgoMotionObserver::store_frame(
    const EgoMotionFrameView& view,
    StoredFrame* out) noexcept {
    if (out == nullptr || !finite_frame(view) ||
        (view.mask_count != 0 && view.masks == nullptr)) return false;
    out->frame_id = view.frame_id;
    out->source_present_qpc = view.source_present_qpc;
    out->source_present_qpc_frequency = view.source_present_qpc_frequency;
    out->source_present_steady_ns = view.source_present_steady_ns;
    out->source_present_calibration_id = view.source_present_calibration_id;
    out->source_present_calibration_uncertainty_ns =
        view.source_present_calibration_uncertainty_ns;
    out->source_present_steady_available = view.source_present_steady_available;
    out->captured_at_ns = view.captured_at_ns;
    out->result_at_ns = view.result_at_ns;
    out->mask_count = std::min(view.mask_count, kEgoMotionMaxMaskRects);
    for (std::size_t i = 0; i < out->mask_count; ++i) out->masks[i] = view.masks[i];
    for (int y = 0; y < view.height; ++y) {
        for (int x = 0; x < view.width; ++x) {
            out->gray[static_cast<std::size_t>(y) * kEgoMotionFrameWidth + x] =
                view.gray[(y * view.row_pitch) + x];
        }
    }
    return true;
}

EgoMotionFrameView EgoMotionObserver::view_of(const StoredFrame& frame) noexcept {
    EgoMotionFrameView view;
    view.frame_id = frame.frame_id;
    view.source_present_qpc = frame.source_present_qpc;
    view.source_present_qpc_frequency = frame.source_present_qpc_frequency;
    view.source_present_steady_ns = frame.source_present_steady_ns;
    view.source_present_calibration_id = frame.source_present_calibration_id;
    view.source_present_calibration_uncertainty_ns =
        frame.source_present_calibration_uncertainty_ns;
    view.source_present_steady_available = frame.source_present_steady_available;
    view.captured_at_ns = frame.captured_at_ns;
    view.result_at_ns = frame.result_at_ns;
    view.width = kEgoMotionFrameWidth;
    view.height = kEgoMotionFrameHeight;
    view.row_pitch = kEgoMotionFrameWidth;
    view.gray = frame.gray.data();
    view.masks = frame.masks.data();
    view.mask_count = frame.mask_count;
    return view;
}

EgoMotionShadowResult EgoMotionObserver::estimate_pair(
    const EgoMotionFrameView& previous,
    const EgoMotionFrameView& current,
    const EgoMotionObserverConfig& config) noexcept {
    EgoMotionShadowResult result;
    result.previous_frame_id = previous.frame_id;
    result.current_frame_id = current.frame_id;
    result.previous_present_qpc = previous.source_present_qpc;
    result.current_present_qpc = current.source_present_qpc;
    result.previous_present_qpc_frequency =
        previous.source_present_qpc_frequency;
    result.current_present_qpc_frequency =
        current.source_present_qpc_frequency;
    result.present_qpc_frequency = current.source_present_qpc_frequency != 0
        ? current.source_present_qpc_frequency
        : previous.source_present_qpc_frequency;
    result.previous_present_steady_ns = previous.source_present_steady_ns;
    result.current_present_steady_ns = current.source_present_steady_ns;
    result.previous_present_calibration_id =
        previous.source_present_calibration_id;
    result.current_present_calibration_id =
        current.source_present_calibration_id;
    result.previous_present_calibration_uncertainty_ns =
        previous.source_present_calibration_uncertainty_ns;
    result.current_present_calibration_uncertainty_ns =
        current.source_present_calibration_uncertainty_ns;
    result.previous_present_steady_available =
        previous.source_present_steady_available;
    result.current_present_steady_available =
        current.source_present_steady_available;
    result.present_clock_valid =
        result.previous_present_steady_available &&
        result.current_present_steady_available &&
        result.previous_present_qpc_frequency != 0 &&
        result.current_present_qpc_frequency != 0 &&
        result.previous_present_qpc_frequency ==
            result.current_present_qpc_frequency &&
        result.previous_present_calibration_id != 0 &&
        result.current_present_calibration_id != 0 &&
        result.previous_present_steady_ns < result.current_present_steady_ns;
    result.previous_capture_copy_complete_ns = previous.captured_at_ns;
    result.current_capture_copy_complete_ns = current.captured_at_ns;
    result.previous_captured_at_ns = previous.captured_at_ns;
    result.current_captured_at_ns = current.captured_at_ns;
    result.previous_result_ns = previous.result_at_ns;
    result.current_result_ns = current.result_at_ns;
    result.search_radius_px = std::max(1, config.search_radius_px);
    if (!finite_frame(previous) || !finite_frame(current) ||
        previous.width != current.width || previous.height != current.height ||
        current.frame_id <= previous.frame_id) {
        result.invalid_reason = current.frame_id <= previous.frame_id
            ? EgoMotionInvalidReason::DuplicateOrOutOfOrder
            : EgoMotionInvalidReason::InvalidInput;
        return result;
    }

    result.available = true;
    const int search = std::max(1, config.search_radius_px);
    const int patch = std::max(0, config.patch_radius_px);
    const int stride = std::max(1, config.grid_stride_px);
    const int margin = search + patch + std::max(0, config.edge_margin_px);
    if (previous.width <= (margin * 2) || previous.height <= (margin * 2)) {
        result.invalid_reason = EgoMotionInvalidReason::LowBackgroundCoverage;
        return result;
    }

    std::array<int, kMaxSamples> sample_dx{};
    std::array<int, kMaxSamples> sample_dy{};
    std::array<float, kMaxSamples> sample_cost{};
    std::size_t sample_count = 0;
    std::size_t grid_count = 0;
    std::uint32_t boundary_hit_count = 0;
    const int total_patch_pixels = (patch * 2 + 1) * (patch * 2 + 1);

    for (int y = margin; y < current.height - margin; y += stride) {
        for (int x = margin; x < current.width - margin; x += stride) {
            ++grid_count;
            if (is_masked(current, x, y, config)) continue;
            int best_dx = 0;
            int best_dy = 0;
            float best_cost = 1.0e30f;
            for (int dy = -search; dy <= search; ++dy) {
                for (int dx = -search; dx <= search; ++dx) {
                    float cost = 0.0f;
                    for (int py = -patch; py <= patch; ++py) {
                        for (int px = -patch; px <= patch; ++px) {
                            const int current_value = pixel(current, x + px, y + py);
                            const int previous_value = pixel(
                                previous, x + px - dx, y + py - dy);
                            cost += static_cast<float>(std::abs(current_value - previous_value));
                        }
                    }
                    if (cost < best_cost) {
                        best_cost = cost;
                        best_dx = dx;
                        best_dy = dy;
                    }
                }
            }
            if (sample_count < kMaxSamples) {
                sample_dx[sample_count] = best_dx;
                sample_dy[sample_count] = best_dy;
                sample_cost[sample_count] = best_cost /
                    static_cast<float>(std::max(1, total_patch_pixels));
                ++sample_count;
            }
            if (best_dx == -search || best_dx == search ||
                best_dy == -search || best_dy == search) {
                ++boundary_hit_count;
            }
        }
    }
    result.sample_count = static_cast<std::uint32_t>(sample_count);
    result.boundary_hit_count = boundary_hit_count;
    result.boundary_hit_rate = sample_count == 0
        ? 0.0f
        : static_cast<float>(boundary_hit_count) /
            static_cast<float>(std::max<std::size_t>(1, sample_count));
    result.valid_background_ratio = grid_count == 0
        ? 0.0f
        : static_cast<float>(sample_count) / static_cast<float>(grid_count);
    if (sample_count < 8 || result.valid_background_ratio <
        std::max(0.0f, config.min_valid_background_ratio)) {
        result.invalid_reason = EgoMotionInvalidReason::LowBackgroundCoverage;
        return result;
    }

    std::array<int, kMaxSamples> sorted_dx = sample_dx;
    std::array<int, kMaxSamples> sorted_dy = sample_dy;
    std::sort(sorted_dx.begin(), sorted_dx.begin() + sample_count);
    std::sort(sorted_dy.begin(), sorted_dy.begin() + sample_count);
    const int median_dx = sorted_dx[sample_count / 2];
    const int median_dy = sorted_dy[sample_count / 2];
    std::uint32_t boundary_consistent_hit_count = 0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        const bool x_consistent =
            (median_dx > 0 && sample_dx[i] == search) ||
            (median_dx < 0 && sample_dx[i] == -search);
        const bool y_consistent =
            (median_dy > 0 && sample_dy[i] == search) ||
            (median_dy < 0 && sample_dy[i] == -search);
        if (x_consistent || y_consistent) ++boundary_consistent_hit_count;
    }
    result.boundary_consistent_hit_count = boundary_consistent_hit_count;
    result.boundary_consistent_hit_rate = static_cast<float>(
        boundary_consistent_hit_count) /
        static_cast<float>(std::max<std::size_t>(1, sample_count));
    float residual_sum = 0.0f;
    float cost_sum = 0.0f;
    std::uint32_t inlier_count = 0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        const float ddx = static_cast<float>(sample_dx[i] - median_dx);
        const float ddy = static_cast<float>(sample_dy[i] - median_dy);
        const float residual = std::sqrt((ddx * ddx) + (ddy * ddy));
        if (residual <= 2.5f) {
            ++inlier_count;
            residual_sum += residual;
        }
        cost_sum += sample_cost[i];
    }
    result.inlier_count = inlier_count;
    result.background_dx = static_cast<float>(median_dx);
    result.background_dy = static_cast<float>(median_dy);
    result.camera_dx = -result.background_dx;
    result.camera_dy = -result.background_dy;
    result.residual_px = inlier_count == 0
        ? 1.0e6f : residual_sum / static_cast<float>(inlier_count);
    const float inlier_ratio = static_cast<float>(inlier_count) /
        static_cast<float>(sample_count);
    const float residual_quality = clamp01(
        1.0f - result.residual_px / std::max(0.1f, config.max_residual_px));
    const float cost_quality = clamp01(
        1.0f - (cost_sum / static_cast<float>(sample_count)) / 48.0f);
    result.confidence = clamp01(inlier_ratio * residual_quality *
        (0.65f + (0.35f * cost_quality)));
    const bool global_boundary_limited =
        median_dx == -search || median_dx == search ||
        median_dy == -search || median_dy == search;
    // A global median at the edge is definitive. If the median is interior,
    // require a substantial fraction of the searched samples to be pinned to
    // an edge before declaring the displacement unidentifiable; isolated
    // ambiguous blocks remain valid and are reported diagnostically.
    const bool boundary_dominant =
        result.boundary_consistent_hit_count >= 8 &&
        result.boundary_consistent_hit_rate >= 0.50f;
    if (global_boundary_limited || boundary_dominant) {
        result.valid = false;
        result.invalid_reason = EgoMotionInvalidReason::SearchBoundaryLimited;
        result.background_dx = 0.0f;
        result.background_dy = 0.0f;
        result.camera_dx = 0.0f;
        result.camera_dy = 0.0f;
        return result;
    }
    if (result.residual_px > config.max_residual_px ||
        result.confidence < config.min_confidence) {
        result.valid = false;
        result.invalid_reason = EgoMotionInvalidReason::LowConfidence;
        result.background_dx = 0.0f;
        result.background_dy = 0.0f;
        result.camera_dx = 0.0f;
        result.camera_dy = 0.0f;
        return result;
    }
    result.valid = true;
    result.invalid_reason = EgoMotionInvalidReason::None;
    return result;
}

bool EgoMotionObserver::submit_frame(const EgoMotionFrameView& frame) noexcept {
    StoredFrame stored;
    if (!store_frame(frame, &stored)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_requested_) return false;
    ++stats_.frames_submitted;
    if (pending_available_) ++stats_.pending_frame_replaced;
    pending_ = stored;
    pending_available_ = true;
    condition_.notify_one();
    return true;
}

bool EgoMotionObserver::take_latest_result(EgoMotionShadowResult* result) noexcept {
    if (result == nullptr) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result_available_) return false;
    *result = result_;
    const std::uint64_t now_ns = steady_now_ns();
    result->result_age_at_take_ns = result->observer_completed_at_ns != 0 &&
        now_ns >= result->observer_completed_at_ns
        ? now_ns - result->observer_completed_at_ns : 0;
    ++stats_.results_taken;
    stats_.result_age_ns_last = result->result_age_at_take_ns;
    stats_.result_age_ns_max = std::max(
        stats_.result_age_ns_max, result->result_age_at_take_ns);
    result_available_ = false;
    return true;
}

EgoMotionObserverStats EgoMotionObserver::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void EgoMotionObserver::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++lifecycle_generation_;
    stats_.lifecycle_generation = lifecycle_generation_;
    pending_available_ = false;
    previous_available_ = false;
    result_available_ = false;
    result_ = EgoMotionShadowResult{};
}

void EgoMotionObserver::worker_loop() noexcept {
    for (;;) {
        StoredFrame current;
        StoredFrame previous;
        std::uint64_t lifecycle_generation = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return stop_requested_ || pending_available_;
            });
            if (stop_requested_ && !pending_available_) return;
            current = pending_;
            pending_available_ = false;
            lifecycle_generation = lifecycle_generation_;
            if (!previous_available_) {
                previous_ = current;
                previous_available_ = true;
                continue;
            }
            previous = previous_;
        }
        // A late or duplicate frame is not a new baseline. Drop it without
        // disturbing the last accepted source frame, so the next fresh frame
        // can still form a valid pair against the same predecessor.
        if (current.frame_id <= previous.frame_id) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.duplicate_or_out_of_order_rejected;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_ || lifecycle_generation != lifecycle_generation_)
                continue;
            ++stats_.pairs_processed;
            previous_ = current;
        }
        if (config_.worker_delay_ms_for_test != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                config_.worker_delay_ms_for_test));
        }
        const auto compute_start = std::chrono::steady_clock::now();
        EgoMotionShadowResult estimate = estimate_pair(
            view_of(previous), view_of(current), config_);
        estimate.result_sequence = next_result_sequence_++;
        const auto compute_end = std::chrono::steady_clock::now();
        const std::uint64_t compute_end_ns = steady_now_ns();
        estimate.compute_ms = static_cast<float>(
            std::chrono::duration<double, std::milli>(compute_end - compute_start).count());
        estimate.observer_completed_at_ns = compute_end_ns;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_ || lifecycle_generation != lifecycle_generation_) continue;
            if (result_available_) ++stats_.unread_result_replaced;
            estimate.submitted_frame_count = stats_.frames_submitted;
            estimate.pending_frame_replaced_count = stats_.pending_frame_replaced;
            estimate.pairs_processed_count = stats_.pairs_processed;
            estimate.unread_result_replaced_count = stats_.unread_result_replaced;
            estimate.duplicate_or_out_of_order_rejected_count =
                stats_.duplicate_or_out_of_order_rejected;
            estimate.observer_lifecycle_generation =
                stats_.lifecycle_generation;
            result_ = estimate;
            result_available_ = true;
        }
    }
}

void EgoMotionObserver::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_) return;
        stop_requested_ = true;
        pending_available_ = false;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

}  // namespace vision_native
