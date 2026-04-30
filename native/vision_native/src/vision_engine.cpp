#include "vision_native/vision_engine.h"

#include "image_ops.h"

#include <d3d11.h>
#include <cuda_d3d11_interop.h>
#include <cuda_runtime_api.h>

#include <chrono>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vision_native {
namespace {

using detail::GrayFrame;
using detail::Point2f;
using detail::RectF;

constexpr const char* kDefaultEnginePath = "models/best.engine";
constexpr float kTorsoBoxShrinkX = 0.22f;
constexpr float kTorsoBoxShrinkTop = 0.18f;
constexpr float kTorsoBoxShrinkBottom = 0.20f;
constexpr float kMaxKeyframeAgeMs = 40.0f;
constexpr float kWarmSnapshotMaxAgeMs = 60.0f;
constexpr size_t kWarmSnapshotTopCandidates = 3;
constexpr int kKeyframeTemplateRadius = 6;

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

float ns_to_ms(uint64_t delta_ns) {
    return static_cast<float>(delta_ns) / 1'000'000.0f;
}

bool interval_elapsed(uint64_t last_ns, uint64_t current_ns, float interval_ms) {
    if (last_ns == 0 || current_ns == 0 || current_ns <= last_ns) {
        return true;
    }
    return ns_to_ms(current_ns - last_ns) >= interval_ms;
}

const char* engine_mode_to_string(VisionEngine::Mode mode) {
    switch (mode) {
    case VisionEngine::Mode::Idle:
        return "idle";
    case VisionEngine::Mode::WarmScan:
        return "warm_scan";
    case VisionEngine::Mode::ActiveTrack:
        return "active_track";
    default:
        return "idle";
    }
}

std::optional<AimSlowZone> slow_zone_from_body_box(const VisionResult& result) {
    if ((result.torso_x2 - result.torso_x1) > 1.0f && (result.torso_y2 - result.torso_y1) > 1.0f) {
        return AimSlowZone{
            result.torso_x1,
            result.torso_y1,
            result.torso_x2,
            result.torso_y2,
        };
    }

    if (!result.has_body_box) {
        return std::nullopt;
    }

    const float box_w = result.body_x2 - result.body_x1;
    const float box_h = result.body_y2 - result.body_y1;
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return std::nullopt;
    }

    return AimSlowZone{
        result.body_x1 + (box_w * kTorsoBoxShrinkX),
        result.body_y1 + (box_h * kTorsoBoxShrinkTop),
        result.body_x2 - (box_w * kTorsoBoxShrinkX),
        result.body_y2 - (box_h * kTorsoBoxShrinkBottom),
    };
}

void check_cuda(cudaError_t status, const char* what) {
    if (status != cudaSuccess) {
        std::ostringstream out;
        out << what << ": " << cudaGetErrorString(status);
        throw std::runtime_error(out.str());
    }
}

void apply_body_state_to_result(VisionResult& result, const BodyStateResult& body_state) {
    result.has_target = body_state.has_target;
    result.has_body_box = body_state.has_body_box;
    result.target_x = body_state.target_x;
    result.target_y = body_state.target_y;
    result.dx = result.target_x - result.screen_center_x;
    result.dy = result.target_y - result.screen_center_y;
    result.anchor_confidence = body_state.anchor_confidence;
    result.body_state_mode = body_state.body_state_mode;
    result.anchor_source = body_state.anchor_source;
    result.target_source = body_state.anchor_source;
    result.body_x1 = body_state.body_x1;
    result.body_y1 = body_state.body_y1;
    result.body_x2 = body_state.body_x2;
    result.body_y2 = body_state.body_y2;
    result.torso_x1 = body_state.torso_x1;
    result.torso_y1 = body_state.torso_y1;
    result.torso_x2 = body_state.torso_x2;
    result.torso_y2 = body_state.torso_y2;
    result.debug_search_x1 = body_state.debug_search_x1;
    result.debug_search_y1 = body_state.debug_search_y1;
    result.debug_search_x2 = body_state.debug_search_x2;
    result.debug_search_y2 = body_state.debug_search_y2;
    result.debug_predicted_x = body_state.debug_predicted_x;
    result.debug_predicted_y = body_state.debug_predicted_y;
    result.debug_patch_x = body_state.debug_patch_x;
    result.debug_patch_y = body_state.debug_patch_y;
    result.debug_patch_valid = body_state.debug_patch_valid;
    result.debug_template_w = body_state.debug_template_w;
    result.debug_template_h = body_state.debug_template_h;
    result.debug_track_points = body_state.debug_track_points;
}

