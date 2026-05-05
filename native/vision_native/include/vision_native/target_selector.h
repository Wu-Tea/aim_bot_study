#pragma once

#include "vision_native/types.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace vision_native {

class VisionTargetSelector {
public:
    struct FrameRegion {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    struct ColorFrameView {
        const uint8_t* data = nullptr;
        int width = 0;
        int height = 0;
        int row_pitch = 0;
        int origin_x = 0;
        int origin_y = 0;
        int frame_width = 0;
        int frame_height = 0;
        PixelFormat format = PixelFormat::RGB8;
    };

    VisionTargetSelector(int frame_width, int frame_height);

    void reset();
    VisionResult select(const DetectionBatch& batch);
    VisionResult select_with_frame(const DetectionBatch& batch, const ColorFrameView& frame);
    bool wants_color_frame() const;
    std::optional<FrameRegion> required_color_region(const DetectionBatch& batch) const;

    struct Rect {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    struct Candidate {
        float target_x = 0.0f;
        float target_y = 0.0f;
        float conf = 0.0f;
        float color_bonus = 0.0f;
        bool has_cue = false;
        float cue_x = 0.0f;
        float cue_y = 0.0f;
        float cue_score = 0.0f;
        Rect body_box;
        Rect slow_zone;
        Rect fire_zone;
        const char* source = "observed";
    };

    struct ScoredCandidate {
        Candidate candidate;
        float score = 0.0f;
        bool has_tracking_distance = false;
        float tracking_distance = 0.0f;
    };

    struct TargetState {
        Candidate candidate;
        float score = 0.0f;
    };

private:
    VisionResult empty_result(float boxes_seen) const;
    VisionResult result_from_target(const TargetState& target, float boxes_seen) const;
    VisionResult select_impl(const DetectionBatch& batch, const ColorFrameView* frame);

    Rect to_rect(const Detection& detection) const;
    std::pair<float, float> target_point(const Rect& box) const;
    Rect fallback_slow_zone(const Rect& box) const;
    Rect fire_zone(const Rect& box) const;
    DetectionBatch annotate_colors(const DetectionBatch& batch, const ColorFrameView& frame) const;
    void clear_tracking_state();
    void clear_auto_fire_state();
    bool is_crosshair_inside_zone(const Rect& zone) const;
    bool update_auto_fire(const TargetState* target);

    bool passes_geometry_gate(float box_w, float box_h, bool tracking_candidate) const;
    bool passes_confidence_gate(float conf, bool tracking_candidate, bool enemy_colored) const;

    std::optional<Candidate> build_candidate(
        const Detection& detection,
        const std::optional<std::pair<float, float>>& last_target_center) const;
    std::vector<Candidate> build_candidates(
        const DetectionBatch& batch,
        const std::optional<std::pair<float, float>>& last_target_center) const;

    float crosshair_distance(float x, float y) const;
    std::optional<float> tracking_distance(
        float x,
        float y,
        const std::optional<std::pair<float, float>>& last_target_center) const;
    float tracking_bonus_for_distance(const std::optional<float>& tracking_distance) const;
    bool prefer_candidate(
        const std::optional<ScoredCandidate>& current,
        const ScoredCandidate& challenger) const;
    ScoredCandidate score_candidate(
        const Candidate& candidate,
        const std::optional<std::pair<float, float>>& last_target_center) const;
    TargetState target_from_candidate(const Candidate& candidate, float score) const;

    bool boxes_match(const Rect& lhs, const Rect& rhs) const;
    bool targets_match(const TargetState& lhs, const TargetState& rhs) const;
    bool active_target_matches_candidate(const Candidate& candidate) const;
    bool should_switch_targets(const TargetState& locked, const TargetState& challenger) const;

    std::optional<TargetState> confirm_pickup(const TargetState& target);
    std::optional<TargetState> confirm_switch(const TargetState& target);
    void clear_pending();
    void clear_switch_pending();
    std::optional<TargetState> commit_target(const TargetState& target, bool clear_switch_pending);

    std::optional<TargetState> select_single_candidate(const Candidate& candidate) const;
    std::pair<std::optional<TargetState>, std::optional<TargetState>> select_multi_candidate(
        const std::vector<Candidate>& candidates,
        const std::optional<std::pair<float, float>>& last_target_center) const;
    std::pair<std::optional<TargetState>, std::optional<TargetState>> select_candidate_targets(
        const std::vector<Candidate>& candidates,
        const std::optional<std::pair<float, float>>& last_target_center) const;

    std::pair<std::optional<TargetState>, bool> resolve_active_target_transition(
        const TargetState& chosen_target,
        const std::optional<TargetState>& active_match_target);

    bool fails_tracking_jump(const std::pair<float, float>& point) const;
    bool fails_first_pickup_flick(const std::pair<float, float>& point) const;
    std::pair<float, float> smooth_target_point(const std::pair<float, float>& point) const;
    std::optional<TargetState> try_external_cue_hold(const DetectionBatch& batch);
    std::optional<TargetState> try_cue_hold(const ColorFrameView& frame);
    std::optional<FrameRegion> cue_hold_search_region() const;
    void update_cue_tracking(const TargetState& target);
    void clear_cue_tracking();
    VisionResult hold_or_reset(float boxes_seen);
    VisionResult finalize_selected_target(
        const TargetState& chosen_target,
        const std::optional<std::pair<float, float>>& last_target_center,
        float boxes_seen,
        bool preserve_switch_pending);

    float frame_width_ = 0.0f;
    float frame_height_ = 0.0f;
    float screen_center_x_ = 0.0f;
    float screen_center_y_ = 0.0f;
    float max_jump_x_ = 0.0f;
    float max_jump_y_ = 0.0f;
    float tracking_radius_ = 0.0f;
    float max_smoothing_jump_ = 0.0f;
    float pickup_confirm_radius_ = 0.0f;
    float switch_crosshair_margin_ = 0.0f;
    float crosshair_priority_margin_ = 0.0f;
    float ideal_area_ = 0.0f;
    float max_area_limit_ = 0.0f;

    std::optional<std::pair<float, float>> last_target_center_;
    std::optional<TargetState> active_target_;
    std::optional<TargetState> pending_target_;
    std::optional<TargetState> pending_switch_target_;
    std::optional<std::pair<float, float>> last_cue_point_;
    std::optional<std::pair<float, float>> last_target_offset_from_cue_;
    int pending_frames_ = 0;
    int pending_switch_frames_ = 0;
    int hold_frames_ = 0;
    int cue_hold_frames_ = 0;
    bool auto_fire_holding_ = false;
    int auto_fire_miss_frames_ = 0;
};

} // namespace vision_native
