#include "vision_native/tensorrt_engine.h"

#include "vision_native/engine_io.h"
#include "vision_native/preprocess.h"

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime.h>
#include <windows.h>

#include <algorithm>
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

uint64_t now_qpc() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) != 0 && value.QuadPart > 0
        ? static_cast<uint64_t>(value.QuadPart)
        : 0;
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

TensorRTEngine::TensorRTEngine(
    std::string engine_path,
    TensorRTEngineOptions options)
    : options_(options) {
    load_engine(engine_path);
    allocate_buffers();
}

TensorRTEngine::~TensorRTEngine() {
    if (cuda_graph_exec_ != nullptr) {
        cudaGraphExecDestroy(cuda_graph_exec_);
    }
    if (cuda_graph_ != nullptr) {
        cudaGraphDestroy(cuda_graph_);
    }
    if (output_copy_end_event_ != nullptr) {
        cudaEventDestroy(output_copy_end_event_);
    }
    if (output_copy_start_event_ != nullptr) {
        cudaEventDestroy(output_copy_start_event_);
    }
    if (infer_end_event_ != nullptr) {
        cudaEventDestroy(infer_end_event_);
    }
    if (infer_start_event_ != nullptr) {
        cudaEventDestroy(infer_start_event_);
    }
    if (preprocess_end_event_ != nullptr) {
        cudaEventDestroy(preprocess_end_event_);
    }
    if (preprocess_start_event_ != nullptr) {
        cudaEventDestroy(preprocess_start_event_);
    }
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
    if (host_output_ != nullptr) {
        cudaFreeHost(host_output_);
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
    if (options_.use_high_priority_stream) {
        int least_priority = 0;
        int greatest_priority = 0;
        check_cuda(
            cudaDeviceGetStreamPriorityRange(&least_priority, &greatest_priority),
            "cudaDeviceGetStreamPriorityRange");
        check_cuda(
            cudaStreamCreateWithPriority(
                &stream,
                cudaStreamNonBlocking,
                greatest_priority),
            "cudaStreamCreateWithPriority");
    } else {
        check_cuda(cudaStreamCreate(&stream), "cudaStreamCreate");
    }
    stream_ = stream;
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_input_), input_element_count_ * sizeof(float)), "cudaMalloc input");
    check_cuda(cudaMalloc(reinterpret_cast<void**>(&device_output_), output_element_count_ * sizeof(float)), "cudaMalloc output");
    check_cuda(cudaMallocHost(reinterpret_cast<void**>(&host_output_), output_element_count_ * sizeof(float)), "cudaMallocHost output");
    if (options_.bind_tensor_addresses_once) {
        bind_tensor_addresses();
    }
    allocate_timing_events();
    if (options_.use_cuda_graph) {
        initialize_cuda_graph();
    }
}

void TensorRTEngine::bind_tensor_addresses() {
    if (!context_->setTensorAddress(input_name_.c_str(), device_input_)) {
        throw std::runtime_error("failed to set TensorRT input address");
    }
    if (!context_->setTensorAddress(output_name_.c_str(), device_output_)) {
        throw std::runtime_error("failed to set TensorRT output address");
    }
}

void TensorRTEngine::initialize_cuda_graph() {
    if (!options_.bind_tensor_addresses_once) {
        throw std::runtime_error("CUDA Graph requires tensor addresses to be bound once");
    }

    cudaStream_t stream = static_cast<cudaStream_t>(stream_);
    check_cuda(
        cudaMemsetAsync(device_input_, 0, input_element_count_ * sizeof(float), stream),
        "cudaMemsetAsync graph warmup input");
    if (!context_->enqueueV3(stream)) {
        throw std::runtime_error("TensorRT enqueueV3 graph warmup failed");
    }
    check_cuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize graph warmup");

    check_cuda(
        cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal),
        "cudaStreamBeginCapture TensorRT");
    if (!context_->enqueueV3(stream)) {
        cudaGraph_t abandoned_graph = nullptr;
        (void)cudaStreamEndCapture(stream, &abandoned_graph);
        if (abandoned_graph != nullptr) {
            cudaGraphDestroy(abandoned_graph);
        }
        throw std::runtime_error("TensorRT enqueueV3 CUDA Graph capture failed");
    }
    cudaGraph_t captured_graph = nullptr;
    check_cuda(
        cudaStreamEndCapture(stream, &captured_graph),
        "cudaStreamEndCapture TensorRT");
    if (captured_graph == nullptr) {
        throw std::runtime_error("TensorRT CUDA Graph capture returned an empty graph");
    }
    cudaGraphExec_t captured_graph_exec = nullptr;
    const cudaError_t instantiate_status =
        cudaGraphInstantiate(&captured_graph_exec, captured_graph, 0);
    if (instantiate_status != cudaSuccess) {
        cudaGraphDestroy(captured_graph);
        check_cuda(instantiate_status, "cudaGraphInstantiate TensorRT");
    }
    cuda_graph_ = captured_graph;
    cuda_graph_exec_ = captured_graph_exec;
}