void apply_center_cue_to_result(VisionResult& result, const CenterCueResult& center_cue) {
    result.yellow_cue_present = center_cue.yellow_cue_present;
    result.yellow_cue_score = center_cue.yellow_cue_score;
    result.yellow_cue_x = center_cue.yellow_cue_x;
    result.yellow_cue_y = center_cue.yellow_cue_y;
    result.yellow_mask_area = center_cue.yellow_mask_area;
    result.yellow_roi_x1 = center_cue.yellow_roi_x1;
    result.yellow_roi_y1 = center_cue.yellow_roi_y1;
    result.yellow_roi_x2 = center_cue.yellow_roi_x2;
    result.yellow_roi_y2 = center_cue.yellow_roi_y2;
    result.refiner_applied = center_cue.refiner_applied;
    result.refined_target_x = center_cue.refined_target_x;
    result.refined_target_y = center_cue.refined_target_y;
    if (center_cue.refiner_applied) {
        result.target_x = center_cue.refined_target_x;
        result.target_y = center_cue.refined_target_y;
        result.dx = result.target_x - result.screen_center_x;
        result.dy = result.target_y - result.screen_center_y;
    }
}

void apply_bbox_fallback_metadata(VisionResult& result) {
    result.anchor_confidence = 0.0f;
    result.body_state_mode = "drop";
    result.anchor_source = result.has_target ? "bbox_fallback" : "none";
    result.target_source = result.has_target ? "bbox_fallback" : "";
    if (result.has_body_box) {
        const float box_w = result.body_x2 - result.body_x1;
        const float box_h = result.body_y2 - result.body_y1;
        result.torso_x1 = result.body_x1 + (box_w * kTorsoBoxShrinkX);
        result.torso_y1 = result.body_y1 + (box_h * kTorsoBoxShrinkTop);
        result.torso_x2 = result.body_x2 - (box_w * kTorsoBoxShrinkX);
        result.torso_y2 = result.body_y2 - (box_h * kTorsoBoxShrinkBottom);
    } else {
        result.torso_x1 = 0.0f;
        result.torso_y1 = 0.0f;
        result.torso_x2 = 0.0f;
        result.torso_y2 = 0.0f;
    }
    result.debug_search_x1 = 0.0f;
    result.debug_search_y1 = 0.0f;
    result.debug_search_x2 = 0.0f;
    result.debug_search_y2 = 0.0f;
    result.debug_predicted_x = 0.0f;
    result.debug_predicted_y = 0.0f;
    result.debug_patch_x = 0.0f;
    result.debug_patch_y = 0.0f;
    result.debug_patch_valid = false;
    result.debug_template_w = 0.0f;
    result.debug_template_h = 0.0f;
    result.debug_track_points.clear();
    result.yellow_cue_present = false;
    result.yellow_cue_score = 0.0f;
    result.yellow_cue_x = 0.0f;
    result.yellow_cue_y = 0.0f;
    result.yellow_mask_area = 0.0f;
    result.yellow_roi_x1 = 0.0f;
    result.yellow_roi_y1 = 0.0f;
    result.yellow_roi_x2 = 0.0f;
    result.yellow_roi_y2 = 0.0f;
    result.refiner_applied = false;
    result.refined_target_x = 0.0f;
    result.refined_target_y = 0.0f;
}

void apply_scan_boxes(VisionResult& result, const DetectionBatch& batch) {
    result.debug_scan_boxes.clear();
    result.debug_scan_boxes.reserve(batch.detections.size() * 5);
    for (const Detection& detection : batch.detections) {
        result.debug_scan_boxes.push_back(detection.x1);
        result.debug_scan_boxes.push_back(detection.y1);
        result.debug_scan_boxes.push_back(detection.x2);
        result.debug_scan_boxes.push_back(detection.y2);
        result.debug_scan_boxes.push_back(detection.conf);
    }
}

