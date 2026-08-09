#pragma once

#include "pipeline_contract/target_snapshot.h"
#include "vision_native/types.h"

#include <cstdint>
#include <array>
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
    VisionResult select(
        const DetectionBatch& batch,
        const pipeline_contract::UserAimIntent& intent);
    VisionResult select_with_frame(const DetectionBatch& batch, const ColorFrameView& frame);
    VisionResult select_with_frame(
        const DetectionBatch& batch,
        const ColorFrameView& frame,
        const pipeline_contract::UserAimIntent& intent);
    bool wants_color_frame() const;
    std::optional<FrameRegion> required_color_region(const DetectionBatch& batch) const;

    struct Rect {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    struct Candidate {
        bool has_source_detection = false;
        std::uint32_t source_detection_index = 0;
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
        float live_score = 1.0f;
        float corpse_risk = 0.0f;
        float uncertainty = 0.0f;
        const char* source = "observed";
    };

    struct ScoredCandidate {
        Candidate candidate;
        float score = 0.0f;
        bool has_tracking_distance = false;
        float tracking_distance = 0.0f;
        bool intent_applied = false;
        const char* intent_decision = "none";
        float intent_score = 0.0f;
    };

    struct TargetState {
        Candidate candidate;
        float score = 0.0f;
        bool intent_applied = false;
        const char* intent_decision = "none";
        float intent_score = 0.0f;
    };

private:
    VisionResult empty_result(float boxes_seen) const;
    VisionResult result_from_target(const TargetState& target, float boxes_seen) const;
    VisionResult select_impl(
        const DetectionBatch& batch,
        const ColorFrameView* frame,
        const pipeline_contract::UserAimIntent* intent);

    Rect to_rect(const Detection& detection) const;
    std::pair<float, float> target_point(const Rect& box) const;
    Rect fallback_slow_zone(const Rect& box) const;
    Rect fire_zone(const Rect& box) const;
    DetectionBatch annotate_colors(const DetectionBatch& batch, const ColorFrameView& frame) const;
    void update_selected_motion_anchor(
        Detection& detection,
        const ColorFrameView& frame);
    void reset_motion_anchor();
    void clear_tracking_state();
    void clear_auto_fire_state();
    bool is_crosshair_inside_zone(const Rect& zone) const;
    bool update_auto_fire(const TargetState* target);

    bool passes_geometry_gate(float box_w, float box_h, bool tracking_candidate) const;
    bool passes_confidence_gate(float conf, bool tracking_candidate, bool enemy_colored) const;

    std::optional<Candidate> build_candidate(
        const Detection& detection,
        std::uint32_t source_detection_index,
        const std::optional<std::pair<float, float>>& last_target_center,
        const pipeline_contract::UserAimIntent* intent) const;
    std::optional<Candidate> build_weak_association_candidate(
        const Detection& detection,
        std::uint32_t source_detection_index) const;
    void build_candidates(
        const DetectionBatch& batch,
        const std::optional<std::pair<float, float>>& last_target_center,
        const pipeline_contract::UserAimIntent* intent);
    std::optional<TargetState> select_weak_association(const DetectionBatch& batch) const;

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
        const std::optional<std::pair<float, float>>& last_target_center,
        const pipeline_contract::UserAimIntent* intent) const;
    TargetState target_from_candidate(const Candidate& candidate, float score) const;
    TargetState target_from_scored_candidate(const ScoredCandidate& scored) const;

    bool boxes_match(const Rect& lhs, const Rect& rhs) const;
    bool targets_match(const TargetState& lhs, const TargetState& rhs) const;
    bool active_target_matches_candidate(const Candidate& candidate) const;
    bool candidate_matches_expired_marker_region(const Candidate& candidate) const;
    bool candidate_is_wide_low(const Candidate& candidate) const;
    bool candidate_has_enemy_evidence(const Candidate& candidate) const;
    bool should_escape_stale_active_match(
        const TargetState& locked,
        const TargetState& challenger) const;
    bool should_switch_targets(const TargetState& locked, const TargetState& challenger) const;

    std::optional<TargetState> confirm_pickup(
        const TargetState& target,
        bool allow_marked_single_frame_pickup);
    std::optional<TargetState> confirm_switch(const TargetState& target);
    void clear_pending();
    void clear_switch_pending();
    std::optional<TargetState> commit_target(
        const TargetState& target,
        bool clear_switch_pending,
        bool allow_marked_single_frame_pickup);

    std::optional<TargetState> select_single_candidate(const Candidate& candidate) const;
    std::pair<std::optional<TargetState>, std::optional<TargetState>> select_multi_candidate(
        const std::vector<Candidate>& candidates,
        const std::optional<std::pair<float, float>>& last_target_center,
        const pipeline_contract::UserAimIntent* intent) const;
    std::pair<std::optional<TargetState>, std::optional<TargetState>> select_candidate_targets(
        const std::vector<Candidate>& candidates,
        const std::optional<std::pair<float, float>>& last_target_center,
        const pipeline_contract::UserAimIntent* intent) const;

    std::pair<std::optional<TargetState>, bool> resolve_active_target_transition(
        const TargetState& chosen_target,
        const std::optional<TargetState>& active_match_target,
        const pipeline_contract::UserAimIntent* intent,
        bool single_credible_candidate);
    std::optional<TargetState> try_external_cue_hold(const DetectionBatch& batch);
    std::optional<TargetState> try_cue_hold(
        const ColorFrameView& frame,
        std::uint64_t observation_ns);
    bool cue_hold_is_active(std::uint64_t observation_ns) const;
    bool enemy_marker_loss_grace_expired(std::uint64_t observation_ns) const;
    std::optional<FrameRegion> cue_hold_search_region(
        std::uint64_t observation_ns) const;
    void update_cue_tracking(
        const TargetState& target,
        std::uint64_t observation_ns);
    void clear_cue_tracking();
    VisionResult finalize_selected_target(
        const TargetState& chosen_target,
        float boxes_seen,
        bool preserve_switch_pending,
        bool single_credible_candidate,
        std::uint64_t observation_ns);

    float frame_width_ = 0.0f;
    float frame_height_ = 0.0f;
    float screen_center_x_ = 0.0f;
    float screen_center_y_ = 0.0f;
    float tracking_radius_ = 0.0f;
    float pickup_confirm_radius_ = 0.0f;
    float switch_crosshair_margin_ = 0.0f;
    float crosshair_priority_margin_ = 0.0f;
    float max_area_limit_ = 0.0f;

    // Identity is owned by this selector's existing association and switch
    // confirmation rules. It is deliberately independent of frame-local
    // detection/observation ids.
    std::uint64_t selector_target_generation_ = 0;
    bool selector_target_changed_ = false;

    std::optional<std::pair<float, float>> last_target_center_;
    std::optional<TargetState> active_target_;
    std::optional<TargetState> pending_target_;
    std::optional<TargetState> pending_switch_target_;
    bool active_generation_had_enemy_evidence_ = false;
    bool active_marker_expired_ = false;
    std::optional<std::pair<float, float>> last_cue_point_;
    std::optional<std::pair<float, float>> last_target_offset_from_cue_;
    std::uint64_t last_direct_cue_observation_ns_ = 0;
    std::uint64_t last_cue_observation_ns_ = 0;
    int pending_frames_ = 0;
    int pending_switch_frames_ = 0;
    int cue_hold_frames_ = 0;
    bool auto_fire_holding_ = false;
    int auto_fire_miss_frames_ = 0;
    std::vector<Candidate> candidate_scratch_;
    static constexpr int kMotionTemplateWidth = 10;
    static constexpr int kMotionTemplateHeight = 10;
    static constexpr int kMotionTemplateSamples =
        kMotionTemplateWidth * kMotionTemplateHeight;
    std::array<float, kMotionTemplateSamples> motion_template_{};
    Rect motion_template_box_{};
    float motion_anchor_x_ = 0.0f;
    float motion_anchor_y_ = 0.0f;
    int motion_template_spacing_ = 1;
    int motion_anchor_misses_ = 0;
    bool has_motion_template_ = false;
};

} // namespace vision_native
