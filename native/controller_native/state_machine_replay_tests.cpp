#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"
#include "vision_native/target_selector.h"

#include <array>
#include <iomanip>

namespace {
struct Digest {
    std::uint64_t value = 14695981039346656037ull;
    template<class T> void add(T v) {
        const auto* p = reinterpret_cast<const unsigned char*>(&v);
        for (std::size_t n = 0; n < sizeof(T); ++n) value = (value ^ p[n]) * 1099511628211ull;
    }
};
void require(bool pass, const char* reason) { if (!pass) throw std::runtime_error(reason); }
std::uint32_t random_next(std::uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
// Inputs and oracles are frozen against the original implementation before
// state ownership changes. The independent validation seed is not a tuning set.
void replay(const native_test::TestContext& context, std::uint32_t seed) {
    using namespace controller_native;
    namespace fixture = controller_native::incident_fixture;
    const auto directory = context.artifact_directory / context.case_name;
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "traces.csv");
    require(bool(csv), "trace open failed");
    csv << "seed,hz,duration_ms,scenario,ticks,ads,bodylock,cue,waiting,hash\n";
    std::array<std::uint64_t, 4> coverage{};
    for (int hz : {160, 250, 1000, 2000}) for (int duration : {600, 1800, 10000}) {
        for (int scenario = 0; scenario != 8; ++scenario) {
            auto config = fixture::base_config(50.0f, 180.0f);
            config.auto_fire.enabled = true;
            config.auto_fire.manual_fire_activates_ai_aim = true;
            config.recoil.enabled = scenario % 2 != 0;
            config.ai_aim.ads_completion_fresh_frames = scenario % 2 ? 1000 : 3;
            double now = 100;
            NativeGamepadController controller(config, &now);
            fixture::TargetSpec target;
            target.has_enemy_cue = target.enemy_identity_confirmed = target.fire_authority = true;
            std::uint32_t rng = seed ^ (hz * 71u + duration * 13u + scenario + 1u);
            ControllerVisionSnapshot previous;
            std::uint64_t frame = 0;
            int next_source = 0;
            std::array<std::uint64_t, 4> counts{};
            Digest hash;
            const int ticks = hz * duration / 1000;
            for (int tick = 0; tick < ticks; ++tick) {
                const int ms = tick * 1000 / hz, phase = ms % 1200;
                now = 100.0 + double(tick) / hz;
                PhysicalGamepadState physical{};
                physical.connected = true;
                physical.left_trigger = phase < 30 ? 0.1f : phase < 1040 ? 1.0f : 0;
                physical.right_trigger = phase >= 800 && phase < 1120 ? 1.0f : 0;
                const float manual = (int(random_next(rng) % 1601) - 800) / 1000.0f;
                physical.right_x = phase >= 500 && phase < 800 ? manual : 0;
                physical.right_y = phase >= 650 && phase < 1000 ? -0.31f : 0;
                physical.left_x = scenario == 7 ? 0.45f : 0;
                if (ms >= next_source) {
                    next_source = ms + 4 + int(random_next(rng) % 10);
                    ++frame;
                    target.observation_id = frame * 100 + 1;
                    target.selector_generation = 7 + ms / 1200 + (scenario == 4 && phase >= 400 ? 1 : 0);
                    const float noise = (random_next(rng) % 100) / 100.0f;
                    const float dx = scenario % 2 ? 65 + noise : phase < 160 ? 65 : 1 + noise;
                    auto snapshot = fixture::observed_snapshot(target, frame, now, dx, 0);
                    snapshot.state.auto_fire_requested = true;
                    if ((phase >= 250 && phase < 275) || (scenario == 3 && phase >= 350 && phase < 440)) {
                        snapshot = fixture::empty_snapshot(target, frame, now);
                        snapshot.selector_target_generation = target.selector_generation;
                    } else if (scenario == 2 && phase >= 450 && phase < 480) {
                        snapshot = fixture::cue_snapshot(target, frame, now, dx, 0);
                    } else if (scenario == 5 && frame % 11 == 0) {
                        snapshot.capture_time_seconds = now - 0.07;
                    } else if (scenario == 6 && frame % 7 == 0) snapshot = previous;
                    if (!(scenario == 1 && phase >= 550 && phase < 630)) {
                        controller.submit_vision_snapshot(snapshot); previous = snapshot;
                    }
                }
                const auto output = controller.build_output(physical);
                const auto& p = controller.last_target_plan();
                const auto& c = controller.last_output_components();
                require(pipeline_contract::valid(p), "invalid plan");
                require(std::isfinite(output.right_x) && std::isfinite(output.right_y) &&
                    std::abs(output.right_x) <= 1 && std::abs(output.right_y) <= 1, "invalid output");
                require(!p.cue_continuation || (!p.fire_authority && !p.fire_requested), "cue fire authority");
                require(p.mode != pipeline_contract::ControlMode::BodyLockFollow || !p.ads_acquisition_active,
                    "BodyLock with active ADS");
                counts[0] += p.mode == pipeline_contract::ControlMode::AdsAcquire;
                counts[1] += p.mode == pipeline_contract::ControlMode::BodyLockFollow;
                counts[2] += p.cue_continuation;
                counts[3] += p.target_id == 0 && p.ads_acquisition_active;
                hash.add(p.target_id); hash.add(p.selector_target_generation); hash.add(p.source_frame_id);
                hash.add(p.mode); hash.add(p.lifecycle); hash.add(p.source_aim_px.x); hash.add(p.source_aim_px.y);
                hash.add(p.aim_px.x); hash.add(p.aim_px.y); hash.add(p.error_px.x); hash.add(p.error_px.y);
                hash.add(p.ads_acquisition_state); hash.add(p.ads_decision_reason); hash.add(p.ads_plan_admitted);
                hash.add(p.source_decision_outcome); hash.add(p.source_decision_reason);
                hash.add(p.acquisition_terminal_reason); hash.add(p.target_acquisition_id);
                hash.add(p.acquisition_elapsed_ms); hash.add(p.ads_epoch_elapsed_ms);
                hash.add(p.ads_acquisition_begin_ns); hash.add(p.ads_acquisition_complete_ns);
                hash.add(p.manual_exit_requested); hash.add(p.fire_authority); hash.add(p.fire_requested);
                hash.add(output.right_x); hash.add(output.right_y); hash.add(output.right_trigger); hash.add(output.rb);
                hash.add(c.requested_assist_stick.x); hash.add(c.requested_assist_stick.y);
                hash.add(c.shaped_assist_stick.x); hash.add(c.shaped_assist_stick.y);
                hash.add(c.handover_requested); hash.add(c.auto_fire_active);
            }
            csv << seed << ',' << hz << ',' << duration << ',' << scenario << ',' << ticks;
            for (std::size_t i = 0; i < counts.size(); ++i) { csv << ',' << counts[i]; coverage[i] += counts[i]; }
            csv << ',' << std::hex << hash.value << std::dec << '\n';
        }
    }
    for (auto count : coverage) require(count > 0, "missing replay trigger");
    require(bool(csv), "trace write failed");
}
}  // namespace

void register_state_machine_replay_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseEndToEnd", "state_machine_replay_development",
        [](const native_test::TestContext& c) { replay(c, 0x28ad5101u); });
    registry.add_context_case("BaseEndToEnd", "state_machine_replay_validation",
        [](const native_test::TestContext& c) { replay(c, 0xa19027d3u); });
}
