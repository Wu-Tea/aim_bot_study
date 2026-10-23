#pragma once
#include "vision_native/types.h"
#include <optional>
#include <utility>
#include <vector>

namespace vision_native {
struct CueObservation {
    float x = 0, y = 0, size = 0, score = 0;
    int color = 0;
};

// Inclusive cue-centre bounds in the same coordinates as CueObservation.
struct CueSearchRegion {
    float left=0, top=0, right=0, bottom=0;
    bool contains(float x,float y) const {
        return x>=left && x<=right && y>=top && y<=bottom;
    }
};

// Independent of body detections. Buffers are reused by the Vision worker.
class CueDetector {
public:
    const std::vector<CueObservation>& detect(const unsigned char* pixels,
        int width, int height, int pitch, PixelFormat format, int origin_x, int origin_y,
        const std::vector<CueSearchRegion>* regions=nullptr);
    // nullptr searches the full frame; an empty list means no consumer needs
    // cues this frame. Restricted search preserves full connected components
    // and the complete hit-marker composition around the real frame centre.
private:
    struct ColorRun { int left, right, y; };
    std::vector<unsigned char> mask_;
    std::vector<int> component_;
    std::vector<ColorRun> runs_;
    std::vector<std::pair<int,int>> seed_spans_;
    std::vector<std::pair<int, CueObservation>> ordered_observations_;
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
    std::optional<CueSearchRegion> search_region() const;
    std::uint64_t id() const { return active_ ? identity_ : 0; }
private:
    std::uint64_t identity_ = 0;
    std::optional<CueObservation> active_;
    std::uint64_t last_ns_ = 0;
    float velocity_x_ = 0, velocity_y_ = 0;
    bool fresh_ = false;
};
} // namespace vision_native
