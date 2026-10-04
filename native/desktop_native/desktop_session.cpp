#include "desktop_session.h"
#include "controller_native/runtime_config.h"
#include <Windows.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include <memory>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace desktop_native {
namespace {
struct CloseHandleDeleter {
    void operator()(void* value) const noexcept {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;
struct ViewDeleter {
    void operator()(runtime_app::RuntimeControlMemory* value) const noexcept {
        if (value) UnmapViewOfFile(value);
    }
};
using View = std::unique_ptr<runtime_app::RuntimeControlMemory, ViewDeleter>;
struct Instance {
    DWORD version = 1, pid = 0;
    std::uint64_t created = 0;
    char fusion_session[128]{};
    bool fusion_enabled = false;
};
std::filesystem::path record_path(const std::filesystem::path& root) {
    return root / "runs/desktop/native-runtime.bin";
}
std::wstring executable_path(const std::filesystem::path& root) {
    return (root / "native/build/Release/cod_native_runtime.exe").lexically_normal().make_preferred().wstring();
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read file: " + path.u8string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || !stream.write(text.data(), static_cast<std::streamsize>(text.size())) || !stream.flush())
        throw std::runtime_error("cannot write file: " + path.u8string());
}
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};
TemporaryFile config_candidate(const std::filesystem::path& root, const std::string& text) {
    static std::atomic<unsigned int> sequence{0};
    auto path = root / (".config-edit-" + std::to_string(GetCurrentProcessId()) + "-" +
        std::to_string(++sequence) + ".toml");
    write_file(path, text);
    return {path};
}
std::uint64_t creation_time(HANDLE process) {
    FILETIME created{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exit, &kernel, &user))
        throw std::runtime_error("cannot identify runtime process");
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
Instance read_instance(const std::filesystem::path& root) {
    Instance instance;
    std::ifstream stream(record_path(root), std::ios::binary);
    if (!stream) return {};
    if (!stream.read(reinterpret_cast<char*>(&instance), sizeof(instance)) || instance.version != 1)
        throw std::runtime_error("invalid native runtime ownership record");
    return instance;
}
Handle open_owned_process(const std::filesystem::path& root, const Instance& instance) {
    if (!instance.pid) return {};
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, instance.pid));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return {};
        throw std::runtime_error("cannot query owned runtime process");
    }
    if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) return {};
    if (creation_time(process.get()) != instance.created) return {};
    std::vector<wchar_t> path(32768);
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size))
        throw std::runtime_error("cannot verify runtime executable");
    if (_wcsicmp(path.data(), executable_path(root).c_str()) != 0)
        throw std::runtime_error("ownership record points to another executable");
    return process;
}
class Channel {
public:
    explicit Channel(const Instance& instance) : instance_(instance) {
        const auto name = L"Local\\cod_native_control_" + std::to_wstring(instance.pid);
        mapping_.reset(OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str()));
        if (!mapping_) throw std::runtime_error("runtime control channel is not ready");
        view_.reset(static_cast<runtime_app::RuntimeControlMemory*>(MapViewOfFile(
            mapping_.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(runtime_app::RuntimeControlMemory))));
        if (!view_) throw std::runtime_error("cannot map runtime control channel");
    }
    runtime_app::RuntimeControlSnapshot read() const {
        for (int retry = 0; retry < 8; ++retry) {
            const LONG sequence = InterlockedCompareExchange(&view_->sequence, 0, 0);
            if (sequence & 1) continue;
            const auto result = view_->snapshot;
            MemoryBarrier();
            if (sequence != InterlockedCompareExchange(&view_->sequence, 0, 0)) continue;
            if (view_->protocol != 1 || result.pid != instance_.pid || result.created != instance_.created)
                throw std::runtime_error("runtime channel identity/protocol mismatch");
            return result;
        }
        throw std::runtime_error("runtime control channel is being updated");
    }
    void reload() {
        if (read().status == runtime_app::Pending)
            throw std::runtime_error("previous reload is still pending");
        Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE,
            (L"Local\\cod_native_reload_" + std::to_wstring(instance_.pid)).c_str()));
        if (!event) throw std::runtime_error("cannot open runtime reload event");
        const LONG64 request = static_cast<LONG64>((GetTickCount64() << 20) | (GetCurrentProcessId() & 0xfffff));
        InterlockedExchange64(&view_->requested_id, request);
        if (!SetEvent(event.get())) throw std::runtime_error("cannot signal runtime reload");
    }
private:
    Instance instance_;
    Handle mapping_;
    View view_;
};
} // namespace

std::wstring to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("configuration text is not UTF-8");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
DesktopSession::DesktopSession(std::filesystem::path root)
    : root_(std::filesystem::weakly_canonical(std::filesystem::absolute(std::move(root))).make_preferred()) {}
