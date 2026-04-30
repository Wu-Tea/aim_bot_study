#pragma once

#include "vision_native/aim_enhancement.h"
#include "vision_native/body_state_tracker.h"
#include "vision_native/center_cue_refiner.h"
#include "vision_native/dxgi_capture.h"
#include "vision_native/ego_motion.h"
#include "vision_native/target_selector.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/types.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace vision_native {

struct WarmSnapshotCandidate {
    TargetKeyframe keyframe;
    float score = 0.0f;
};

struct WarmScanSnapshot {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    size_t candidate_count = 0;
    std::vector<WarmSnapshotCandidate> top_candidates;
};

class VisionEngine {
public:
    enum class Mode {
        Idle,
        WarmScan,
        ActiveTrack,
    };

    VisionEngine(
        int width,
        int height,
        int adapter_index = 0,
        int output_index = -1,
        int timeout_ms = 0,
        float track_interval_ms = 0.0f,
        float warm_scan_interval_ms = 50.0f,
        float active_normal_scan_interval_ms = 12.0f,
        float active_recovery_scan_interval_ms = 8.0f);
    ~VisionEngine();

    VisionEngine(const VisionEngine&) = delete;
    VisionEngine& operator=(const VisionEngine&) = delete;

    void set_mode(Mode mode);
    void set_aiming(bool aiming);
    void reset();
    VisionResult poll_once();

    int width() const;
    int height() const;
    const std::vector<uint8_t>& last_debug_frame_bgra() const;
    bool has_debug_frame() const;

private:
    DxgiRoiCapture capture_;
    EgoMotionEstimator ego_motion_;
    VisionTargetSelector selector_;
    BodyStateTracker body_tracker_;
    CenterCueRefiner center_cue_refiner_;
    AimEnhancementPipeline enhancer_;
    TensorRTEngine engine_;
    std::atomic<Mode> mode_{Mode::WarmScan};
    void* graphics_resource_ = nullptr;
    std::vector<uint8_t> host_color_frame_;
    DetectionBatch last_scan_batch_;
    WarmScanSnapshot warm_snapshot_;
    std::optional<TargetKeyframe> active_keyframe_;
    std::optional<TargetKeyframe> pending_prewarm_keyframe_;
    std::optional<BodyStateResult> last_body_state_;
    uint64_t last_scan_captured_at_ns_ = 0;
    uint64_t last_track_captured_at_ns_ = 0;
    bool force_active_scan_ = false;
    bool last_scan_missed_target_ = false;
    float track_interval_ms_ = 0.0f;
    float warm_scan_interval_ms_ = 50.0f;
    float active_normal_scan_interval_ms_ = 12.0f;
    float active_recovery_scan_interval_ms_ = 8.0f;
    int width_ = 0;
    int height_ = 0;
};

} // namespace vision_native
