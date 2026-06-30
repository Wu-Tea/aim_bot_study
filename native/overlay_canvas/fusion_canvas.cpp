// fusion_canvas.cpp — standalone transparent full-screen overlay that
// renders vision detections and the selected target from a shared-memory
// channel published by cod_native_runtime.
//
// Build: cmake --build ... --target fusion_canvas --config Release
// Run:   fusion_canvas.exe [--session <name>] [--max-fps <n>]
//
// Hotkeys (global, registered via RegisterHotKey):
//   Ctrl+Shift+F10   toggle visibility
//   Ctrl+Shift+F11   exit

#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "shared_fusion/fusion_channel.h"

#include <Windows.h>
#include <d3d11.h>
#include <d2d1_2.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <dwrite.h>
#include <shellscalingapi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "Shcore.lib")

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

constexpr UINT HOTKEY_TOGGLE = 1;
constexpr UINT HOTKEY_EXIT   = 2;
constexpr DWORD HOTKEY_MODIFIERS = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
constexpr auto STALE_DATA_TIMEOUT = std::chrono::milliseconds(250);

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

FILE* g_log_file = nullptr;

enum class IdleMode {
    Hide,
    Crosshair,
};

void log_line(const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    std::printf("%s\n", message);
    std::fflush(stdout);

    if (g_log_file != nullptr) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::fprintf(
            g_log_file,
            "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n",
            static_cast<unsigned>(now.wYear),
            static_cast<unsigned>(now.wMonth),
            static_cast<unsigned>(now.wDay),
            static_cast<unsigned>(now.wHour),
            static_cast<unsigned>(now.wMinute),
            static_cast<unsigned>(now.wSecond),
            static_cast<unsigned>(now.wMilliseconds),
            message);
        std::fflush(g_log_file);
    }
}

void open_log_file(const char* path) {
    if (path == nullptr || path[0] == '\0' || g_log_file != nullptr) {
        return;
    }
    fopen_s(&g_log_file, path, "a");
    if (g_log_file != nullptr) {
        log_line("[FusionCanvas] log_file=\"%s\"", path);
    }
}

std::wstring widen(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
    out.resize(static_cast<std::size_t>(n) - 1);
    return out;
}

RECT virtual_screen_rect() {
    return {
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
}

const char* idle_mode_name(IdleMode mode) {
    return mode == IdleMode::Crosshair ? "crosshair" : "hide";
}

IdleMode parse_idle_mode(const char* value, IdleMode fallback) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    if (_stricmp(value, "crosshair") == 0 || _stricmp(value, "center") == 0) {
        return IdleMode::Crosshair;
    }
    if (_stricmp(value, "hide") == 0 || _stricmp(value, "none") == 0) {
        return IdleMode::Hide;
    }
    return fallback;
}

const char* environment_string_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

// ---------------------------------------------------------------------------
// Shared-memory reader
// ---------------------------------------------------------------------------

class FusionChannelReader {
public:
    ~FusionChannelReader() { close(); }

    bool open(const wchar_t* session) {
        close();
        const std::wstring mem_name =
            std::wstring(shared_fusion::FUSION_CHANNEL_MEMORY_PREFIX) +
            session +
            shared_fusion::FUSION_CHANNEL_MEMORY_SUFFIX;

        map_handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, mem_name.c_str());
        if (map_handle_ == nullptr) return false;

