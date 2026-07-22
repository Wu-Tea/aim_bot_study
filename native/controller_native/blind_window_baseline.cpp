#include "blind_window_baseline.h"

#include "blind_window_fixtures.h"

#include <array>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace controller_native::blind_window {

BlindBaselineSummary run_k1_baseline_matrix() {
    constexpr std::array<std::uint32_t, 3> kSeeds{
        1337u, 7331u, 20260722u};
    constexpr std::array<int, 3> kVisionHz{80, 100, 120};
    constexpr std::array<int, 5> kPhases{5, 25, 50, 75, 95};
    constexpr std::array<int, 3> kResultLatencyMs{8, 16, 28};
    constexpr std::array<int, 5> kResponseDelayMs{10, 25, 45, 70, 100};

    BlindBaselineSummary summary;
    summary.episodes.reserve(
        kSeeds.size() * kVisionHz.size() * kPhases.size() *
        kResultLatencyMs.size() * kResponseDelayMs.size());
    for (const std::uint32_t seed : kSeeds) {
        for (const int vision_hz : kVisionHz) {
            for (const int phase_percent : kPhases) {
                for (const int result_latency_ms : kResultLatencyMs) {
                    for (const int response_delay_ms : kResponseDelayMs) {
                        BlindTimingProfile timing;
                        timing.vision_period_us = 1'000'000 / vision_hz;
                        timing.event_phase_per_mille = phase_percent * 10;
                        timing.result_latency_us = result_latency_ms * 1'000;
                        timing.response_delay_us = response_delay_ms * 1'000;
                        const BlindFixture fixture =
                            bodylock_pending_crossing_fixture(seed, timing);
                        const BlindWindowRun run = run_blind_fixture(
                            fixture, stale_proportional_controller());
                        BlindEpisodeSummary episode;
                        episode.seed = seed;
                        episode.vision_hz = vision_hz;
                        episode.phase_percent = phase_percent;
                        episode.result_latency_ms = result_latency_ms;
                        episode.response_delay_ms = response_delay_ms;
                        episode.metrics = run.metrics;
                        if ((phase_percent == 5 || phase_percent == 25) &&
                            response_delay_ms >= 25 && response_delay_ms <= 70 &&
                            episode.metrics.harmful_pending_at_reveal_px > 0.5 &&
                            episode.metrics.reverse_correction_80_stick_ms > 0.0) {
                            summary.baseline_discriminating = true;
                        }
                        summary.episodes.push_back(episode);
                    }
                }
            }
        }
    }
    return summary;
}

namespace {

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 != 0) return values[middle];
    return (values[middle - 1] + values[middle]) * 0.5;
}

std::string escape_json(const std::string& value) {
    std::ostringstream output;
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') output << '\\';
        output << ch;
    }
    return output.str();
}

void write_metrics(std::ostream& output, const BlindWindowMetrics& metrics) {
    output
        << "\"blind_duration_ms\":" << metrics.blind_duration_ms
        << ",\"stale_ai_impulse_stick_ms\":"
        << metrics.stale_ai_impulse_stick_ms
        << ",\"harmful_ai_motion_px\":" << metrics.harmful_ai_motion_px
        << ",\"harmful_pending_at_reveal_px\":"
        << metrics.harmful_pending_at_reveal_px
        << ",\"future_burden_40_px_ms\":"
        << metrics.future_burden_40_px_ms
        << ",\"future_burden_80_px_ms\":"
        << metrics.future_burden_80_px_ms
        << ",\"future_burden_160_px_ms\":"
        << metrics.future_burden_160_px_ms
        << ",\"reverse_correction_80_stick_ms\":"
        << metrics.reverse_correction_80_stick_ms
        << ",\"reveal_to_reacquire_ms\":"
        << metrics.reveal_to_reacquire_ms
        << ",\"post_cross_area_px_ms\":"
        << metrics.post_cross_area_px_ms
        << ",\"user_fight_stick_ms\":"
        << metrics.user_fight_stick_ms
        << ",\"far_error_closing_speed_px_per_sec\":"
        << metrics.far_error_closing_speed_px_per_sec
        << ",\"output_total_variation\":"
        << metrics.output_total_variation
        << ",\"p95_output_delta\":" << metrics.p95_output_delta
        << ",\"incorrect_interruption_count\":"
        << metrics.incorrect_interruption_count
        << ",\"identity_authority_violations\":"
        << metrics.identity_authority_violations
        << ",\"future_dependency_violations\":"
        << metrics.future_dependency_violations;
}

