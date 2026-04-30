#pragma once

#include "vision_native/gray_frame.h"
#include "vision_native/types.h"

#include <cstdint>
#include <vector>

namespace vision_native {

struct EgoFrameView {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int row_pitch = 0;
    PixelFormat format = PixelFormat::RGB8;
};

struct EgoWarp {
    float a00 = 1.0f;
    float a01 = 0.0f;
    float a10 = 0.0f;
    float a11 = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float confidence = 0.0f;
    int valid_points = 0;
    int inlier_points = 0;
    const char* model = "identity";
};

class EgoMotionEstimator {
public:
    EgoMotionEstimator(int width, int height);

    void reset();
    EgoWarp estimate(const EgoFrameView& frame, const DetectionBatch& batch);
    EgoWarp estimate_gray(const GrayFrame& frame, const DetectionBatch& batch);

private:
    int width_ = 0;
    int height_ = 0;
    bool has_previous_ = false;
    std::vector<uint8_t> previous_gray_;
    int previous_gray_width_ = 0;
    int previous_gray_height_ = 0;
};

} // namespace vision_native
