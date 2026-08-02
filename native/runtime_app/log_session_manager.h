#pragma once

#include <filesystem>
#include <string>

namespace runtime_app {

struct LogSessionOptions {
    bool enabled = false;
    std::filesystem::path root = "runs/native_perf";
    std::string git_commit;
    std::string config_hash;
    std::string engine_hash;
    std::string executable_sha256;
    int capture_width = 0;
    int capture_height = 0;
    int tensor_width = 0;
    int tensor_height = 0;
    bool require_isotropic_resize = true;
    std::string model_path;
};

class LogSessionManager {
public:
    explicit LogSessionManager(LogSessionOptions options);
    ~LogSessionManager();

    LogSessionManager(const LogSessionManager&) = delete;
    LogSessionManager& operator=(const LogSessionManager&) = delete;

    bool active() const noexcept;
    const std::string& session_id() const noexcept;
    const std::filesystem::path& session_directory() const noexcept;
    std::filesystem::path child_path(const std::filesystem::path& name) const;
    void close() noexcept;

private:
    void start();
    void write_session_manifest(const char* state);
    void publish_fresh_manifest();

    LogSessionOptions options_{};
    std::string session_id_;
    std::filesystem::path session_directory_;
    bool active_ = false;
};

}  // namespace runtime_app
