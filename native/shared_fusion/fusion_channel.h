#pragma once

#include <cstddef>
#include <cstdint>

namespace shared_fusion {

inline constexpr std::uint32_t FUSION_CHANNEL_MAGIC = 0x4655534E; // "FUSN"
inline constexpr std::uint32_t FUSION_CHANNEL_VERSION = 2;
inline constexpr std::uint32_t FUSION_CHANNEL_MAX_DETECTIONS = 256;
inline constexpr std::uint32_t FUSION_CHANNEL_SLOT_COUNT = 2;
inline constexpr const wchar_t* FUSION_CHANNEL_MEMORY_PREFIX =
    L"Local\\YoloStudy001.Fusion.";
inline constexpr const wchar_t* FUSION_CHANNEL_MEMORY_SUFFIX = L".Vision.Mapping";
inline constexpr const wchar_t* FUSION_CHANNEL_EVENT_PREFIX =
    L"Local\\YoloStudy001.Fusion.";
inline constexpr const wchar_t* FUSION_CHANNEL_EVENT_SUFFIX = L".Vision.Event";

struct FusionDetection {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float conf = 0.0f;
    std::int32_t class_id = 0;
    float color_bonus = 0.0f;
    bool is_friendly = false;
    std::uint8_t reserved[3] = {};
};

struct FusionTarget {
    bool has_target = false;
    bool auto_fire = false;
    bool has_body_box = false;
    bool direct_observation = false;
    bool enemy_identity_confirmed = false;
    std::uint8_t reserved[3] = {};
    std::uint64_t selector_target_generation = 0;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float confidence = 0.0f;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
};

struct FusionFrameGeometry {
    std::int32_t output_left = 0;
    std::int32_t output_top = 0;
    std::int32_t output_width = 0;
    std::int32_t output_height = 0;
    std::int32_t roi_left = 0;
    std::int32_t roi_top = 0;
};

struct FusionSlot {
    volatile std::uint32_t write_sequence = 0;
    std::uint64_t timestamp = 0;
    std::uint64_t frame_id = 0;
    std::int32_t frame_width = 0;
    std::int32_t frame_height = 0;
    FusionFrameGeometry geometry;
    std::uint32_t detection_count = 0;
    FusionTarget target;
    FusionDetection detections[FUSION_CHANNEL_MAX_DETECTIONS];
    std::uint32_t reserved[4] = {};
};

struct FusionChannelHeader {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t qpc_frequency = 0;
    volatile std::uint32_t active_slot = 0;
    std::uint32_t reserved = 0;
    FusionSlot slots[FUSION_CHANNEL_SLOT_COUNT];
};

static_assert(sizeof(FusionChannelHeader) < 65536,
              "FusionChannelHeader must fit within a 64 KiB mapping");

inline constexpr std::size_t fusion_channel_size() noexcept {
    return sizeof(FusionChannelHeader);
}

inline bool fusion_channel_valid(const FusionChannelHeader* header) noexcept {
    return header != nullptr &&
           header->magic == FUSION_CHANNEL_MAGIC &&
           header->version == FUSION_CHANNEL_VERSION &&
           header->qpc_frequency != 0;
}

}  // namespace shared_fusion
