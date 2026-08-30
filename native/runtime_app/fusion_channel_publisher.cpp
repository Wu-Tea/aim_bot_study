#include "fusion_channel_publisher.h"

#include <Windows.h>

#include <cstdlib>
#include <algorithm>
#include <string>

namespace runtime_app {

namespace {

constexpr int kMaxConsecutiveFailures = 5;

std::wstring widen(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') {
        return {};
    }
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, utf8, -1, &out[0], needed);
    out.resize(static_cast<std::size_t>(needed) - 1);  // strip NUL
    return out;
}

bool environment_flag_off(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    const std::string text(value);
    return text == "1" || text == "true" || text == "True" || text == "on" || text == "ON";
}

const char* environment_string_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

bool environment_bool_flag(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    const std::string text(value);
    if (text == "1" || text == "true" || text == "True" || text == "on" || text == "ON") {
        return true;
    }
    if (text == "0" || text == "false" || text == "False" || text == "off" || text == "OFF") {
        return false;
    }
    return fallback;
}

}  // namespace

FusionChannelPublisher::FusionChannelPublisher() = default;

FusionChannelPublisher::~FusionChannelPublisher() {
    if (header_ != nullptr && mapping_handle_ != nullptr) {
        UnmapViewOfFile(header_);
        header_ = nullptr;
    }
    if (mapping_handle_ != nullptr) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (event_handle_ != nullptr) {
        CloseHandle(event_handle_);
        event_handle_ = nullptr;
    }
}

bool FusionChannelPublisher::open(const char* session, bool show_all_detections) {
    // --- env kill-switch ---
    if (environment_flag_off("FUSION_FORCE_OFF")) {
        return false;
    }

    const char* effective_session =
        environment_string_or("FUSION_SESSION", session);
    show_all_ = environment_bool_flag(
        "FUSION_SHOW_ALL_DETECTIONS", show_all_detections);

    if (effective_session == nullptr || effective_session[0] == '\0') {
        effective_session = "dev";
    }

    // --- build names ---
    const std::wstring session_wide = widen(effective_session);
    if (session_wide.empty()) {
        return false;
    }

    const std::wstring memory_name =
        std::wstring(shared_fusion::FUSION_CHANNEL_MEMORY_PREFIX) +
        session_wide +
        shared_fusion::FUSION_CHANNEL_MEMORY_SUFFIX;
    const std::wstring event_name =
        std::wstring(shared_fusion::FUSION_CHANNEL_EVENT_PREFIX) +
        session_wide +
        shared_fusion::FUSION_CHANNEL_EVENT_SUFFIX;

    // --- create shared memory ---
    const std::size_t mapping_size = shared_fusion::fusion_channel_size();
    mapping_handle_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,    // pagefile-backed
        nullptr,                 // default security
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(mapping_size),
        memory_name.c_str());
    if (mapping_handle_ == nullptr) {
        return false;
    }

    const bool existed = (GetLastError() == ERROR_ALREADY_EXISTS);
    if (existed) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
        return false;
    }

    header_ = static_cast<shared_fusion::FusionChannelHeader*>(
        MapViewOfFile(mapping_handle_, FILE_MAP_ALL_ACCESS, 0, 0, mapping_size));
    if (header_ == nullptr) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
        return false;
    }

    // --- initialise header on first creation ---
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);

    header_->magic         = shared_fusion::FUSION_CHANNEL_MAGIC;
    header_->version       = shared_fusion::FUSION_CHANNEL_VERSION;
    header_->qpc_frequency = static_cast<std::uint64_t>(freq.QuadPart);
    header_->active_slot   = 0;
    header_->slots[0].write_sequence = 0;
    header_->slots[1].write_sequence = 0;

    // --- validate ---
    if (!shared_fusion::fusion_channel_valid(header_)) {
        UnmapViewOfFile(header_);
        header_ = nullptr;
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
        return false;
    }

    // --- create event ---
    event_handle_ = CreateEventW(nullptr, FALSE, FALSE, event_name.c_str());
    if (event_handle_ == nullptr) {
        UnmapViewOfFile(header_);
        header_ = nullptr;
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(event_handle_);
        event_handle_ = nullptr;
        UnmapViewOfFile(header_);
        header_ = nullptr;
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
        return false;
    }

    enabled_ = true;
    consecutive_failures_ = 0;
    return true;
}

void FusionChannelPublisher::publish(
    std::uint64_t frame_id,
    std::int32_t  frame_width,
    std::int32_t  frame_height,
    const shared_fusion::FusionFrameGeometry& geometry,
    const shared_fusion::FusionTarget& target,
    const shared_fusion::FusionDetection* detections,
    std::uint32_t detection_count) {

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    publish(frame_id, frame_width, frame_height, geometry, target, detections,
            detection_count, static_cast<std::uint64_t>(qpc.QuadPart));
}

void FusionChannelPublisher::publish(
    std::uint64_t frame_id,
    std::int32_t  frame_width,
    std::int32_t  frame_height,
    const shared_fusion::FusionFrameGeometry& geometry,
    const shared_fusion::FusionTarget& target,
    const shared_fusion::FusionDetection* detections,
    std::uint32_t detection_count,
    std::uint64_t qpc_timestamp) {

    if (!enabled_ || header_ == nullptr) {
        return;
    }

    if (frame_width <= 0 || frame_height <= 0) {
        return;
    }

    // --- pick next slot ---
    const std::uint32_t slot_index =
        (header_->active_slot + 1) % shared_fusion::FUSION_CHANNEL_SLOT_COUNT;
    shared_fusion::FusionSlot& slot = header_->slots[slot_index];

    // --- seqlock begin: mark writing ---
    ++slot.write_sequence;  // odd → writing
    _WriteBarrier();

    // --- write payload ---
    slot.timestamp       = qpc_timestamp;
    slot.frame_id        = frame_id;
    slot.frame_width     = frame_width;
    slot.frame_height    = frame_height;
    slot.geometry        = geometry;
    slot.target          = target;

    const std::uint32_t clamped_count = show_all_
        ? std::min(detection_count,
                   shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS)
        : 0;
    slot.detection_count = clamped_count;

    if (clamped_count > 0 && detections != nullptr) {
        for (std::uint32_t i = 0; i < clamped_count; ++i) {
            slot.detections[i] = detections[i];
        }
    }

    // --- seqlock end: mark stable ---
    _WriteBarrier();
    ++slot.write_sequence;  // even → stable

    // --- publish slot index ---
    header_->active_slot = slot_index;

    // --- signal consumer best-effort ---
    if (event_handle_ != nullptr) {
        if (SetEvent(event_handle_) == 0) {
            ++consecutive_failures_;
            if (consecutive_failures_ >= kMaxConsecutiveFailures) {
                disable();
            }
            return;
        }
    }

    consecutive_failures_ = 0;
}

void FusionChannelPublisher::disable() {
    enabled_ = false;
    // Keep kernel handles alive so the canvas doesn't see the mapping
    // disappear mid-read, but stop publishing.
}

}  // namespace runtime_app
