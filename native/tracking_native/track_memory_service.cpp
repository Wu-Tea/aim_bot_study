#include "track_memory_service.h"

#include "fps_reference_tracker.h"

#include <utility>

namespace tracking_native {

TrackMemoryService::TrackMemoryService(
    pipeline_contract::TargetTrackerConfig config)
    : backend_(std::make_unique<FpsReferenceTracker>(std::move(config))) {}

void TrackMemoryService::reset() {
    backend_->reset();
}

void TrackMemoryService::ingest(
    const pipeline_contract::TrackObservationBatch& batch) {
    backend_->ingest_batch(batch);
}

void TrackMemoryService::push_control_sample(const TrackerControlSample& sample) {
    backend_->push_control_sample(sample);
}

std::vector<pipeline_contract::TrackEstimate> TrackMemoryService::estimates(
    common_native::TimeSeconds query_time) const {
    return backend_->estimates(query_time);
}

pipeline_contract::SelectedTrackRef
TrackMemoryService::resolve_selected_observation(
    std::uint64_t observation_id,
    common_native::TimeSeconds query_time) const {
    pipeline_contract::SelectedTrackRef selected;
    if (observation_id == 0) {
        return selected;
    }

    for (const pipeline_contract::TrackEstimate& estimate : estimates(query_time)) {
        if (estimate.backing_observation_id != observation_id) {
            continue;
        }
        selected.has_selection = true;
        selected.selected_observation_id = observation_id;
        selected.track_id = estimate.track_id;
        selected.backing_frame_id = estimate.backing_frame_id;
        selected.confidence = estimate.confidence;
        selected.reason = pipeline_contract::SelectedTrackReason::VisionSelector;
        return selected;
    }
    return selected;
}

}  // namespace tracking_native
