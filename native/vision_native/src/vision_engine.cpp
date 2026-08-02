#include "vision_native/vision_engine.h"
#include "vision_native/vision_result_copy.h"
#include "vision_native/preprocess.h"
#include "color_readback.h"
#include "vision_native/build_family.h"

#include <d3d11.h>
#include <cuda_d3d11_interop.h>
#include <cuda_runtime_api.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace vision_native {
namespace {

constexpr const char* kDefaultEnginePath =
    "models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine";
constexpr float kSelectorDecodeConfidenceFloor = 0.40f;
constexpr float kTorsoBoxShrinkX = 0.22f;
constexpr float kTorsoBoxShrinkTop = 0.18f;
constexpr float kTorsoBoxShrinkBottom = 0.20f;

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

float ns_to_ms(uint64_t delta_ns) {
    return static_cast<float>(delta_ns) / 1'000'000.0f;
}

const char* viewport_level_name(int level) {
    switch (level) {
    case 0:
        return "precision";
    case 2:
        return "rescue";
    case 1:
    default:
        return "normal";
    }
}

std::string default_engine_path() {
    const char* from_env = std::getenv("VISION_MODEL_PATH");
    if (from_env != nullptr && from_env[0] != '\0') {
        return std::string(from_env);
    }
    return std::string(kDefaultEnginePath);
}

std::optional<AimSlowZone> slow_zone_from_body_box(const VisionResult& result) {
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

} // namespace

VisionEngine::VisionEngine(
    int width,
    int height,
    int adapter_index,
    int output_index,
    int timeout_ms,
    std::string engine_path,
    std::string color_readback_mode,
    int expected_tensor_width,
    int expected_tensor_height,
    bool require_isotropic_resize)
    : capture_(width, height, adapter_index, output_index, timeout_ms),
      selector_(width, height),
      host_color_frame_(std::make_unique<ColorReadbackBuffer>(color_readback_mode == "pinned")),
      width_(width),
      height_(height),
      active_viewport_width_(width),
      active_viewport_height_(height) {
    requested_viewport_width_.store(width, std::memory_order_relaxed);
    requested_viewport_height_.store(height, std::memory_order_relaxed);
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
    cudaDeviceProp properties{};
    check_cuda(cudaGetDeviceProperties(&properties, cuda_device), "cudaGetDeviceProperties");
    const std::string resolved_engine_path =
        engine_path.empty() ? default_engine_path() : std::move(engine_path);
    validate_runtime_artifact_family(
        compiled_build_family(), resolved_engine_path, properties.major, properties.minor);
    engine_ = std::make_unique<TensorRTEngine>(resolved_engine_path);
    resize_contract_ = validate_resize_contract(
        width_,
        height_,
        engine_->input_width(),
        engine_->input_height(),
        expected_tensor_width,
        expected_tensor_height,
        require_isotropic_resize);

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
    const cudaError_t ego_alloc_status = cudaMalloc(
        reinterpret_cast<void**>(&device_ego_gray_), kEgoMotionPixelCount);
    if (ego_alloc_status == cudaSuccess) {
        ego_motion_staging_available_ = true;
    } else {
        (void)cudaGetLastError();
        device_ego_gray_ = nullptr;
    }
}

VisionEngine::~VisionEngine() {
    ego_motion_observer_.reset();
    if (device_ego_gray_ != nullptr) {
        (void)cudaFree(device_ego_gray_);
        device_ego_gray_ = nullptr;
    }
    if (graphics_resource_ != nullptr) {
        cudaGraphicsUnregisterResource(static_cast<cudaGraphicsResource_t>(graphics_resource_));
        graphics_resource_ = nullptr;
    }
}

void VisionEngine::set_aiming(bool aiming) {
    aiming_.store(aiming, std::memory_order_relaxed);
    if (!aiming) {
        selector_.reset();
        enhancer_.reset();
        ego_motion_observer_.reset();
        user_aim_intent_ = pipeline_contract::UserAimIntent{};
        external_cue_found_ = false;
        external_cue_x_ = 0.0f;
        external_cue_y_ = 0.0f;
        external_cue_score_ = 0.0f;
    }
}

void VisionEngine::set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) {
    user_aim_intent_ = intent;
}

