#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <fstream>
#include <random>
#include <string>

namespace {
using vision_native::Detection;
using vision_native::DetectionBatch;
using vision_native::VisionTargetSelector;

Detection person(float x, float y, float w, float h, bool marked = false) {
    Detection d;
    const float ratio = h / w < .65f ? .65f : .35f;
    d.x1 = x - w / 2; d.x2 = x + w / 2;
    d.y1 = y - h * ratio; d.y2 = d.y1 + h;
    d.conf = .92f;
    d.color_classified = true;
    d.has_cue_point = marked;
    d.color_bonus = marked ? .3f : 0;
    d.cue_x = x; d.cue_y = d.y1 - 8; d.cue_score = marked ? .95f : 0;
    return d;
}

DetectionBatch batch(std::initializer_list<Detection> detections) {
    DetectionBatch b;
    b.frame_width = 640; b.frame_height = 512;
    b.frame_id = 1; b.captured_at_ns = 1'000'000'000ull;
    b.detections = detections;
    return b;
}

void advance(DetectionBatch& b, std::uint64_t step = 5'000'000ull) {
    ++b.frame_id; b.captured_at_ns += step;
}

struct Metrics {
    int ranking_cases = 0, ranking_failures = 0;
    int posture_frames = 0, posture_rejections = 0, unsolicited_switches = 0;
    int handover_cases = 0, handover_failures = 0;
    int clipped_cases = 0, clipped_failures = 0, controls_failed = 0;
};

int run(const char* path) {
    std::ofstream out(path);
    if (!out) return 3;
    Metrics m;
    out << "{\"ranking\":[";
    bool first = true;
    // Fixed development and independent validation seeds. Mirror both axes;
    // permute detector order so an index/default-value pass is impossible.
    for (unsigned seed : {20260929u, 593867u}) {
        std::mt19937 random(seed);
        for (int c = 0; c < 128; ++c) {
            const bool vertical = c % 2;
            const float sign = c % 4 < 2 ? -1.f : 1.f;
            const float a = 50.f + float(random() % 5);
            const float b = 32.f + float(random() % 5);
            const bool swapped = c % 3 == 0;
            auto visible = vertical
                ? person(320, 256 + sign * a, 120, (a - 10) / .18f)
                : person(320 + sign * a, 256, (a - 10) / .28f, 220);
            auto small = vertical ? person(320, 256 + sign * b, 24, 60)
                                  : person(320 + sign * b, 256, 24, 60);
            auto input = swapped ? batch({small, visible}) : batch({visible, small});
            VisionTargetSelector selector(640, 512, 150, true, .35f, .65f);
            selector.select(input); advance(input);
            const auto selected = selector.select(input);
            ++m.ranking_cases;
            const bool correct = selected.has_selected_detection &&
                selected.selected_detection_index == unsigned(swapped ? 1 : 0);
            m.ranking_failures += !correct;
            // The region is nearer, even though its configured point is farther.
            if (!first) out << ',';
            first = false;
            out << "{\"seed\":" << seed << ",\"case\":" << c
                << ",\"selected\":" << selected.selected_detection_index
                << ",\"has_target\":" << selected.has_target
                << ",\"correct\":" << correct << '}';

            // Negative control: a target directly under the reticle still wins.
            selector.reset(); input = batch({visible, person(320, 256, 24, 60)});
            selector.select(input); advance(input);
            const auto centered = selector.select(input);
            if (!centered.has_selected_detection || centered.selected_detection_index != 1)
                ++m.controls_failed;

            selector.reset(); input = batch({person(260, 256, 60, 140, true)});
            const auto locked = selector.select(input);
            if (!locked.has_target) ++m.controls_failed;
            const auto generation = locked.selector_target_generation;
            const int frames = c % 2 ? 240 : 24;
            for (int f = 0; f < frames; ++f) {
                advance(input, c % 2 ? 8'333'333ull : 5'000'000ull);
                input.detections = {
                    person(260, 256 + 3 * std::sin(f * .07f), 120, 40),
                    person(344, 256, 60, 140, true)};
                const auto observed = selector.select(input);
                ++m.posture_frames;
                m.posture_rejections += !observed.has_target || !observed.aim_authority;
                m.unsolicited_switches += observed.has_target &&
                    (observed.selector_target_generation != generation ||
                     observed.selected_detection_index != 0);
                if (observed.has_target && observed.selected_detection_index == 0 &&
                    observed.fire_authority) ++m.controls_failed;
            }
            pipeline_contract::UserAimIntent intent;
            intent.valid = true; intent.aiming = true; intent.has_direction = true;
            intent.strength = 1; intent.direction.x = 1; intent.intent_id = c + 1;
            intent.purpose = pipeline_contract::UserAimIntentPurpose::HandoverTarget;
            advance(input);
            const auto moved = selector.select(input, intent);
            ++m.handover_cases;
            m.handover_failures += !moved.has_target || moved.selected_detection_index != 1;
        }
    }
    out << "],\"clipped\":[";
    for (int edge = 0; edge < 4; ++edge) {
        auto d = person(320, 256, 160, 280, true);
        if (edge == 0) { d.x1 = -90; d.x2 = 110; }
        if (edge == 1) { d.x1 = 530; d.x2 = 730; }
        if (edge == 2) { d.y1 = -180; d.y2 = 330; }
        if (edge == 3) { d.y1 = 182; d.y2 = 692; }
        auto input = batch({d});
        VisionTargetSelector selector(640, 512, 1000, true, .35f, .65f);
        selector.select(input); advance(input);
        const auto r = selector.select(input);
        ++m.clipped_cases;
        const float left = std::max(0.f, d.x1), right = std::min(640.f, d.x2);
        const float top = std::max(0.f, d.y1), bottom = std::min(512.f, d.y2);
        const bool correct = r.has_target && r.has_aim_region &&
            r.detections.size() == 1 && r.detections[0].x1 == left &&
            r.detections[0].x2 == right && r.detections[0].y1 == top &&
            r.detections[0].y2 == bottom &&
            r.body_x1 == left && r.body_y1 == top && r.body_x2 == right && r.body_y2 == bottom &&
            r.aim_region_x1 >= left && r.aim_region_x2 <= right &&
            r.aim_region_y1 >= top && r.aim_region_y2 <= bottom &&
            std::fabs(r.target_x - (left + right) / 2) < .001f &&
            std::fabs(r.target_y - (top + (bottom - top) * .35f)) < .001f;
        m.clipped_failures += !correct;
        if (edge) out << ',';
        out << "{\"edge\":" << edge << ",\"correct\":" << correct << '}';
        selector.reset(); d.is_friendly = true; input.detections = {d};
        selector.select(input); advance(input);
        if (selector.select(input).has_target) ++m.controls_failed;
    }
    out << "],\"totals\":{\"ranking_cases\":" << m.ranking_cases
        << ",\"ranking_failures\":" << m.ranking_failures
        << ",\"posture_frames\":" << m.posture_frames
        << ",\"posture_rejections\":" << m.posture_rejections
        << ",\"unsolicited_switches\":" << m.unsolicited_switches
        << ",\"handover_cases\":" << m.handover_cases
        << ",\"handover_failures\":" << m.handover_failures
        << ",\"clipped_cases\":" << m.clipped_cases
        << ",\"clipped_failures\":" << m.clipped_failures
        << ",\"controls_failed\":" << m.controls_failed << "}}\n";
    if (m.controls_failed) return 3;
    return m.ranking_failures || m.posture_rejections || m.unsolicited_switches ||
        m.handover_failures || m.clipped_failures ? 1 : 0;
}
} // namespace

void register_visible_selection_contract_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseVisionSelection", "visible_region_and_posture_authority",
        [](const native_test::TestContext& context) {
            std::filesystem::create_directories(context.artifact_directory);
            const int status = run(context.artifact_path("visible_selection.json").string().c_str());
            if (status == 3) native_test::invalid_fixture("visible selection controls failed");
            if (status) throw std::runtime_error("visible region / aimable posture contract failed");
        });
}
#ifdef VISIBLE_SELECTION_STANDALONE
int main(int argc, char** argv) { return argc == 2 ? run(argv[1]) : 3; }
#endif
