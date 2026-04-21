#include "vision_native/vision_engine.h"

#include <d3d11.h>
#include <cuda_d3d11_interop.h>
#include <cuda_runtime_api.h>

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace vision_native {
namespace {

constexpr const char* kDefaultEnginePath = "models/best.engine";

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

float ns_to_ms(uint64_t delta_ns) {
    return static_cast<float>(delta_ns) / 1'000'000.0f;
}

void check_cuda(cudaError_t status, const char* what) {
    if (status != cudaSuccess) {
        std::ostringstream out;
        out << what << ": " << cudaGetErrorString(status);
        throw std::runtime_error(out.str());
    }
}

const Detection* select_best_detection(const DetectionBatch& batch) {
    const Detection* best = nullptr;
    for (const auto& detection : batch.detections) {
        if (best == nullptr || detection.conf > best->conf) {
            best = &detection;
        }
    }
    return best;
}

} // namespace

VisionEngine::VisionEngine(
    int width,
    int height,
    int adapter_index,
    int output_index,
    int timeout_ms)
    : capture_(width, height, adapter_index, output_index, timeout_ms),
      engine_(kDefaultEnginePath),
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
    result.wait_ms = metadata.acquire_ms + metadata.copy_ms;
    result.target_x = result.screen_center_x;
    result.target_y = result.screen_center_y;

    if (!metadata.updated || metadata.frame.data == nullptr) {
        result.result_at_ns = now_ns();
        if (result.captured_at_ns != 0) {
            result.age_ms = ns_to_ms(result.result_at_ns - result.captured_at_ns);
        }
        return result;
    }

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

        DetectionBatch batch = engine_.infer_bgra_array(frame_array, width_, height_);
        batch.frame_id = metadata.frame.frame_id;
        batch.captured_at_ns = metadata.frame.captured_at_ns;

        check_cuda(cudaGraphicsUnmapResources(1, &graphics_resource, nullptr), "cudaGraphicsUnmapResources");
        mapped = false;

        const uint64_t post_start = now_ns();
        result.frame_id = batch.frame_id;
        result.captured_at_ns = batch.captured_at_ns;
        result.inferred_at_ns = batch.inferred_at_ns;
        result.preprocess_ms = batch.preprocess_ms;
        result.infer_ms = batch.infer_ms;
        result.boxes_seen = static_cast<float>(batch.detections.size());

        const Detection* best = select_best_detection(batch);
        if (best != nullptr) {
            result.has_target = true;
            result.has_body_box = true;
            result.body_x1 = best->x1;
            result.body_y1 = best->y1;
            result.body_x2 = best->x2;
            result.body_y2 = best->y2;
            result.target_x = (best->x1 + best->x2) * 0.5f;
            result.target_y = best->y1 + ((best->y2 - best->y1) * 0.38f);
            result.dx = result.target_x - result.screen_center_x;
            result.dy = result.target_y - result.screen_center_y;
            result.target_source = "observed";
        }

        result.result_at_ns = now_ns();
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

} // namespace vision_native
