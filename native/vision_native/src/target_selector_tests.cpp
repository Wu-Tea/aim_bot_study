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

void draw_motion_pattern(
    ColorFrameFixture& frame,
    int center_x,
    int center_y) {
    for (int y = center_y - 20; y <= center_y + 20; ++y) {
        for (int x = center_x - 20; x <= center_x + 20; ++x) {
            if (x < 0 || y < 0 ||
                x >= frame.view.width || y >= frame.view.height) {
                continue;
            }
            const std::size_t offset = static_cast<std::size_t>(
                y * frame.view.row_pitch + x * 4);
            const int local_x = x - center_x;
            const int local_y = y - center_y;
            const std::uint8_t value = static_cast<std::uint8_t>(
                35 + ((local_x * 17 + local_y * 29 +
                       (local_x * local_y)) & 0x9f));
            frame.pixels[offset + 0] = value;
            frame.pixels[offset + 1] =
                static_cast<std::uint8_t>(value / 2);
            frame.pixels[offset + 2] =
                static_cast<std::uint8_t>(220 - value / 3);
        }
    }
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

void test_unaligned_intent_does_not_confirm_right_side_challenger() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto active_batch = single_target_batch(260.0f, 256.0f, 0.92f);

    selector.select(active_batch);
    const vision_native::VisionResult locked = selector.select(active_batch);
    require_true(locked.has_target, "setup should acquire active target");
    require_near(
        locked.target_x,
        260.0f,
        0.001f,
        "setup should lock the left target before the unaligned control");

    vision_native::DetectionBatch crossing;
    crossing.frame_width = 640;
    crossing.frame_height = 512;
    crossing.detections.push_back(detection_for_target(260.0f, 256.0f, 0.92f));
    crossing.detections.push_back(detection_for_target(336.0f, 256.0f, 0.92f));

    // This direction supports the retained left target and is deliberately
    // unaligned with the right-side challenger. It is the counterfactual to
    // test_intent_switch_waits_for_confirmation_before_changing_active_target.
    const auto intent = lower_left_intent(18);
    const vision_native::VisionResult first = selector.select(crossing, intent);
    const vision_native::VisionResult second = selector.select(crossing, intent);

    require_true(first.has_target && second.has_target,
                 "unaligned control must retain a target on both frames");
    require_near(
        first.target_x,
        260.0f,
        0.001f,
        "unaligned intent must retain the current target on frame one");
    require_near(
        second.target_x,
        260.0f,
        0.001f,
        "unaligned intent must not confirm the right challenger");
    require_true(second.intent_id == 18,
                 "unaligned control must retain the current intent id for diagnosis");
    require_text(
        second.intent_decision,
        "ignored_unaligned_challenger",
        "unaligned control must expose the blocked handover reason");
}

void test_dead_active_target_does_not_override_unaligned_handover_intent() {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch live;
    live.frame_width = 640;
    live.frame_height = 512;
    auto live_enemy = detection_for_target(260.0f, 256.0f, 0.92f);
    live_enemy.has_cue_point = true;
    live_enemy.cue_x = 260.0f;
    live_enemy.cue_y = 190.0f;
    live_enemy.cue_score = 0.95f;
    live_enemy.color_bonus = 0.30f;
    live.detections.push_back(live_enemy);

    selector.select(live);
    const auto locked = selector.select(live);
    require_true(locked.has_target, "setup should acquire the marked live target");

    vision_native::DetectionBatch death_transition;
    death_transition.frame_width = 640;
    death_transition.frame_height = 512;
    death_transition.detections.push_back(
        wide_low_detection_for_target(260.0f, 274.0f, 0.92f));
    auto live_challenger = detection_for_target(344.0f, 256.0f, 0.92f);
    live_challenger.has_cue_point = true;
    live_challenger.cue_x = 344.0f;
    live_challenger.cue_y = 190.0f;
    live_challenger.cue_score = 0.95f;
    live_challenger.color_bonus = 0.30f;
    death_transition.detections.push_back(live_challenger);

    const auto intent = lower_left_intent(19);
    const auto pending = selector.select(death_transition, intent);
    require_true(pending.has_target, "death transition should retain a target while switch confirms");
    require_near(
        pending.target_x,
        260.0f,
        0.001f,
        "first death-transition frame should preserve the old target until confirmation");

    const auto switched = selector.select(death_transition, intent);
    require_true(switched.has_target, "confirmed death transition should select the live challenger");
    require_near(
        switched.target_x,
        344.0f,
        0.001f,
        "corpse invalidation must outrank an unaligned manual handover intent");
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

void test_single_marked_enemy_pickup_does_not_wait_for_a_second_frame() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto enemy = detection_for_target(320.0f, 256.0f, 0.72f);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.push_back(enemy);
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, color_region_for(enemy), 255, 255, 0);

    const auto first = selector.select_with_frame(batch, frame.view);

    require_true(first.has_target && first.aim_authority,
                 "one credible marked enemy must publish aim authority on its first frame");
    require_true(first.has_selected_detection && first.selected_detection_index == 0,
                 "fast pickup must retain the marked person's source identity");
}

