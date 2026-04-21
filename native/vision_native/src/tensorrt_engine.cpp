#include "vision_native/tensorrt_engine.h"

#include "vision_native/engine_io.h"
#include "vision_native/preprocess.h"

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace vision_native {
namespace {

class Logger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, char const* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            last_message_ = msg ? msg : "";
        }
    }

    std::string last_message_;
};

void check_cuda(cudaError_t status, const char* what) {
    if (status != cudaSuccess) {
        std::ostringstream out;
        out << what << ": " << cudaGetErrorString(status);
        throw std::runtime_error(out.str());
    }
}

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

size_t volume(nvinfer1::Dims const& dims) {
    if (dims.nbDims <= 0) {
        return 0;
    }

    size_t result = 1;
    for (int32_t i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) {
            throw std::runtime_error("dynamic or invalid tensor dimensions are not supported in Phase 1");
        }
        result *= static_cast<size_t>(dims.d[i]);
    }
    return result;
}

void require_float_tensor(nvinfer1::ICudaEngine* engine, const std::string& name) {
    if (engine->getTensorDataType(name.c_str()) != nvinfer1::DataType::kFLOAT) {
        throw std::runtime_error("Phase 1 supports float32 TensorRT bindings only: " + name);
    }
}

} // namespace

TensorRTEngine::TensorRTEngine(std::string engine_path) {
    load_engine(engine_path);
    allocate_buffers();
}

TensorRTEngine::~TensorRTEngine() {
    if (stream_ != nullptr) {
        cudaStreamDestroy(static_cast<cudaStream_t>(stream_));
    }
    if (device_frame_ != nullptr) {
        cudaFree(device_frame_);
    }
    if (device_input_ != nullptr) {
        cudaFree(device_input_);
    }
    if (device_output_ != nullptr) {
        cudaFree(device_output_);
    }
}

void TensorRTEngine::load_engine(const std::string& engine_path) {
    logger_ = std::make_unique<Logger>();
    initLibNvInferPlugins(logger_.get(), "");

    SerializedEngine bytes = read_serialized_engine(engine_path);
    runtime_.reset(nvinfer1::createInferRuntime(*logger_));
    if (!runtime_) {
        throw std::runtime_error("failed to create TensorRT runtime");
    }

    engine_.reset(runtime_->deserializeCudaEngine(bytes.plan.data(), bytes.plan.size()));
    if (!engine_) {
        throw std::runtime_error("failed to deserialize TensorRT engine: " + engine_path);
    }

    for (int32_t i = 0; i < engine_->getNbIOTensors(); ++i) {
        const char* tensor_name = engine_->getIOTensorName(i);
        if (tensor_name == nullptr) {
            continue;
        }

        const auto mode = engine_->getTensorIOMode(tensor_name);
        if (mode == nvinfer1::TensorIOMode::kINPUT && input_name_.empty()) {
            input_name_ = tensor_name;
        } else if (mode == nvinfer1::TensorIOMode::kOUTPUT && output_name_.empty()) {
            output_name_ = tensor_name;
        }
    }

    if (input_name_.empty() || output_name_.empty()) {
        throw std::runtime_error("TensorRT engine must expose one input and one output tensor");
    }

    require_float_tensor(engine_.get(), input_name_);
    require_float_tensor(engine_.get(), output_name_);

    const nvinfer1::Dims input_dims = engine_->getTensorShape(input_name_.c_str());
    if (input_dims.nbDims != 4 || input_dims.d[0] != 1 || input_dims.d[1] != 3) {
        throw std::runtime_error("Phase 1 expects input shape [1,3,H,W]");
    }
    input_channels_ = static_cast<int>(input_dims.d[1]);
    input_height_ = static_cast<int>(input_dims.d[2]);
    input_width_ = static_cast<int>(input_dims.d[3]);
    input_element_count_ = volume(input_dims);

    const nvinfer1::Dims output_dims = engine_->getTensorShape(output_name_.c_str());
    if (output_dims.nbDims != 3 || output_dims.d[0] != 1 || output_dims.d[2] != 6) {
        throw std::runtime_error("Phase 1 expects output shape [1,300,6]");
    }
    output_rows_ = static_cast<int>(output_dims.d[1]);
    output_cols_ = static_cast<int>(output_dims.d[2]);
    output_element_count_ = volume(output_dims);

    context_.reset(engine_->createExecutionContext());
    if (!context_) {
        throw std::runtime_error("failed to create TensorRT execution context");
    }
}

void TensorRTEngine::allocate_buffers() {
    cudaStream_t stream = nullptr;
    check_cuda(cudaStreamCreate(&stream), "cudaStreamCreate");
    stream_ = stream;
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_input_), input_element_count_ * sizeof(float)), "cudaMalloc input");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_output_), output_element_count_ * sizeof(float)), "cudaMalloc output");
    host_output_.resize(output_element_count_);
}

