#include "vision_native/target_selector.h"

#include "pipeline_contract/target_snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void require_text(const char* actual, const char* expected, const char* message) {
    if (actual == nullptr || std::strcmp(actual, expected) != 0) {
        throw std::runtime_error(message);
    }
}

vision_native::Detection detection_for_target(
    float target_x,
    float target_y,
    float conf) {
    constexpr float width = 60.0f;
    constexpr float height = 140.0f;
    vision_native::Detection detection;
    detection.x1 = target_x - (width * 0.5f);
    detection.x2 = target_x + (width * 0.5f);
    detection.y1 = target_y - (height * 0.40f);
    detection.y2 = detection.y1 + height;
    detection.conf = conf;
    return detection;
}

vision_native::Detection sized_detection_for_target(
    float target_x,
    float target_y,
    float width,
    float height,
    float conf) {
    vision_native::Detection detection;
    detection.x1 = target_x - (width * 0.5f);
    detection.x2 = target_x + (width * 0.5f);
    detection.y1 = target_y - (height * 0.40f);
    detection.y2 = detection.y1 + height;
    detection.conf = conf;
    return detection;
}

vision_native::DetectionBatch two_target_batch() {
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(detection_for_target(260.0f, 256.0f, 0.92f));
    batch.detections.push_back(detection_for_target(380.0f, 256.0f, 0.92f));
    return batch;
}

pipeline_contract::UserAimIntent rightward_intent(std::uint64_t intent_id) {
    pipeline_contract::UserAimIntent intent;
    intent.valid = true;
    intent.intent_id = intent_id;
    intent.strength = 1.0f;
    intent.has_direction = true;
    intent.direction.x = 1.0f;
    intent.direction.y = 0.0f;
    intent.aiming = true;
    return intent;
}

pipeline_contract::UserAimIntent lower_left_intent(std::uint64_t intent_id) {
    pipeline_contract::UserAimIntent intent;
    intent.valid = true;
    intent.intent_id = intent_id;
    intent.strength = 1.0f;
    intent.has_direction = true;
    intent.direction.x = -0.75f;
    intent.direction.y = 0.65f;
    intent.aiming = true;
    return intent;
}

vision_native::DetectionBatch single_target_batch(float target_x, float target_y, float conf) {
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(detection_for_target(target_x, target_y, conf));
    return batch;
}

vision_native::Detection wide_low_detection_for_target(
    float target_x,
    float target_y,
    float conf) {
    constexpr float width = 120.0f;
    constexpr float height = 60.0f;
    vision_native::Detection detection;
    detection.x1 = target_x - (width * 0.5f);
    detection.x2 = target_x + (width * 0.5f);
    detection.y1 = target_y - (height * 0.50f);
    detection.y2 = detection.y1 + height;
    detection.conf = conf;
    return detection;
}

int region_area(const vision_native::VisionTargetSelector::FrameRegion& region) {
    return std::max(0, region.right - region.left) *
        std::max(0, region.bottom - region.top);
}

struct ColorFrameFixture {
    std::vector<std::uint8_t> pixels;
    vision_native::VisionTargetSelector::ColorFrameView view;
};

ColorFrameFixture color_frame_for_region(
    const vision_native::VisionTargetSelector::FrameRegion& region,
    bool enemy_colored) {
    ColorFrameFixture frame;
    frame.view.width = std::max(0, region.right - region.left);
    frame.view.height = std::max(0, region.bottom - region.top);
    frame.view.row_pitch = frame.view.width * 3;
    frame.view.origin_x = region.left;
    frame.view.origin_y = region.top;
    frame.view.frame_width = 640;
    frame.view.frame_height = 512;
    frame.view.format = vision_native::PixelFormat::RGB8;
    frame.pixels.assign(
        static_cast<std::size_t>(std::max(0, frame.view.row_pitch * frame.view.height)),
        0);
    for (int y = 0; y < frame.view.height; ++y) {
        for (int x = 0; x < frame.view.width; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y * frame.view.row_pitch + x * 3);
            if (enemy_colored && ((x + (y * 2)) % 5 == 0)) {
                frame.pixels[offset + 0] = 255;
                frame.pixels[offset + 1] = 0;
                frame.pixels[offset + 2] = 0;
            } else {
                frame.pixels[offset + 0] = 12;
                frame.pixels[offset + 1] = 12;
                frame.pixels[offset + 2] = 12;
            }
        }
    }
    frame.view.data = frame.pixels.data();
    return frame;
}

