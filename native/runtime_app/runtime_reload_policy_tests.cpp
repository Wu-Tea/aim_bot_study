#include "runtime_reload_policy.h"
#include "runtime_control_bridge.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void test_reload_additions_removals_and_unchanged_validation() {
    controller_native::RuntimeConfig before, after;
    before.effective_values = {{"a.structural", "1"}, {"gamepad.ai_aim.hipfire_multiplier", "1"}};
    after.effective_values = {{"gamepad.ai_aim.hipfire_multiplier", "0.5"}, {"z.structural", "1"}};
    require(runtime_app::hot_reload_restrictions(before, after) == "a.structural, z.structural",
            "both removed and added structural settings require restart in key order");
    require(!runtime_app::preserve_response_learning_on_reload(before, after),
            "mixed structural and learning-safe changes cannot retain learning");
    before.effective_values.erase("a.structural");
    after.effective_values.erase("z.structural");
    require(runtime_app::hot_reload_restrictions(before, after).empty() &&
            runtime_app::preserve_response_learning_on_reload(before, after),
            "a learning-safe change alone remains hot eligible");
    require(!runtime_app::preserve_response_learning_on_reload(after, after),
            "a no-op reload keeps the existing reset contract");
    after.effective_values["gamepad.recoil.enabled"] = "invalid";
    bool rejected = false;
    try { runtime_app::hot_reload_restrictions(after, after); }
    catch (const std::exception&) { rejected = true; }
    require(rejected, "unchanged invalid hot settings must not evade boundary validation");
}

void test_control_channel_releases_pending_transaction() {
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    const auto mapping_name = L"Local\\cod_native_control_" + suffix;
    const auto request_name = L"Local\\cod_native_reload_" + suffix;
    for (int round = 0; round < 8; ++round) {
        {
            controller_native::RuntimeConfig config;
            runtime_app::RuntimeControlBridge bridge(config, [config] { return config; });
            HANDLE request = OpenEventW(EVENT_MODIFY_STATE, FALSE, request_name.c_str());
            require(request != nullptr, "request event must exist during channel lifetime");
            const bool sent = SetEvent(request) != FALSE;
            CloseHandle(request);
            require(sent, "reload request must reach worker");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            bool pending = false;
            while (!pending && std::chrono::steady_clock::now() < deadline) {
                pending = bool(bridge.take_prepared());
                std::this_thread::yield();
            }
            require(pending, "worker must prepare transaction before shutdown");
            // No tick commits this transaction. Destruction must still wake
            // and join the worker, then release all named objects.
        }
        HANDLE request = OpenEventW(EVENT_MODIFY_STATE, FALSE, request_name.c_str());
        if (request) CloseHandle(request);
        require(request == nullptr, "request event must not outlive bridge");
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
        if (mapping) CloseHandle(mapping);
        require(mapping == nullptr, "mapping must not outlive bridge");
    }
}

} // namespace

void register_runtime_reload_policy_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "reload_policy_transaction_boundaries", test_reload_additions_removals_and_unchanged_validation);
    registry.add_case("BaseContracts", "control_channel_pending_shutdown", test_control_channel_releases_pending_transaction);
}
