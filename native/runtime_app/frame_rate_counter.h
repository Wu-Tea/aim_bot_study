#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

namespace runtime_app {

// Plain counters shared with the UI, independent of every logging switch.
struct FrameRateCounts {
    std::uint64_t elapsed_ns = 0, aim_ns = 0;
    std::uint64_t vision_frames = 0, aim_frames = 0, controller_ticks = 0;
    std::uint64_t recent_elapsed_ns = 0, recent_aim_ns = 0;
    std::uint64_t recent_vision_frames = 0, recent_aim_frames = 0;
    std::uint32_t aiming = 0, reserved = 0;
};
static_assert(sizeof(FrameRateCounts) == 80);
static_assert(std::is_trivially_copyable_v<FrameRateCounts>);

// Single controller-thread owner. No heap allocation, synchronization, OS calls
// or I/O. Times come from the existing tick clock; a fresh consumed result adds
// exactly one frame. The preceding ADS state owns the elapsed interval.
class FrameRateCounter {
public:
    static constexpr std::uint64_t bucket_ns = 100'000'000;
    static constexpr std::uint64_t window_ns = 5'000'000'000;

    void record_tick(std::uint64_t now_ns, bool aiming, bool fresh_frame) noexcept {
        if (!started_) {
            started_ = true;
            first_ns_ = last_ns_ = now_ns;
        } else {
            // The runtime clock is monotonic. Reject invalid injected samples
            // without corrupting instrumentation or touching controller state.
            if (now_ns < last_ns_) return;
            advance(now_ns);
        }
        aiming_ = aiming;
        ++ticks_;
        if (fresh_frame) {
            ++frames_;
            if (aiming) ++aim_frames_;
            auto& current = bucket(now_ns / bucket_ns);
            ++current.frames;
            if (aiming) ++current.aim_frames;
        }
    }

    void finish(std::uint64_t now_ns) noexcept {
        if (started_ && now_ns >= last_ns_) advance(now_ns);
    }

    FrameRateCounts snapshot() const noexcept {
        FrameRateCounts value;
        if (!started_) return value;
        value.elapsed_ns = last_ns_ - first_ns_;
        value.aim_ns = aim_ns_;
        value.vision_frames = frames_;
        value.aim_frames = aim_frames_;
        value.controller_ticks = ticks_;
        value.aiming = aiming_;
        // Exactly the latest completed 100ms boundary, excluding the current
        // partial bucket. This gives an exact <=5s window rather than estimating
        // the distribution of frames/ADS transitions inside a partial bucket.
        const auto end_bucket = last_ns_ / bucket_ns;
        const auto end_ns = end_bucket * bucket_ns;
        if (end_ns <= first_ns_) return value;
        const auto begin_bucket = end_bucket > 50 ? end_bucket - 50 : 0;
        const auto begin_ns = std::max(first_ns_, begin_bucket * bucket_ns);
        value.recent_elapsed_ns = end_ns - begin_ns;
        for (auto index = begin_bucket; index < end_bucket; ++index) {
            const auto& current = buckets_[index % buckets_.size()];
            if (!current.valid || current.index != index) continue;
            value.recent_aim_ns += current.aim_ns;
            value.recent_vision_frames += current.frames;
            value.recent_aim_frames += current.aim_frames;
        }
        return value;
    }

private:
    struct Bucket {
        std::uint64_t index = 0, aim_ns = 0, frames = 0, aim_frames = 0;
        bool valid = false;
    };
    Bucket& bucket(std::uint64_t index) noexcept {
        auto& value = buckets_[index % buckets_.size()];
        if (!value.valid || value.index != index) value = {index, 0, 0, 0, true};
        return value;
    }
    void advance(std::uint64_t now_ns) noexcept {
        if (aiming_) aim_ns_ += now_ns - last_ns_;
        // An arbitrarily long stall is fully charged to session time, but
        // touches at most 51 recent buckets, never one bucket per missed tick.
        const auto current = now_ns / bucket_ns;
        const auto oldest = current > 50 ? (current - 50) * bucket_ns : 0;
        auto begin = std::max(last_ns_, oldest);
        while (begin < now_ns) {
            const auto index = begin / bucket_ns;
            const auto end = std::min(now_ns, (index + 1) * bucket_ns);
            auto& value = bucket(index);
            if (aiming_) value.aim_ns += end - begin;
            begin = end;
        }
        last_ns_ = now_ns;
    }
    std::array<Bucket, 51> buckets_{};
    std::uint64_t first_ns_ = 0, last_ns_ = 0, aim_ns_ = 0;
    std::uint64_t ticks_ = 0, frames_ = 0, aim_frames_ = 0;
    bool started_ = false, aiming_ = false;
};

} // namespace runtime_app