ColorFrameFixture full_bgra_frame() {
    ColorFrameFixture frame;
    frame.view.width = 640;
    frame.view.height = 512;
    frame.view.row_pitch = frame.view.width * 4;
    frame.view.frame_width = frame.view.width;
    frame.view.frame_height = frame.view.height;
    frame.view.format = vision_native::PixelFormat::BGRA8;
    frame.pixels.assign(
        static_cast<std::size_t>(frame.view.row_pitch * frame.view.height),
        12);
    for (std::size_t offset = 3; offset < frame.pixels.size(); offset += 4) {
        frame.pixels[offset] = 255;
    }
    frame.view.data = frame.pixels.data();
    return frame;
}

void paint_sparse_bgra(
    ColorFrameFixture& frame,
    const vision_native::VisionTargetSelector::FrameRegion& region,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue) {
    for (int y = region.top; y < region.bottom; ++y) {
        for (int x = region.left; x < region.right; ++x) {
            if ((x + y * 2) % 5 != 0) continue;
            const auto offset = static_cast<std::size_t>(
                y * frame.view.row_pitch + x * 4);
            frame.pixels[offset + 0] = blue;
            frame.pixels[offset + 1] = green;
            frame.pixels[offset + 2] = red;
            frame.pixels[offset + 3] = 255;
        }
    }
}

vision_native::VisionTargetSelector::FrameRegion color_region_for(
    const vision_native::Detection& detection) {
    vision_native::VisionTargetSelector probe(640, 512);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(detection);
    const auto region = probe.required_color_region(batch);
    require_true(region.has_value(), "fixture detection must request a color ROI");
    return *region;
}

void test_intent_direction_ranks_plausible_multi_target_candidates() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto batch = two_target_batch();
    const auto intent = rightward_intent(7);

    selector.select(batch, intent);
    const vision_native::VisionResult result = selector.select(batch, intent);

    require_true(result.has_target, "intent-ranked second pickup should acquire a target");
    require_true(
        result.has_selected_detection && result.selected_detection_index == 1,
        "selector result must name the exact detection that owns the target");
    require_near(
        result.target_x,
        380.0f,
        0.001f,
        "rightward intent should select the right candidate when base scores tie");
    require_true(result.intent_applied, "intent-ranked result should report intent_applied");
    require_true(result.intent_id == 7, "intent-ranked result should carry intent_id");
    require_text(
        result.intent_decision,
        "applied_direction",
        "intent-ranked result should report direction application reason");
    require_true(result.fire_authority, "intent must not strip observed fire authority");
}

void test_user_intent_prefers_lower_left_close_target_over_far_upper_right() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(detection_for_target(250.0f, 310.0f, 0.58f));
    batch.detections.push_back(detection_for_target(390.0f, 210.0f, 0.86f));
    const auto intent = lower_left_intent(23);

    selector.select(batch, intent);
    const vision_native::VisionResult result = selector.select(batch, intent);

    require_true(result.has_target, "lower-left intent scenario should select a target");
    require_true(result.target_x < 320.0f, "lower-left intent should not select upper-right target");
    require_true(result.target_y > 256.0f, "lower-left intent should keep selection below center");
    require_true(result.intent_applied, "lower-left intent should be applied");
    require_true(result.intent_id == 23, "lower-left intent result should carry intent id");
}

void test_crosshair_near_target_beats_physically_near_large_target() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(sized_detection_for_target(
        320.0f, 256.0f, 24.0f, 60.0f, 0.92f));
    batch.detections.push_back(sized_detection_for_target(
        350.0f, 256.0f, 80.0f, 200.0f, 0.92f));

    selector.select(batch);
    const vision_native::VisionResult result = selector.select(batch);

    require_true(result.has_target, "size-weight scenario should acquire a target");
    require_true(
        result.has_selected_detection && result.selected_detection_index == 0,
        "crosshair distance must outrank apparent physical target size");
}

