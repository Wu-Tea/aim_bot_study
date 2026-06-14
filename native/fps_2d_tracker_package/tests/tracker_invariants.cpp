#include <fps_tracker/target_tracker.hpp>

#include <cassert>
#include <iostream>

int main() {
    fps::TrackerConfig cfg;
    cfg.confirmHits = 2;
    cfg.fireMaxCaptureAgeSec = 0.050;
    fps::TargetTracker tracker(cfg);

    fps::ModeState mode;
    mode.ads = true;
    mode.zoom = 1.0;

    fps::ControlSample cs;
    cs.applyTime = 0.0;
    cs.mode = mode;
    tracker.pushFinalControlSample(cs);

    auto makeDetection = [](fps::DetectionId id, fps::Vec2 center) {
        fps::Detection d;
        d.id = id;
        d.cls = fps::TargetClass::Player;
        d.tier = fps::TargetTier::UpperBody;
        d.confidence = 0.95;
        d.bodyCenterPx = center;
        d.bodyBoxPx = fps::Box2::fromCenterSize(center, {80.0, 170.0});
        d.aimPointPx = {center.x, center.y - 45.0};
        d.validAimPoint = true;
        return d;
    };

    fps::VisionFrame f1;
    f1.frameSeq = 1;
    f1.captureTime = 0.000;
    f1.readyTime = 0.010;
    f1.mode = mode;
    f1.detections.push_back(makeDetection(1, {1020.0, 540.0}));
    tracker.ingestVisionFrame(f1);

    fps::VisionFrame f2 = f1;
    f2.frameSeq = 2;
    f2.captureTime = 0.010;
    f2.readyTime = 0.020;
    f2.detections.clear();
    f2.detections.push_back(makeDetection(2, {1021.0, 540.0}));
    tracker.ingestVisionFrame(f2);

    auto observed = tracker.query(0.021);
    assert(observed.hasSelection);
    assert(!observed.selected.predictedOnly);

    // Next usable vision frame explicitly misses the track. It may coast for assist,
    // but fireAuthority must immediately drop to None.
    fps::VisionFrame miss;
    miss.frameSeq = 3;
    miss.captureTime = 0.020;
    miss.readyTime = 0.030;
    miss.mode = mode;
    tracker.ingestVisionFrame(miss);

    auto coast = tracker.query(0.031);
    assert(coast.hasSelection || coast.candidates.empty());
    for (const fps::TrackSnapshot& s : coast.candidates) {
        if (s.predictedOnly) {
            assert(s.fireAuthority == fps::FireAuthority::None);
        }
        assert(!(s.predictedOnly && s.fireAuthority != fps::FireAuthority::None));
    }

    std::cout << "tracker_invariants: OK\n";
    return 0;
}