RectF rect_from_result(const VisionResult& result) {
    return {
        result.body_x1,
        result.body_y1,
        result.body_x2,
        result.body_y2,
    };
}

RectF rect_from_candidate(const VisionTargetSelector::Candidate& candidate) {
    return {
        candidate.body_box.left,
        candidate.body_box.top,
        candidate.body_box.right,
        candidate.body_box.bottom,
    };
}

Point2f clamp_anchor_prior(Point2f anchor, const RectF& torso_box) {
    anchor.x = detail::clampf(anchor.x, torso_box.left, torso_box.right);
    anchor.y = detail::clampf(anchor.y, torso_box.top, torso_box.bottom);
    return anchor;
}

TargetKeyframe build_keyframe(
    uint64_t frame_id,
    uint64_t captured_at_ns,
    const RectF& body_box,
    Point2f anchor_prior,
    const char* source,
    float score,
    const GrayFrame& gray) {
    TargetKeyframe keyframe;
    const RectF clamped_body = detail::clamp_rect(body_box, gray.width, gray.height);
    const RectF torso_box = detail::clamp_rect(detail::torso_band_from_body_box(clamped_body), gray.width, gray.height);
    const Point2f clamped_anchor = clamp_anchor_prior(anchor_prior, torso_box);

    keyframe.frame_id = frame_id;
    keyframe.captured_at_ns = captured_at_ns;
    keyframe.body_x1 = clamped_body.left;
    keyframe.body_y1 = clamped_body.top;
    keyframe.body_x2 = clamped_body.right;
    keyframe.body_y2 = clamped_body.bottom;
    keyframe.torso_x1 = torso_box.left;
    keyframe.torso_y1 = torso_box.top;
    keyframe.torso_x2 = torso_box.right;
    keyframe.torso_y2 = torso_box.bottom;
    keyframe.anchor_prior_x = clamped_anchor.x;
    keyframe.anchor_prior_y = clamped_anchor.y;
    keyframe.score = score;
    keyframe.target_source = source;
    detail::extract_patch(
        gray,
        static_cast<int>(std::round(clamped_anchor.x)),
        static_cast<int>(std::round(clamped_anchor.y)),
        kKeyframeTemplateRadius,
        keyframe.torso_patch,
        keyframe.patch_width,
        keyframe.patch_height);
    return keyframe;
}

std::optional<TargetKeyframe> make_keyframe_from_result(
    const VisionResult& result,
    const GrayFrame& gray) {
    if (!result.has_body_box || gray.empty()) {
        return std::nullopt;
    }
    return build_keyframe(
        result.frame_id,
        result.captured_at_ns,
        rect_from_result(result),
        Point2f{result.target_x, result.target_y},
        result.target_source,
        result.anchor_confidence,
        gray);
}

std::optional<TargetKeyframe> make_keyframe_from_scored_candidate(
    const VisionTargetSelector::ScoredCandidate& scored,
    uint64_t frame_id,
    uint64_t captured_at_ns,
    const GrayFrame& gray) {
    if (gray.empty()) {
        return std::nullopt;
    }
    return build_keyframe(
        frame_id,
        captured_at_ns,
        rect_from_candidate(scored.candidate),
        Point2f{scored.candidate.target_x, scored.candidate.target_y},
        scored.candidate.source,
        scored.score,
        gray);
}

void populate_warm_snapshot(
    WarmScanSnapshot& snapshot,
    const std::vector<VisionTargetSelector::ScoredCandidate>& ranked,
    uint64_t frame_id,
    uint64_t captured_at_ns,
    const GrayFrame& gray) {
    snapshot = WarmScanSnapshot{};
    snapshot.frame_id = frame_id;
    snapshot.captured_at_ns = captured_at_ns;
    snapshot.candidate_count = ranked.size();
    snapshot.top_candidates.clear();
    for (size_t index = 0; index < ranked.size() && index < kWarmSnapshotTopCandidates; ++index) {
        const auto keyframe = make_keyframe_from_scored_candidate(ranked[index], frame_id, captured_at_ns, gray);
        if (!keyframe.has_value()) {
            continue;
        }
        snapshot.top_candidates.push_back(WarmSnapshotCandidate{*keyframe, ranked[index].score});
    }
}

