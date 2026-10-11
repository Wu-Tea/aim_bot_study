#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "dse_haptics.h"
#include <iostream>
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace runtime_app {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char message[160];
        sprintf_s(message, "%s HRESULT=0x%08lX", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(message);
    }
}
GUID physical_container(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower.find("vid_054c&pid_0df2&mi_03") == std::string::npos)
        throw std::runtime_error("selected input is not a USB DualSense Edge");
    HDEVINFO devices = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (devices == INVALID_HANDLE_VALUE) throw std::runtime_error("device information unavailable");
    struct Cleanup { HDEVINFO value; ~Cleanup() { SetupDiDestroyDeviceInfoList(value); } } cleanup{devices};
    SP_DEVICE_INTERFACE_DATA interface_data{sizeof(SP_DEVICE_INTERFACE_DATA)};
    if (!SetupDiOpenDeviceInterfaceA(devices, path.c_str(), 0, &interface_data))
        throw std::runtime_error("selected USB interface unavailable");
    DWORD size = 0;
    SetupDiGetDeviceInterfaceDetailA(devices, &interface_data, nullptr, 0, &size, nullptr);
    std::vector<BYTE> bytes(size);
    if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A)) throw std::runtime_error("invalid USB interface detail");
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(bytes.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    SP_DEVINFO_DATA device{sizeof(SP_DEVINFO_DATA)};
    if (!SetupDiGetDeviceInterfaceDetailA(devices, &interface_data, detail, size, nullptr, &device))
        throw std::runtime_error("USB device identity unavailable");
    GUID container{}; DEVPROPTYPE type = 0;
    if (!SetupDiGetDevicePropertyW(devices, &device, &DEVPKEY_Device_ContainerId, &type,
            reinterpret_cast<BYTE*>(&container), sizeof(container), nullptr, 0) ||
        type != DEVPROP_TYPE_GUID || IsEqualGUID(container, GUID_NULL))
        throw std::runtime_error("USB container identity unavailable");
    return container;
}

struct EndpointLevel {
    ComPtr<IAudioEndpointVolume> volume;
    float original = 0;
    bool raised = false;
    ~EndpointLevel() {
        if (!raised) return;
        float current = 0;
        // Do not overwrite a volume change made by the user while running.
        if (SUCCEEDED(volume->GetMasterVolumeLevelScalar(&current)) && std::abs(current - .5f) < .00001f) {
            const HRESULT hr = volume->SetMasterVolumeLevelScalar(original, nullptr);
            if (FAILED(hr)) std::clog << "[DseHaptics] endpoint volume restoration failed\n";
        }
    }
};

class AudioStream {
public:
    explicit AudioStream(const std::string& path) {
        const GUID identity = physical_container(path);
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "enumerator");
        ComPtr<IMMDeviceCollection> collection;
        check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection), "endpoints");
        UINT count = 0; check(collection->GetCount(&count), "endpoint count");
        ComPtr<IMMDevice> selected; unsigned matches = 0;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device; check(collection->Item(i, &device), "endpoint");
            ComPtr<IPropertyStore> properties; check(device->OpenPropertyStore(STGM_READ, &properties), "properties");
            PROPVARIANT container{};
            const auto hr = properties->GetValue(PKEY_Device_ContainerId, &container);
            const bool same = SUCCEEDED(hr) && container.vt == VT_CLSID && container.puuid && IsEqualGUID(*container.puuid, identity);
            PropVariantClear(&container);
            if (same) { selected = device; ++matches; }
        }
        if (matches != 1) throw std::runtime_error("no unique active audio endpoint for selected DSE");
        check(selected->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_), "audio client");
        WAVEFORMATEX* allocated = nullptr;
        check(client_->GetMixFormat(&allocated), "mix format");
        std::vector<BYTE> format_bytes(sizeof(WAVEFORMATEX) + allocated->cbSize);
        memcpy(format_bytes.data(), allocated, format_bytes.size()); CoTaskMemFree(allocated);
        auto* format = reinterpret_cast<WAVEFORMATEX*>(format_bytes.data());
        floating_ = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        bool pcm = format->wFormatTag == WAVE_FORMAT_PCM;
        DWORD mask = 0;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
            const auto* extended = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
            floating_ = IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
            pcm = IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
            mask = extended->dwChannelMask;
        }
        if (format->nChannels != 4 || format->nSamplesPerSec != 48000 ||
            (mask != 0 && mask != 0x33) || (!floating_ && !pcm) ||
            (floating_ && format->wBitsPerSample != 32) || (pcm && format->wBitsPerSample != 16) ||
            format->nBlockAlign != 4 * format->wBitsPerSample / 8)
            throw std::runtime_error("DSE requires a four-channel 48 kHz float32/PCM16 endpoint");
        rate_ = format->nSamplesPerSec; block_ = format->nBlockAlign;
        check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, nullptr), "initialize");
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) throw std::runtime_error("audio event unavailable");
        // Use a member handle owner so initialization failures release the event too.
        check(client_->SetEventHandle(event_.get()), "set audio event");
        check(client_->GetBufferSize(&capacity_), "buffer size");
        check(client_->GetService(IID_PPV_ARGS(&render_)), "render service");
        check(selected->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &level_.volume), "volume interface");
        BOOL mute = FALSE; check(level_.volume->GetMute(&mute), "mute read");
        if (mute) throw std::runtime_error("DSE audio is muted; unmute it to enable feedback");
        check(level_.volume->GetMasterVolumeLevelScalar(&level_.original), "volume read");
        if (level_.original == 0) {
            check(level_.volume->SetMasterVolumeLevelScalar(.5f, nullptr), "temporary volume");
            level_.raised = true;
        }
        fill(HapticDirection::None, 0, true);
        check(client_->Start(), "start");
    }
    ~AudioStream() { if (client_) { client_->Stop(); client_->Reset(); } }
    HANDLE event() const { return event_.get(); }
    void fill(HapticDirection direction, float strength, bool initial = false) {
        if (direction != HapticDirection::None) { direction_ = direction; cursor_ = 0; }
        UINT padding = 0;
        if (!initial) check(client_->GetCurrentPadding(&padding), "padding");
        if (padding >= capacity_) return;
        const UINT frames = capacity_ - padding;
        BYTE* bytes = nullptr; check(render_->GetBuffer(frames, &bytes), "buffer");
        memset(bytes, 0, frames * block_);
        for (UINT i = 0; i < frames && direction_ != HapticDirection::None; ++i, ++cursor_) {
            if (cursor_ >= rate_ / 10) { direction_ = HapticDirection::None; break; }
            const auto sample = detection_haptic_sample(double(cursor_) / rate_, strength);
            for (unsigned channel = 2; channel <= 3; ++channel) {
                if (!haptic_channel_active(direction_, channel)) continue;
                if (floating_) reinterpret_cast<float*>(bytes)[i * 4 + channel] = sample;
                else reinterpret_cast<std::int16_t*>(bytes)[i * 4 + channel] = static_cast<std::int16_t>(sample * 32767);
            }
        }
        check(render_->ReleaseBuffer(frames, 0), "submit");
    }
