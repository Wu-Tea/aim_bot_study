#include "vision_native/target_selector.h"

#include "pipeline_contract/target_snapshot.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

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

void test_intent_direction_ranks_plausible_multi_target_candidates() {
    vision_native::VisionTargetSelector selector(640, 512);
    const auto batch = two_target_batch();
    const auto intent = rightward_intent(7);

    selector.select(batch, intent);
    const vision_native::VisionResult result = selector.select(batch, intent);

    require_true(result.has_target, "intent-ranked second pickup should acquire a target");
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
        !held.intent_applied,
        "hold frame without current intent must not inherit old intent_applied");
    require_true(held.intent_id == 0, "hold frame without current intent should not carry old intent id");
    require_text(
        held.intent_decision,
        "none",
        "hold frame without current intent should not carry old intent decision");
}

}  // namespace

int main() {
    try {
        test_intent_direction_ranks_plausible_multi_target_candidates();
        test_intent_does_not_grant_fire_authority_to_weak_association();
        test_intent_metadata_does_not_leak_into_later_hold_frame();
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "[TargetSelectorTests] FAIL " << exc.what() << "\n";
        return 1;
    }
}