bool observation_confirmed(const VisionResult& targeting) {
    if (!targeting.has_target) {
        return false;
    }

    const std::string source = targeting.target_source;
    return source == "observed" || source == "reconstructed";
}

bool has_recent_prewarm_candidate(const WarmScanSnapshot& snapshot, uint64_t captured_at_ns) {
    if (snapshot.top_candidates.empty() || snapshot.captured_at_ns == 0 || captured_at_ns == 0) {
        return false;
    }
    if (captured_at_ns <= snapshot.captured_at_ns) {
        return true;
    }
    return ns_to_ms(captured_at_ns - snapshot.captured_at_ns) <= kWarmSnapshotMaxAgeMs;
}

VisionTargetSelector::ColorFrameView color_frame_view(
    const std::vector<uint8_t>& host_color_frame,
    int width,
    int height) {
    return VisionTargetSelector::ColorFrameView{
        host_color_frame.data(),
        width,
        height,
        width * 4,
        PixelFormat::BGRA8,
    };
}

CenterCueFrameView center_cue_frame_view(
    const std::vector<uint8_t>& host_color_frame,
    int width,
    int height) {
    return CenterCueFrameView{
        host_color_frame.data(),
        width,
        height,
        width * 4,
        PixelFormat::BGRA8,
    };
}

EgoFrameView ego_frame_view(const std::vector<uint8_t>& host_color_frame, int width, int height) {
    return EgoFrameView{
        host_color_frame.data(),
        width,
        height,
        width * 4,
        PixelFormat::BGRA8,
    };
}

void fill_host_color_frame(
    std::vector<uint8_t>& host_color_frame,
    cudaArray_t frame_array,
    int width,
    int height) {
    const size_t host_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (host_color_frame.size() != host_bytes) {
        host_color_frame.resize(host_bytes);
    }
    check_cuda(
        cudaMemcpy2DFromArray(
            host_color_frame.data(),
            static_cast<size_t>(width) * 4,
            frame_array,
            0,
            0,
            static_cast<size_t>(width) * 4,
            static_cast<size_t>(height),
            cudaMemcpyDeviceToHost),
        "cudaMemcpy2DFromArray host_color_frame");
}

} // namespace

VisionEngine::VisionEngine(
    int width,
    int height,
    int adapter_index,
    int output_index,
    int timeout_ms,
    float track_interval_ms,
    float warm_scan_interval_ms,
    float active_normal_scan_interval_ms,
    float active_recovery_scan_interval_ms)
    : capture_(width, height, adapter_index, output_index, timeout_ms),
      ego_motion_(width, height),
      selector_(width, height),
      body_tracker_(width, height),
      center_cue_refiner_(width, height),
      engine_(kDefaultEnginePath),
      track_interval_ms_(track_interval_ms),
      warm_scan_interval_ms_(warm_scan_interval_ms),
      active_normal_scan_interval_ms_(active_normal_scan_interval_ms),
      active_recovery_scan_interval_ms_(active_recovery_scan_interval_ms),
      width_(width),
      height_(height) {
    auto* d3d_device = static_cast<ID3D11Device*>(capture_.d3d11_device());
    if (d3d_device == nullptr) {
        throw std::runtime_error("DxgiRoiCapture did not expose a D3D11 device");
    }

    unsigned int cuda_device_count = 0;
    int cuda_device = 0;
    check_cuda(
        cudaD3D11GetDevices(
            &cuda_device_count,
            &cuda_device,
            1,
            d3d_device,
            cudaD3D11DeviceListCurrentFrame),
        "cudaD3D11GetDevices");
    if (cuda_device_count == 0) {
        throw std::runtime_error("cudaD3D11GetDevices returned no CUDA device");
    }

    check_cuda(cudaSetDevice(cuda_device), "cudaSetDevice");

    auto* resource = static_cast<ID3D11Resource*>(capture_.texture());
    if (resource == nullptr) {
        throw std::runtime_error("DxgiRoiCapture did not expose an ROI texture");
    }

    cudaGraphicsResource_t graphics_resource = nullptr;
    check_cuda(
        cudaGraphicsD3D11RegisterResource(
            &graphics_resource,
            resource,
            cudaGraphicsRegisterFlagsNone),
        "cudaGraphicsD3D11RegisterResource");
    graphics_resource_ = graphics_resource;
}

