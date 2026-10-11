#include "detection_haptics_policy.h"
#include "runtime_reload_policy.h"
#include "controller_native/runtime_config.h"
#include "test_support/native_test_registry.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace {
using namespace runtime_app;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
vision_native::VisionResult frame(unsigned milliseconds, float center = -1) {
    vision_native::VisionResult result;
    result.frame_updated = true; result.frame_id = milliseconds;
    result.captured_at_ns = std::uint64_t(milliseconds) * 1'000'000;
    if (center >= 0) {
        vision_native::Detection box; box.x1 = center - 5; box.x2 = center + 5;
        box.y1 = 20; box.y2 = 100; box.conf = .9f;
        result.detections.push_back(box);
    }
    return result;
}
void directions_and_admission() {
    for (auto pair : {std::pair<float,HapticDirection>{50.f,HapticDirection::Left},
                      {300.f,HapticDirection::Center}, {550.f,HapticDirection::Right}}) {
        DetectionHapticsPolicy policy;
        auto visible = frame(1, pair.first);
        require(!visible.has_target && policy.observe(visible,600) == pair.second, "raw person detection must not require a selected aim target");
        require(policy.observe(frame(2,pair.first),600) == HapticDirection::None, "continuous presence repeats");
    }
    DetectionHapticsPolicy policy;
    auto invalid = frame(1,30); invalid.detections[0].is_friendly = true;
    require(policy.observe(invalid,600) == HapticDirection::None, "friendly notification");
    invalid = frame(2,30); invalid.detections[0].class_id = 1;
    require(policy.observe(invalid,600) == HapticDirection::None, "non-person notification");
    invalid = frame(3,30); invalid.detections[0].x2 = std::numeric_limits<float>::quiet_NaN();
    require(policy.observe(invalid,600) == HapticDirection::None, "invalid box notification");
    auto multiple = frame(4,20); multiple.detections.push_back(frame(4,320).detections[0]);
    require(policy.observe(multiple,600) == HapticDirection::Center, "nearest-to-center person must own combined notification");
}
void absence_and_stalls() {
    DetectionHapticsPolicy policy;
    require(policy.observe(frame(1,20),600) == HapticDirection::Left,"initial appearance");
    policy.observe(frame(20),600);
    require(policy.observe(frame(35,580),600) == HapticDirection::None,"one missing frame / handover must not repeat");
    policy.observe(frame(50),600);
    auto cached = frame(2000); cached.frame_updated = false;
    policy.observe(cached,600);
    policy.observe(frame(2010),600);
    require(policy.observe(frame(2020,300),600) == HapticDirection::None,"stall must not count as confirmed empty time");
    for (unsigned time=2030; time<=2290; time+=10) policy.observe(frame(time),600);
    require(policy.observe(frame(2300,580),600) == HapticDirection::Right,"confirmed absence re-arms");
    require(policy.observe(frame(2300,580),600) == HapticDirection::None,"duplicate frame");
    require(policy.observe(frame(2200,580),600) == HapticDirection::None,"out of order frame");
    policy.reset();
    require(policy.observe(frame(1,300),600) == HapticDirection::Center,"device session reset");
}
void randomized(unsigned seed) {
    std::mt19937 rng(seed);
    DetectionHapticsPolicy policy;
    bool armed=true; unsigned empty_frames=0;
    for(unsigned step=1;step<=100000;++step) {
        const bool present = rng()%100 < 6;
        const bool expected=present && armed;
        if(present) { armed=false; empty_frames=0; }
        else { ++empty_frames; if(empty_frames>=26) armed=true; }
        auto result=policy.observe(frame(step*10,present?50.f:-1.f),600);
        require((result!=HapticDirection::None)==expected,"randomized occupancy lifecycle mismatch");
    }
}
void waveform_contract() {
    float peak=0;
    for(unsigned sample=0;sample<6000;++sample) {
        const auto low=detection_haptic_sample(sample/48000.0,.24f);
        const auto high=detection_haptic_sample(sample/48000.0,.48f);
        require(std::isfinite(low) && std::abs(low)<=.24001f,"bounded pulse");
        require(std::abs(high-2*low)<1e-6,"linear strength");
        if(sample>=4800) require(low==0,"pulse ends at 100 ms");
        peak=std::max(peak,std::abs(low));
    }
    require(peak>.23f,"nonzero pulse amplitude");
    for(auto direction:{HapticDirection::None,HapticDirection::Left,HapticDirection::Center,HapticDirection::Right}) {
        require(!haptic_channel_active(direction,0) && !haptic_channel_active(direction,1),"speaker channels must remain silent");
    }
    require(haptic_channel_active(HapticDirection::Center,2) && haptic_channel_active(HapticDirection::Center,3),"center uses both");
    require(haptic_channel_active(HapticDirection::Left,2) && !haptic_channel_active(HapticDirection::Left,3),"left routing");
    require(haptic_channel_active(HapticDirection::Right,3) && !haptic_channel_active(HapticDirection::Right,2),"right routing");
}
void config_contract() {
    const auto path=std::filesystem::temp_directory_path()/"cod_detection_haptics_test.toml";
    struct Remove { std::filesystem::path p; ~Remove(){std::error_code ec;std::filesystem::remove(p,ec);} } remove{path};
    auto load=[&](const char* value){std::ofstream file(path);file<<value;file.close();return controller_native::load_runtime_config(path);};
    auto before=load("[gamepad.detection_haptics]\nenabled=false\nstrength=0.24\n");
    auto after=load("[gamepad.detection_haptics]\nenabled=true\nstrength=0.5\n");
    require(!before.gamepad.detection_haptics.enabled && after.gamepad.detection_haptics.enabled,"boolean parse");
    require(after.gamepad.detection_haptics.strength==.5f,"strength parse");
    require(hot_reload_restrictions(before,after).empty(),"notification changes require no restart");
    require(haptics_only_reload(before,after) && preserve_response_learning_on_reload(before,after),"notification reload must preserve controller state and learning");
    after.effective_values["gamepad.recoil.enabled"]="false";
    require(!haptics_only_reload(before,after),"mixed reload must still apply control changes");
    for(auto text:{"[gamepad.detection_haptics]\nenabled=invalid\n", "[gamepad.detection_haptics]\nstrength=1.01\n", "[gamepad.detection_haptics]\nstrength=nan\n"}) {
        bool rejected=false;try{load(text);}catch(const std::exception&){rejected=true;}
        require(rejected,"invalid haptic config must fail");
    }
}
}
void register_detection_haptics_tests(native_test::Registry& registry) {
    registry.add_case("FeatureAutoFireAndMarker","detection_haptics_directions_and_admission",directions_and_admission);
    registry.add_case("FeatureAutoFireAndMarker","detection_haptics_absence_and_stalls",absence_and_stalls);
    registry.add_case("FeatureAutoFireAndMarker","detection_haptics_random_development",[]{randomized(20261012);});
    registry.add_case("FeatureAutoFireAndMarker","detection_haptics_random_validation",[]{randomized(0x95ab4731);});
    registry.add_case("FeatureAutoFireAndMarker","detection_haptics_waveform",waveform_contract);
    registry.add_case("FeatureAutoFireAndMarker","detection_haptics_config_reload",config_contract);
}
