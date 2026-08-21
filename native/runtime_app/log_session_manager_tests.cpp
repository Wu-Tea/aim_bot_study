#include "log_session_manager.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path temp_root() {
    return std::filesystem::temp_directory_path() /
        ("cod_log_session_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

void test_fresh_manifest_and_markers_are_atomic_session_contract() {
    const auto root = temp_root();
    {
        runtime_app::LogSessionOptions options;
        options.enabled = true;
        options.root = root;
        options.git_commit = "abc123";
        options.config_hash = "cfg456";
        options.executable_sha256 = "exe789";
        options.control_contract_sha256 = "control123";
        options.control_architecture_version = 2;
        options.control_event_schema_version = 1;
        runtime_app::LogSessionManager manager(std::move(options));
        require_true(manager.active(), "enabled manager must start a session");
        require_true(std::filesystem::exists(manager.session_directory() / ".active"),
                     "active session must have marker");
        require_true(std::filesystem::exists(manager.session_directory() / "session.json"),
                     "active session must have manifest");
        const auto session = read_all(manager.session_directory() / "session.json");
        require_true(session.find("causal_response_journal") == std::string::npos,
                     "session manifest must not advertise the retired causal journal");
        require_true(session.find("engine_hash") != std::string::npos &&
                         session.find("capture_width") != std::string::npos &&
                         session.find("tensor_width") != std::string::npos &&
                         session.find("require_isotropic_resize") != std::string::npos,
                     "session manifest must retain engine, crop and Tensor provenance");
        require_true(session.find("executable_sha256") != std::string::npos &&
                         session.find("exe789") != std::string::npos &&
                         session.find("control_contract_sha256") != std::string::npos &&
                         session.find("control123") != std::string::npos &&
                         session.find("control_architecture_version\": 2") != std::string::npos &&
                         session.find("control_event_schema_version\": 1") != std::string::npos &&
                         std::filesystem::exists(
                             manager.session_directory() / "session_metadata.json"),
                     "session metadata must record executable provenance without a path");
        const auto fresh = read_all(root / "fresh_session.json");
        require_true(fresh.find(manager.session_id()) != std::string::npos,
                     "fresh manifest must name the active session");
        require_true(fresh.find("sessions/") != std::string::npos,
                     "fresh manifest must contain a relative session path");
        manager.close();
        require_true(!std::filesystem::exists(manager.session_directory() / ".active"),
                     "clean close must remove active marker");
        require_true(std::filesystem::exists(manager.session_directory() / ".closed"),
                     "clean close must create closed marker");
    }
    std::filesystem::remove_all(root);
}

void test_sessions_are_unique_and_share_child_paths() {
    const auto root = temp_root();
    runtime_app::LogSessionOptions first_options;
    first_options.enabled = true;
    first_options.root = root;
    first_options.git_commit = "a";
    first_options.config_hash = "b";
    runtime_app::LogSessionManager first(std::move(first_options));
    const auto first_id = first.session_id();
    first.close();
    runtime_app::LogSessionOptions second_options;
    second_options.enabled = true;
    second_options.root = root;
    second_options.git_commit = "a";
    second_options.config_hash = "b";
    runtime_app::LogSessionManager second(std::move(second_options));
    require_true(second.session_id() != first_id, "each run must own a unique session");
    require_true(second.child_path("telemetry_0001.jsonl").parent_path() ==
                     second.session_directory(),
                 "all log writers must resolve into the same session");
    second.close();
    std::filesystem::remove_all(root);
}

}  // namespace

void register_log_session_manager_tests(native_test::Registry& registry) {
    registry.add_case("FeatureTelemetryAndDiagnostics", "manifest_and_markers_are_atomic", test_fresh_manifest_and_markers_are_atomic_session_contract);
    registry.add_case("FeatureTelemetryAndDiagnostics", "sessions_are_unique_and_share_child_paths", test_sessions_are_unique_and_share_child_paths);
}