void write_episode(std::ostream& output, const BlindEpisodeSummary& episode) {
    output << '{'
        << "\"seed\":" << episode.seed
        << ",\"vision_hz\":" << episode.vision_hz
        << ",\"phase_percent\":" << episode.phase_percent
        << ",\"result_latency_ms\":" << episode.result_latency_ms
        << ",\"response_delay_ms\":" << episode.response_delay_ms
        << ",\"metrics\":{";
    write_metrics(output, episode.metrics);
    output << "}}";
}

}  // namespace

std::string serialize_k1_baseline_json(
    const BlindBaselineSummary& summary,
    const std::string& revision,
    bool dirty) {
    std::ostringstream output;
    output << std::setprecision(17);
    std::vector<double> harmful_pending;
    std::vector<double> future_burden;
    std::vector<const BlindEpisodeSummary*> worst;
    int harmful_episode_count = 0;
    for (const BlindEpisodeSummary& episode : summary.episodes) {
        harmful_pending.push_back(
            episode.metrics.harmful_pending_at_reveal_px);
        future_burden.push_back(episode.metrics.future_burden_80_px_ms);
        if (episode.metrics.harmful_pending_at_reveal_px > 0.5) {
            ++harmful_episode_count;
        }
        worst.push_back(&episode);
    }
    std::stable_sort(
        worst.begin(), worst.end(),
        [](const BlindEpisodeSummary* left,
           const BlindEpisodeSummary* right) {
            return left->metrics.harmful_pending_at_reveal_px >
                right->metrics.harmful_pending_at_reveal_px;
        });
    if (worst.size() > 10) worst.resize(10);

    output << '{'
        << "\"schema\":\"vision_blind_window_baseline_v1\""
        << ",\"fixture_semantics_version\":"
        << kBlindWindowFixtureSemanticsVersion
        << ",\"revision\":\"" << escape_json(revision) << "\""
        << ",\"dirty\":" << (dirty ? "true" : "false")
        << ",\"policy_identity\":\"stale_proportional_fixture_baseline\""
        << ",\"production_controller\":false"
        << ",\"baseline_discriminating\":"
        << (summary.baseline_discriminating ? "true" : "false")
        << ",\"episode_count\":" << summary.episodes.size()
        << ",\"summary\":{"
        << "\"harmful_episode_count\":" << harmful_episode_count
        << ",\"median_harmful_pending_at_reveal_px\":"
        << median(harmful_pending)
        << ",\"median_future_burden_80_px_ms\":"
        << median(future_burden)
        << "},\"phase_summaries\":[";

    constexpr std::array<int, 5> kPhases{5, 25, 50, 75, 95};
    bool first = true;
    for (const int phase : kPhases) {
        std::vector<double> phase_pending;
        std::vector<double> phase_burden;
        for (const BlindEpisodeSummary& episode : summary.episodes) {
            if (episode.phase_percent != phase) continue;
            phase_pending.push_back(
                episode.metrics.harmful_pending_at_reveal_px);
            phase_burden.push_back(episode.metrics.future_burden_80_px_ms);
        }
        if (!first) output << ',';
        first = false;
        output << "{\"phase_percent\":" << phase
            << ",\"episode_count\":" << phase_pending.size()
            << ",\"median_harmful_pending_at_reveal_px\":"
            << median(phase_pending)
            << ",\"median_future_burden_80_px_ms\":"
            << median(phase_burden) << '}';
    }

    output << "],\"worst_episodes\":[";
    first = true;
    for (const BlindEpisodeSummary* episode : worst) {
        if (!first) output << ',';
        first = false;
        write_episode(output, *episode);
    }
    output << "],\"episodes\":[";
    first = true;
    for (const BlindEpisodeSummary& episode : summary.episodes) {
        if (!first) output << ',';
        first = false;
        write_episode(output, episode);
    }
    output << "]}\n";
    return output.str();
}

}  // namespace controller_native::blind_window
