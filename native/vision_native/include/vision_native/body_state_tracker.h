#pragma once

#include "vision_native/ego_motion.h"
#include "vision_native/gray_frame.h"
#include "vision_native/types.h"

#include <cstdint>
#include <vector>

namespace vision_native {

struct TargetKeyframe {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    float torso_x1 = 0.0f;
    float torso_y1 = 0.0f;
    float torso_x2 = 0.0f;
    float torso_y2 = 0.0f;
    float anchor_prior_x = 0.0f;
    float anchor_prior_y = 0.0f;
    std::vector<uint8_t> torso_patch;
    int patch_width = 0;
    int patch_height = 0;
    float score = 0.0f;
    const char* target_source = "observed";
};

struct BodyStateResult {
    bool has_target = false;
    bool has_body_box = false;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float anchor_confidence = 0.0f;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    float torso_x1 = 0.0f;
    float torso_y1 = 0.0f;
    float torso_x2 = 0.0f;
    float torso_y2 = 0.0f;
    const char* body_state_mode = "drop";
    const char* anchor_source = "none";
    float debug_search_x1 = 0.0f;
    float debug_search_y1 = 0.0f;
    float debug_search_x2 = 0.0f;
    float debug_search_y2 = 0.0f;
    float debug_predicted_x = 0.0f;
    float debug_predicted_y = 0.0f;
    float debug_patch_x = 0.0f;
    float debug_patch_y = 0.0f;
    bool debug_patch_valid = false;
    float debug_template_w = 0.0f;
    float debug_template_h = 0.0f;
    std::vector<float> debug_track_points;
};

class BodyStateTracker {
public:
    enum class Mode {
        Strong,
        Weak,
        Hold,
        Reacquire,
        Drop,
    };

    struct Point2f {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct RectF {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    BodyStateTracker(int width, int height);

    void reset();
    bool has_active_target() const;
    Mode mode() const;
    float anchor_confidence() const;

    void prime_from_keyframe(const TargetKeyframe& keyframe, const EgoFrameView& frame);
    void prime_from_keyframe_gray(const TargetKeyframe& keyframe, const GrayFrame& frame);
    BodyStateResult update_observed(
        const TargetKeyframe& keyframe,
        const EgoFrameView& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_observed_gray(
        const TargetKeyframe& keyframe,
        const GrayFrame& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_interframe(
        const EgoFrameView& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_interframe_gray(
        const GrayFrame& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_scan_miss(
        const EgoFrameView& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_scan_miss_gray(
        const GrayFrame& frame,
        const EgoWarp& ego_warp);

    BodyStateResult update_selected(
        const VisionResult& selected_target,
        const EgoFrameView& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_selected_gray(
        const VisionResult& selected_target,
        const GrayFrame& frame,
        const EgoWarp& ego_warp);

    BodyStateResult update_missing(
        const EgoFrameView& frame,
        const EgoWarp& ego_warp);
    BodyStateResult update_missing_gray(
        const GrayFrame& frame,
        const EgoWarp& ego_warp);

private:
    int width_ = 0;
    int height_ = 0;
    bool active_ = false;
    std::vector<uint8_t> previous_gray_;
    int previous_gray_width_ = 0;
    int previous_gray_height_ = 0;
    RectF body_box_{};
    RectF torso_box_{};
    Point2f anchor_{};
    Point2f residual_velocity_{};
    std::vector<Point2f> previous_points_;
    std::vector<Point2f> point_offsets_;
    std::vector<uint8_t> template_patch_;
    int template_patch_width_ = 0;
    int template_patch_height_ = 0;
    int missing_frames_ = 0;
    float anchor_confidence_ = 0.0f;
    Mode mode_ = Mode::Drop;
    RectF debug_search_roi_{};
    Point2f debug_predicted_anchor_{};
    Point2f debug_patch_anchor_{};
    bool debug_patch_valid_ = false;
    std::vector<Point2f> debug_track_points_;

    BodyStateResult build_result(const char* anchor_source) const;
    BodyStateResult update_unobserved(
        const EgoFrameView& frame,
        const EgoWarp& ego_warp,
        bool consume_scan_miss_budget);
    BodyStateResult update_unobserved_gray(
        const GrayFrame& frame,
        const EgoWarp& ego_warp,
        bool consume_scan_miss_budget);
};

} // namespace vision_native
