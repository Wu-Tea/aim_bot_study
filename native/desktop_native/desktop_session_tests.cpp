#include "desktop_session.h"
#include "test_support/native_test_registry.h"
#include <fstream>
#include <stdexcept>

namespace {
struct Directory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("native-desktop-test-" + std::to_string(GetCurrentProcessId()));
    Directory() { std::filesystem::create_directories(path); }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Operation> void rejects(Operation operation) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid config/conflicting save/unsafe game must be rejected");
}
void config_save_preserves_user_data() {
    Directory directory;
    const auto config = directory.path / "config.toml";
    const std::string original = "# user comment\n[runtime.output]\nenabled=false\n";
    std::ofstream(config, std::ios::binary) << original;
    desktop_native::DesktopSession session(directory.path);
    require(session.read_config() == original, "config read must preserve exact contents");
    rejects([&] { session.save_config("[runtime.output]\nenabled=bad\n", "default"); });
    require(session.read_config() == original, "invalid save replaced user config");
    const std::string valid = "# changed comment\n[runtime.output]\nenabled=false\n";
    session.save_config(valid, "default");
    require(session.read_config() == valid, "valid config was not saved");
    const std::string external = "# external edit\n[runtime.output]\nenabled=false\n";
    std::ofstream(config, std::ios::binary) << external;
    rejects([&] { session.save_config(valid, "default"); });
    require(session.read_config() == external, "conflicting save overwrote external config");
    rejects([&] { session.start_command("default --unsafe"); });
    const auto native_path = (session.root() / "native/build/Release/cod_native_runtime.exe").make_preferred().wstring();
    require(session.start_command("default").find(native_path) != std::wstring::npos,
        "launch and process identity must use the same normalized executable path");
    require(!session.running(), "empty ownership record implies a runtime");
    rejects([&] { session.request_reload(); });
    session.stop();
    require(session.runtime_info() == "null", "stopped runtime info must be null");
    const std::string unicode = u8"曲线配置 / 手动控制";
    require(desktop_native::to_utf8(desktop_native::to_wide(unicode)) == unicode,
        "Unicode editor text must round trip without corruption");
}
} // namespace
void register_desktop_session_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "native_desktop_config_and_process_boundaries", config_save_preserves_user_data);
}