private:
    struct Handle {
        HANDLE value = nullptr;
        ~Handle() { if (value) CloseHandle(value); }
        Handle& operator=(HANDLE h) { value = h; return *this; }
        explicit operator bool() const { return value != nullptr; }
        HANDLE get() const { return value; }
    } event_;
    EndpointLevel level_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    UINT rate_ = 0, capacity_ = 0, block_ = 0, cursor_ = 0;
    bool floating_ = false;
    HapticDirection direction_ = HapticDirection::None;
};
} // namespace

DseHaptics::DseHaptics() {
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wake_) return;
    try { worker_ = std::thread([this] { run(); }); }
    catch (...) { CloseHandle(wake_); wake_ = nullptr; throw; }
}
DseHaptics::~DseHaptics() { stop(); if (wake_) CloseHandle(wake_); }
void DseHaptics::stop() noexcept {
    stopping_.store(true); ready_.store(false); pending_.store(0);
    if (wake_) SetEvent(wake_);
    if (worker_.joinable()) worker_.join();
}
void DseHaptics::configure(bool enabled, float strength) noexcept {
    const unsigned next = enabled && std::isfinite(strength) ?
        static_cast<unsigned>(std::lround(std::clamp(strength, 0.0f, 1.0f) * 10000)) : 0;
    const auto previous = settings_.exchange(next);
    if (!next) { ready_.store(false); pending_.store(0); }
    if (previous != next && wake_) SetEvent(wake_);
}
void DseHaptics::bind_device(const std::string& path) {
    if (path == input_owner_path_) return;
    input_owner_path_ = path;
    ready_.store(false); pending_.store(0);
    std::atomic_store(&device_path_, std::make_shared<const std::string>(path));
    if (wake_) SetEvent(wake_);
}
void DseHaptics::notify(HapticDirection direction) noexcept {
    if (direction == HapticDirection::None || !ready() || !settings_.load()) return;
    pending_.store((GetTickCount64() << 2) | static_cast<unsigned>(direction));
    if (wake_) SetEvent(wake_);
}
void DseHaptics::run() noexcept {
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) { std::clog << "[DseHaptics] COM unavailable\n"; return; }
    std::string last_error;
    while (!stopping_.load()) {
        const auto path = std::atomic_load(&device_path_);
        if (!settings_.load() || !path || path->empty()) { WaitForSingleObject(wake_, 500); continue; }
        try {
            AudioStream stream(*path);
            if (stopping_.load() || !settings_.load() || std::atomic_load(&device_path_) != path) continue;
            pending_.store(0);
            ready_.store(true, std::memory_order_release);
            std::clog << "[DseHaptics] ready: selected USB DSE, 48kHz, directional 100ms pulses\n";
            last_error.clear();
            HANDLE events[] = {static_cast<HANDLE>(wake_), stream.event()};
            while (!stopping_.load() && settings_.load() && std::atomic_load(&device_path_) == path) {
                const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 500);
                if (wait == WAIT_TIMEOUT || wait == WAIT_FAILED) throw std::runtime_error("audio stream event timeout");
                if (stopping_.load() || !settings_.load() || std::atomic_load(&device_path_) != path) break;
                // A rapid off/on can happen between worker iterations. Re-publish
                // the still-valid stream after configure(false) revoked readiness.
                ready_.store(true, std::memory_order_release);
                const auto event = pending_.exchange(0);
                const auto timestamp = event >> 2;
                const auto direction = event && GetTickCount64() >= timestamp && GetTickCount64() - timestamp <= 250 ?
                    static_cast<HapticDirection>(event & 3) : HapticDirection::None;
                stream.fill(direction, settings_.load() / 10000.0f);
            }
        } catch (const std::exception& error) {
            // Audio/USB is an optional device boundary: failures must not stop manual input or Vision.
            if (last_error != error.what()) { last_error = error.what(); std::clog << "[DseHaptics] unavailable: " << last_error << '\n'; }
        }
        ready_.store(false); pending_.store(0);
        // Device-bound recovery only. A toggle/rebind/stop wakes this immediately.
        if (!stopping_.load() && settings_.load()) WaitForSingleObject(wake_, 2000);
    }
    CoUninitialize();
}
} // namespace runtime_app