void VisionEngine::set_viewport(
    int level,
    int width,
    int height,
    std::uint64_t sequence,
    std::uint64_t source_frame_id) {
    if (width <= 0 || height <= 0 || width > width_ || height > height_) {
        std::ostringstream message;
        message << "requested viewport " << width << 'x' << height
                << " must fit capture " << width_ << 'x' << height_;
        throw std::invalid_argument(message.str());
    }
    (void)validate_resize_contract(
        width,
        height,
        engine_->input_width(),
        engine_->input_height(),
        engine_->input_width(),
        engine_->input_height(),
        true);
    std::lock_guard<std::mutex> lock(viewport_mutex_);
    requested_viewport_level_.store(level, std::memory_order_relaxed);
    requested_viewport_width_.store(width, std::memory_order_relaxed);
    requested_viewport_height_.store(height, std::memory_order_relaxed);
    requested_viewport_source_frame_id_.store(
        source_frame_id, std::memory_order_relaxed);
    requested_viewport_sequence_.store(sequence, std::memory_order_release);
}

void VisionEngine::set_external_cue(bool found, float cue_x, float cue_y, float cue_score) {
    external_cue_found_ = found;
    external_cue_x_ = cue_x;
    external_cue_y_ = cue_y;
    external_cue_score_ = cue_score;
}

void VisionEngine::reset() {
    aiming_.store(false, std::memory_order_relaxed);
    selector_.reset();
    enhancer_.reset();
    ego_motion_observer_.reset();
    user_aim_intent_ = pipeline_contract::UserAimIntent{};
    external_cue_found_ = false;
    external_cue_x_ = 0.0f;
    external_cue_y_ = 0.0f;
    external_cue_score_ = 0.0f;
}

