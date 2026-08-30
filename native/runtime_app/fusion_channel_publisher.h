#pragma once
// fusion_channel_publisher.h — zero-allocation best-effort publisher that
// copies the latest VisionResult into a shared-memory double-buffer for the
// fusion canvas.
//
// The publish() path does not wait on the canvas, open/reopen IPC handles,
// allocate per call, perform file I/O, or synchronously log per publish.
// It may iterate over detections (bounded count) and call SetEvent (best-effort).
// Repeated failures will self-disable the publisher.

#include "shared_fusion/fusion_channel.h"

#include <Windows.h>

#include <cstdint>
#include <string>

namespace runtime_app {

class FusionChannelPublisher {
public:
    FusionChannelPublisher();
    ~FusionChannelPublisher();

    // Not copyable or movable — owns kernel handles.
    FusionChannelPublisher(const FusionChannelPublisher&) = delete;
    FusionChannelPublisher& operator=(const FusionChannelPublisher&) = delete;

    // Open (or create) the shared-memory channel for `session`.
    // Returns false when FUSION_FORCE_OFF=1 or when kernel objects cannot be
    // created — the caller must treat this as "publisher unavailable".
    bool open(const char* session, bool show_all_detections);

    // True once open() succeeded and we haven't self-disabled.
    bool enabled() const noexcept { return enabled_; }

    // Publish a batch of detections + target info.
    // `frame_width` / `frame_height` must be > 0.
    void publish(
        std::uint64_t frame_id,
        std::int32_t  frame_width,
        std::int32_t  frame_height,
        const shared_fusion::FusionFrameGeometry& geometry,
        const shared_fusion::FusionTarget& target,
        const shared_fusion::FusionDetection* detections,
        std::uint32_t detection_count);

    // Simple convenience overload.
    void publish(
        std::uint64_t frame_id,
        std::int32_t  frame_width,
        std::int32_t  frame_height,
        const shared_fusion::FusionFrameGeometry& geometry,
        const shared_fusion::FusionTarget& target,
        const shared_fusion::FusionDetection* detections,
        std::uint32_t detection_count,
        std::uint64_t qpc_timestamp);

private:
    void disable();

    HANDLE mapping_handle_  = nullptr;
    HANDLE event_handle_    = nullptr;
    shared_fusion::FusionChannelHeader* header_ = nullptr;
    bool     enabled_       = false;
    bool     show_all_      = true;
    int      consecutive_failures_ = 0;
};

}  // namespace runtime_app