void TensorRTEngine::enqueue_inference(cudaStream_t stream) {
    if (cuda_graph_exec_ != nullptr) {
        check_cuda(cudaGraphLaunch(cuda_graph_exec_, stream), "cudaGraphLaunch TensorRT");
        return;
    }
    if (!context_->enqueueV3(stream)) {
        throw std::runtime_error("TensorRT enqueueV3 failed");
    }
}

void TensorRTEngine::allocate_timing_events() {
    check_cuda(cudaEventCreate(&preprocess_start_event_), "cudaEventCreate preprocess_start");
    check_cuda(cudaEventCreate(&preprocess_end_event_), "cudaEventCreate preprocess_end");
    check_cuda(cudaEventCreate(&infer_start_event_), "cudaEventCreate infer_start");
    check_cuda(cudaEventCreate(&infer_end_event_), "cudaEventCreate infer_end");
    check_cuda(cudaEventCreate(&output_copy_start_event_), "cudaEventCreate output_copy_start");
    check_cuda(cudaEventCreate(&output_copy_end_event_), "cudaEventCreate output_copy_end");
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
    if (width <= 0 || height <= 0) {
        std::ostringstream out;
        out << "frame shape must be positive, got " << height << "x" << width;
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
    batch.preprocess_mode = PreprocessMode::RgbHostCopy;

    const size_t frame_bytes = static_cast<size_t>(row_pitch) * static_cast<size_t>(height);
    ensure_frame_buffer(frame_bytes);

    cudaStream_t stream = static_cast<cudaStream_t>(stream_);
    check_cuda(cudaEventRecord(preprocess_start_event_, stream), "cudaEventRecord preprocess_start");
    check_cuda(cudaMemcpyAsync(device_frame_, frame_rgb, frame_bytes, cudaMemcpyHostToDevice, stream), "cudaMemcpyAsync frame");
    launch_rgb_hwc_to_chw_float(
        static_cast<const uint8_t*>(device_frame_),
        width,
        height,
        row_pitch,
        input_width_,
        input_height_,
        device_input_,
        stream);
    check_cuda(cudaGetLastError(), "launch_rgb_hwc_to_chw_float");
    check_cuda(cudaEventRecord(preprocess_end_event_, stream), "cudaEventRecord preprocess_end");

    if (!options_.bind_tensor_addresses_once) {
        bind_tensor_addresses();
    }

    check_cuda(cudaEventRecord(infer_start_event_, stream), "cudaEventRecord infer_start");
    const uint64_t enqueue_start = now_ns();
    enqueue_inference(stream);
    const uint64_t enqueue_end = now_ns();
    batch.enqueue_cpu_ms = static_cast<float>(enqueue_end - enqueue_start) / 1'000'000.0f;
    check_cuda(cudaEventRecord(infer_end_event_, stream), "cudaEventRecord infer_end");
    const uint64_t output_copy_start = now_ns();
    check_cuda(cudaEventRecord(output_copy_start_event_, stream), "cudaEventRecord output_copy_start");
    check_cuda(
        cudaMemcpyAsync(host_output_, device_output_, output_element_count_ * sizeof(float), cudaMemcpyDeviceToHost, stream),
        "cudaMemcpyAsync output");
    check_cuda(cudaEventRecord(output_copy_end_event_, stream), "cudaEventRecord output_copy_end");
    check_cuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize");
    const uint64_t output_copy_end = now_ns();
    batch.gpu_complete_at_ns = output_copy_end;
    batch.gpu_complete_qpc = now_qpc();
    batch.output_copy_sync_ms = static_cast<float>(output_copy_end - output_copy_start) / 1'000'000.0f;

    check_cuda(cudaEventElapsedTime(&batch.preprocess_ms, preprocess_start_event_, preprocess_end_event_), "cudaEventElapsedTime preprocess");
    check_cuda(cudaEventElapsedTime(&batch.infer_ms, infer_start_event_, infer_end_event_), "cudaEventElapsedTime infer");
    check_cuda(cudaEventElapsedTime(&batch.gpu_total_ms, preprocess_start_event_, infer_end_event_), "cudaEventElapsedTime gpu_total");
    check_cuda(cudaEventElapsedTime(&batch.output_copy_ms, output_copy_start_event_, output_copy_end_event_), "cudaEventElapsedTime output_copy");
    batch.output_wait_ms = std::max(0.0f, batch.output_copy_sync_ms - batch.output_copy_ms);

    const uint64_t decode_start = now_ns();
    batch.detections.reserve(static_cast<size_t>(output_rows_));
    const float scale_x = static_cast<float>(width) / static_cast<float>(input_width_);
    const float scale_y = static_cast<float>(height) / static_cast<float>(input_height_);
    for (int row = 0; row < output_rows_; ++row) {
        const float* item = host_output_ + (static_cast<size_t>(row) * output_cols_);
        const float conf = item[4];
        if (conf < conf_threshold) {
            continue;
        }

        Detection detection;
        detection.x1 = item[0] * scale_x;
        detection.y1 = item[1] * scale_y;
        detection.x2 = item[2] * scale_x;
        detection.y2 = item[3] * scale_y;
        detection.conf = conf;
        detection.class_id = static_cast<int>(std::round(item[5]));
        batch.detections.push_back(detection);
    }
    const uint64_t decode_end = now_ns();
    batch.decode_ms = static_cast<float>(decode_end - decode_start) / 1'000'000.0f;
    batch.inferred_at_ns = decode_end;
    return batch;
}

DetectionBatch TensorRTEngine::infer_bgra_array(
    cudaArray_t frame_bgra,
    int width,
    int height,
    float conf_threshold) {
    return infer_bgra_array_roi(
        frame_bgra,
        width,
        height,
        0,
        0,
        width,
        height,
        conf_threshold);
}

DetectionBatch TensorRTEngine::infer_bgra_array_roi(
    cudaArray_t frame_bgra,
    int array_width,
    int array_height,
    int roi_left,
    int roi_top,
    int width,
    int height,
    float conf_threshold) {
    if (frame_bgra == nullptr) {
        throw std::runtime_error("frame_bgra must not be null");
    }
    if (array_width <= 0 || array_height <= 0 || width <= 0 || height <= 0) {
        std::ostringstream out;
        out << "frame and ROI shapes must be positive, got frame "
            << array_height << "x" << array_width << " ROI "
            << height << "x" << width;
        throw std::runtime_error(out.str());
    }
    if (roi_left < 0 || roi_top < 0 ||
        roi_left + width > array_width || roi_top + height > array_height) {
        std::ostringstream out;
        out << "BGRA ROI [" << roi_left << ',' << roi_top << ',' << width << 'x'
            << height << "] exceeds frame " << array_width << 'x' << array_height;
        throw std::runtime_error(out.str());
    }

    DetectionBatch batch;
    batch.frame_id = next_frame_id_++;
    batch.captured_at_ns = now_ns();
    batch.frame_width = width;
    batch.frame_height = height;
    batch.preprocess_mode = PreprocessMode::OldBgraCopy;

    const int row_pitch = width * 4;
    const size_t frame_bytes = static_cast<size_t>(row_pitch) * static_cast<size_t>(height);
    ensure_frame_buffer(frame_bytes);

    cudaStream_t stream = static_cast<cudaStream_t>(stream_);
    check_cuda(cudaEventRecord(preprocess_start_event_, stream), "cudaEventRecord preprocess_start");
    check_cuda(
        cudaMemcpy2DFromArrayAsync(
            device_frame_,
            static_cast<size_t>(row_pitch),
            frame_bgra,
            static_cast<size_t>(roi_left) * 4,
            static_cast<size_t>(roi_top),
            static_cast<size_t>(row_pitch),
            static_cast<size_t>(height),
            cudaMemcpyDeviceToDevice,
            stream),
        "cudaMemcpy2DFromArrayAsync frame");
    launch_bgra_hwc_to_chw_float(
        static_cast<const uint8_t*>(device_frame_),
        width,
        height,
        row_pitch,
        input_width_,
        input_height_,
        device_input_,
        stream);
    check_cuda(cudaGetLastError(), "launch_bgra_hwc_to_chw_float");
    check_cuda(cudaEventRecord(preprocess_end_event_, stream), "cudaEventRecord preprocess_end");

    if (!options_.bind_tensor_addresses_once) {
        bind_tensor_addresses();
    }

    check_cuda(cudaEventRecord(infer_start_event_, stream), "cudaEventRecord infer_start");
    const uint64_t enqueue_start = now_ns();
    enqueue_inference(stream);
    const uint64_t enqueue_end = now_ns();
    batch.enqueue_cpu_ms = static_cast<float>(enqueue_end - enqueue_start) / 1'000'000.0f;
    check_cuda(cudaEventRecord(infer_end_event_, stream), "cudaEventRecord infer_end");
    const uint64_t output_copy_start = now_ns();
    check_cuda(cudaEventRecord(output_copy_start_event_, stream), "cudaEventRecord output_copy_start");
    check_cuda(
        cudaMemcpyAsync(host_output_, device_output_, output_element_count_ * sizeof(float), cudaMemcpyDeviceToHost, stream),
        "cudaMemcpyAsync output");
    check_cuda(cudaEventRecord(output_copy_end_event_, stream), "cudaEventRecord output_copy_end");
    check_cuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize");
    const uint64_t output_copy_end = now_ns();
    batch.gpu_complete_at_ns = output_copy_end;
    batch.gpu_complete_qpc = now_qpc();
    batch.output_copy_sync_ms = static_cast<float>(output_copy_end - output_copy_start) / 1'000'000.0f;

    check_cuda(cudaEventElapsedTime(&batch.preprocess_ms, preprocess_start_event_, preprocess_end_event_), "cudaEventElapsedTime preprocess");
    check_cuda(cudaEventElapsedTime(&batch.infer_ms, infer_start_event_, infer_end_event_), "cudaEventElapsedTime infer");
    check_cuda(cudaEventElapsedTime(&batch.gpu_total_ms, preprocess_start_event_, infer_end_event_), "cudaEventElapsedTime gpu_total");
    check_cuda(cudaEventElapsedTime(&batch.output_copy_ms, output_copy_start_event_, output_copy_end_event_), "cudaEventElapsedTime output_copy");
    batch.output_wait_ms = std::max(0.0f, batch.output_copy_sync_ms - batch.output_copy_ms);

    const uint64_t decode_start = now_ns();
    batch.detections.reserve(static_cast<size_t>(output_rows_));
    const float scale_x = static_cast<float>(width) / static_cast<float>(input_width_);
    const float scale_y = static_cast<float>(height) / static_cast<float>(input_height_);
    for (int row = 0; row < output_rows_; ++row) {
        const float* item = host_output_ + (static_cast<size_t>(row) * output_cols_);
        const float conf = item[4];
        if (conf < conf_threshold) {
            continue;
        }

        Detection detection;
        detection.x1 = item[0] * scale_x;
        detection.y1 = item[1] * scale_y;
        detection.x2 = item[2] * scale_x;
        detection.y2 = item[3] * scale_y;
        detection.conf = conf;
        detection.class_id = static_cast<int>(std::round(item[5]));
        batch.detections.push_back(detection);
    }
    const uint64_t decode_end = now_ns();
    batch.decode_ms = static_cast<float>(decode_end - decode_start) / 1'000'000.0f;
    batch.inferred_at_ns = decode_end;
    return batch;
}

cudaStream_t TensorRTEngine::cuda_stream() const noexcept {
    return static_cast<cudaStream_t>(stream_);
}

} // namespace vision_native
