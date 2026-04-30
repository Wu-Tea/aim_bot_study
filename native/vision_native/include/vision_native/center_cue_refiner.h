#pragma once

#include "vision_native/types.h"

namespace vision_native {

struct CenterCueFrameView {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int row_pitch = 0;
    PixelFormat format = PixelFormat::RGB8;
};

struct CenterCueResult {
    bool yellow_cue_present = false;
    float yellow_cue_score = 0.0f;
    float yellow_cue_x = 0.0f;
    float yellow_cue_y = 0.0f;
    float yellow_mask_area = 0.0f;
    float yellow_roi_x1 = 0.0f;
    float yellow_roi_y1 = 0.0f;
    float yellow_roi_x2 = 0.0f;
    float yellow_roi_y2 = 0.0f;
    bool refiner_applied = false;
    float refined_target_x = 0.0f;
    float refined_target_y = 0.0f;
};

class CenterCueRefiner {
public:
    CenterCueRefiner(int width, int height);

    void reset();
    CenterCueResult detect(
        const CenterCueFrameView& frame,
        float screen_center_x,
        float screen_center_y);
    CenterCueResult refine_detected(
        const CenterCueResult& detected,
        float target_x,
        float target_y,
        float body_x1,
        float body_y1,
        float body_x2,
        float body_y2,
        float torso_x1,
        float torso_y1,
        float torso_x2,
        float torso_y2,
        const char* body_state_mode);
    CenterCueResult refine(
        const CenterCueFrameView& frame,
        float target_x,
        float target_y,
        float screen_center_x,
        float screen_center_y,
        float body_x1,
        float body_y1,
        float body_x2,
        float body_y2,
        float torso_x1,
        float torso_y1,
        float torso_x2,
        float torso_y2,
        const char* body_state_mode);

private:
    struct StableCueState {
        bool has_value = false;
        float x = 0.0f;
        float y = 0.0f;
        float score = 0.0f;
        int missing_frames = 0;
    };

    StableCueState stable_cue_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace vision_native
