#pragma once
#include "runtime_app/runtime_control_protocol.h"
#include <filesystem>
#include <string>

namespace desktop_native {
// UI and command line share one file/process/IPC owner; neither owns gameplay.
class DesktopSession {
public:
    explicit DesktopSession(std::filesystem::path root);
    std::string read_config();
    void save_config(const std::string& text, const std::string& game);
    void validate_config(const std::string& text, const std::string& game) const;
    std::wstring start_command(const std::string& game) const;
    void start(const std::string& game);
    void stop();
    bool running() const;
    runtime_app::RuntimeControlSnapshot snapshot() const;
    std::string runtime_info() const;
    void request_reload();
    const std::filesystem::path& root() const { return root_; }
private:
    std::filesystem::path root_;
    std::string original_text_;
    bool config_loaded_ = false;
};
std::wstring to_wide(const std::string& value);
std::string to_utf8(const std::wstring& value);
} // namespace desktop_native