VisionResult VisionEngine::poll_once() {
    VisionResult result;
    std::uint64_t requested_sequence = 0;
    std::uint64_t viewport_source_frame_id = 0;
    int viewport_level = 1;
    int viewport_width = width_;
    int viewport_height = height_;
    {
        std::lock_guard<std::mutex> lock(viewport_mutex_);
        requested_sequence =
            requested_viewport_sequence_.load(std::memory_order_relaxed);
        viewport_level =
            requested_viewport_level_.load(std::memory_order_relaxed);
        viewport_width =
            requested_viewport_width_.load(std::memory_order_relaxed);
        viewport_height =
            requested_viewport_height_.load(std::memory_order_relaxed);
        viewport_source_frame_id =
            requested_viewport_source_frame_id_.load(std::memory_order_relaxed);
    }
    const int viewport_left = (width_ - viewport_width) / 2;
    const int viewport_top = (height_ - viewport_height) / 2;
    const bool viewport_changed =
        viewport_width != active_viewport_width_ ||
        viewport_height != active_viewport_height_ ||
        requested_sequence != active_viewport_sequence_;
    active_viewport_width_ = viewport_width;
    active_viewport_height_ = viewport_height;
    active_viewport_sequence_ = requested_sequence;
    result.viewport_level = viewport_level_name(viewport_level);
    result.viewport_sequence = requested_sequence;
    result.viewport_source_frame_id = viewport_source_frame_id;
    result.viewport_width = viewport_width;
    result.viewport_height = viewport_height;
    result.viewport_left = viewport_left;
    result.viewport_top = viewport_top;
    result.viewport_changed = viewport_changed;
    result.user_aim_intent = user_aim_intent_;
    result.screen_center_x = static_cast<float>(width_) * 0.5f;
    result.screen_center_y = static_cast<float>(height_) * 0.5f;
    result.has_external_cue = external_cue_found_;
    result.external_cue_x = external_cue_x_;
    result.external_cue_y = external_cue_y_;
    result.external_cue_score = external_cue_score_;

    if (!aiming_.load(std::memory_order_relaxed)) {
        result.result_at_ns = now_ns();
        result.age_ms = 0.0f;
        return result;
    }

    const DxgiCaptureMetadata metadata = capture_.grab();
    result.frame_id = metadata.frame.frame_id;
    result.capture_acquire_begin_ns = metadata.capture_acquire_begin_ns;
    result.capture_acquire_complete_ns = metadata.capture_acquire_complete_ns;
    result.capture_copy_complete_ns = metadata.capture_copy_complete_ns;
    result.source_present_qpc = metadata.source_present_qpc;
    result.source_present_qpc_frequency = metadata.source_present_qpc_frequency;
    result.source_present_available = metadata.source_present_available;
    result.accumulated_frames = metadata.accumulated_frames;
    result.captured_at_ns = metadata.frame.captured_at_ns;
    result.wait_ms = metadata.acquire_ms + metadata.copy_ms;
    result.capture_acquire_ms = metadata.acquire_ms;
    result.capture_copy_ms = metadata.copy_ms;
    result.target_x = result.screen_center_x;
    result.target_y = result.screen_center_y;
    EgoMotionShadowResult completed_shadow;
    if (ego_motion_observer_.take_latest_result(&completed_shadow)) {
        result.ego_motion_shadow = completed_shadow;
    }

    if (!metadata.updated || metadata.frame.data == nullptr) {
        result.result_at_ns = now_ns();
        if (result.captured_at_ns != 0) {
            result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
        }
        return result;
    }
    result.frame_updated = true;

    cudaGraphicsResource_t graphics_resource = static_cast<cudaGraphicsResource_t>(graphics_resource_);
    if (graphics_resource == nullptr) {
        throw std::runtime_error("VisionEngine graphics resource is not registered");
    }

    bool mapped = false;
    try {
        const uint64_t map_start = now_ns();
        check_cuda(cudaGraphicsMapResources(1, &graphics_resource, nullptr), "cudaGraphicsMapResources");
        mapped = true;

        cudaArray_t frame_array = nullptr;
        check_cuda(
            cudaGraphicsSubResourceGetMappedArray(&frame_array, graphics_resource, 0, 0),
            "cudaGraphicsSubResourceGetMappedArray");
        result.cuda_map_ms = ns_to_ms(now_ns() - map_start);

        DetectionBatch batch = engine_->infer_bgra_array_roi(
            frame_array,
            width_,
            height_,
            viewport_left,
            viewport_top,
            viewport_width,
            viewport_height,
            kSelectorDecodeConfidenceFloor);
        for (Detection& detection : batch.detections) {
            detection.x1 += static_cast<float>(viewport_left);
            detection.x2 += static_cast<float>(viewport_left);
            detection.y1 += static_cast<float>(viewport_top);
            detection.y2 += static_cast<float>(viewport_top);
            if (detection.has_cue_point) {
                detection.cue_x += static_cast<float>(viewport_left);
                detection.cue_y += static_cast<float>(viewport_top);
            }
        }
        batch.frame_width = width_;
        batch.frame_height = height_;
        batch.frame_id = metadata.frame.frame_id;
        batch.capture_acquire_begin_ns = metadata.capture_acquire_begin_ns;
        batch.capture_acquire_complete_ns = metadata.capture_acquire_complete_ns;
        batch.capture_copy_complete_ns = metadata.capture_copy_complete_ns;
        batch.source_present_qpc = metadata.source_present_qpc;
        batch.source_present_qpc_frequency = metadata.source_present_qpc_frequency;
        batch.source_present_available = metadata.source_present_available;
        batch.accumulated_frames = metadata.accumulated_frames;
        batch.captured_at_ns = metadata.frame.captured_at_ns;
        batch.has_external_cue = external_cue_found_;
        batch.external_cue_x = external_cue_x_;
        batch.external_cue_y = external_cue_y_;
        batch.external_cue_score = external_cue_score_;

        // Stage only a small grayscale image while the D3D resource is
        // mapped. This is shadow input; no controller/tracker code consumes
        // it and a staging failure simply disables this frame's shadow pair.
        if (ego_motion_staging_available_ && device_ego_gray_ != nullptr) {
            const auto ego_stage_start = now_ns();
            const cudaTextureObject_t ego_texture = launch_bgra_array_to_gray_u8(
                frame_array,
                width_,
                height_,
                kEgoMotionFrameWidth,
                kEgoMotionFrameHeight,
                device_ego_gray_,
                engine_->cuda_stream());
            cudaError_t ego_status = ego_texture != 0
                ? cudaGetLastError() : cudaErrorUnknown;
            if (ego_status == cudaSuccess) {
                ego_status = cudaMemcpyAsync(
                    host_ego_gray_.data(),
                    device_ego_gray_,
                    host_ego_gray_.size(),
                    cudaMemcpyDeviceToHost,
                    engine_->cuda_stream());
            }
            if (ego_status == cudaSuccess) {
                ego_status = cudaStreamSynchronize(engine_->cuda_stream());
            }
            if (ego_texture != 0) {
                (void)cudaDestroyTextureObject(ego_texture);
            }
            if (ego_status == cudaSuccess) {
                result.ego_motion_stage_ms = ns_to_ms(now_ns() - ego_stage_start);
                std::array<EgoMotionMaskRect, kEgoMotionMaxMaskRects> masks{};
                std::size_t mask_count = 0;
                for (const Detection& detection : batch.detections) {
                    if (mask_count >= masks.size()) break;
                    const float sx = static_cast<float>(kEgoMotionFrameWidth) /
                        static_cast<float>(width_);
                    const float sy = static_cast<float>(kEgoMotionFrameHeight) /
                        static_cast<float>(height_);
                    masks[mask_count++] = EgoMotionMaskRect{
                        std::max(0, static_cast<int>(detection.x1 * sx) - 4),
                        std::max(0, static_cast<int>(detection.y1 * sy) - 4),
                        std::min(kEgoMotionFrameWidth,
                            static_cast<int>(detection.x2 * sx) + 5),
                        std::min(kEgoMotionFrameHeight,
                            static_cast<int>(detection.y2 * sy) + 5)};
                }
                const EgoMotionFrameView ego_frame{
                    metadata.frame.frame_id,
                    metadata.source_present_qpc,
                    metadata.source_present_qpc_frequency,
                    metadata.frame.captured_at_ns,
                    now_ns(),
                    kEgoMotionFrameWidth,
                    kEgoMotionFrameHeight,
                    kEgoMotionFrameWidth,
                    host_ego_gray_.data(),
                    masks.data(),
                    mask_count};
                (void)ego_motion_observer_.submit_frame(ego_frame);
            } else {
                (void)cudaGetLastError();
            }
        }

        const auto color_region = selector_.required_color_region(batch);
        bool has_color_frame = false;
        if (color_region.has_value()) {
            const int region_width = color_region->right - color_region->left;
            const int region_height = color_region->bottom - color_region->top;
            const size_t host_bytes =
                static_cast<size_t>(region_width) * static_cast<size_t>(region_height) * 4;
            if (!host_color_frame_->ensure(host_bytes)) {
                throw std::runtime_error("failed to allocate color readback buffer");
            }
            const uint64_t color_copy_start = now_ns();
            const cudaStream_t stream = engine_->cuda_stream();
            cudaError_t copy_status = cudaMemcpy2DFromArrayAsync(
                    host_color_frame_->data(),
                    static_cast<size_t>(region_width) * 4,
                    frame_array,
                    color_region->left * 4,
                    color_region->top,
                    static_cast<size_t>(region_width) * 4,
                    static_cast<size_t>(region_height),
                    cudaMemcpyDeviceToHost,
                    stream);
            if (copy_status == cudaSuccess) copy_status = cudaStreamSynchronize(stream);
            if (copy_status != cudaSuccess) {
                // Pinned/async setup is an optimization.  Clear its error and
                // retry this frame through the legacy pageable synchronous path.
                (void)cudaGetLastError();
                if (!host_color_frame_->fallback_to_pageable(host_bytes)) {
                    throw std::runtime_error("failed to allocate pageable color readback fallback");
                }
                check_cuda(
                    cudaMemcpy2DFromArray(
                        host_color_frame_->data(),
                        static_cast<size_t>(region_width) * 4,
                        frame_array,
                        color_region->left * 4,
                        color_region->top,
                        static_cast<size_t>(region_width) * 4,
                        static_cast<size_t>(region_height),
                        cudaMemcpyDeviceToHost),
                    "cudaMemcpy2DFromArray pageable color fallback");
            }
            result.color_copy_ms = ns_to_ms(now_ns() - color_copy_start);
            result.color_copy_required = true;
            result.color_copy_bytes = host_bytes;
            result.color_copy_region_ratio = static_cast<float>(
                static_cast<double>(region_width) * static_cast<double>(region_height) /
                static_cast<double>(width_ * height_));
            result.color_readback_mode = color_readback_mode_name(host_color_frame_->mode());
            has_color_frame = true;
        }

        const uint64_t unmap_start = now_ns();
        check_cuda(cudaGraphicsUnmapResources(1, &graphics_resource, nullptr), "cudaGraphicsUnmapResources");
        result.cuda_unmap_ms = ns_to_ms(now_ns() - unmap_start);
        mapped = false;

        const uint64_t post_start = now_ns();
        result.frame_id = batch.frame_id;
        result.capture_acquire_begin_ns = batch.capture_acquire_begin_ns;
        result.capture_acquire_complete_ns = batch.capture_acquire_complete_ns;
        result.capture_copy_complete_ns = batch.capture_copy_complete_ns;
        result.source_present_qpc = batch.source_present_qpc;
        result.source_present_qpc_frequency = batch.source_present_qpc_frequency;
        result.source_present_available = batch.source_present_available;
        result.accumulated_frames = batch.accumulated_frames;
        result.captured_at_ns = batch.captured_at_ns;
        result.inferred_at_ns = batch.inferred_at_ns;
        result.has_external_cue = batch.has_external_cue;
        result.external_cue_x = batch.external_cue_x;
        result.external_cue_y = batch.external_cue_y;
        result.external_cue_score = batch.external_cue_score;
        result.preprocess_ms = batch.preprocess_ms;
        result.infer_ms = batch.infer_ms;
        result.output_copy_sync_ms = batch.output_copy_sync_ms;
        result.gpu_total_ms = batch.gpu_total_ms;
        result.output_copy_ms = batch.output_copy_ms;
        result.output_wait_ms = batch.output_wait_ms;
        result.decode_ms = batch.decode_ms;
        result.preprocess_mode = batch.preprocess_mode;
        result.boxes_seen = static_cast<float>(batch.detections.size());

        VisionResult targeting;
        const pipeline_contract::UserAimIntent user_aim_intent = user_aim_intent_;
        const uint64_t selector_start = now_ns();
        if (has_color_frame) {
            result.color_candidate_count = static_cast<std::uint32_t>(batch.detections.size());
            targeting = selector_.select_with_frame(
                batch,
                VisionTargetSelector::ColorFrameView{
                    host_color_frame_->data(),
                    color_region->right - color_region->left,
                    color_region->bottom - color_region->top,
                    (color_region->right - color_region->left) * 4,
                    color_region->left,
                    color_region->top,
                    width_,
                    height_,
                    PixelFormat::BGRA8,
                },
                user_aim_intent);
        } else {
            targeting = selector_.select(batch, user_aim_intent);
        }
        result.selector_ms = ns_to_ms(now_ns() - selector_start);
        result.color_classify_ms = has_color_frame ? result.selector_ms : 0.0f;
        result.has_target = targeting.has_target;
        result.auto_fire = targeting.auto_fire;
        result.dx = targeting.dx;
        result.dy = targeting.dy;
        result.target_x = targeting.target_x;
        result.target_y = targeting.target_y;
        result.has_body_box = targeting.has_body_box;
        result.body_x1 = targeting.body_x1;
        result.body_y1 = targeting.body_y1;
        result.body_x2 = targeting.body_x2;
        result.body_y2 = targeting.body_y2;
        result.target_source = targeting.target_source;
        result.target_tier = targeting.target_tier;
        result.aim_authority = targeting.aim_authority;
        result.fire_authority = targeting.fire_authority;
        result.association_stage = targeting.association_stage;
        result.target_confidence = targeting.target_confidence;
        result.intent_id = targeting.intent_id;
        result.intent_applied = targeting.intent_applied;
        result.intent_decision = targeting.intent_decision;
        result.intent_score = targeting.intent_score;
        result.user_aim_intent = user_aim_intent;
        result.boxes_seen = targeting.boxes_seen;
        copy_selector_identity_fields(result, targeting);
        result.detections = std::move(targeting.detections);

        if (result.has_target) {
            const uint64_t enhance_start = now_ns();
            const double enhancement_timestamp =
                batch.inferred_at_ns != 0
                    ? static_cast<double>(batch.inferred_at_ns) / 1'000'000'000.0
                    : static_cast<double>(now_ns()) / 1'000'000'000.0;
            const VisionResult enhanced = enhancer_.process(
                result,
                enhancement_timestamp,
                slow_zone_from_body_box(result));
            result.dx = enhanced.dx;
            result.dy = enhanced.dy;
            result.enhance_ms = ns_to_ms(now_ns() - enhance_start);
        } else {
            enhancer_.reset();
        }

        result.result_at_ns = now_ns();
        if (ego_motion_observer_.take_latest_result(&completed_shadow)) {
            result.ego_motion_shadow = completed_shadow;
        }
        result.post_ms = batch.decode_ms + ns_to_ms(result.result_at_ns - post_start);
        if (result.captured_at_ns != 0) {
            result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
        }
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

int VisionEngine::tensor_width() const {
    return resize_contract_.tensor_width;
}

int VisionEngine::tensor_height() const {
    return resize_contract_.tensor_height;
}

float VisionEngine::resize_scale_x() const {
    return resize_contract_.scale_x;
}

float VisionEngine::resize_scale_y() const {
    return resize_contract_.scale_y;
}

bool VisionEngine::resize_isotropic() const {
    return resize_contract_.isotropic;
}

} // namespace vision_native