VisionEngine::~VisionEngine() {
    if (graphics_resource_ != nullptr) {
        cudaGraphicsUnregisterResource(static_cast<cudaGraphicsResource_t>(graphics_resource_));
        graphics_resource_ = nullptr;
    }
}

void VisionEngine::set_mode(Mode mode) {
    const Mode previous = mode_.exchange(mode, std::memory_order_relaxed);
    if (mode == Mode::Idle) {
        reset();
        return;
    }

    if (mode == previous) {
        return;
    }

    ego_motion_.reset();
    center_cue_refiner_.reset();
    enhancer_.reset();

    if (mode == Mode::WarmScan) {
        selector_.reset();
        body_tracker_.reset();
        active_keyframe_.reset();
        pending_prewarm_keyframe_.reset();
        last_body_state_.reset();
        last_track_captured_at_ns_ = 0;
        force_active_scan_ = false;
        last_scan_missed_target_ = false;
        return;
    }

    selector_.reset();
    body_tracker_.reset();
    active_keyframe_.reset();
    last_body_state_.reset();
    last_track_captured_at_ns_ = 0;
    last_scan_missed_target_ = false;
    force_active_scan_ = true;
    pending_prewarm_keyframe_.reset();
    if (!warm_snapshot_.top_candidates.empty()) {
        pending_prewarm_keyframe_ = warm_snapshot_.top_candidates.front().keyframe;
    }
}

void VisionEngine::set_aiming(bool aiming) {
    set_mode(aiming ? Mode::ActiveTrack : Mode::WarmScan);
}

void VisionEngine::reset() {
    mode_.store(Mode::Idle, std::memory_order_relaxed);
    ego_motion_.reset();
    selector_.reset();
    body_tracker_.reset();
    center_cue_refiner_.reset();
    enhancer_.reset();
    host_color_frame_.clear();
    last_scan_batch_ = DetectionBatch{};
    warm_snapshot_ = WarmScanSnapshot{};
    active_keyframe_.reset();
    pending_prewarm_keyframe_.reset();
    last_body_state_.reset();
    last_scan_captured_at_ns_ = 0;
    last_track_captured_at_ns_ = 0;
    force_active_scan_ = false;
    last_scan_missed_target_ = false;
}

