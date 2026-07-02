#include "tracker_backend.h"

#include "fps_reference_tracker.h"
#include "kalman_tracker.h"
#include "legacy_projection_tracker.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace tracking_native {

namespace {

std::string normalized_backend_name(std::string_view value) {
    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

}  // namespace

TrackerBackendKind parse_tracker_backend_kind(std::string_view value) {
    const std::string normalized = normalized_backend_name(value);
    if (normalized.empty() || normalized == "fps_reference") {
        return TrackerBackendKind::FpsReference;
    }
    if (normalized == "legacy_projection") {
        return TrackerBackendKind::LegacyProjection;
    }
    if (normalized == "kalman_experimental") {
        return TrackerBackendKind::KalmanExperimental;
    }
    throw std::runtime_error("unknown tracker_backend: " + std::string(value));
}

std::string_view tracker_backend_kind_name(TrackerBackendKind kind) {
    switch (kind) {
    case TrackerBackendKind::FpsReference:
        return "fps_reference";
    case TrackerBackendKind::LegacyProjection:
        return "legacy_projection";
    case TrackerBackendKind::KalmanExperimental:
        return "kalman_experimental";
    }
    return "legacy_projection";
}

std::unique_ptr<TrackerBackend> create_tracker_backend(
    TrackerBackendKind kind,
    pipeline_contract::TargetTrackerConfig config) {
    switch (kind) {
    case TrackerBackendKind::FpsReference:
        return std::make_unique<FpsReferenceTracker>(config);
    case TrackerBackendKind::LegacyProjection:
        return std::make_unique<LegacyProjectionTracker>(config);
    case TrackerBackendKind::KalmanExperimental:
        return std::make_unique<KalmanTracker>(config);
    }
    return std::make_unique<FpsReferenceTracker>(config);
}

}  // namespace tracking_native
