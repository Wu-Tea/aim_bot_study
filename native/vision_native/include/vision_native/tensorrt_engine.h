#pragma once

#include "vision_native/types.h"

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

class TensorRTEngine {
public:
    explicit TensorRTEngine(std::string engine_path);
    ~TensorRTEngine();

    TensorRTEngine(const TensorRTEngine&) = delete;
    TensorRTEngine& operator=(const TensorRTEngine&) = delete;

    DetectionBatch infer_rgb(
        const uint8_t* frame_rgb,
        int width,
        int height,
        int row_pitch,
        float conf_threshold = 0.4f);

    int input_width() const { return input_width_; }
    int input_height() const { return input_height_; }
    int output_rows() const { return output_rows_; }
    int output_cols() const { return output_cols_; }

private:
    void load_engine(const std::string& engine_path);
    void allocate_buffers();
    void ensure_frame_buffer(size_t bytes);

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
    std::vector<float> host_output_;
    void* stream_ = nullptr;
    uint64_t next_frame_id_ = 1;
};

} // namespace vision_native