void test_multiple_marked_enemies_still_require_confirmation() {
    vision_native::VisionTargetSelector selector(640, 512);
    auto left = detection_for_target(280.0f, 256.0f, 0.82f);
    auto right = detection_for_target(360.0f, 256.0f, 0.82f);
    for (auto* detection : {&left, &right}) {
        detection->has_cue_point = true;
        detection->cue_x = (detection->x1 + detection->x2) * 0.5f;
        detection->cue_y = detection->y1 - 8.0f;
        detection->cue_score = 0.95f;
        detection->color_bonus = 0.30f;
    }
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections = {left, right};

    const auto first = selector.select(batch);
    const auto second = selector.select(batch);

    require_true(!first.has_target,
                 "multiple credible marked enemies must not take the single-target fast path");
    require_true(second.has_target,
                 "stable multiple-target ranking should still commit after confirmation");
}

void test_yellow_cue_continuation_tracks_visible_marker_for_bounded_ads_hold() {
    constexpr std::uint64_t kMillisecondNs = 1'000'000ull;
    constexpr std::uint64_t kLockTimeNs = 1'000'000'000ull;
    vision_native::VisionTargetSelector selector(640, 512);
    const auto enemy = detection_for_target(320.0f, 256.0f, 0.82f);
    vision_native::DetectionBatch observed;
    observed.frame_width = 640;
    observed.frame_height = 512;
    observed.detections.push_back(enemy);
    auto frame = full_bgra_frame();
    paint_sparse_bgra(frame, color_region_for(enemy), 255, 255, 0);

    observed.captured_at_ns = kLockTimeNs - (5ull * kMillisecondNs);
    selector.select_with_frame(observed, frame.view);
    observed.captured_at_ns = kLockTimeNs;
    const auto locked = selector.select_with_frame(observed, frame.view);
    require_true(locked.has_target, "fixture must acquire a timestamped person+cue target");

    vision_native::DetectionBatch occluded;
    occluded.frame_width = 640;
    occluded.frame_height = 512;
    for (std::uint64_t elapsed_ms = 5ull; elapsed_ms <= 1000ull; elapsed_ms += 5ull) {
        occluded.captured_at_ns = kLockTimeNs + (elapsed_ms * kMillisecondNs);
        const auto held = selector.select_with_frame(occluded, frame.view);
        require_true(held.has_target && held.aim_authority,
                     "visible cue should bridge a bounded ADS person occlusion");
        require_text(held.target_source, "cue_hold",
                     "boosted occlusion continuation must remain cue_hold evidence");
        require_true(!held.fire_authority && !held.auto_fire,
                     "extended cue boost must never grant fire authority");
    }

    occluded.captured_at_ns = kLockTimeNs + (1001ull * kMillisecondNs);
    require_true(!selector.required_color_region(occluded).has_value(),
                 "expired cue boost must stop requesting continuation color ROI");
    const auto expired = selector.select_with_frame(occluded, frame.view);
    require_true(!expired.has_target && !expired.aim_authority && !expired.fire_authority,
                  "cue must lose authority after the bounded ADS continuation ceiling");
}

