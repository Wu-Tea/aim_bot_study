#include "detection_haptics_policy.h"
#include "runtime_reload_policy.h"
#include "controller_native/runtime_config.h"
#include "test_support/native_test_registry.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <limits>
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
vision_native::VisionResult selected(unsigned ms, float x, unsigned generation = 1) {
    auto result = frame(ms, x);
    result.has_target = result.has_selected_detection = true;
    result.selector_target_generation = generation;
    result.target_x = x;
    return result;
}
void directions_and_admission() {
    DetectionHapticsPolicy rejected;
    require(rejected.observe(frame(1,30),600) == HapticDirection::None,
        "unselected raw person must not bypass canonical selector admission");
    for (auto pair : {std::pair<float,HapticDirection>{50.f,HapticDirection::Left},
                      {300.f,HapticDirection::Center}, {550.f,HapticDirection::Right}}) {
        DetectionHapticsPolicy policy;
        auto visible = selected(1, pair.first);
        // Idle observations are selected but have no actuator authority.
        require(!visible.aim_authority && policy.observe(visible,600) == pair.second,
            "canonical selection must own idle notification direction");
        require(policy.observe(selected(2,pair.first),600) == HapticDirection::None,
            "same selected identity must not repeat");
    }
    auto multiple = selected(2,20);
    multiple.detections.push_back(frame(2,320).detections[0]);
    require(rejected.observe(multiple,600) == HapticDirection::Left,
        "unselected central person must not override selected target direction");
    auto cue = selected(3,580,2); cue.has_selected_detection = false;
    require(rejected.observe(cue,600) == HapticDirection::None,"cue cannot acquire a notification");
    auto malformed = selected(4,580,2); malformed.target_x = std::numeric_limits<float>::quiet_NaN();
    require(rejected.observe(malformed,600) == HapticDirection::None,"invalid target position");
    auto no_identity = selected(5,580,0);
    require(rejected.observe(no_identity,600) == HapticDirection::None,"missing canonical identity");
    require(rejected.observe(selected(6,580,2),600) == HapticDirection::Right,
        "new canonical identity must notify during continuous person presence");
}
void absence_and_stalls() {
    DetectionHapticsPolicy policy;
    require(policy.observe(selected(1,20),600) == HapticDirection::Left,"initial selection");
    policy.observe(frame(20),600);
    require(policy.observe(selected(35,580),600) == HapticDirection::None,"same identity after missing frame");
    auto cached = selected(2000,580,2); cached.frame_updated = false;
    require(policy.observe(cached,600) == HapticDirection::None,"cached selection cannot notify");
    for(unsigned time=2010;time<=2290;time+=10) policy.observe(frame(time),600);
    require(policy.observe(selected(2300,580),600) == HapticDirection::None,"absence cannot invent selector identity");
    require(policy.observe(selected(2301,580,2),600) == HapticDirection::Right,"selector reacquisition notifies");
    require(policy.observe(selected(2301,50,3),600) == HapticDirection::None,"duplicate frame");
    require(policy.observe(selected(2200,50,3),600) == HapticDirection::None,"out of order frame");
    policy.reset();
    require(policy.observe(selected(1,300),600) == HapticDirection::Center,"device session reset");
}
void randomized(unsigned seed) {
    std::mt19937 rng(seed);
    DetectionHapticsPolicy policy;
    unsigned generation=1, notified=0;
    for(unsigned step=1;step<=100000;++step) {
        if(rng()%100 < 8) ++generation;
        const bool admitted = rng()%100 < 65;
        const bool fresh = rng()%100 < 90;
        const bool direct = rng()%100 < 95;
        auto input=selected(step*10,50.f,generation);
        input.has_target=admitted; input.frame_updated=fresh; input.has_selected_detection=direct;
        const bool expected=fresh && admitted && direct && generation != notified;
        if(expected) notified=generation;
        require((policy.observe(input,600)!=HapticDirection::None)==expected,
            "randomized selector identity notification mismatch");
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