void test_short_occlusion_does_not_switch_locked_near_target_to_visible_far_target() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch visible;
    visible.frame_width = 640;
    visible.frame_height = 512;
    visible.detections.push_back(sized_detection_for_target(
        270.0f, 256.0f, 24.0f, 60.0f, 0.92f));
    visible.detections.push_back(sized_detection_for_target(
        350.0f, 256.0f, 80.0f, 200.0f, 0.92f));

    selector.select(visible);
    const vision_native::VisionResult locked = selector.select(visible);
    require_true(
        locked.has_selected_detection && locked.selected_detection_index == 1,
        "occlusion setup must lock the near large target");

    vision_native::DetectionBatch occluded;
    occluded.frame_width = 640;
    occluded.frame_height = 512;
    occluded.detections.push_back(sized_detection_for_target(
        270.0f, 256.0f, 24.0f, 60.0f, 0.92f));
    for (int tick = 0; tick < 6; ++tick) {
        const vision_native::VisionResult held = selector.select(occluded);
        require_true(held.has_target,
                     "short occlusion window must keep a predicted target");
        require_near(held.target_x, 350.0f, 0.001f,
                     "short occlusion must not redirect aim to the visible far target");
        require_true(!held.has_selected_detection,
                     "occlusion hold must not claim backing from the far detection");
    }

    selector.select(occluded);
    const vision_native::VisionResult released = selector.select(occluded);
    require_true(
        released.has_selected_detection && released.selected_detection_index == 0,
        "persistent occlusion must eventually release to the visible target");
}

void test_intent_favored_challenger_logs_ignored_active_lock() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto active_batch = single_target_batch(296.0f, 256.0f, 0.92f);

    selector.select(active_batch);
    const vision_native::VisionResult locked = selector.select(active_batch);
    require_true(locked.has_target, "setup should acquire active target");
    require_near(locked.target_x, 296.0f, 0.001f, "setup should lock left target");

    vision_native::DetectionBatch crossing;
    crossing.frame_width = 640;
    crossing.frame_height = 512;
    crossing.detections.push_back(detection_for_target(296.0f, 256.0f, 0.40f));
    crossing.detections.push_back(detection_for_target(344.0f, 256.0f, 0.92f));

    const vision_native::VisionResult result = selector.select(crossing, rightward_intent(13));

    require_true(result.has_target, "active-lock frame should retain a target");
    require_near(
        result.target_x,
        296.0f,
        0.001f,
        "intent-favored challenger should not switch away from active target immediately");
    require_true(result.intent_id == 13, "active-lock ignored intent should carry intent id");
    require_true(!result.intent_applied, "active-lock ignored intent should not report applied");
    require_text(
        result.intent_decision,
        "ignored_active_lock",
        "active-lock ignored intent should explain why the intent did not switch targets");
}

void test_intent_switch_waits_for_confirmation_before_changing_active_target() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto active_batch = single_target_batch(260.0f, 256.0f, 0.92f);

    selector.select(active_batch);
    const vision_native::VisionResult locked = selector.select(active_batch);
    require_true(locked.has_target, "setup should acquire active target");
    require_near(locked.target_x, 260.0f, 0.001f, "setup should lock far-left target");

    vision_native::DetectionBatch crossing;
    crossing.frame_width = 640;
    crossing.frame_height = 512;
    crossing.detections.push_back(detection_for_target(260.0f, 256.0f, 0.40f));
    crossing.detections.push_back(detection_for_target(336.0f, 256.0f, 0.92f));

    const auto intent = rightward_intent(17);
    const vision_native::VisionResult first = selector.select(crossing, intent);

    require_true(first.has_target, "first switch-confirm frame should retain a target");
    require_near(
        first.target_x,
        260.0f,
        0.001f,
        "first switch-confirm frame should retain active target");
    require_true(first.intent_id == 17, "delayed switch should carry intent id");
    require_true(!first.intent_applied, "delayed switch should not report applied yet");
    require_text(
        first.intent_decision,
        "delayed_switch_confirm",
        "delayed switch should explain that switch confirmation is pending");

    const vision_native::VisionResult second = selector.select(crossing, intent);

    require_true(second.has_target, "confirmed switch should retain a target");
    require_near(
        second.target_x,
        336.0f,
        0.001f,
        "second switch-confirm frame should switch to challenger");
    require_true(second.intent_applied, "confirmed switch should report applied intent");
    require_text(
        second.intent_decision,
        "applied_direction",
        "confirmed switch should preserve applied intent reason");
}