void test_yellow_cue_continuation_releases_when_marker_evidence_stops() {
    constexpr std::uint64_t kMillisecondNs = 1'000'000ull;
    constexpr std::uint64_t kLockTimeNs = 1'500'000'000ull;
    vision_native::VisionTargetSelector selector(640, 512);
    const auto enemy = detection_for_target(320.0f, 256.0f, 0.82f);
    vision_native::DetectionBatch observed;
    observed.frame_width = 640;
    observed.frame_height = 512;
    observed.detections.push_back(enemy);
    auto marker_frame = full_bgra_frame();
    paint_sparse_bgra(marker_frame, color_region_for(enemy), 255, 255, 0);
    auto dark_frame = full_bgra_frame();

    observed.captured_at_ns = kLockTimeNs - (5ull * kMillisecondNs);
    selector.select_with_frame(observed, marker_frame.view);
    observed.captured_at_ns = kLockTimeNs;
    require_true(
        selector.select_with_frame(observed, marker_frame.view).has_target,
        "fixture must acquire a timestamped person+cue target");

    vision_native::DetectionBatch occluded;
    occluded.frame_width = 640;
    occluded.frame_height = 512;
    occluded.captured_at_ns = kLockTimeNs + (10ull * kMillisecondNs);
    require_true(
        selector.select_with_frame(occluded, marker_frame.view).has_target,
        "current marker should start cue continuation");

    occluded.captured_at_ns = kLockTimeNs + (61ull * kMillisecondNs);
    const auto expired = selector.select_with_frame(occluded, dark_frame.view);
    require_true(
        !expired.has_target && !expired.aim_authority,
        "cue continuation must release after 50 ms without marker evidence");
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

void test_enemy_marker_history_survives_one_upright_gap_and_rejects_corpse() {
    constexpr std::uint64_t kMillisecondNs = 1'000'000ull;
    constexpr std::uint64_t kLockTimeNs = 2'000'000'000ull;
    vision_native::VisionTargetSelector selector(640, 512);
    auto live = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto live_region = selector.required_color_region(live);
    require_true(live_region.has_value(), "setup should request a live target color ROI");
    ColorFrameFixture live_frame = color_frame_for_region(*live_region, true);

    live.captured_at_ns = kLockTimeNs - (5ull * kMillisecondNs);
    selector.select_with_frame(live, live_frame.view);
    live.captured_at_ns = kLockTimeNs;
    const auto locked = selector.select_with_frame(live, live_frame.view);
    require_true(locked.has_target, "setup should acquire target with enemy marker evidence");

    ColorFrameFixture dark_full_frame = color_frame_for_region({0, 0, 640, 512}, false);
    auto upright_gap = single_target_batch(320.0f, 256.0f, 0.92f);
    upright_gap.captured_at_ns = kLockTimeNs + (10ull * kMillisecondNs);
    const auto continued = selector.select_with_frame(upright_gap, dark_full_frame.view);
    require_true(
        continued.has_target,
        "one upright marker gap should not immediately discard a plausible live target");

    vision_native::DetectionBatch corpse;
    corpse.frame_width = 640;
    corpse.frame_height = 512;
    corpse.captured_at_ns = kLockTimeNs + (20ull * kMillisecondNs);
    corpse.detections.push_back(wide_low_detection_for_target(320.0f, 280.0f, 0.92f));
    const auto result = selector.select_with_frame(corpse, dark_full_frame.view);

    require_true(
        !result.has_target,
        "a marker-backed active generation must remember the marker loss across an intermediate frame and reject the corpse");
}

void test_enemy_marker_loss_expires_while_person_box_remains_upright() {
    constexpr std::uint64_t kMillisecondNs = 1'000'000ull;
    constexpr std::uint64_t kLockTimeNs = 3'000'000'000ull;
    vision_native::VisionTargetSelector selector(640, 512);
    auto marked = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto marked_region = selector.required_color_region(marked);
    require_true(marked_region.has_value(), "setup should request an enemy-marker ROI");
    ColorFrameFixture marked_frame = color_frame_for_region(*marked_region, true);

    marked.captured_at_ns = kLockTimeNs - (5ull * kMillisecondNs);
    selector.select_with_frame(marked, marked_frame.view);
    marked.captured_at_ns = kLockTimeNs;
    const auto locked = selector.select_with_frame(marked, marked_frame.view);
    require_true(locked.has_target, "setup should acquire the marked target");

    ColorFrameFixture dark_full_frame = color_frame_for_region({0, 0, 640, 512}, false);
    auto marker_gap = single_target_batch(320.0f, 256.0f, 0.92f);
    marker_gap.captured_at_ns = kLockTimeNs + (40ull * kMillisecondNs);
    const auto grace = selector.select_with_frame(marker_gap, dark_full_frame.view);
    require_true(
        grace.has_target,
        "a brief marker gap should retain the same upright target during grace");

    marker_gap.captured_at_ns = kLockTimeNs + (81ull * kMillisecondNs);
    const auto expired = selector.select_with_frame(marker_gap, dark_full_frame.view);
    require_true(
        !expired.has_target,
        "an enemy-marker-backed target must stop being selectable after the bounded marker-loss grace");

    marker_gap.captured_at_ns = kLockTimeNs + (140ull * kMillisecondNs);
    const auto still_rejected = selector.select_with_frame(marker_gap, dark_full_frame.view);
    marker_gap.captured_at_ns = kLockTimeNs + (200ull * kMillisecondNs);
    const auto not_reacquired = selector.select_with_frame(marker_gap, dark_full_frame.view);
    require_true(
        !still_rejected.has_target && !not_reacquired.has_target,
        "an expired corpse must not be reacquired as a fresh unmarked person on following frames");

    auto drifting_corpse = single_target_batch(370.0f, 270.0f, 0.92f);
    drifting_corpse.captured_at_ns = kLockTimeNs + (210ull * kMillisecondNs);
    const auto drift_pending = selector.select_with_frame(drifting_corpse, dark_full_frame.view);
    drifting_corpse.captured_at_ns = kLockTimeNs + (220ull * kMillisecondNs);
    const auto drift_rejected = selector.select_with_frame(drifting_corpse, dark_full_frame.view);
    require_true(
        !drift_pending.has_target && !drift_rejected.has_target,
        "a falling corpse must not escape suppression by drifting beyond pickup-confirm distance");

    marked.captured_at_ns = kLockTimeNs + (230ull * kMillisecondNs);
    const auto marker_returned = selector.select_with_frame(marked, marked_frame.view);
    require_true(
        marker_returned.has_target,
        "a direct person-plus-enemy-marker observation should revive the same spatial target");
}

void test_upright_candidate_without_prior_marker_is_not_blanket_rejected() {
    constexpr std::uint64_t kMillisecondNs = 1'000'000ull;
    vision_native::VisionTargetSelector selector(640, 512);
    auto unmarked = single_target_batch(320.0f, 256.0f, 0.92f);
    ColorFrameFixture dark_full_frame = color_frame_for_region({0, 0, 640, 512}, false);

    unmarked.captured_at_ns = 4'000'000'000ull;
    selector.select_with_frame(unmarked, dark_full_frame.view);
    unmarked.captured_at_ns += 200ull * kMillisecondNs;
    const auto result = selector.select_with_frame(unmarked, dark_full_frame.view);

    require_true(
        result.has_target,
        "marker-loss expiry must not turn the enemy marker into a global pickup requirement");
}

void test_selector_does_not_duplicate_coordinator_ads_activation_gate() {
    vision_native::VisionTargetSelector selector(640, 512);
    // A 140 px-tall body expands the coordinator-owned 135 px activation
    // radius beyond 150 px. The selector's old rectangular first-pickup gate
    // rejects this otherwise admissible candidate and delays ownership until
    // the user has already moved the reticle most of the way there.
    vision_native::DetectionBatch candidate;
    candidate.frame_width = 640;
    candidate.frame_height = 512;
    candidate.detections.push_back(
        detection_for_target(320.0f, 406.0f, 0.92f));

    const auto pending = selector.select(candidate);
    const auto admitted = selector.select(candidate);
    require_true(!pending.has_target,
                 "selector pickup must retain its two-frame identity confirmation");
    require_true(admitted.has_target,
                 "selector must leave spatial ADS activation to TargetCoordinator");
    require_near(admitted.target_y, 406.0f, 0.001f,
                 "selector must publish the confirmed candidate geometry unchanged");
}

void test_marker_loss_memory_survives_one_detection_dropout() {
    constexpr std::uint64_t kMillisecondNs = 1'000'000ull;
    constexpr std::uint64_t kLockTimeNs = 5'000'000'000ull;
    vision_native::VisionTargetSelector selector(640, 512);
    auto marked = single_target_batch(320.0f, 256.0f, 0.45f);
    const auto marked_region = selector.required_color_region(marked);
    require_true(marked_region.has_value(), "setup should request an enemy-marker ROI");
    ColorFrameFixture marked_frame = color_frame_for_region(*marked_region, true);
    ColorFrameFixture dark_full_frame = color_frame_for_region({0, 0, 640, 512}, false);

    marked.captured_at_ns = kLockTimeNs - (5ull * kMillisecondNs);
    selector.select_with_frame(marked, marked_frame.view);
    marked.captured_at_ns = kLockTimeNs;
    require_true(
        selector.select_with_frame(marked, marked_frame.view).has_target,
        "setup should acquire the marked target");

    vision_native::DetectionBatch dropout;
    dropout.frame_width = 640;
    dropout.frame_height = 512;
    dropout.captured_at_ns = kLockTimeNs + (60ull * kMillisecondNs);
    const auto missing = selector.select_with_frame(dropout, dark_full_frame.view);
    require_true(
        !missing.has_target,
        "a frame with neither person nor marker should publish no target");

    auto corpse = single_target_batch(320.0f, 256.0f, 0.92f);
    corpse.captured_at_ns = kLockTimeNs + (90ull * kMillisecondNs);
    const auto expired = selector.select_with_frame(corpse, dark_full_frame.view);
    corpse.captured_at_ns = kLockTimeNs + (120ull * kMillisecondNs);
    const auto not_reacquired = selector.select_with_frame(corpse, dark_full_frame.view);
    require_true(
        !expired.has_target && !not_reacquired.has_target,
        "one detector dropout must not erase the marked target's corpse suppression memory");
}

void test_motion_anchor_ignores_box_edge_reconstruction_and_tracks_person() {
    vision_native::VisionTargetSelector selector(640, 512);
    auto frame = full_bgra_frame();
    draw_motion_pattern(frame, 320, 242);
    auto stable = single_target_batch(320.0f, 256.0f, 0.92f);

    selector.select_with_frame(stable, frame.view);
    const auto locked = selector.select_with_frame(stable, frame.view);
    require_true(
        locked.has_selected_detection,
        "setup should select the textured person");
    const auto initial = locked.detections[
        locked.selected_detection_index];
    require_true(
        initial.has_motion_anchor,
        "selected textured person should expose a motion anchor");

    auto deformed = stable;
    deformed.detections[0].x2 += 16.0f;
    deformed.detections[0].y2 -= 40.0f;
    const auto reconstructed =
        selector.select_with_frame(deformed, frame.view);
    const auto reconstructed_detection = reconstructed.detections[
        reconstructed.selected_detection_index];
    require_true(
        reconstructed_detection.has_motion_anchor,
        "box reconstruction should retain the visual motion anchor");
    require_near(
        reconstructed_detection.motion_anchor_x,
        initial.motion_anchor_x,
        1.0f,
        "single horizontal edge motion must not move person anchor");
    require_near(
        reconstructed_detection.motion_anchor_y,
        initial.motion_anchor_y,
        1.0f,
        "single vertical edge motion must not move person anchor");

    auto severely_deformed = stable;
    severely_deformed.detections[0].x2 += 28.0f;
    severely_deformed.detections[0].y2 -= 54.0f;
    const auto required_after_severe_deformation =
        selector.required_color_region(severely_deformed);
    require_true(
        required_after_severe_deformation.has_value() &&
            required_after_severe_deformation->left <=
                static_cast<int>(initial.motion_anchor_x) - 12 &&
            required_after_severe_deformation->right >=
                static_cast<int>(initial.motion_anchor_x) + 12 &&
            required_after_severe_deformation->top <=
                static_cast<int>(initial.motion_anchor_y) - 12 &&
            required_after_severe_deformation->bottom >=
                static_cast<int>(initial.motion_anchor_y) + 12,
        "color readback should retain the confirmed anchor search neighborhood");
    const auto severely_reconstructed =
        selector.select_with_frame(severely_deformed, frame.view);
    const auto severely_reconstructed_detection =
        severely_reconstructed.detections[
            severely_reconstructed.selected_detection_index];
    require_true(
        severely_reconstructed_detection.has_motion_anchor,
        "large body-box reconstruction should retain the visual motion anchor");
    require_near(
        severely_reconstructed_detection.motion_anchor_x,
        initial.motion_anchor_x,
        1.0f,
        "large horizontal edge motion must not move person anchor");
    require_near(
        severely_reconstructed_detection.motion_anchor_y,
        initial.motion_anchor_y,
        1.0f,
        "large vertical edge motion must not move person anchor");

    auto moved_frame = full_bgra_frame();
    draw_motion_pattern(moved_frame, 325, 238);
    auto moved = stable;
    moved.detections[0].x1 += 5.0f;
    moved.detections[0].x2 += 5.0f;
    moved.detections[0].y1 -= 4.0f;
    moved.detections[0].y2 -= 4.0f;
    const auto translated =
        selector.select_with_frame(moved, moved_frame.view);
    const auto translated_detection =
        translated.detections[translated.selected_detection_index];
    require_true(
        translated_detection.has_motion_anchor,
        "rigidly translated person should retain the visual anchor");
    require_near(
        translated_detection.motion_anchor_x -
            severely_reconstructed_detection.motion_anchor_x,
        5.0f,
        1.0f,
        "motion anchor should preserve horizontal person translation");
    require_near(
        translated_detection.motion_anchor_y -
            severely_reconstructed_detection.motion_anchor_y,
        -4.0f,
        1.0f,
        "motion anchor should preserve vertical person translation");
}

void test_selector_generation_survives_frame_local_observation_changes() {
    vision_native::VisionTargetSelector selector(640, 512);
    auto same_target = single_target_batch(260.0f, 256.0f, 0.92f);
    same_target.frame_id = 101;
    const auto pending_pick = selector.select(same_target);
    same_target.frame_id = 102;
    const auto first = selector.select(same_target);
    same_target.frame_id = 103;
    const auto second = selector.select(same_target);
    require_true(first.selector_identity_protocol,
                 "selector must publish its identity protocol");
    require_true(first.selector_target_generation != 0,
                 "initial selector commit must create a generation");
    require_true(second.selector_target_generation == first.selector_target_generation,
                 "frame-local source ids must not create a new selector generation");
    require_true(second.selector_target_generation == first.selector_target_generation,
                 "same target across a third fresh frame must retain generation");
    require_true(!second.selector_target_changed,
                 "same-target continuation must not report replacement");

    vision_native::DetectionBatch replacement = same_target;
    replacement.frame_id = 104;
    replacement.detections[0] = detection_for_target(260.0f, 256.0f, 0.40f);
    replacement.detections.push_back(detection_for_target(336.0f, 256.0f, 0.92f));
    const auto pending = selector.select(replacement, rightward_intent(81));
    require_true(pending.selector_target_generation == first.selector_target_generation,
                 "replacement must wait for selector confirmation");
    replacement.frame_id = 105;
    const auto switched = selector.select(replacement, rightward_intent(81));
    require_true(switched.selector_target_generation > first.selector_target_generation,
                 "confirmed selector replacement must increment generation");
    require_true(switched.selector_target_changed,
                  "confirmed selector replacement must publish changed=true");
}

void test_confirmed_frame_replacement_bootstraps_a_new_motion_anchor() {
    vision_native::VisionTargetSelector selector(640, 512);
    auto old_target = single_target_batch(260.0f, 256.0f, 0.92f);
    auto old_frame = full_bgra_frame();
    draw_motion_pattern(old_frame, 260, 242);

    selector.select_with_frame(old_target, old_frame.view);
    const auto old_locked =
        selector.select_with_frame(old_target, old_frame.view);
    require_true(old_locked.has_selected_detection,
                 "replacement fixture must first lock the original person");
    const auto old_detection = old_locked.detections[
        old_locked.selected_detection_index];
    require_true(old_detection.has_motion_anchor,
                 "replacement fixture must establish the old appearance anchor");

    auto replacement = old_target;
    replacement.frame_id = 104;
    replacement.detections[0] = detection_for_target(260.0f, 256.0f, 0.40f);
    replacement.detections.push_back(
        detection_for_target(340.0f, 256.0f, 0.92f));
    auto replacement_frame = full_bgra_frame();
    draw_motion_pattern(replacement_frame, 340, 242);

    const auto pending = selector.select_with_frame(
        replacement, replacement_frame.view);
    require_true(
        pending.selector_target_generation == old_locked.selector_target_generation,
        "an unconfirmed replacement must retain the old selector generation");
    replacement.frame_id = 105;
    const auto switched = selector.select_with_frame(
        replacement, replacement_frame.view);
    require_true(switched.selector_target_changed,
                 "replacement fixture must reach the selector confirmation boundary");
    require_true(switched.has_selected_detection,
                 "confirmed replacement must publish the new detection");
    const auto new_detection = switched.detections[
        switched.selected_detection_index];
    require_true(
        new_detection.has_motion_anchor &&
            std::fabs(new_detection.motion_anchor_x - 340.0f) <= 2.0f &&
            std::fabs(new_detection.motion_anchor_y - 242.0f) <= 2.0f,
        "confirmed replacement must bootstrap its anchor from the new person, not the old patch");
    require_true(
        std::fabs(new_detection.motion_anchor_x - old_detection.motion_anchor_x) > 20.0f,
        "replacement anchor must not inherit the previous person's location");
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
        test_unaligned_intent_does_not_confirm_right_side_challenger();
        test_dead_active_target_does_not_override_unaligned_handover_intent();
        test_intent_does_not_grant_fire_authority_to_weak_association();
        test_intent_metadata_does_not_leak_into_later_hold_frame();
        test_partial_color_frame_origin_classifies_candidate_cue();
        test_bgra_green_friendly_is_hard_rejected();
        test_bgra_green_friendly_cannot_beat_yellow_enemy();
        test_bgra_yellow_cue_assists_low_confidence_person_pickup();
        test_single_marked_enemy_pickup_does_not_wait_for_a_second_frame();
        test_multiple_marked_enemies_still_require_confirmation();
        test_yellow_pixels_without_person_never_create_authority();
        test_yellow_cue_hold_is_aim_only();
        test_yellow_cue_continuation_tracks_visible_marker_for_bounded_ads_hold();
        test_yellow_cue_continuation_releases_when_marker_evidence_stops();
        test_roi_miss_does_not_immediately_clear_active_target();
        test_required_color_region_clamps_edge_candidate_to_screen();
        test_external_cue_continuation_does_not_request_full_color_frame();
        test_wide_low_no_cue_candidate_degrades_to_weak_without_death_transition();
        test_wide_low_no_cue_candidate_does_not_keep_dead_active_target_locked();
        test_enemy_marker_history_survives_one_upright_gap_and_rejects_corpse();
        test_enemy_marker_loss_expires_while_person_box_remains_upright();
        test_upright_candidate_without_prior_marker_is_not_blanket_rejected();
        test_selector_does_not_duplicate_coordinator_ads_activation_gate();
        test_marker_loss_memory_survives_one_detection_dropout();
        test_motion_anchor_ignores_box_edge_reconstruction_and_tracks_person();
        test_selector_generation_survives_frame_local_observation_changes();
        test_confirmed_frame_replacement_bootstraps_a_new_motion_anchor();
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "[TargetSelectorTests] FAIL " << exc.what() << "\n";
        return 1;
    }
}
