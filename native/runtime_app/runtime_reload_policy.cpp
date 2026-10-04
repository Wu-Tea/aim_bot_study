#include "runtime_reload_policy.h"

#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

namespace runtime_app {
namespace {

const std::set<std::string> hot_keys{
    "gamepad.ads.strength_scale", "gamepad.ads.vertical_strength_scale",
    "gamepad.bodylock.strength", "gamepad.bodylock.vertical_strength",
    "gamepad.ai_aim.hipfire_multiplier", "gamepad.ai_aim.aim_response_learning_enabled",
    "gamepad.ai_aim.aim_response_initial_scale",
    "gamepad.ai_aim.body_free_initial_scale", "gamepad.ai_aim.body_slow_initial_scale",
    "gamepad.ai_aim.ads_free_initial_scale", "gamepad.ai_aim.ads_slow_initial_scale",
    "gamepad.recoil.enabled", "gamepad.recoil.feedback_amount", "gamepad.recoil.hipfire_multiplier",
    "gamepad.auto_fire.fire_output", "gamepad.auto_fire.manual_fire_input",
    "gamepad.auto_fire.manual_fire_activates_ai_aim",
    "runtime.vision.friendly_filter_enabled", "runtime.vision.target_height_ratio",
    "runtime.vision.target_wide_low_height_ratio"};

// Walk the sorted maps once, including additions and removals. Both restart
// eligibility and learning retention use the same definition of a change.
std::vector<std::string> changed_keys(const controller_native::RuntimeConfig& before,
                                      const controller_native::RuntimeConfig& after) {
    std::vector<std::string> changed;
    auto a = before.effective_values.begin();
    auto b = after.effective_values.begin();
    while (a != before.effective_values.end() || b != after.effective_values.end()) {
        if (b == after.effective_values.end() ||
            (a != before.effective_values.end() && a->first < b->first)) {
            changed.push_back(a++->first);
        } else if (a == before.effective_values.end() || b->first < a->first) {
            changed.push_back(b++->first);
        } else {
            if (a->second != b->second) changed.push_back(a->first);
            ++a;
            ++b;
        }
    }
    return changed;
}

} // namespace

std::string hot_reload_restrictions(const controller_native::RuntimeConfig& before,
                                   const controller_native::RuntimeConfig& after) {
    if (!after.diagnostics.empty()) throw std::runtime_error(after.diagnostics.front());
    if (!before.vision.gpu_service_enabled &&
        (before.vision.friendly_filter_enabled != after.vision.friendly_filter_enabled ||
         before.vision.target_height_ratio != after.vision.target_height_ratio ||
         before.vision.target_wide_low_height_ratio != after.vision.target_wide_low_height_ratio))
        return "vision policy requires GPU service or restart";

    // Keep strict boundary validation: the general loader still has legacy
    // fallback parsers. Validate even unchanged hot values before committing.
    for (const auto& entry : after.effective_values) {
        const auto& key = entry.first;
        const auto& value = entry.second;
        if (!hot_keys.count(key)) continue;
        if (key.find("fire_output") != std::string::npos || key.find("manual_fire_input") != std::string::npos) continue;
        if (key.find("enabled") != std::string::npos || key.find("activates") != std::string::npos) {
            if (value != "true" && value != "false") throw std::runtime_error("invalid boolean: " + key);
        } else {
            std::size_t used = 0;
            const float number = std::stof(value, &used);
            if (used != value.size() || !std::isfinite(number)) throw std::runtime_error("invalid number: " + key);
        }
    }
    std::string restart;
    for (const auto& key : changed_keys(before, after)) {
        if (!hot_keys.count(key)) restart += (restart.empty() ? "" : ", ") + key;
    }
    return restart;
}

bool preserve_response_learning_on_reload(const controller_native::RuntimeConfig& before,
                                          const controller_native::RuntimeConfig& after) {
    const auto changed = changed_keys(before, after);
    if (changed.empty()) return false;
    // These controls change actuation/training admission, not game response.
    for (const auto& key : changed) {
        if (key != "gamepad.ai_aim.hipfire_multiplier" &&
            key != "gamepad.ai_aim.aim_response_learning_enabled") return false;
    }
    return true;
}

} // namespace runtime_app
