#include "log_session_manager.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace runtime_app {
namespace {

std::atomic<unsigned long long> g_session_nonce{0};

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    std::ostringstream text;
    text << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return text.str();
}

std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') result.push_back('\\');
        result.push_back(ch);
    }
    return result;
}

void atomic_replace(const std::filesystem::path& temporary, const std::filesystem::path& target) {
    if (!MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("failed to publish log session manifest");
    }
}

void write_text_atomic(const std::filesystem::path& path, const std::string& text) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) throw std::runtime_error("failed to open temporary manifest");
        output << text;
        output.flush();
        if (!output) throw std::runtime_error("failed to write temporary manifest");
    }
    atomic_replace(temporary, path);
}

}  // namespace

LogSessionManager::LogSessionManager(LogSessionOptions options)
    : options_(std::move(options)) {
    if (options_.enabled) start();
}

LogSessionManager::~LogSessionManager() {
    close();
}

void LogSessionManager::start() {
    std::filesystem::create_directories(options_.root / "sessions");
    const auto nonce = g_session_nonce.fetch_add(1, std::memory_order_relaxed) + 1;
    session_id_ = utc_timestamp() + "_" + std::to_string(GetCurrentProcessId()) + "_" +
        std::to_string(nonce);
    session_directory_ = options_.root / "sessions" / session_id_;
    std::filesystem::create_directories(session_directory_);
    std::ofstream(session_directory_ / ".active", std::ios::trunc).close();
    active_ = true;
    write_session_manifest("active");
    publish_fresh_manifest();
}

void LogSessionManager::write_session_manifest(const char* state) {
    std::ostringstream json;
    json << "{\n"
         << "  \"schema_version\": 2,\n"
         << "  \"session_id\": \"" << json_escape(session_id_) << "\",\n"
         << "  \"state\": \"" << state << "\",\n"
         << "  \"pid\": " << GetCurrentProcessId() << ",\n"
         << "  \"git_commit\": \"" << json_escape(options_.git_commit) << "\",\n"
         << "  \"config_hash\": \"" << json_escape(options_.config_hash) << "\",\n"
         << "  \"engine_hash\": \"" << json_escape(options_.engine_hash) << "\",\n"
         << "  \"executable_sha256\": \"" << json_escape(options_.executable_sha256) << "\",\n"
         << "  \"capture_width\": " << options_.capture_width << ",\n"
         << "  \"capture_height\": " << options_.capture_height << ",\n"
         << "  \"tensor_width\": " << options_.tensor_width << ",\n"
         << "  \"tensor_height\": " << options_.tensor_height << ",\n"
         << "  \"require_isotropic_resize\": "
         << (options_.require_isotropic_resize ? "true" : "false") << ",\n"
         << "  \"model_path\": \"" << json_escape(options_.model_path) << "\",\n"
         << "  \"causal_response_journal_schema\": \"causal_response_journal_v1\",\n"
         << "  \"updated_utc\": \"" << utc_timestamp() << "\"\n"
         << "}\n";
    write_text_atomic(session_directory_ / "session.json", json.str());

    std::ostringstream metadata;
    metadata << "{\n"
             << "  \"schema_version\": 1,\n"
             << "  \"session_id\": \"" << json_escape(session_id_) << "\",\n"
             << "  \"state\": \"" << state << "\",\n"
             << "  \"git_commit\": \"" << json_escape(options_.git_commit) << "\",\n"
             << "  \"config_hash\": \"" << json_escape(options_.config_hash) << "\",\n"
             << "  \"engine_hash\": \"" << json_escape(options_.engine_hash) << "\",\n"
             << "  \"executable_sha256\": \""
             << json_escape(options_.executable_sha256) << "\"\n"
             << "}\n";
    write_text_atomic(session_directory_ / "session_metadata.json", metadata.str());
}

void LogSessionManager::publish_fresh_manifest() {
    std::ostringstream json;
    json << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"session_id\": \"" << json_escape(session_id_) << "\",\n"
         << "  \"relative_path\": \"sessions/" << json_escape(session_id_) << "\"\n"
         << "}\n";
    write_text_atomic(options_.root / "fresh_session.json", json.str());
}

bool LogSessionManager::active() const noexcept {
    return active_;
}

const std::string& LogSessionManager::session_id() const noexcept {
    return session_id_;
}

const std::filesystem::path& LogSessionManager::session_directory() const noexcept {
    return session_directory_;
}

std::filesystem::path LogSessionManager::child_path(const std::filesystem::path& name) const {
    if (!active_) return {};
    if (name.is_absolute() || name.has_parent_path()) {
        throw std::invalid_argument("log child path must be a file name");
    }
    return session_directory_ / name;
}

void LogSessionManager::close() noexcept {
    if (!active_) return;
    try {
        write_session_manifest("closed");
        std::error_code error;
        std::filesystem::remove(session_directory_ / ".active", error);
        std::ofstream(session_directory_ / ".closed", std::ios::trunc).close();
    } catch (...) {
        return;
    }
    active_ = false;
}

}  // namespace runtime_app