std::string DesktopSession::read_config() {
    original_text_ = read_file(root_ / "config.toml");
    config_loaded_ = true;
    return original_text_;
}
void DesktopSession::validate_config(const std::string& text, const std::string& game) const {
    auto candidate = config_candidate(root_, text);
    (void)controller_native::load_runtime_config(candidate.path, {}, game);
}
void DesktopSession::save_config(const std::string& text, const std::string& game) {
    if (!config_loaded_) throw std::runtime_error("read configuration before saving");
    auto candidate = config_candidate(root_, text);
    (void)controller_native::load_runtime_config(candidate.path, {}, game);
    // The editor preserves exact file contents and detects concurrent user edits.
    // Native file replacement is atomic; failed validation never replaces config.
    if (read_file(root_ / "config.toml") != original_text_)
        throw std::runtime_error("configuration changed externally; reload before saving");
    if (!MoveFileExW(candidate.path.c_str(), (root_ / "config.toml").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot replace configuration");
    original_text_ = text;
}
std::wstring DesktopSession::start_command(const std::string& game) const {
    if (game.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos || game.empty())
        throw std::runtime_error("invalid game name");
    return L"\"" + executable_path(root_) + L"\" --config \"" +
        (root_ / "config.toml").wstring() + L"\" --game " + to_wide(game);
}
bool DesktopSession::running() const {
    const auto instance = read_instance(root_);
    return static_cast<bool>(open_owned_process(root_, instance));
}
void DesktopSession::start(const std::string& game) {
    // An exclusive record handle serializes startup across GUI/CLI instances.
    const auto record = record_path(root_);
    std::filesystem::create_directories(record.parent_path());
    Handle file(CreateFileW(record.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file || file.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("runtime ownership record is in use");
    Instance previous{};
    DWORD size = 0;
    if (!ReadFile(file.get(), &previous, sizeof(previous), &size, nullptr))
        throw std::runtime_error("cannot read ownership record");
    if (size && (size != sizeof(previous) || previous.version != 1))
        throw std::runtime_error("invalid runtime ownership record");
    if (size && open_owned_process(root_, previous)) throw std::runtime_error("runtime is already running");
    const auto config = controller_native::load_runtime_config(root_ / "config.toml", {}, game);
    auto command = start_command(game);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle log(CreateFileW((record.parent_path() / "native-runtime.log").c_str(),
        FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!log || log.get() == INVALID_HANDLE_VALUE || !input || input.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot open native runtime logs");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get(); startup.hStdOutput = log.get(); startup.hStdError = log.get();
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable_path(root_).c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, root_.c_str(), &startup, &info))
        throw std::runtime_error("cannot start native runtime");
    Handle process(info.hProcess), thread(info.hThread);
    struct StartupOwnership {
        HANDLE process;
        bool committed = false;
        ~StartupOwnership() { if (!committed) TerminateProcess(process, 1); }
    } ownership{process.get()};
    Instance instance{1, info.dwProcessId, creation_time(process.get())};
    if (config.vision.fusion_session.size() >= sizeof(instance.fusion_session))
        throw std::runtime_error("Fusion session name is too long");
    strcpy_s(instance.fusion_session, config.vision.fusion_session.c_str());
    instance.fusion_enabled = config.vision.fusion_enabled;
    SetFilePointer(file.get(), 0, nullptr, FILE_BEGIN);
    if (!WriteFile(file.get(), &instance, sizeof(instance), &size, nullptr) || size != sizeof(instance) ||
        !SetEndOfFile(file.get()) || !FlushFileBuffers(file.get())) {
        throw std::runtime_error("cannot record newly started runtime ownership");
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1))
        throw std::runtime_error("cannot resume newly started runtime");
    ownership.committed = true;
    const auto deadline = GetTickCount64() + 15000;
    const auto channel_name = L"Local\\cod_native_control_" + std::to_wstring(instance.pid);
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(process.get(), 50) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(process.get(), &exit_code);
            throw std::runtime_error("native runtime exited during initialization (" +
                std::to_string(exit_code) + "); see runs/desktop/native-runtime.log");
        }
        Handle channel(OpenFileMappingW(FILE_MAP_READ, FALSE, channel_name.c_str()));
        if (channel) {
            (void)Channel(instance).read();
            return;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("cannot query runtime startup control channel");
    }
    throw std::runtime_error("native runtime startup is still pending; inspect status/log before restarting");
}
void DesktopSession::stop() {
    const auto instance = read_instance(root_);
    auto process = open_owned_process(root_, instance);
    if (!process) return;
    Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE,
        (L"Local\\cod_native_runtime_stop_" + std::to_wstring(instance.pid)).c_str()));
    if (!event || !SetEvent(event.get())) throw std::runtime_error("runtime stop event is not ready");
    if (WaitForSingleObject(process.get(), 15000) != WAIT_OBJECT_0)
        throw std::runtime_error("runtime has not finished shutdown");
    // Keep the identity record for audit/debugging; a exited identity is inert.
}
runtime_app::RuntimeControlSnapshot DesktopSession::snapshot() const {
    const auto instance = read_instance(root_);
    auto process = open_owned_process(root_, instance);
    if (!process) throw std::runtime_error("native runtime is not running");
    return Channel(instance).read();
}
void DesktopSession::request_reload() {
    const auto instance = read_instance(root_);
    auto process = open_owned_process(root_, instance);
    if (!process) throw std::runtime_error("native runtime is not running");
    Channel(instance).reload();
}
std::string DesktopSession::runtime_info() const {
    const auto instance = read_instance(root_);
    if (!open_owned_process(root_, instance)) return "null";
    std::ostringstream result;
    result << "{\"process_id\":" << instance.pid << ",\"process_created\":" << instance.created
        << ",\"executable_path\":" << std::quoted(to_utf8(executable_path(root_)))
        << ",\"fusion_channel_enabled\":" << (instance.fusion_enabled ? "true" : "false")
        << ",\"fusion_session\":" << std::quoted(instance.fusion_session) << '}';
    return result.str();
}
} // namespace desktop_native
