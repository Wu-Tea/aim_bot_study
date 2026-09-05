#pragma once

#include "fusion_channel.h"

#include <array>
#include <atomic>
#include <algorithm>

namespace shared_fusion {

// Reader-owned payload. No pointer outlives a seqlock interval in shared memory.
struct FusionSnapshot {
    std::uint64_t timestamp = 0;
    std::uint64_t frame_id = 0;
    std::int32_t frame_width = 0;
    std::int32_t frame_height = 0;
    FusionFrameGeometry geometry;
    FusionTarget target;
    std::uint32_t detection_count = 0;
    std::array<FusionDetection, FUSION_CHANNEL_MAX_DETECTIONS> detections;
};

inline bool try_copy_fusion_snapshot(
    const FusionChannelHeader& header, FusionSnapshot& output) noexcept {
    // A visual consumer must never wait indefinitely for a paused/crashed writer.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto active_slot = header.active_slot;
        if (active_slot >= FUSION_CHANNEL_SLOT_COUNT) return false;
        const auto& slot = header.slots[active_slot];
        const auto sequence = slot.write_sequence;
        if (sequence & 1u) continue;
        std::atomic_thread_fence(std::memory_order_acquire);

        FusionSnapshot candidate;
        candidate.timestamp = slot.timestamp;
        candidate.frame_id = slot.frame_id;
        candidate.frame_width = slot.frame_width;
        candidate.frame_height = slot.frame_height;
        candidate.geometry = slot.geometry;
        candidate.target = slot.target;
        candidate.detection_count = slot.detection_count;
        const auto count = std::min(candidate.detection_count, FUSION_CHANNEL_MAX_DETECTIONS);
        std::copy_n(slot.detections, count, candidate.detections.begin());

        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence != slot.write_sequence) continue;
        if (candidate.frame_id == 0 || candidate.detection_count > FUSION_CHANNEL_MAX_DETECTIONS)
            return false;
        output = candidate;
        return true;
    }
    return false;
}

}  // namespace shared_fusion