void TensorRTEngine::ensure_frame_buffer(size_t bytes) {
    if (bytes <= device_frame_bytes_) {
        return;
    }
    if (device_frame_ != nullptr) {
        check_cuda(cudaFree(device_frame_), "cudaFree frame");
        device_frame_ = nullptr;
    }
    check_cuda(cudaMalloc(&device_frame_, bytes), "cudaMalloc frame");
    device_frame_bytes_ = bytes;
}

DetectionBatch TensorRTEngine::infer_rgb(
    const uint8_t* frame_rgb,
    int width,
    int height,
    int row_pitch,
    float conf_threshold) {
    if (frame_rgb == nullptr) {
        throw std::runtime_error("frame_rgb must not be null");
    }
    if (width != input_width_ || height != input_height_) {
        std::ostringstream out;
        out << "frame shape mismatch: expected " << input_height_ << "x" << input_width_
            << " RGB, got " << height << "x" << width;
        throw std::runtime_error(out.str());
    }
    if (row_pitch < width * 3) {
        throw std::runtime_error("row_pitch is smaller than width * 3");
    }

    DetectionBatch batch;
    batch.frame_id = next_frame_id_++;
    batch.captured_at_ns = now_ns();
    batch.frame_width = width;
    batch.frame_height = height;

    const size_t frame_bytes = static_cast<size_t>(row_pitch) * static_cast<size_t>(height);
    ensure_frame_buffer(frame_bytes);

    cudaEvent_t preprocess_start = nullptr;
    cudaEvent_t preprocess_end = nullptr;
    cudaEvent_t infer_start = nullptr;
    cudaEvent_t infer_end = nullptr;
    check_cuda(cudaEventCreate(&preprocess_start), "cudaEventCreate preprocess_start");
    check_cuda(cudaEventCreate(&preprocess_end), "cudaEventCreate preprocess_end");
    check_cuda(cudaEventCreate(&infer_start), "cudaEventCreate infer_start");
    check_cuda(cudaEventCreate(&infer_end), "cudaEventCreate infer_end");

    cudaStream_t stream = static_cast<cudaStream_t>(stream_);
    check_cuda(cudaEventRecord(preprocess_start, stream), "cudaEventRecord preprocess_start");
    check_cuda(cudaMemcpyAsync(device_frame_, frame_rgb, frame_bytes, cudaMemcpyHostToDevice, stream), "cudaMemcpyAsync frame");
    launch_rgb_hwc_to_chw_float(
        static_cast<const uint8_t*>(device_frame_),
        width,
        height,
        row_pitch,
        device_input_,
        stream);
    check_cuda(cudaGetLastError(), "launch_rgb_hwc_to_chw_float");
    check_cuda(cudaEventRecord(preprocess_end, stream), "cudaEventRecord preprocess_end");

    if (!context_->setTensorAddress(input_name_.c_str(), device_input_)) {
        throw std::runtime_error("failed to set TensorRT input address");
    }
    if (!context_->setTensorAddress(output_name_.c_str(), device_output_)) {
        throw std::runtime_error("failed to set TensorRT output address");
    }

    check_cuda(cudaEventRecord(infer_start, stream), "cudaEventRecord infer_start");
    if (!context_->enqueueV3(stream)) {
        throw std::runtime_error("TensorRT enqueueV3 failed");
    }
    check_cuda(cudaEventRecord(infer_end, stream), "cudaEventRecord infer_end");
    check_cuda(
        cudaMemcpyAsync(host_output_.data(), device_output_, output_element_count_ * sizeof(float), cudaMemcpyDeviceToHost, stream),
        "cudaMemcpyAsync output");
    check_cuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize");

    check_cuda(cudaEventElapsedTime(&batch.preprocess_ms, preprocess_start, preprocess_end), "cudaEventElapsedTime preprocess");
    check_cuda(cudaEventElapsedTime(&batch.infer_ms, infer_start, infer_end), "cudaEventElapsedTime infer");
    cudaEventDestroy(preprocess_start);
    cudaEventDestroy(preprocess_end);
    cudaEventDestroy(infer_start);
    cudaEventDestroy(infer_end);

    const uint64_t decode_start = now_ns();
    for (int row = 0; row < output_rows_; ++row) {
        const float* item = host_output_.data() + (static_cast<size_t>(row) * output_cols_);
        const float conf = item[4];
        if (conf < conf_threshold) {
            continue;
        }

        Detection detection;
        detection.x1 = item[0];
        detection.y1 = item[1];
        detection.x2 = item[2];
        detection.y2 = item[3];
        detection.conf = conf;
        detection.class_id = static_cast<int>(std::round(item[5]));
        batch.detections.push_back(detection);
    }
    const uint64_t decode_end = now_ns();
    batch.decode_ms = static_cast<float>(decode_end - decode_start) / 1'000'000.0f;
    batch.inferred_at_ns = decode_end;
    return batch;
}

} // namespace vision_native
