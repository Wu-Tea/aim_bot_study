#pragma once

#include "tracker_contract.h"

#include "../pipeline_contract/tracker_config.h"

#include <memory>
#include <string_view>
#include <vector>

namespace tracking_native {

enum class TrackerBackendKind {
    FpsReference,
    LegacyProjection,
    KalmanExperimental,
};

struct TrackerDebugTrack {
    common_native::Vec2f aim_error_px;
    common_native::Vec2f velocity_px_per_sec;
    double age_ms = 0.0;
};

class TrackerBackend {
public:
    virtual ~TrackerBackend() = default;

    virtual void reset() = 0;
    virtual void ingest(const TrackerObservation& observation) = 0;
    virtual void push_control_sample(const TrackerControlSample& sample) = 0;
    virtual TrackerSnapshot query(const TrackerQuery& query) const = 0;
    virtual std::vector<TrackerDebugTrack> debug_tracks() const = 0;
};

TrackerBackendKind parse_tracker_backend_kind(std::string_view value);
std::string_view tracker_backend_kind_name(TrackerBackendKind kind);

std::unique_ptr<TrackerBackend> create_tracker_backend(
    TrackerBackendKind kind,
    pipeline_contract::TargetTrackerConfig config);

}  // namespace tracking_native