void test_intent_does_not_grant_fire_authority_to_weak_association() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch initial;
    initial.frame_width = 640;
    initial.frame_height = 512;
    initial.detections.push_back(detection_for_target(320.0f, 256.0f, 0.92f));

    selector.select(initial);
    const vision_native::VisionResult locked = selector.select(initial);
    require_true(locked.fire_authority, "initial observed target should have fire authority");

    vision_native::DetectionBatch weak;
    weak.frame_width = 640;
    weak.frame_height = 512;
    weak.detections.push_back(detection_for_target(320.0f, 256.0f, 0.30f));

    const vision_native::VisionResult result = selector.select(weak, rightward_intent(9));

    require_true(result.has_target, "weak association should retain a target");
    require_text(
        result.target_source,
        "associated_weak",
        "weak association should preserve weak source");
    require_true(!result.fire_authority, "intent must not grant fire authority to weak association");
    require_true(result.intent_id == 9, "weak association result should carry intent id for logs");
    require_true(!result.intent_applied, "weak association should not report intent-applied ranking");
}

void test_intent_metadata_does_not_leak_into_later_hold_frame() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto intent = rightward_intent(11);
    const auto batch = two_target_batch();

    selector.select(batch, intent);
    const vision_native::VisionResult selected = selector.select(batch, intent);
    require_true(selected.intent_applied, "setup should select via intent");

    vision_native::DetectionBatch jump;
    jump.frame_width = 640;
    jump.frame_height = 512;
    jump.detections.push_back(detection_for_target(610.0f, 256.0f, 0.95f));

    const vision_native::VisionResult held = selector.select(jump);

    require_true(held.has_target, "large tracking jump should hold previous target briefly");
    require_true(
        !held.has_selected_detection,
        "a held target must not claim backing from an unrelated current detection");
    require_true(
        !held.intent_applied,
        "hold frame without current intent must not inherit old intent_applied");
    require_true(held.intent_id == 0, "hold frame without current intent should not carry old intent id");
    require_text(
        held.intent_decision,
        "none",
        "hold frame without current intent should not carry old intent decision");
}

void test_partial_color_frame_origin_classifies_candidate_cue() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto batch = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto region = selector.required_color_region(batch);
    require_true(region.has_value(), "selector should request a color ROI for plausible low-confidence pickup");
    require_true(region_area(*region) < 640 * 512, "requested color ROI should be partial");

    ColorFrameFixture frame = color_frame_for_region(*region, true);
    const vision_native::VisionResult result = selector.select_with_frame(batch, frame.view);

    require_true(!result.detections.empty(), "annotated result should preserve detections");
    require_true(
        result.detections.front().color_classified,
        "partial color frame should mark detection color classified");
    require_true(
        result.detections.front().has_cue_point,
        "partial color frame origin should let selector detect enemy cue pixels");
}

void test_bgra_green_friendly_is_hard_rejected() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto friendly = detection_for_target(320.0f, 256.0f, 0.92f);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(friendly);
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, color_region_for(friendly), 0, 255, 0);

    const auto first = selector.select_with_frame(batch, frame.view);
    const auto second = selector.select_with_frame(batch, frame.view);

    require_true(!first.has_target && !second.has_target,
                 "green friendly must never enter target selection");
    require_true(second.detections.front().color_classified,
                 "production BGRA path must classify friendly color");
    require_true(second.detections.front().is_friendly,
                 "green marker must set the friendly hard-filter flag");
}

void test_bgra_green_friendly_cannot_beat_yellow_enemy() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto friendly = detection_for_target(270.0f, 256.0f, 0.95f);
    const auto enemy = detection_for_target(390.0f, 256.0f, 0.44f);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections = {friendly, enemy};
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, color_region_for(friendly), 0, 255, 0);
    paint_sparse_bgra(frame, color_region_for(enemy), 255, 255, 0);

    selector.select_with_frame(batch, frame.view);
    const auto result = selector.select_with_frame(batch, frame.view);

    require_true(result.has_target, "yellow enemy must remain selectable");
    require_true(result.has_selected_detection && result.selected_detection_index == 1,
                 "green friendly must be removed before yellow enemy ranking");
    require_true(result.detections[0].is_friendly,
                 "mixed frame must preserve the friendly classification");
    require_true(result.detections[1].color_bonus > 0.0f,
                 "yellow marker must contribute enemy cue score");
}

