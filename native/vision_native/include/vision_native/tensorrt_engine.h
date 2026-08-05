#pragma once

#include "vision_native/types.h"

#include <cuda_runtime_api.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvinfer1 {
class ICudaEngine;
class IExecutionContext;
class IRuntime;
class ILogger;
}

namespace vision_native {

struct TensorRTEngineOptions {
    bool bind_tensor_addresses_once = true;
    bool use_high_priority_stream = true;
    bool use_cuda_graph = true;
};

class TensorRTEngine {
public:
    explicit TensorRTEngine(
        std::string engine_path,
        TensorRTEngineOptions options = {});
    ~TensorRTEngine();

    TensorRTEngine(const TensorRTEngine&) = delete;
    TensorRTEngine& operator=(const TensorRTEngine&) = delete;

    DetectionBatch infer_rgb(
        const uint8_t* frame_rgb,
        int width,
        int height,
        int row_pitch,
        float conf_threshold = 0.4f);

    DetectionBatch infer_bgra_array(
        cudaArray_t frame_bgra,
        int width,
        int height,
        float conf_threshold = 0.4f);
    DetectionBatch infer_bgra_array_roi(
        cudaArray_t frame_bgra,
        int array_width,
        int array_height,
        int roi_left,
        int roi_top,
        int roi_width,
        int roi_height,
        float conf_threshold = 0.4f);
    cudaStream_t cuda_stream() const noexcept;

    int input_width() const { return input_width_; }
    int input_height() const { return input_height_; }
    int output_rows() const { return output_rows_; }
    int output_cols() const { return output_cols_; }

private:
    void load_engine(const std::string& engine_path);
    void allocate_buffers();
    void allocate_timing_events();
    void bind_tensor_addresses();
    void initialize_cuda_graph();
    void enqueue_inference(cudaStream_t stream);
    void ensure_frame_buffer(size_t bytes);

    TensorRTEngineOptions options_;

    std::unique_ptr<nvinfer1::ILogger> logger_;
    std::unique_ptr<nvinfer1::IRuntime> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;

    std::string input_name_;
    std::string output_name_;
    int input_width_ = 0;
    int input_height_ = 0;
    int input_channels_ = 0;
    int output_rows_ = 0;
    int output_cols_ = 0;
    size_t input_element_count_ = 0;
    size_t output_element_count_ = 0;

    void* device_frame_ = nullptr;
    size_t device_frame_bytes_ = 0;
    float* device_input_ = nullptr;
    float* device_output_ = nullptr;
    float* host_output_ = nullptr;
    void* stream_ = nullptr;
    cudaGraph_t cuda_graph_ = nullptr;
    cudaGraphExec_t cuda_graph_exec_ = nullptr;
    cudaEvent_t preprocess_start_event_ = nullptr;
    cudaEvent_t preprocess_end_event_ = nullptr;
    cudaEvent_t infer_start_event_ = nullptr;
    cudaEvent_t infer_end_event_ = nullptr;
    cudaEvent_t output_copy_start_event_ = nullptr;
    cudaEvent_t output_copy_end_event_ = nullptr;
    uint64_t next_frame_id_ = 1;
};

} // namespace vision_native
