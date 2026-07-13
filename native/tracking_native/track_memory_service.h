#pragma once

#include "../pipeline_contract/track_memory.h"
#include "../pipeline_contract/tracker_config.h"
#include "tracker_backend.h"

#include <memory>
#include <vector>

namespace tracking_native {

class TrackMemoryService {
public:
    explicit TrackMemoryService(
        pipeline_contract::TargetTrackerConfig config = {});

    void reset();
    void ingest(const pipeline_contract::TrackObservationBatch& batch);
    void push_control_sample(const TrackerControlSample& sample);

    [[nodiscard]] std::vector<pipeline_contract::TrackEstimate> estimates(
        common_native::TimeSeconds query_time) const;
    [[nodiscard]] pipeline_contract::SelectedTrackRef resolve_selected_observation(
        std::uint64_t observation_id,
        common_native::TimeSeconds query_time) const;

private:
    std::unique_ptr<TrackerBackend> backend_;
};

}  // namespace tracking_native
