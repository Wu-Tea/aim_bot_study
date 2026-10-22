#pragma once
#include "vision_native/types.h"
#include <optional>
#include <vector>

namespace vision_native {
struct CueObservation {
    float x = 0, y = 0, size = 0, score = 0;
    int color = 0;
};

// Independent of body detections. Buffers are reused by the Vision worker.
class CueDetector {
public:
    const std::vector<CueObservation>& detect(const unsigned char* pixels,
        int width, int height, int pitch, PixelFormat format, int origin_x, int origin_y);
private:
    std::vector<unsigned char> mask_;
    std::vector<int> component_;
    std::vector<CueObservation> observations_;
};

// A missing/ambiguous observation retains identity for 50 ms, never a fresh
// coordinate. Person availability and confidence are deliberately not inputs.
class CueIdentity {
public:
    void reset();
    void seed(const CueObservation& cue, std::uint64_t timestamp);
    void update(const std::vector<CueObservation>& cues, std::uint64_t timestamp);
    const std::optional<CueObservation>& active() const { return active_; }
    bool fresh() const { return fresh_; }
    std::uint64_t id() const { return active_ ? identity_ : 0; }
private:
    std::uint64_t identity_ = 0;
    std::optional<CueObservation> active_;
    std::uint64_t last_ns_ = 0;
    float velocity_x_ = 0, velocity_y_ = 0;
    bool fresh_ = false;
};
} // namespace vision_native