        header_ = static_cast<const shared_fusion::FusionChannelHeader*>(
            MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0,
                          shared_fusion::fusion_channel_size()));
        if (header_ == nullptr) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
            return false;
        }

        if (!shared_fusion::fusion_channel_valid(header_)) {
            close();
            return false;
        }

        // Open the signalling event (may not exist yet — that's fine).
        const std::wstring evt_name =
            std::wstring(shared_fusion::FUSION_CHANNEL_EVENT_PREFIX) +
            session +
            shared_fusion::FUSION_CHANNEL_EVENT_SUFFIX;
        event_handle_ = OpenEventW(SYNCHRONIZE, FALSE, evt_name.c_str());
        // event_handle_ == nullptr is OK — we'll poll with a timeout.

        return true;
    }

    void close() {
        if (header_ != nullptr) {
            UnmapViewOfFile(header_);
            header_ = nullptr;
        }
        if (map_handle_ != nullptr) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
        }
        if (event_handle_ != nullptr) {
            CloseHandle(event_handle_);
            event_handle_ = nullptr;
        }
    }

    bool is_open() const noexcept { return header_ != nullptr; }

    HANDLE event() const noexcept { return event_handle_; }

    // Try to read the latest stable slot. Returns true if data changed.
    bool try_read(std::uint64_t& out_frame_id,
                  std::int32_t& out_fw, std::int32_t& out_fh,
                  shared_fusion::FusionTarget& out_target,
                  const shared_fusion::FusionDetection*& out_dets,
                  std::uint32_t& out_count) {
        if (header_ == nullptr) return false;

        const std::uint32_t slot_idx = header_->active_slot;
        if (slot_idx >= shared_fusion::FUSION_CHANNEL_SLOT_COUNT) return false;

        const shared_fusion::FusionSlot& slot = header_->slots[slot_idx];

        // --- seqlock read ---
        std::uint32_t seq;
        do {
            seq = slot.write_sequence;
            if (seq & 1) continue;  // writing in progress
            _ReadBarrier();

            out_frame_id = slot.frame_id;
            out_fw       = slot.frame_width;
            out_fh       = slot.frame_height;
            out_target   = slot.target;
            out_count    = slot.detection_count;
            out_dets     = slot.detections;  // pointer into mapping

            _ReadBarrier();
        } while (seq != slot.write_sequence);

        // Deduplicate by frame_id.
        if (out_frame_id == last_frame_id_) return false;
        last_frame_id_ = out_frame_id;
        return true;
    }

private:
    HANDLE map_handle_ = nullptr;
    HANDLE event_handle_ = nullptr;
    const shared_fusion::FusionChannelHeader* header_ = nullptr;
    std::uint64_t last_frame_id_ = UINT64_MAX;
};

// ---------------------------------------------------------------------------
// D3D11 + D2D1 + DirectComposition renderer
// ---------------------------------------------------------------------------