void test_bgra_yellow_cue_assists_low_confidence_person_pickup() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto enemy = detection_for_target(320.0f, 256.0f, 0.44f);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(enemy);
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, color_region_for(enemy), 255, 255, 0);

    const auto first = selector.select_with_frame(batch, frame.view);
    const auto result = selector.select_with_frame(batch, frame.view);

    require_true(!first.has_target, "cue-assisted pickup must retain switch confirmation");
    require_true(result.has_target, "yellow cue must assist a low-confidence person pickup");
    require_true(result.detections.front().has_cue_point,
                 "yellow cue must expose its associated cue point");
}

void test_yellow_pixels_without_person_never_create_authority() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch empty;
    empty.frame_width = 640;
    empty.frame_height = 512;
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, {280, 100, 360, 136}, 255, 255, 0);

    const auto result = selector.select_with_frame(empty, frame.view);

    require_true(!result.has_target && !result.aim_authority && !result.fire_authority,
                 "yellow UI pixels without a person detection must remain non-authoritative");
}

void test_yellow_cue_hold_is_aim_only() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto enemy = detection_for_target(320.0f, 256.0f, 0.82f);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(enemy);
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, color_region_for(enemy), 255, 255, 0);
    selector.select_with_frame(batch, frame.view);
    const auto locked = selector.select_with_frame(batch, frame.view);
    require_true(locked.has_target, "fixture must acquire the yellow-associated person");

    vision_native::DetectionBatch empty;
    empty.frame_width = 640;
    empty.frame_height = 512;
    const auto held = selector.select_with_frame(empty, frame.view);

    require_true(held.has_target && held.aim_authority,
                 "short yellow cue gap should retain bounded aim continuity");
    require_true(!held.fire_authority && !held.auto_fire,
                 "yellow cue hold must never grant fire authority");
}

void test_roi_miss_does_not_immediately_clear_active_target() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto batch = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto region = selector.required_color_region(batch);
    require_true(region.has_value(), "setup should request color ROI");
    ColorFrameFixture frame = color_frame_for_region(*region, true);

    selector.select_with_frame(batch, frame.view);
    const vision_native::VisionResult locked = selector.select_with_frame(batch, frame.view);
    require_true(locked.has_target, "setup should acquire target with cue evidence");
    require_true(selector.wants_color_frame(), "setup should leave selector wanting cue-hold color frame");

    vision_native::DetectionBatch empty;
    empty.frame_width = 640;
    empty.frame_height = 512;
    const auto cue_region = selector.required_color_region(empty);
    require_true(cue_region.has_value(), "active cue target should request cue-hold ROI");

    ColorFrameFixture missing_roi = color_frame_for_region({0, 0, 8, 8}, false);
    const vision_native::VisionResult held = selector.select_with_frame(empty, missing_roi.view);

    require_true(
        held.has_target,
        "a partial color frame that misses the requested cue ROI must not immediately clear active target");
}

void test_required_color_region_clamps_edge_candidate_to_screen() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto batch = single_target_batch(10.0f, 72.0f, 0.92f);

    const auto region = selector.required_color_region(batch);

    require_true(region.has_value(), "edge candidate should still request a color ROI");
    require_true(region->left >= 0, "edge ROI left should clamp to screen");
    require_true(region->top >= 0, "edge ROI top should clamp to screen");
    require_true(region->right <= 640, "edge ROI right should clamp to screen");
    require_true(region->bottom <= 512, "edge ROI bottom should clamp to screen");
}