VisionResult VisionEngine::poll_once() {
    VisionResult result;
    result.screen_center_x = static_cast<float>(width_) * 0.5f;
    result.screen_center_y = static_cast<float>(height_) * 0.5f;
    result.target_x = result.screen_center_x;
    result.target_y = result.screen_center_y;

    const Mode mode = mode_.load(std::memory_order_relaxed);
    result.engine_mode = engine_mode_to_string(mode);
    if (mode == Mode::Idle) {
        result.result_at_ns = now_ns();
        return result;
    }

    const DxgiCaptureMetadata metadata = capture_.grab();
    result.frame_id = metadata.frame.frame_id;
    result.captured_at_ns = metadata.frame.captured_at_ns;
    result.acquire_ms = metadata.acquire_ms;
    result.copy_ms = metadata.copy_ms;
    result.capture_ms = metadata.acquire_ms + metadata.copy_ms;
    result.wait_ms = result.capture_ms;

    if (!metadata.updated || metadata.frame.data == nullptr) {
        result.result_at_ns = now_ns();
        if (result.captured_at_ns != 0) {
            result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
        }
        return result;
    }

    if (active_keyframe_.has_value() && result.captured_at_ns != 0 && active_keyframe_->captured_at_ns != 0) {
        if (result.captured_at_ns > active_keyframe_->captured_at_ns) {
            result.keyframe_age_ms = ns_to_ms(result.captured_at_ns - active_keyframe_->captured_at_ns);
        }
    }
    if (last_scan_captured_at_ns_ != 0 && result.captured_at_ns != 0) {
        result.scan_age_ms = result.captured_at_ns > last_scan_captured_at_ns_
            ? ns_to_ms(result.captured_at_ns - last_scan_captured_at_ns_)
            : 0.0f;
    }

    bool should_scan = false;
    const char* scan_reason = "";
    if (mode == Mode::WarmScan) {
        if (interval_elapsed(last_scan_captured_at_ns_, result.captured_at_ns, warm_scan_interval_ms_)) {
            should_scan = true;
            scan_reason = "interval";
        }
    } else {
        const BodyStateTracker::Mode tracker_mode = body_tracker_.mode();
        const bool weak_tracker =
            tracker_mode == BodyStateTracker::Mode::Weak
            || tracker_mode == BodyStateTracker::Mode::Reacquire
            || tracker_mode == BodyStateTracker::Mode::Drop;
        if (force_active_scan_) {
            should_scan = true;
            scan_reason = pending_prewarm_keyframe_.has_value() ? "aim_entry" : "no_target";
        } else if (!active_keyframe_.has_value()) {
            should_scan = true;
            scan_reason = "no_target";
        } else if (result.keyframe_age_ms > kMaxKeyframeAgeMs) {
            should_scan = true;
            scan_reason = "stale_keyframe";
        } else if (last_scan_missed_target_
            && interval_elapsed(last_scan_captured_at_ns_, result.captured_at_ns, active_recovery_scan_interval_ms_)) {
            should_scan = true;
            scan_reason = "recovery";
        } else if (weak_tracker
            && interval_elapsed(last_scan_captured_at_ns_, result.captured_at_ns, active_recovery_scan_interval_ms_)) {
            should_scan = true;
            scan_reason = "weak_anchor";
        } else if (interval_elapsed(last_scan_captured_at_ns_, result.captured_at_ns, active_normal_scan_interval_ms_)) {
            should_scan = true;
            scan_reason = "interval";
        }
    }
    result.scan_ran = should_scan;
    result.scan_reason = scan_reason;

    const bool should_track_interframe =
        mode == Mode::ActiveTrack
        && !should_scan
        && body_tracker_.has_active_target()
        && (track_interval_ms_ <= 0.0f
            || interval_elapsed(last_track_captured_at_ns_, result.captured_at_ns, track_interval_ms_));

    const bool need_host_frame =
        should_track_interframe
        || should_scan
        || pending_prewarm_keyframe_.has_value();

    cudaGraphicsResource_t graphics_resource = static_cast<cudaGraphicsResource_t>(graphics_resource_);
    if (graphics_resource == nullptr) {
        throw std::runtime_error("VisionEngine graphics resource is not registered");
    }

    bool mapped = false;
    try {
        check_cuda(cudaGraphicsMapResources(1, &graphics_resource, nullptr), "cudaGraphicsMapResources");
        mapped = true;

        cudaArray_t frame_array = nullptr;
        check_cuda(
            cudaGraphicsSubResourceGetMappedArray(&frame_array, graphics_resource, 0, 0),
            "cudaGraphicsSubResourceGetMappedArray");

        DetectionBatch batch;
        if (should_scan) {
            batch = engine_.infer_bgra_array(frame_array, width_, height_);
            batch.frame_id = metadata.frame.frame_id;
            batch.captured_at_ns = metadata.frame.captured_at_ns;
        }
        if (need_host_frame) {
            fill_host_color_frame(host_color_frame_, frame_array, width_, height_);
        }

        check_cuda(cudaGraphicsUnmapResources(1, &graphics_resource, nullptr), "cudaGraphicsUnmapResources");
        mapped = false;

        const uint64_t post_start = now_ns();
        result.preprocess_ms = batch.preprocess_ms;
        result.infer_ms = batch.infer_ms;
        result.decode_ms = batch.decode_ms;
        result.boxes_seen = should_scan
            ? static_cast<float>(batch.detections.size())
            : static_cast<float>(last_scan_batch_.detections.size());
        apply_scan_boxes(result, should_scan ? batch : last_scan_batch_);

        EgoFrameView ego_view{};
        GrayFrame current_gray;
        GrayFrame ego_gray;
        CenterCueResult cue_detection;
        bool has_host_frame = need_host_frame && !host_color_frame_.empty();
        if (has_host_frame) {
            ego_view = ego_frame_view(host_color_frame_, width_, height_);
            current_gray = detail::to_grayscale(ego_view, 1);
            ego_gray = detail::downsample_gray(current_gray, 2);
        }

        if (mode == Mode::WarmScan) {
            if (should_scan && has_host_frame) {
                last_scan_batch_ = batch;
                last_scan_captured_at_ns_ = batch.captured_at_ns;
                last_scan_missed_target_ = false;
                apply_scan_boxes(result, last_scan_batch_);
                populate_warm_snapshot(
                    warm_snapshot_,
                    selector_.rank_candidates_with_frame(batch, color_frame_view(host_color_frame_, width_, height_)),
                    batch.frame_id,
                    batch.captured_at_ns,
                    current_gray);
                result.scan_age_ms = 0.0f;
            }
            result.result_at_ns = now_ns();
            result.post_ms = (should_scan ? batch.decode_ms : 0.0f) + ns_to_ms(result.result_at_ns - post_start);
            if (result.captured_at_ns != 0) {
                result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
            }
            return result;
        }

        EgoWarp ego_warp;
        if (has_host_frame) {
            ego_warp = ego_motion_.estimate_gray(ego_gray, should_scan ? batch : last_scan_batch_);
            if (mode == Mode::ActiveTrack) {
                cue_detection = center_cue_refiner_.detect(
                    center_cue_frame_view(host_color_frame_, width_, height_),
                    result.screen_center_x,
                    result.screen_center_y);
            }
        }
        result.ego_confidence = ego_warp.confidence;
        result.ego_model = ego_warp.model;

        if (pending_prewarm_keyframe_.has_value() && has_host_frame) {
            if (has_recent_prewarm_candidate(warm_snapshot_, result.captured_at_ns)) {
                body_tracker_.prime_from_keyframe_gray(*pending_prewarm_keyframe_, current_gray);
                active_keyframe_ = pending_prewarm_keyframe_;
                result.prewarm_used = true;
            }
            pending_prewarm_keyframe_.reset();
        }

        if (should_scan && has_host_frame) {
            last_scan_batch_ = batch;
            last_scan_captured_at_ns_ = batch.captured_at_ns;
            result.scan_age_ms = 0.0f;
            force_active_scan_ = false;
            apply_scan_boxes(result, last_scan_batch_);

            const BodyStateTracker::Mode selector_tracker_mode = body_tracker_.mode();
            const bool cue_seed_can_bias_scan =
                cue_detection.yellow_cue_present
                && (!body_tracker_.has_active_target()
                    || selector_tracker_mode == BodyStateTracker::Mode::Weak
                    || selector_tracker_mode == BodyStateTracker::Mode::Reacquire
                    || selector_tracker_mode == BodyStateTracker::Mode::Drop);
            const std::optional<VisionTargetSelector::CueSeed> cue_seed = cue_seed_can_bias_scan
                ? std::optional<VisionTargetSelector::CueSeed>(VisionTargetSelector::CueSeed{
                    cue_detection.yellow_cue_x,
                    cue_detection.yellow_cue_y,
                    std::max(0.1f, cue_detection.yellow_cue_score),
                })
                : std::nullopt;

            const VisionResult targeting = selector_.select_with_frame(
                batch,
                color_frame_view(host_color_frame_, width_, height_),
                ego_warp,
                cue_seed);

            if (observation_confirmed(targeting)) {
                result.auto_fire = targeting.auto_fire;
                const auto keyframe = make_keyframe_from_result(targeting, current_gray);
                if (keyframe.has_value()) {
                    active_keyframe_ = keyframe;
                    const BodyStateResult body_state = body_tracker_.update_observed_gray(*keyframe, current_gray, ego_warp);
                    if (body_state.has_target) {
                        apply_body_state_to_result(result, body_state);
                        last_body_state_ = body_state;
                        last_track_captured_at_ns_ = result.captured_at_ns;
                    } else {
                        result = targeting;
                        apply_bbox_fallback_metadata(result);
                        last_body_state_.reset();
                        last_track_captured_at_ns_ = 0;
                    }
                } else {
                    result = targeting;
                    apply_bbox_fallback_metadata(result);
                    last_body_state_.reset();
                    last_track_captured_at_ns_ = 0;
                }
                last_scan_missed_target_ = false;
            } else if (body_tracker_.has_active_target()) {
                const BodyStateResult body_state = body_tracker_.update_scan_miss_gray(current_gray, ego_warp);
                if (body_state.has_target) {
                    apply_body_state_to_result(result, body_state);
                    last_body_state_ = body_state;
                    last_track_captured_at_ns_ = result.captured_at_ns;
                } else {
                    active_keyframe_.reset();
                    last_body_state_.reset();
                    last_track_captured_at_ns_ = 0;
                }
                result.auto_fire = false;
                last_scan_missed_target_ = true;
            } else {
                active_keyframe_.reset();
                last_body_state_.reset();
                last_track_captured_at_ns_ = 0;
                last_scan_missed_target_ = true;
            }
        } else if (has_host_frame && should_track_interframe) {
            const BodyStateResult body_state = body_tracker_.update_interframe_gray(current_gray, ego_warp);
            if (body_state.has_target) {
                apply_body_state_to_result(result, body_state);
                last_body_state_ = body_state;
                last_track_captured_at_ns_ = result.captured_at_ns;
            } else {
                active_keyframe_.reset();
                last_body_state_.reset();
                last_track_captured_at_ns_ = 0;
            }
            result.auto_fire = false;
        } else if (last_body_state_.has_value()) {
            apply_body_state_to_result(result, *last_body_state_);
            result.auto_fire = false;
        }

        if (mode == Mode::ActiveTrack && cue_detection.yellow_cue_present) {
            apply_center_cue_to_result(result, cue_detection);
        }

        if (mode == Mode::ActiveTrack && has_host_frame && result.has_target && cue_detection.yellow_cue_present) {
            const CenterCueResult center_cue = center_cue_refiner_.refine_detected(
                cue_detection,
                result.target_x,
                result.target_y,
                result.body_x1,
                result.body_y1,
                result.body_x2,
                result.body_y2,
                result.torso_x1,
                result.torso_y1,
                result.torso_x2,
                result.torso_y2,
                result.body_state_mode);
            apply_center_cue_to_result(result, center_cue);
        }

        if (result.has_target) {
            const double enhancement_timestamp =
                result.captured_at_ns != 0
                    ? static_cast<double>(result.captured_at_ns) / 1'000'000'000.0
                    : static_cast<double>(now_ns()) / 1'000'000'000.0;
            const VisionResult enhanced = enhancer_.process_damping_only(
                result,
                enhancement_timestamp,
                slow_zone_from_body_box(result));
            result.dx = enhanced.dx;
            result.dy = enhanced.dy;
        } else {
            enhancer_.reset();
            result.target_x = result.screen_center_x;
            result.target_y = result.screen_center_y;
            result.dx = 0.0f;
            result.dy = 0.0f;
        }

        result.result_at_ns = now_ns();
        result.post_ms = (should_scan ? batch.decode_ms : 0.0f) + ns_to_ms(result.result_at_ns - post_start);
        if (result.captured_at_ns != 0) {
            result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
        }
        result.engine_mode = engine_mode_to_string(mode);
        return result;
    } catch (...) {
        if (mapped) {
            cudaGraphicsUnmapResources(1, &graphics_resource, nullptr);
        }
        throw;
    }
}

int VisionEngine::width() const {
    return width_;
}

int VisionEngine::height() const {
    return height_;
}

const std::vector<uint8_t>& VisionEngine::last_debug_frame_bgra() const {
    return host_color_frame_;
}

bool VisionEngine::has_debug_frame() const {
    return !host_color_frame_.empty();
}

} // namespace vision_native