class FusionRenderer {
public:
    bool initialize(HWND hwnd, int width, int height) {
        hwnd_ = hwnd;
        width_ = width;
        height_ = height;

        // --- D3D11 device ---
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            &d3d_device_, nullptr, &d3d_context_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] D3D11CreateDevice failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        // --- DXGI factory ---
        IDXGIDevice* dxgi_device = nullptr;
        hr = d3d_device_->QueryInterface(IID_PPV_ARGS(&dxgi_device));
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] ID3D11Device::QueryInterface(IDXGIDevice) failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        IDXGIAdapter* dxgi_adapter = nullptr;
        hr = dxgi_device->GetAdapter(&dxgi_adapter);
        dxgi_device->Release();
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDXGIDevice::GetAdapter failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        hr = dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory_));
        dxgi_adapter->Release();
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDXGIAdapter::GetParent(IDXGIFactory2) failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        // --- D2D1 device ---
        hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2d_factory_));
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] D2D1CreateFactory failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        IDXGIDevice* dxgi_dev2 = nullptr;
        hr = d3d_device_->QueryInterface(IID_PPV_ARGS(&dxgi_dev2));
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] ID3D11Device::QueryInterface(IDXGIDevice for D2D/DComp) failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        hr = d2d_factory_->CreateDevice(dxgi_dev2, &d2d_device_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] ID2D1Factory2::CreateDevice failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            dxgi_dev2->Release();
            return false;
        }

        hr = d2d_device_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] ID2D1Device::CreateDeviceContext failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            dxgi_dev2->Release();
            return false;
        }

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&dwrite_factory_));
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] DWriteCreateFactory failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            dxgi_dev2->Release();
            return false;
        }
        hr = dwrite_factory_->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-us",
            &text_format_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDWriteFactory::CreateTextFormat failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            dxgi_dev2->Release();
            return false;
        }

        // --- DirectComposition ---
        hr = DCompositionCreateDevice(dxgi_dev2, IID_PPV_ARGS(&dcomp_device_));
        dxgi_dev2->Release();
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] DCompositionCreateDevice failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        hr = dcomp_device_->CreateTargetForHwnd(hwnd_, TRUE, &dcomp_target_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDCompositionDevice::CreateTargetForHwnd failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        // --- swap chain for composition ---
        if (!create_swap_chain(width, height)) {
            return false;
        }

        // --- brushes ---
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 1.0f, 0.0f, 0.65f), &brush_green_);
        if (FAILED(hr)) {
            log_line("[FusionCanvas] CreateSolidColorBrush green failed: 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 0.0f, 0.65f), &brush_yellow_);
        if (FAILED(hr)) {
            log_line("[FusionCanvas] CreateSolidColorBrush yellow failed: 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.65f), &brush_red_);
        if (FAILED(hr)) {
            log_line("[FusionCanvas] CreateSolidColorBrush red failed: 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 0.0f, 1.0f, 0.85f), &brush_target_);
        if (FAILED(hr)) {
            log_line("[FusionCanvas] CreateSolidColorBrush target failed: 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.90f), &brush_white_);
        if (FAILED(hr)) {
            log_line("[FusionCanvas] CreateSolidColorBrush white failed: 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }

        log_line("[FusionCanvas] renderer initialized width=%d height=%d", width_, height_);
        return true;
    }

    void resize(int width, int height) {
        if (width == width_ && height == height_) return;
        width_ = width;
        height_ = height;

        d2d_context_->SetTarget(nullptr);
        swap_chain_ = nullptr;
        dcomp_visual_ = nullptr;

        create_swap_chain(width, height);
    }

    void render(const shared_fusion::FusionTarget& target,
                const shared_fusion::FusionDetection* detections,
                std::uint32_t detection_count,
                std::int32_t frame_width,
                std::int32_t frame_height,
                IdleMode idle_mode) {

        if (swap_chain_ == nullptr) return;

        // --- get back buffer ---
        IDXGISurface2* surface = nullptr;
        HRESULT hr = swap_chain_->GetBuffer(
            0, IID_PPV_ARGS(&surface));
        if (FAILED(hr)) return;

        ID2D1Bitmap1* bitmap = nullptr;
        hr = d2d_context_->CreateBitmapFromDxgiSurface(
            surface, nullptr, &bitmap);
        surface->Release();
        if (FAILED(hr)) return;

        // --- draw ---
        d2d_context_->SetTarget(bitmap);
        d2d_context_->BeginDraw();
        d2d_context_->Clear(nullptr);  // transparent

        const float sw = static_cast<float>(width_);
        const float sh = static_cast<float>(height_);
        const float fw = static_cast<float>(frame_width > 0 ? frame_width : 640);
        const float fh = static_cast<float>(frame_height > 0 ? frame_height : 512);

        // Scale factors from normalised → screen pixels
        const float sx = sw;
        const float sy = sh;

        // --- optional debug detections ---
        for (std::uint32_t i = 0; i < detection_count; ++i) {
            const auto& d = detections[i];

            const float x1 = d.x1 * sx;
            const float y1 = d.y1 * sy;
            const float x2 = d.x2 * sx;
            const float y2 = d.y2 * sy;

            const D2D1_RECT_F rect = D2D1::RectF(x1, y1, x2, y2);

            ID2D1SolidColorBrush* brush = brush_green_;
            if (d.is_friendly) {
                brush = brush_green_;
            } else if (d.conf > 0.70f) {
                brush = brush_red_;
            } else if (d.conf > 0.40f) {
                brush = brush_yellow_;
            }

            d2d_context_->DrawRectangle(rect, brush, 1.5f);

            // confidence label
            if (d.conf > 0.25f) {
                char label[32];
                std::snprintf(label, sizeof(label), "%.2f", static_cast<double>(d.conf));
                const std::wstring wlabel(label, label + std::strlen(label));
                d2d_context_->DrawText(
                    wlabel.c_str(), static_cast<UINT32>(wlabel.size()),
                    text_format_,
                    D2D1::RectF(x1, y1 - 16.0f, x2, y1),
                    brush);
            }
        }

        // --- draw selected target point ---
        if (target.has_target) {
            const float tx = (sw * 0.5f) + (target.dx * fw);
            const float ty = (sh * 0.5f) + (target.dy * fh);
            const D2D1_ELLIPSE outer = D2D1::Ellipse(
                D2D1::Point2F(tx, ty), 8.0f, 8.0f);
            const D2D1_ELLIPSE inner = D2D1::Ellipse(
                D2D1::Point2F(tx, ty), 3.5f, 3.5f);
            d2d_context_->DrawEllipse(outer, brush_white_, 2.0f);
            d2d_context_->FillEllipse(inner, brush_target_);
        } else if (idle_mode == IdleMode::Crosshair) {
            const float cx = sw * 0.5f;
            const float cy = sh * 0.5f;
            const float arm = 8.0f;
            const float gap = 3.0f;
            d2d_context_->DrawLine(
                D2D1::Point2F(cx - arm, cy),
                D2D1::Point2F(cx - gap, cy),
                brush_white_,
                1.5f);
            d2d_context_->DrawLine(
                D2D1::Point2F(cx + gap, cy),
                D2D1::Point2F(cx + arm, cy),
                brush_white_,
                1.5f);
            d2d_context_->DrawLine(
                D2D1::Point2F(cx, cy - arm),
                D2D1::Point2F(cx, cy - gap),
                brush_white_,
                1.5f);
            d2d_context_->DrawLine(
                D2D1::Point2F(cx, cy + gap),
                D2D1::Point2F(cx, cy + arm),
                brush_white_,
                1.5f);
        }

        hr = d2d_context_->EndDraw();
        bitmap->Release();

        // --- present ---
        if (visible_) {
            swap_chain_->Present(1, 0);
        } else {
            // Present a transparent frame so the overlay truly disappears.
            DXGI_PRESENT_PARAMETERS pp{};
            swap_chain_->Present1(1, 0, &pp);
        }
        dcomp_device_->Commit();
    }

    void set_visible(bool v) { visible_ = v; }
    bool visible() const noexcept { return visible_; }

    void cleanup() {
        d2d_context_->SetTarget(nullptr);
        brush_green_ = nullptr;
        brush_yellow_ = nullptr;
        brush_red_ = nullptr;
        brush_target_ = nullptr;
        brush_white_ = nullptr;
        text_format_ = nullptr;
        dwrite_factory_ = nullptr;
        dcomp_visual_ = nullptr;
        dcomp_target_ = nullptr;
        dcomp_device_ = nullptr;
        swap_chain_ = nullptr;
        d2d_context_ = nullptr;
        d2d_device_ = nullptr;
        d2d_factory_ = nullptr;
        dxgi_factory_ = nullptr;
        d3d_context_ = nullptr;
        d3d_device_ = nullptr;
    }

private:
    bool create_swap_chain(int width, int height) {
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width       = static_cast<UINT>(std::max(1, width));
        scd.Height      = static_cast<UINT>(std::max(1, height));
        scd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.Stereo      = FALSE;
        scd.SampleDesc  = {1, 0};
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.Scaling     = DXGI_SCALING_STRETCH;
        scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;

        HRESULT hr = dxgi_factory_->CreateSwapChainForComposition(
            d3d_device_, &scd, nullptr, &swap_chain_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDXGIFactory2::CreateSwapChainForComposition failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        // --- DirectComposition visual ---
        hr = dcomp_device_->CreateVisual(&dcomp_visual_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDCompositionDevice::CreateVisual failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        hr = dcomp_visual_->SetContent(swap_chain_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDCompositionVisual::SetContent failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        hr = dcomp_target_->SetRoot(dcomp_visual_);
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDCompositionTarget::SetRoot failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }

        hr = dcomp_device_->Commit();
        if (FAILED(hr)) {
            log_line(
                "[FusionCanvas] IDCompositionDevice::Commit after swapchain failed: 0x%08lx",
                static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool visible_ = true;

    ID3D11Device*        d3d_device_  = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;
    IDXGIFactory2*       dxgi_factory_ = nullptr;
    ID2D1Factory2*       d2d_factory_ = nullptr;
    ID2D1Device*         d2d_device_  = nullptr;
    ID2D1DeviceContext*  d2d_context_ = nullptr;
    IDCompositionDevice*      dcomp_device_  = nullptr;
    IDCompositionTarget*      dcomp_target_  = nullptr;
    IDCompositionVisual*      dcomp_visual_  = nullptr;
    IDXGISwapChain1*          swap_chain_    = nullptr;
    IDWriteFactory*           dwrite_factory_ = nullptr;
    IDWriteTextFormat*        text_format_ = nullptr;

    ID2D1SolidColorBrush* brush_green_  = nullptr;
    ID2D1SolidColorBrush* brush_yellow_ = nullptr;
    ID2D1SolidColorBrush* brush_red_    = nullptr;
    ID2D1SolidColorBrush* brush_target_ = nullptr;
    ID2D1SolidColorBrush* brush_white_  = nullptr;
};

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

std::atomic<bool> g_running{true};
std::atomic<bool> g_visible{true};

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wparam == HOTKEY_TOGGLE) {
            g_visible.store(!g_visible.load());
            log_line(
                "[FusionCanvas] visibility=%s",
                g_visible.load() ? "on" : "off");
        } else if (wparam == HOTKEY_EXIT) {
            log_line("[FusionCanvas] exit requested (Ctrl+Shift+F11)");
            g_running.store(false);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_DISPLAYCHANGE:
        // The virtual desktop may have changed; post a refresh.
        PostMessageW(hwnd, WM_USER + 1, 0, 0);
        return 0;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    // --- parse CLI ---
    const char* session   = "dev";
    const char* log_file  = nullptr;
    int         max_fps   = 30;
    IdleMode    idle_mode = parse_idle_mode(
        environment_string_or("FUSION_IDLE_MODE", "hide"),
        IdleMode::Hide);

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--session" && i + 1 < argc) {
            session = argv[++i];
        } else if (arg == "--log-file" && i + 1 < argc) {
            log_file = argv[++i];
        } else if (arg == "--max-fps" && i + 1 < argc) {
            max_fps = std::atoi(argv[++i]);
            if (max_fps <= 0) max_fps = 30;
        } else if (arg == "--idle-mode" && i + 1 < argc) {
            idle_mode = parse_idle_mode(argv[++i], idle_mode);
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "fusion_canvas — visual fusion overlay\n"
                "  --session <name>   shared-memory session (default: dev)\n"
                "  --max-fps <n>      max render fps (default: 30)\n"
                "  --log-file <path>  append diagnostic log\n"
                "  --help             show this help\n"
                "\n"
                "Hotkeys:\n"
                "  Ctrl+Shift+F10     toggle visibility\n"
                "  Ctrl+Shift+F11     exit\n");
            return 0;
        }
    }

    open_log_file(log_file);
    log_line(
        "[FusionCanvas] session=\"%s\" max_fps=%d idle_mode=%s",
        session,
        max_fps,
        idle_mode_name(idle_mode));

    // --- DPI awareness ---
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    // --- open shared-memory channel ---
    FusionChannelReader reader;
    const std::wstring wsess = widen(session);
    if (!reader.open(wsess.c_str())) {
        log_line(
            "[FusionCanvas] waiting for shared-memory channel \"%s\"; start runtime with FUSION_ENABLED=1",
            session);
        int attempt = 0;
        while (g_running.load()) {
            if (reader.open(wsess.c_str())) {
                break;
            }
            ++attempt;
            if (attempt == 1 || attempt % 50 == 0) {
                log_line(
                    "[FusionCanvas] still waiting for channel \"%s\" attempt=%d last_error=%lu",
                    session,
                    attempt,
                    static_cast<unsigned long>(GetLastError()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!reader.is_open()) {
            log_line("[FusionCanvas] stopped before channel connected");
            return 1;
        }
    }
    log_line("[FusionCanvas] channel connected");

    // --- window ---
    const WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW),
        CS_HREDRAW | CS_VREDRAW,
        wnd_proc,
        0, 0,
        GetModuleHandleW(nullptr),
        nullptr,
        LoadCursorW(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"FusionCanvasClass",
        nullptr,
    };
    RegisterClassExW(&wc);

    const RECT vr = virtual_screen_rect();
    const int win_w = vr.right - vr.left;
    const int win_h = vr.bottom - vr.top;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
            WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT,
        L"FusionCanvasClass",
        L"Fusion Canvas",
        WS_POPUP,
        vr.left, vr.top, win_w, win_h,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (hwnd == nullptr) {
        log_line(
            "[FusionCanvas] CreateWindowEx failed: %lu",
            static_cast<unsigned long>(GetLastError()));
        return 1;
    }

    if (SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        log_line("[FusionCanvas] display_affinity=exclude_from_capture");
    } else {
        log_line(
            "[FusionCanvas] SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed: %lu",
            static_cast<unsigned long>(GetLastError()));
    }

    // --- global hotkeys ---
    if (!RegisterHotKey(hwnd, HOTKEY_TOGGLE, HOTKEY_MODIFIERS, VK_F10)) {
        log_line(
            "[FusionCanvas] RegisterHotKey F10 failed: %lu",
            static_cast<unsigned long>(GetLastError()));
        // non-fatal
    }
    if (!RegisterHotKey(hwnd, HOTKEY_EXIT, HOTKEY_MODIFIERS, VK_F11)) {
        log_line(
            "[FusionCanvas] RegisterHotKey F11 failed: %lu",
            static_cast<unsigned long>(GetLastError()));
        // non-fatal
    }

    // --- renderer ---
    FusionRenderer renderer;
    if (!renderer.initialize(hwnd, win_w, win_h)) {
        log_line("[FusionCanvas] renderer init failed");
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    // Don't call UpdateWindow — let DirectComposition drive painting.

    // --- render loop ---
    const auto frame_interval = std::chrono::microseconds(
        static_cast<std::int64_t>(1'000'000.0 / static_cast<double>(max_fps)));

    auto last_frame_time = std::chrono::steady_clock::now();
    shared_fusion::FusionTarget current_target{};
    shared_fusion::FusionDetection current_detections[
        shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS]{};
    std::uint32_t current_detection_count = 0;
    std::int32_t current_frame_width = 640;
    std::int32_t current_frame_height = 512;
    std::uint64_t current_frame_id = UINT64_MAX;
    auto last_data_time = std::chrono::steady_clock::time_point{};

    log_line("[FusionCanvas] running (Ctrl+Shift+F10 toggle, Ctrl+Shift+F11 exit)");

    while (g_running.load()) {
        // --- pump Windows messages ---
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running.store(false);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!g_running.load()) break;

        // --- handle display change ---
        // (we don't have a custom message pump for WM_USER+1 yet; just
        //  re-check virtual desktop on a timer every few seconds)

        // --- apply visibility toggle ---
        renderer.set_visible(g_visible.load());

        // --- wait for new data ---
        DWORD wait_ms = static_cast<DWORD>(
            std::max(1L, static_cast<long>(frame_interval.count() / 1000)));
        if (reader.event() != nullptr && g_visible.load()) {
            WaitForSingleObject(reader.event(), wait_ms);
        } else {
            std::this_thread::sleep_for(frame_interval);
        }

        // --- throttle to max_fps ---
        const auto now = std::chrono::steady_clock::now();
        if (now - last_frame_time < frame_interval) {
            continue;
        }
        last_frame_time = now;

        // --- read shared memory ---
        std::uint64_t frame_id = 0;
        std::int32_t fw = 0, fh = 0;
        shared_fusion::FusionTarget target{};
        const shared_fusion::FusionDetection* detections = nullptr;
        std::uint32_t det_count = 0;

        if (reader.try_read(frame_id, fw, fh, target, detections, det_count)) {
            current_target = target;
            current_frame_id = frame_id;
            last_data_time = now;
            current_frame_width = fw > 0 ? fw : current_frame_width;
            current_frame_height = fh > 0 ? fh : current_frame_height;
            current_detection_count = std::min(
                det_count,
                shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS);
            for (std::uint32_t i = 0; i < current_detection_count; ++i) {
                current_detections[i] = detections[i];
            }
        }

        shared_fusion::FusionTarget render_target = current_target;
        std::uint32_t render_detection_count = current_detection_count;
        const bool has_data = current_frame_id != UINT64_MAX;
        const bool stale_data =
            !has_data ||
            (last_data_time != std::chrono::steady_clock::time_point{} &&
             now - last_data_time > STALE_DATA_TIMEOUT);
        if (stale_data || !render_target.has_target) {
            render_target = {};
            render_detection_count = 0;
        }

        // --- render ---
        renderer.render(
            render_target,
            current_detections,
            render_detection_count,
            current_frame_width,
            current_frame_height,
            idle_mode);
    }

    // --- cleanup ---
    UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
    UnregisterHotKey(hwnd, HOTKEY_EXIT);
    renderer.cleanup();
    DestroyWindow(hwnd);
    reader.close();

    log_line("[FusionCanvas] exited cleanly");
    if (g_log_file != nullptr) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
    return 0;
}