void test_external_cue_continuation_does_not_request_full_color_frame() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto batch = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto region = selector.required_color_region(batch);
    require_true(region.has_value(), "setup should request color ROI");
    ColorFrameFixture frame = color_frame_for_region(*region, true);
    selector.select_with_frame(batch, frame.view);
    const vision_native::VisionResult locked = selector.select_with_frame(batch, frame.view);
    require_true(locked.has_target, "setup should acquire target with cue evidence");

    vision_native::DetectionBatch cue;
    cue.frame_width = 640;
    cue.frame_height = 512;
    cue.has_external_cue = true;
    cue.external_cue_x = locked.detections.front().cue_x;
    cue.external_cue_y = locked.detections.front().cue_y;
    cue.external_cue_score = 0.90f;

    const auto requested = selector.required_color_region(cue);

    require_true(
        !requested.has_value(),
        "external cue continuation should not request a full color frame when there are no detections");
}

void test_wide_low_no_cue_candidate_degrades_to_weak_without_death_transition() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch uncertain;
    uncertain.frame_width = 640;
    uncertain.frame_height = 512;
    uncertain.detections.push_back(wide_low_detection_for_target(320.0f, 256.0f, 0.92f));
    ColorFrameFixture dark_full_frame = color_frame_for_region({0, 0, 640, 512}, false);

    selector.select_with_frame(uncertain, dark_full_frame.view);
    const vision_native::VisionResult result =
        selector.select_with_frame(uncertain, dark_full_frame.view);

    require_true(result.has_target, "wide-low no-cue candidate should remain aimable as weak evidence");
    require_text(
        result.target_source,
        "weak_observed",
        "wide-low no-cue candidate should be downgraded instead of treated as strong observed");
    require_true(result.aim_authority, "weak observed target should retain aim authority");
    require_true(!result.fire_authority, "weak observed target must not grant fire authority");
}

void test_wide_low_no_cue_candidate_does_not_keep_dead_active_target_locked() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto live = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto live_region = selector.required_color_region(live);
    require_true(live_region.has_value(), "setup should request a live target color ROI");
    ColorFrameFixture live_frame = color_frame_for_region(*live_region, true);

    selector.select_with_frame(live, live_frame.view);
    const vision_native::VisionResult locked = selector.select_with_frame(live, live_frame.view);
    require_true(locked.has_target, "setup should acquire target with head cue evidence");
    require_true(selector.wants_color_frame(), "setup should keep cue tracking active");

    vision_native::DetectionBatch corpse;
    corpse.frame_width = 640;
    corpse.frame_height = 512;
    corpse.detections.push_back(wide_low_detection_for_target(320.0f, 280.0f, 0.92f));
    ColorFrameFixture dark_full_frame = color_frame_for_region({0, 0, 640, 512}, false);

    const vision_native::VisionResult result = selector.select_with_frame(corpse, dark_full_frame.view);

    require_true(
        !result.has_target,
        "wide-low candidate without head cue evidence should not keep a just-dead target locked");
}

}  // namespace

int main() {
    try {
        test_intent_direction_ranks_plausible_multi_target_candidates();
        test_user_intent_prefers_lower_left_close_target_over_far_upper_right();
        test_crosshair_near_target_beats_physically_near_large_target();
        test_short_occlusion_does_not_switch_locked_near_target_to_visible_far_target();
        test_intent_favored_challenger_logs_ignored_active_lock();
        test_intent_switch_waits_for_confirmation_before_changing_active_target();
        test_intent_does_not_grant_fire_authority_to_weak_association();
        test_intent_metadata_does_not_leak_into_later_hold_frame();
        test_partial_color_frame_origin_classifies_candidate_cue();
        test_bgra_green_friendly_is_hard_rejected();
        test_bgra_green_friendly_cannot_beat_yellow_enemy();
        test_bgra_yellow_cue_assists_low_confidence_person_pickup();
        test_yellow_pixels_without_person_never_create_authority();
        test_yellow_cue_hold_is_aim_only();
        test_roi_miss_does_not_immediately_clear_active_target();
        test_required_color_region_clamps_edge_candidate_to_screen();
        test_external_cue_continuation_does_not_request_full_color_frame();
        test_wide_low_no_cue_candidate_degrades_to_weak_without_death_transition();
        test_wide_low_no_cue_candidate_does_not_keep_dead_active_target_locked();
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "[TargetSelectorTests] FAIL " << exc.what() << "\n";
        return 1;
    }
}
