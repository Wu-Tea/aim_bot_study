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
#include "capture_isolation_guard.h"
#include "overlay_window_policy.h"
#include "vision_native/dxgi_capture.h"

#include <Windows.h>
#include <d3d11.h>
#include <d2d1_2.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <dwrite.h>
#include <shellscalingapi.h>
#include <share.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

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
constexpr std::uint64_t STALE_DATA_TIMEOUT_MS = 120;

void log_line(const char* format, ...);

bool configure_fusion_canvas_window(HWND hwnd) noexcept {
    if (hwnd == nullptr) return false;

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR extended_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const DWORD style_error = GetLastError();
    if (extended_style == 0 && style_error != ERROR_SUCCESS) {
        log_line(
            "[FusionCanvas] input_passthrough=failed read_style error=%lu",
            static_cast<unsigned long>(style_error));
        return false;
    }

    const DWORD style = static_cast<DWORD>(extended_style);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR window_style_value = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const DWORD window_style_error = GetLastError();
    if (window_style_value == 0 && window_style_error != ERROR_SUCCESS) {
        log_line(
            "[FusionCanvas] input_passthrough=failed read_window_style error=%lu",
            static_cast<unsigned long>(window_style_error));
        return false;
    }
    const DWORD window_style = static_cast<DWORD>(window_style_value);
    if (!fusion_overlay::satisfies_mouse_passthrough_contract(
            style,
            window_style)) {
        log_line(
            "[FusionCanvas] input_passthrough=failed ex_style=0x%08lx style=0x%08lx",
            static_cast<unsigned long>(style),
            static_cast<unsigned long>(window_style));
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)) {
        log_line(
            "[FusionCanvas] input_passthrough=failed layered_alpha error=%lu",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }

    log_line(
        "[FusionCanvas] input_passthrough=verified style=0x%08lx disabled=1",
        static_cast<unsigned long>(style));
    return true;
}

DWORD legacy_no_redirection_overlay_style_for_probe() noexcept {
    return WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT;
}

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
    // The background launcher reads readiness from this file while the canvas
    // is alive, so the writer must not take the CRT's default exclusive lock.
    g_log_file = _fsopen(path, "a", _SH_DENYNO);
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

    std::uint64_t qpc_frequency() const noexcept {
        return header_ != nullptr ? header_->qpc_frequency : 0;
    }

    // Try to read the latest stable slot. Returns true if data changed.
    bool try_read(std::uint64_t& out_frame_id,
                  std::uint64_t& out_timestamp,
                  std::int32_t& out_fw, std::int32_t& out_fh,
                  shared_fusion::FusionFrameGeometry& out_geometry,
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
            out_timestamp = slot.timestamp;
            out_fw       = slot.frame_width;
            out_fh       = slot.frame_height;
            out_geometry = slot.geometry;
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
            D2D1::ColorF(0xFFE607, 1.0f), &brush_target_);
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
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush_black_);
        if (FAILED(hr)) {
            log_line("[FusionCanvas] CreateSolidColorBrush black failed: 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }

        log_line("[FusionCanvas] renderer initialized width=%d height=%d", width_, height_);
        return true;
    }

    bool resize(int width, int height) {
        if (width == width_ && height == height_) return true;
        width_ = width;
        height_ = height;

        d2d_context_->SetTarget(nullptr);
        if (dcomp_visual_ != nullptr) {
            dcomp_visual_->Release();
            dcomp_visual_ = nullptr;
        }
        if (swap_chain_ != nullptr) {
            swap_chain_->Release();
            swap_chain_ = nullptr;
        }

        return create_swap_chain(width, height);
    }

    void render(const shared_fusion::FusionTarget& target,
                const shared_fusion::FusionDetection* detections,
                std::uint32_t detection_count,
                std::int32_t frame_width,
                std::int32_t frame_height,
                const shared_fusion::FusionFrameGeometry& geometry,
                int virtual_left,
                int virtual_top,
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
        const float roi_origin_x = static_cast<float>(
            geometry.output_left + geometry.roi_left - virtual_left);
        const float roi_origin_y = static_cast<float>(
            geometry.output_top + geometry.roi_top - virtual_top);

        // --- optional debug detections ---
        for (std::uint32_t i = 0; i < detection_count; ++i) {
            const auto& d = detections[i];

            const float x1 = roi_origin_x + (d.x1 * fw);
            const float y1 = roi_origin_y + (d.y1 * fh);
            const float x2 = roi_origin_x + (d.x2 * fw);
            const float y2 = roi_origin_y + (d.y2 * fh);

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

        const float display_scale = std::clamp(
            static_cast<float>(geometry.output_height > 0
                ? geometry.output_height : height_) / 1080.0f,
            0.85f,
            1.60f);
        fusion_overlay::MarkerLayoutInput marker_input;
        marker_input.has_target = target.has_target;
        marker_input.direct_observation = target.direct_observation;
        marker_input.frame_width = frame_width;
        marker_input.frame_height = frame_height;
        marker_input.output_left = geometry.output_left;
        marker_input.output_top = geometry.output_top;
        marker_input.output_width = geometry.output_width;
        marker_input.output_height = geometry.output_height;
        marker_input.roi_left = geometry.roi_left;
        marker_input.roi_top = geometry.roi_top;
        marker_input.virtual_left = virtual_left;
        marker_input.virtual_top = virtual_top;
        marker_input.virtual_width = width_;
        marker_input.virtual_height = height_;
        marker_input.target_x = target.target_x;
        marker_input.target_y = target.target_y;
        marker_input.marker_radius_px = 6.0f * display_scale;
        const fusion_overlay::MarkerLayout marker =
            fusion_overlay::layout_target_point_marker(marker_input);

        // Three opaque layers stay readable in bright and dark MW4 scenes
        // while keeping the selector-owned target point unambiguous.
        if (marker.visible) {
            const D2D1_POINT_2F center =
                D2D1::Point2F(marker.center_x, marker.center_y);
            d2d_context_->FillEllipse(
                D2D1::Ellipse(center, marker.radius + 2.0f, marker.radius + 2.0f),
                brush_black_);
            d2d_context_->FillEllipse(
                D2D1::Ellipse(center, marker.radius, marker.radius),
                brush_white_);
            const float inner_radius = std::max(2.0f, marker.radius - 2.0f);
            d2d_context_->FillEllipse(
                D2D1::Ellipse(center, inner_radius, inner_radius),
                brush_target_);
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
        // DWM owns composition timing. Waiting for vertical sync here would
        // block IPC ingestion long enough to miss short detection bursts.
        if (visible_) {
            swap_chain_->Present(0, 0);
        } else {
            // Present a transparent frame so the overlay truly disappears.
            DXGI_PRESENT_PARAMETERS pp{};
            swap_chain_->Present1(0, 0, &pp);
        }
        dcomp_device_->Commit();
    }

    void set_visible(bool v) { visible_ = v; }
    bool visible() const noexcept { return visible_; }

    bool render_isolation_probe(const D2D1_RECT_F& probe_rect) {
        if (swap_chain_ == nullptr) return false;

        IDXGISurface2* surface = nullptr;
        HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&surface));
        if (FAILED(hr)) return false;

        ID2D1Bitmap1* bitmap = nullptr;
        hr = d2d_context_->CreateBitmapFromDxgiSurface(surface, nullptr, &bitmap);
        surface->Release();
        if (FAILED(hr)) return false;

        ID2D1SolidColorBrush* probe_brush = nullptr;
        hr = d2d_context_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 0.0f, 1.0f, 1.0f),
            &probe_brush);
        if (FAILED(hr)) {
            bitmap->Release();
            return false;
        }

        d2d_context_->SetTarget(bitmap);
        d2d_context_->BeginDraw();
        d2d_context_->Clear(nullptr);
        d2d_context_->FillRectangle(probe_rect, probe_brush);
        hr = d2d_context_->EndDraw();
        probe_brush->Release();
        bitmap->Release();
        if (FAILED(hr)) return false;

        hr = swap_chain_->Present(1, 0);
        if (FAILED(hr)) return false;
        return SUCCEEDED(dcomp_device_->Commit());
    }

    void cleanup() {
        d2d_context_->SetTarget(nullptr);
        brush_green_ = nullptr;
        brush_yellow_ = nullptr;
        brush_red_ = nullptr;
        brush_target_ = nullptr;
        brush_white_ = nullptr;
        brush_black_ = nullptr;
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
    ID2D1SolidColorBrush* brush_black_  = nullptr;
};

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

std::atomic<bool> g_running{true};
std::atomic<bool> g_visible{true};
constexpr std::uint32_t ISOLATION_INVALIDATED_DISPLAY = 1u << 0;
constexpr std::uint32_t ISOLATION_INVALIDATED_DWM = 1u << 1;
std::atomic<std::uint32_t> g_isolation_invalidation_reasons{0};

const char* isolation_invalidation_reason_name(std::uint32_t reasons) noexcept {
    if (reasons == ISOLATION_INVALIDATED_DISPLAY) return "display_change";
    if (reasons == ISOLATION_INVALIDATED_DWM) return "dwm_composition_changed";
    if (reasons ==
        (ISOLATION_INVALIDATED_DISPLAY | ISOLATION_INVALIDATED_DWM)) {
        return "display_and_dwm_change";
    }
    return "unknown";
}

void handle_hotkey(WPARAM hotkey_id) {
    if (hotkey_id == HOTKEY_TOGGLE) {
        g_visible.store(!g_visible.load());
        log_line(
            "[FusionCanvas] visibility=%s",
            g_visible.load() ? "on" : "off");
    } else if (hotkey_id == HOTKEY_EXIT) {
        log_line("[FusionCanvas] exit requested (Ctrl+Shift+F11)");
        g_running.store(false);
    }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_HOTKEY:
        handle_hotkey(wparam);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_DISPLAYCHANGE:
        g_isolation_invalidation_reasons.fetch_or(
            ISOLATION_INVALIDATED_DISPLAY);
        return 0;

    case WM_DWMCOMPOSITIONCHANGED:
        g_isolation_invalidation_reasons.fetch_or(ISOLATION_INVALIDATED_DWM);
        return 0;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK isolation_probe_wnd_proc(
    HWND hwnd,
    UINT msg,
    WPARAM wparam,
    LPARAM lparam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK isolation_control_wnd_proc(
    HWND hwnd,
    UINT msg,
    WPARAM wparam,
    LPARAM lparam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH brush = CreateSolidBrush(RGB(0, 255, 255));
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &paint);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

struct InputPassthroughProbeState {
    HANDLE ready_event = nullptr;
    HANDLE clicked_event = nullptr;
    HANDLE stop_event = nullptr;
    std::atomic<HWND> target_hwnd{nullptr};
    std::atomic<DWORD> target_error{ERROR_SUCCESS};
};

LRESULT CALLBACK input_passthrough_target_wnd_proc(
    HWND hwnd,
    UINT msg,
    WPARAM wparam,
    LPARAM lparam) {
    auto* state = reinterpret_cast<InputPassthroughProbeState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<InputPassthroughProbeState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    switch (msg) {
    case WM_LBUTTONDOWN:
        if (state != nullptr && state->clicked_event != nullptr) {
            SetEvent(state->clicked_event);
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH brush = CreateSolidBrush(RGB(0, 160, 255));
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &paint);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool wait_for_probe_click(HANDLE clicked_event, DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (true) {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now >= deadline
            ? 0
            : static_cast<DWORD>(std::min<ULONGLONG>(
                  deadline - now,
                  static_cast<ULONGLONG>(MAXDWORD - 1u)));
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            1,
            &clicked_event,
            remaining,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0) return true;
        if (wait == WAIT_TIMEOUT || wait == WAIT_FAILED) return false;
        if (wait != WAIT_OBJECT_0 + 1) return false;

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

int run_input_passthrough_probe(
    DWORD overlay_style,
    DWORD window_style,
    bool verify_production_configuration,
    const char* probe_variant) {
    constexpr int probe_size = 64;
    InputPassthroughProbeState state;
    state.ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.clicked_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state.ready_event == nullptr || state.clicked_event == nullptr ||
        state.stop_event == nullptr) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed create_event error=%lu",
            static_cast<unsigned long>(GetLastError()));
        if (state.ready_event != nullptr) CloseHandle(state.ready_event);
        if (state.clicked_event != nullptr) CloseHandle(state.clicked_event);
        if (state.stop_event != nullptr) CloseHandle(state.stop_event);
        return 4;
    }

    RECT work_area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        work_area = virtual_screen_rect();
    }
    const int probe_left = work_area.left + 32;
    const int probe_top = work_area.top + 32;

    std::thread target_thread([&]() {
        const WNDCLASSEXW target_class = {
            sizeof(WNDCLASSEXW),
            CS_HREDRAW | CS_VREDRAW,
            input_passthrough_target_wnd_proc,
            0, 0,
            GetModuleHandleW(nullptr),
            nullptr,
            LoadCursorW(nullptr, IDC_ARROW),
            nullptr,
            nullptr,
            L"FusionCanvasInputPassthroughTargetClass",
            nullptr,
        };
        if (RegisterClassExW(&target_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            state.target_error.store(GetLastError());
            SetEvent(state.ready_event);
            return;
        }

        HWND target_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"FusionCanvasInputPassthroughTargetClass",
            L"Fusion Canvas Input Target",
            WS_POPUP,
            probe_left,
            probe_top,
            probe_size,
            probe_size,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            &state);
        if (target_hwnd == nullptr) {
            state.target_error.store(GetLastError());
            SetEvent(state.ready_event);
            return;
        }

        state.target_hwnd.store(target_hwnd);
        SetWindowPos(
            target_hwnd,
            HWND_TOPMOST,
            probe_left,
            probe_top,
            probe_size,
            probe_size,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(target_hwnd);
        SetEvent(state.ready_event);

        bool stop = false;
        while (!stop) {
            const DWORD wait = MsgWaitForMultipleObjects(
                1,
                &state.stop_event,
                FALSE,
                100,
                QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) {
                stop = true;
            } else if (wait == WAIT_OBJECT_0 + 1) {
                MSG msg{};
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        stop = true;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            } else if (wait == WAIT_FAILED) {
                state.target_error.store(GetLastError());
                stop = true;
            }
        }

        DestroyWindow(target_hwnd);
        state.target_hwnd.store(nullptr);
    });

    HWND overlay_hwnd = nullptr;
    POINT original_cursor{};
    bool cursor_saved = false;
    const auto cleanup = [&]() {
        if (cursor_saved) SetCursorPos(original_cursor.x, original_cursor.y);
        if (overlay_hwnd != nullptr) DestroyWindow(overlay_hwnd);
        SetEvent(state.stop_event);
        if (target_thread.joinable()) target_thread.join();
        CloseHandle(state.ready_event);
        CloseHandle(state.clicked_event);
        CloseHandle(state.stop_event);
    };

    if (WaitForSingleObject(state.ready_event, 2000) != WAIT_OBJECT_0 ||
        state.target_hwnd.load() == nullptr) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed target_ready error=%lu",
            static_cast<unsigned long>(state.target_error.load()));
        cleanup();
        return 4;
    }

    const WNDCLASSEXW overlay_class = {
        sizeof(WNDCLASSEXW),
        CS_HREDRAW | CS_VREDRAW,
        wnd_proc,
        0, 0,
        GetModuleHandleW(nullptr),
        nullptr,
        LoadCursorW(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        L"FusionCanvasInputPassthroughOverlayClass",
        nullptr,
    };
    if (RegisterClassExW(&overlay_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed register_overlay error=%lu",
            static_cast<unsigned long>(GetLastError()));
        cleanup();
        return 4;
    }

    overlay_hwnd = CreateWindowExW(
        overlay_style,
        L"FusionCanvasInputPassthroughOverlayClass",
        L"Fusion Canvas Input Overlay",
        window_style,
        probe_left,
        probe_top,
        probe_size,
        probe_size,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (overlay_hwnd == nullptr) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed create_overlay error=%lu",
            static_cast<unsigned long>(GetLastError()));
        cleanup();
        return 4;
    }
    if (verify_production_configuration) {
        if (!configure_fusion_canvas_window(overlay_hwnd)) {
            cleanup();
            return 4;
        }
    } else {
        log_line(
            "[FusionCanvas] input_passthrough=%s ex_style=0x%08lx style=0x%08lx",
            probe_variant,
            static_cast<unsigned long>(overlay_style),
            static_cast<unsigned long>(window_style));
    }

    SetWindowPos(
        overlay_hwnd,
        HWND_TOPMOST,
        probe_left,
        probe_top,
        probe_size,
        probe_size,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
        !GetCursorPos(&original_cursor)) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed unsafe_mouse_state");
        cleanup();
        return 4;
    }
    cursor_saved = true;

    const int click_x = probe_left + probe_size / 2;
    const int click_y = probe_top + probe_size / 2;
    if (!SetCursorPos(click_x, click_y)) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed set_cursor error=%lu",
            static_cast<unsigned long>(GetLastError()));
        cleanup();
        return 4;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(2, input, sizeof(INPUT)) != 2) {
        log_line(
            "[FusionCanvas] input_passthrough_probe=failed send_input error=%lu",
            static_cast<unsigned long>(GetLastError()));
        cleanup();
        return 4;
    }

    const bool passed = wait_for_probe_click(state.clicked_event, 750);
    log_line(
        "[FusionCanvas] input_passthrough_probe=%s style=0x%08lx target_thread=%lu overlay_thread=%lu",
        passed ? "passed" : "blocked",
        static_cast<unsigned long>(overlay_style),
        static_cast<unsigned long>(GetWindowThreadProcessId(
            state.target_hwnd.load(), nullptr)),
        static_cast<unsigned long>(GetCurrentThreadId()));
    cleanup();
    return passed ? 0 : 5;
}

std::size_t count_color_signature(
    const std::vector<std::uint8_t>& pixels,
    int width,
    int height,
    int left,
    int top,
    int size,
    bool cyan) {
    std::size_t count = 0;
    const int right = std::min(width, left + size);
    const int bottom = std::min(height, top + size);
    for (int y = std::max(0, top); y < bottom; ++y) {
        for (int x = std::max(0, left); x < right; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            const int blue = pixels[offset + 0];
            const int green = pixels[offset + 1];
            const int red = pixels[offset + 2];
            const bool matches = cyan
                ? (blue >= 160 && green >= 160 && red <= 120)
                : (blue >= 160 && red >= 160 && green <= 120);
            if (matches) ++count;
        }
    }
    return count;
}

int run_capture_isolation_probe() {
    constexpr int capture_width = 640;
    constexpr int capture_height = 512;
    constexpr int box_size = 72;
    constexpr int control_buffer_x = 48;
    constexpr int probe_buffer_x = capture_width - 48 - box_size;
    constexpr int buffer_y = (capture_height - box_size) / 2;

    HWND overlay_hwnd = nullptr;
    HWND control_hwnd = nullptr;
    FusionRenderer renderer;
    bool renderer_initialized = false;
    const auto cleanup = [&]() {
        if (control_hwnd != nullptr) DestroyWindow(control_hwnd);
        if (renderer_initialized) renderer.cleanup();
        if (overlay_hwnd != nullptr) DestroyWindow(overlay_hwnd);
    };

    try {
        vision_native::DxgiRoiCapture capture(
            capture_width, capture_height, 0, -1, 100);
        const RECT virtual_rect = virtual_screen_rect();
        const int virtual_width = virtual_rect.right - virtual_rect.left;
        const int virtual_height = virtual_rect.bottom - virtual_rect.top;

        const WNDCLASSEXW overlay_class = {
            sizeof(WNDCLASSEXW),
            CS_HREDRAW | CS_VREDRAW,
            isolation_probe_wnd_proc,
            0, 0,
            GetModuleHandleW(nullptr),
            nullptr,
            LoadCursorW(nullptr, IDC_ARROW),
            nullptr,
            nullptr,
            L"FusionCanvasIsolationProbeClass",
            nullptr,
        };
        if (RegisterClassExW(&overlay_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            log_line("[FusionCanvas] isolation_probe=failed register_overlay_class error=%lu",
                     static_cast<unsigned long>(GetLastError()));
            return 4;
        }

        const WNDCLASSEXW control_class = {
            sizeof(WNDCLASSEXW),
            CS_HREDRAW | CS_VREDRAW,
            isolation_control_wnd_proc,
            0, 0,
            GetModuleHandleW(nullptr),
            nullptr,
            LoadCursorW(nullptr, IDC_ARROW),
            nullptr,
            nullptr,
            L"FusionCanvasIsolationControlClass",
            nullptr,
        };
        if (RegisterClassExW(&control_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            log_line("[FusionCanvas] isolation_probe=failed register_control_class error=%lu",
                     static_cast<unsigned long>(GetLastError()));
            return 4;
        }

        overlay_hwnd = CreateWindowExW(
            fusion_overlay::fusion_canvas_extended_style(),
            L"FusionCanvasIsolationProbeClass",
            L"Fusion Canvas Isolation Probe",
            fusion_overlay::fusion_canvas_window_style(),
            virtual_rect.left,
            virtual_rect.top,
            virtual_width,
            virtual_height,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (overlay_hwnd == nullptr) {
            log_line("[FusionCanvas] isolation_probe=failed create_overlay error=%lu",
                     static_cast<unsigned long>(GetLastError()));
            return 4;
        }
        if (!configure_fusion_canvas_window(overlay_hwnd)) {
            log_line(
                "[FusionCanvas] isolation_probe=failed input_passthrough_config");
            cleanup();
            return 4;
        }

        if (!renderer.initialize(overlay_hwnd, virtual_width, virtual_height)) {
            log_line("[FusionCanvas] isolation_probe=failed renderer_init");
            cleanup();
            return 4;
        }
        renderer_initialized = true;

        const int control_screen_x =
            capture.output_left() + capture.roi_left() + control_buffer_x;
        const int control_screen_y =
            capture.output_top() + capture.roi_top() + buffer_y;
        control_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"FusionCanvasIsolationControlClass",
            L"Fusion Capture Control",
            WS_POPUP,
            control_screen_x,
            control_screen_y,
            box_size,
            box_size,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (control_hwnd == nullptr) {
            log_line("[FusionCanvas] isolation_probe=failed create_control error=%lu",
                     static_cast<unsigned long>(GetLastError()));
            cleanup();
            return 4;
        }

        const float probe_screen_x = static_cast<float>(
            capture.output_left() + capture.roi_left() + probe_buffer_x -
            virtual_rect.left);
        const float probe_screen_y = static_cast<float>(
            capture.output_top() + capture.roi_top() + buffer_y -
            virtual_rect.top);

        ShowWindow(overlay_hwnd, SW_SHOWNA);
        if (!renderer.render_isolation_probe(D2D1::RectF(
                probe_screen_x,
                probe_screen_y,
                probe_screen_x + box_size,
                probe_screen_y + box_size))) {
            log_line("[FusionCanvas] isolation_probe=failed render_probe");
            cleanup();
            return 4;
        }
        ShowWindow(control_hwnd, SW_SHOWNA);
        UpdateWindow(control_hwnd);

        const std::size_t box_area =
            static_cast<std::size_t>(box_size) * static_cast<std::size_t>(box_size);
        bool visible_control_observed = false;
        bool visible_probe_observed = false;
        std::size_t visible_control_count = 0;
        std::size_t visible_probe_count = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            RedrawWindow(
                control_hwnd,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const auto metadata = capture.grab();
            if (!metadata.updated) continue;

            const auto pixels = capture.readback_bgra();
            visible_control_count = count_color_signature(
                pixels,
                capture_width,
                capture_height,
                control_buffer_x,
                buffer_y,
                box_size,
                true);
            visible_probe_count = count_color_signature(
                pixels,
                capture_width,
                capture_height,
                probe_buffer_x,
                buffer_y,
                box_size,
                false);
            visible_control_observed = visible_control_count >= box_area / 3u;
            visible_probe_observed = visible_probe_count >= box_area / 3u;
            if (visible_control_observed && visible_probe_observed) break;
        }
        if (!visible_control_observed || !visible_probe_observed) {
            cleanup();
            log_line(
                "[FusionCanvas] isolation_probe=failed overlay_not_visible_before_isolation "
                "control_pixels=%zu probe_pixels=%zu",
                visible_control_count,
                visible_probe_count);
            return 4;
        }

        const auto isolation =
            fusion_overlay::enable_and_verify_capture_isolation(overlay_hwnd);
        if (!isolation.decision.may_show) {
            log_line(
                "[FusionCanvas] isolation_probe=failed_closed reason=%s affinity=0x%08lx",
                fusion_overlay::capture_isolation_failure_name(
                    isolation.decision.failure),
                static_cast<unsigned long>(isolation.observation.affinity));
            cleanup();
            return 3;
        }
        if (!renderer.render_isolation_probe(D2D1::RectF(
                probe_screen_x,
                probe_screen_y,
                probe_screen_x + box_size,
                probe_screen_y + box_size))) {
            log_line(
                "[FusionCanvas] isolation_probe=failed render_after_isolation");
            cleanup();
            return 4;
        }

        bool fresh_control_observed = false;
        std::size_t last_control_count = 0;
        std::size_t last_probe_count = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            RedrawWindow(
                control_hwnd,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const auto metadata = capture.grab();
            if (!metadata.updated) continue;

            const auto pixels = capture.readback_bgra();
            last_control_count = count_color_signature(
                pixels,
                capture_width,
                capture_height,
                control_buffer_x,
                buffer_y,
                box_size,
                true);
            last_probe_count = count_color_signature(
                pixels,
                capture_width,
                capture_height,
                probe_buffer_x,
                buffer_y,
                box_size,
                false);
            if (last_control_count >= box_area / 3u) {
                fresh_control_observed = true;
                break;
            }
        }

        cleanup();
        if (!fresh_control_observed) {
            log_line(
                "[FusionCanvas] isolation_probe=failed no_fresh_control control_pixels=%zu",
                last_control_count);
            return 4;
        }
        if (last_probe_count > box_area / 20u) {
            log_line(
                "[FusionCanvas] isolation_probe=failed overlay_visible_in_dxgi "
                "control_pixels=%zu probe_pixels=%zu",
                last_control_count,
                last_probe_count);
            return 3;
        }

        log_line(
            "[FusionCanvas] isolation_probe=passed visible_probe_pixels=%zu "
            "excluded_probe_pixels=%zu control_pixels=%zu "
            "capture=%dx%d output_origin=%d,%d roi_origin=%d,%d",
            visible_probe_count,
            last_probe_count,
            last_control_count,
            capture_width,
            capture_height,
            capture.output_left(),
            capture.output_top(),
            capture.roi_left(),
            capture.roi_top());
        return 0;
    } catch (const std::exception& error) {
        cleanup();
        log_line("[FusionCanvas] isolation_probe=failed exception=\"%s\"", error.what());
        return 4;
    }
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
    bool        verify_capture_isolation = false;
    bool        verify_input_passthrough = false;
    bool        verify_legacy_input_passthrough = false;
    bool        verify_disabled_no_redirection_passthrough = false;
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
        } else if (arg == "--verify-capture-isolation") {
            verify_capture_isolation = true;
        } else if (arg == "--verify-input-passthrough") {
            verify_input_passthrough = true;
        } else if (arg == "--verify-input-passthrough-legacy") {
            verify_legacy_input_passthrough = true;
        } else if (arg == "--verify-input-passthrough-disabled-no-redirection") {
            verify_disabled_no_redirection_passthrough = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "fusion_canvas — visual fusion overlay\n"
                "  --session <name>   shared-memory session (default: dev)\n"
                "  --max-fps <n>      max render fps (default: 30)\n"
                "  --log-file <path>  append diagnostic log\n"
                "  --verify-capture-isolation  run the DXGI startup gate and exit\n"
                "  --verify-input-passthrough run the cross-thread mouse gate and exit\n"
                "  --verify-input-passthrough-legacy  reproduce the legacy blocked gate\n"
                "  --verify-input-passthrough-disabled-no-redirection  isolate WS_DISABLED\n"
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

    if (verify_capture_isolation) {
        return run_capture_isolation_probe();
    }
    if (verify_input_passthrough) {
        return run_input_passthrough_probe(
            fusion_overlay::fusion_canvas_extended_style(),
            fusion_overlay::fusion_canvas_window_style(),
            true,
            "production");
    }
    if (verify_legacy_input_passthrough) {
        return run_input_passthrough_probe(
            legacy_no_redirection_overlay_style_for_probe(),
            WS_POPUP,
            false,
            "legacy_negative_control");
    }
    if (verify_disabled_no_redirection_passthrough) {
        return run_input_passthrough_probe(
            legacy_no_redirection_overlay_style_for_probe(),
            WS_POPUP | WS_DISABLED,
            false,
            "disabled_no_redirection_isolate");
    }

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

    RECT vr = virtual_screen_rect();
    const int win_w = vr.right - vr.left;
    const int win_h = vr.bottom - vr.top;

    HWND hwnd = CreateWindowExW(
        fusion_overlay::fusion_canvas_extended_style(),
        L"FusionCanvasClass",
        L"Fusion Canvas",
        fusion_overlay::fusion_canvas_window_style(),
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
    if (!configure_fusion_canvas_window(hwnd)) {
        DestroyWindow(hwnd);
        return 1;
    }

    const auto startup_isolation =
        fusion_overlay::enable_and_verify_capture_isolation(hwnd);
    if (!startup_isolation.decision.may_show) {
        log_line(
            "[FusionCanvas] capture_isolation=failed_closed reason=%s "
            "dwm_hr=0x%08lx set_error=%lu readback_error=%lu affinity=0x%08lx",
            fusion_overlay::capture_isolation_failure_name(
                startup_isolation.decision.failure),
            static_cast<unsigned long>(startup_isolation.dwm_hresult),
            static_cast<unsigned long>(startup_isolation.set_error),
            static_cast<unsigned long>(startup_isolation.readback_error),
            static_cast<unsigned long>(startup_isolation.observation.affinity));
        DestroyWindow(hwnd);
        return 3;
    }
    log_line(
        "[FusionCanvas] capture_isolation=verified affinity=0x%08lx",
        static_cast<unsigned long>(startup_isolation.observation.affinity));

    // --- global hotkeys ---
    if (!RegisterHotKey(nullptr, HOTKEY_TOGGLE, HOTKEY_MODIFIERS, VK_F10)) {
        log_line(
            "[FusionCanvas] RegisterHotKey F10 failed: %lu",
            static_cast<unsigned long>(GetLastError()));
        // non-fatal
    }
    if (!RegisterHotKey(nullptr, HOTKEY_EXIT, HOTKEY_MODIFIERS, VK_F11)) {
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

    const auto loop_started = std::chrono::steady_clock::now();
    auto last_frame_time = loop_started - frame_interval;
    auto last_isolation_check = loop_started;
    auto isolation_state =
        fusion_overlay::CaptureIsolationLifecycleState::Verified;
    bool isolation_failed = false;
    bool wait_failed = false;
    bool render_dirty = true;
    bool last_visibility = g_visible.load();
    bool rendered_marker_visible = false;
    auto rendered_marker_expiry = loop_started;
    HANDLE deadline_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    bool timer_failure_logged = false;
    if (deadline_timer == nullptr) {
        log_line(
            "[FusionCanvas] waitable timer unavailable; using deadline timeout error=%lu",
            static_cast<unsigned long>(GetLastError()));
    }
    shared_fusion::FusionTarget current_target{};
    shared_fusion::FusionDetection current_detections[
        shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS]{};
    std::uint32_t current_detection_count = 0;
    std::int32_t current_frame_width = 640;
    std::int32_t current_frame_height = 512;
    shared_fusion::FusionFrameGeometry current_geometry{};
    std::uint64_t current_frame_id = UINT64_MAX;
    std::uint64_t current_timestamp = 0;
    shared_fusion::FusionTarget latched_direct_target{};
    shared_fusion::FusionFrameGeometry latched_direct_geometry{};
    std::int32_t latched_direct_frame_width = 640;
    std::int32_t latched_direct_frame_height = 512;
    std::uint64_t latched_direct_timestamp = 0;
    bool latched_direct_available = false;
    bool latched_direct_pending_render = false;
    std::uint32_t direct_burst_sample_count = 0;
    bool previous_sample_was_direct = false;
    std::uint64_t previous_sample_generation = 0;
    const std::uint64_t qpc_frequency = reader.qpc_frequency();

    log_line("[FusionCanvas] running (Ctrl+Shift+F10 toggle, Ctrl+Shift+F11 exit)");

    while (g_running.load()) {
        // --- pump Windows messages ---
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running.store(false);
                break;
            }
            if (msg.message == WM_HOTKEY) {
                handle_hotkey(msg.wParam);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!g_running.load()) break;

        const std::uint32_t invalidation_reasons =
            g_isolation_invalidation_reasons.exchange(0);
        if (invalidation_reasons != 0) {
            const auto pending = fusion_overlay::transition_capture_isolation(
                isolation_state,
                fusion_overlay::CaptureIsolationLifecycleEvent::IsolationInvalidated);
            isolation_state = pending.state;
            ShowWindow(hwnd, SW_HIDE);
            renderer.set_visible(false);
            log_line(
                "[FusionCanvas] capture_isolation=revalidation_pending reason=%s",
                isolation_invalidation_reason_name(invalidation_reasons));

            bool revalidation_succeeded = pending.should_revalidate;
            const char* failure_stage = pending.should_revalidate
                ? "none"
                : "lifecycle";
            RECT next_vr = virtual_screen_rect();
            const int next_width = next_vr.right - next_vr.left;
            const int next_height = next_vr.bottom - next_vr.top;
            if (revalidation_succeeded &&
                (next_width <= 0 || next_height <= 0)) {
                revalidation_succeeded = false;
                failure_stage = "virtual_screen_geometry";
            }
            if (revalidation_succeeded &&
                !SetWindowPos(
                    hwnd,
                    nullptr,
                    next_vr.left,
                    next_vr.top,
                    next_width,
                    next_height,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER)) {
                revalidation_succeeded = false;
                failure_stage = "window_geometry";
                log_line(
                    "[FusionCanvas] capture_isolation=revalidation_window_geometry_failed error=%lu",
                    static_cast<unsigned long>(GetLastError()));
            }
            if (revalidation_succeeded &&
                !renderer.resize(next_width, next_height)) {
                revalidation_succeeded = false;
                failure_stage = "renderer_resize";
            }
            if (revalidation_succeeded) {
                vr = next_vr;
                const auto window_isolation =
                    fusion_overlay::enable_and_verify_capture_isolation(hwnd);
                if (!window_isolation.decision.may_show) {
                    revalidation_succeeded = false;
                    failure_stage = "window_affinity_before_probe";
                    log_line(
                        "[FusionCanvas] capture_isolation=revalidation_window_failed "
                        "reason=%s dwm_hr=0x%08lx set_error=%lu "
                        "readback_error=%lu affinity=0x%08lx",
                        fusion_overlay::capture_isolation_failure_name(
                            window_isolation.decision.failure),
                        static_cast<unsigned long>(window_isolation.dwm_hresult),
                        static_cast<unsigned long>(window_isolation.set_error),
                        static_cast<unsigned long>(window_isolation.readback_error),
                        static_cast<unsigned long>(
                            window_isolation.observation.affinity));
                }
            }
            if (revalidation_succeeded) {
                const int probe_exit_code = run_capture_isolation_probe();
                if (probe_exit_code != 0) {
                    revalidation_succeeded = false;
                    failure_stage = "dxgi_capture_probe";
                    log_line(
                        "[FusionCanvas] capture_isolation=revalidation_probe_failed code=%d",
                        probe_exit_code);
                }
            }
            if (revalidation_succeeded) {
                const auto window_isolation =
                    fusion_overlay::enable_and_verify_capture_isolation(hwnd);
                if (!window_isolation.decision.may_show) {
                    revalidation_succeeded = false;
                    failure_stage = "window_affinity_after_probe";
                    log_line(
                        "[FusionCanvas] capture_isolation=revalidation_window_failed "
                        "reason=%s dwm_hr=0x%08lx set_error=%lu "
                        "readback_error=%lu affinity=0x%08lx",
                        fusion_overlay::capture_isolation_failure_name(
                            window_isolation.decision.failure),
                        static_cast<unsigned long>(window_isolation.dwm_hresult),
                        static_cast<unsigned long>(window_isolation.set_error),
                        static_cast<unsigned long>(window_isolation.readback_error),
                        static_cast<unsigned long>(
                            window_isolation.observation.affinity));
                }
            }
            const std::uint32_t changed_during_revalidation =
                g_isolation_invalidation_reasons.exchange(0);
            if (revalidation_succeeded && changed_during_revalidation != 0) {
                revalidation_succeeded = false;
                failure_stage = "changed_during_revalidation";
                log_line(
                    "[FusionCanvas] capture_isolation=revalidation_invalidated reason=%s",
                    isolation_invalidation_reason_name(
                        changed_during_revalidation));
            }

            const auto completed = fusion_overlay::transition_capture_isolation(
                isolation_state,
                revalidation_succeeded
                    ? fusion_overlay::CaptureIsolationLifecycleEvent::RevalidationPassed
                    : fusion_overlay::CaptureIsolationLifecycleEvent::RevalidationFailed);
            isolation_state = completed.state;
            if (!completed.may_show) {
                log_line(
                    "[FusionCanvas] capture_isolation=revalidation_failed_closed "
                    "reason=%s stage=%s",
                    isolation_invalidation_reason_name(invalidation_reasons),
                    failure_stage);
                isolation_failed = true;
                g_running.store(false);
                break;
            }

            last_isolation_check = std::chrono::steady_clock::now();
            renderer.set_visible(g_visible.load());
            ShowWindow(hwnd, SW_SHOWNA);
            render_dirty = true;
            log_line(
                "[FusionCanvas] capture_isolation=revalidated reason=%s "
                "virtual_screen=%d,%d,%dx%d",
                isolation_invalidation_reason_name(invalidation_reasons),
                vr.left,
                vr.top,
                next_width,
                next_height);
        }

        const auto isolation_now = std::chrono::steady_clock::now();
        if (isolation_now - last_isolation_check >= std::chrono::seconds(1)) {
            last_isolation_check = isolation_now;
            const auto isolation =
                fusion_overlay::inspect_capture_isolation(hwnd);
            if (!isolation.decision.may_show) {
                ShowWindow(hwnd, SW_HIDE);
                log_line(
                    "[FusionCanvas] capture_isolation=lost_fail_closed reason=%s "
                    "dwm_hr=0x%08lx readback_error=%lu affinity=0x%08lx",
                    fusion_overlay::capture_isolation_failure_name(
                        isolation.decision.failure),
                    static_cast<unsigned long>(isolation.dwm_hresult),
                    static_cast<unsigned long>(isolation.readback_error),
                    static_cast<unsigned long>(isolation.observation.affinity));
                const auto failed = fusion_overlay::transition_capture_isolation(
                    isolation_state,
                    fusion_overlay::CaptureIsolationLifecycleEvent::VerificationFailed);
                isolation_state = failed.state;
                isolation_failed = true;
                g_running.store(false);
                break;
            }
        }

        // --- apply visibility toggle ---
        const bool visibility = g_visible.load();
        if (visibility != last_visibility) {
            last_visibility = visibility;
            render_dirty = true;
        }
        renderer.set_visible(visibility);

        const auto wait_now = std::chrono::steady_clock::now();
        const bool rendered_marker_expired =
            rendered_marker_visible && wait_now >= rendered_marker_expiry;
        if (rendered_marker_expired) {
            render_dirty = true;
        }

        auto wake_deadline = last_isolation_check + std::chrono::seconds(1);
        if (render_dirty) {
            wake_deadline = std::min(
                wake_deadline,
                last_frame_time + frame_interval);
        }
        if (rendered_marker_visible && !rendered_marker_expired) {
            wake_deadline = std::min(wake_deadline, rendered_marker_expiry);
        }
        if (reader.event() == nullptr) {
            wake_deadline = std::min(wake_deadline, wait_now + frame_interval);
        }

        const auto remaining_us = std::max<std::int64_t>(
            0,
            std::chrono::duration_cast<std::chrono::microseconds>(
                wake_deadline - wait_now).count());
        const std::uint64_t rounded_wait_ms =
            (static_cast<std::uint64_t>(remaining_us) + 999u) / 1000u;
        const DWORD fallback_wait_ms = static_cast<DWORD>(std::min<std::uint64_t>(
            rounded_wait_ms,
            static_cast<std::uint64_t>(MAXDWORD - 1u)));

        HANDLE wait_handles[2]{};
        DWORD handle_count = 0;
        DWORD channel_handle_index = MAXDWORD;
        if (reader.event() != nullptr) {
            channel_handle_index = handle_count;
            wait_handles[handle_count++] = reader.event();
        }
        bool timer_armed = false;
        if (deadline_timer != nullptr) {
            LARGE_INTEGER due_time{};
            due_time.QuadPart = -std::max<LONGLONG>(
                1,
                static_cast<LONGLONG>(remaining_us) * 10);
            timer_armed = SetWaitableTimer(
                deadline_timer,
                &due_time,
                0,
                nullptr,
                nullptr,
                FALSE) != FALSE;
            if (timer_armed) {
                wait_handles[handle_count++] = deadline_timer;
            } else if (!timer_failure_logged) {
                timer_failure_logged = true;
                log_line(
                    "[FusionCanvas] waitable timer arm failed; using deadline timeout error=%lu",
                    static_cast<unsigned long>(GetLastError()));
            }
        }

        const DWORD wait_result = MsgWaitForMultipleObjectsEx(
            handle_count,
            handle_count > 0 ? wait_handles : nullptr,
            timer_armed ? INFINITE : fallback_wait_ms,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (wait_result == WAIT_FAILED) {
            log_line(
                "[FusionCanvas] event wait failed error=%lu",
                static_cast<unsigned long>(GetLastError()));
            wait_failed = true;
            g_running.store(false);
            break;
        }
        const bool channel_signaled =
            channel_handle_index != MAXDWORD &&
            wait_result == WAIT_OBJECT_0 + channel_handle_index;

        // Consume each signaled latest-state update before applying the render
        // cap. Direct observations can be shorter than one 30 FPS interval;
        // reading only on render ticks loses those observations entirely.
        std::uint64_t frame_id = 0;
        std::uint64_t timestamp = 0;
        std::int32_t fw = 0, fh = 0;
        shared_fusion::FusionFrameGeometry geometry{};
        shared_fusion::FusionTarget target{};
        const shared_fusion::FusionDetection* detections = nullptr;
        std::uint32_t det_count = 0;

        const bool poll_without_event = reader.event() == nullptr;
        if ((channel_signaled || poll_without_event) && reader.try_read(
                frame_id,
                timestamp,
                fw,
                fh,
                geometry,
                target,
                detections,
                det_count)) {
            current_target = target;
            current_frame_id = frame_id;
            current_timestamp = timestamp;
            current_frame_width = fw > 0 ? fw : current_frame_width;
            current_frame_height = fh > 0 ? fh : current_frame_height;
            current_geometry = geometry;
            current_detection_count = std::min(
                det_count,
                shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS);
            for (std::uint32_t i = 0; i < current_detection_count; ++i) {
                current_detections[i] = detections[i];
            }

            if (target.has_target && target.has_body_box &&
                target.direct_observation &&
                target.selector_target_generation != 0) {
                if (!latched_direct_available ||
                    latched_direct_target.selector_target_generation !=
                        target.selector_target_generation) {
                    latched_direct_pending_render = false;
                }
                if (previous_sample_was_direct &&
                    previous_sample_generation ==
                        target.selector_target_generation) {
                    ++direct_burst_sample_count;
                } else {
                    direct_burst_sample_count = 1;
                }
                latched_direct_target = target;
                latched_direct_geometry = geometry;
                latched_direct_frame_width = current_frame_width;
                latched_direct_frame_height = current_frame_height;
                latched_direct_timestamp = timestamp;
                latched_direct_available = true;
                latched_direct_pending_render =
                    latched_direct_pending_render ||
                    direct_burst_sample_count >= 2;
            } else if (
                latched_direct_available &&
                target.selector_target_generation != 0 &&
                target.selector_target_generation !=
                    latched_direct_target.selector_target_generation) {
                latched_direct_target = {};
                latched_direct_geometry = {};
                latched_direct_timestamp = 0;
                latched_direct_available = false;
                latched_direct_pending_render = false;
                direct_burst_sample_count = 0;
            } else {
                direct_burst_sample_count = 0;
            }
            previous_sample_was_direct =
                target.has_target && target.has_body_box &&
                target.direct_observation;
            previous_sample_generation = target.selector_target_generation;
            if (visibility) {
                render_dirty = true;
            }
        }

        const auto after_wait = std::chrono::steady_clock::now();
        if (rendered_marker_visible && after_wait >= rendered_marker_expiry) {
            render_dirty = true;
        }

        // Events drive state ingestion. The cap only coalesces DWM presents.
        const auto now = std::chrono::steady_clock::now();
        if (!render_dirty || now - last_frame_time < frame_interval) {
            continue;
        }
        last_frame_time = now;

        shared_fusion::FusionTarget render_target = current_target;
        shared_fusion::FusionFrameGeometry render_geometry = current_geometry;
        std::int32_t render_frame_width = current_frame_width;
        std::int32_t render_frame_height = current_frame_height;
        std::uint32_t render_detection_count = current_detection_count;
        const bool has_data = current_frame_id != UINT64_MAX;
        LARGE_INTEGER qpc_now{};
        const bool qpc_available =
            qpc_frequency != 0 && QueryPerformanceCounter(&qpc_now) != FALSE;
        const bool current_sample_fresh =
            has_data &&
            qpc_available &&
            fusion_overlay::fusion_sample_is_fresh(
                static_cast<std::uint64_t>(qpc_now.QuadPart),
                current_timestamp,
                qpc_frequency,
                STALE_DATA_TIMEOUT_MS);
        const fusion_overlay::MarkerContinuitySource continuity_source =
            fusion_overlay::decide_marker_continuity({
                current_sample_fresh,
                current_target.has_target,
                current_target.has_body_box,
                current_target.direct_observation,
                current_target.enemy_identity_confirmed,
                current_target.selector_target_generation,
                latched_direct_available,
                latched_direct_pending_render,
                latched_direct_target.selector_target_generation,
                latched_direct_timestamp,
                qpc_available ? static_cast<std::uint64_t>(qpc_now.QuadPart) : 0,
                qpc_frequency,
                STALE_DATA_TIMEOUT_MS,
            });
        if (continuity_source ==
            fusion_overlay::MarkerContinuitySource::LatchedDirectPendingRender) {
            render_target = latched_direct_target;
            render_geometry = latched_direct_geometry;
            render_frame_width = latched_direct_frame_width;
            render_frame_height = latched_direct_frame_height;
            render_detection_count = 0;
        } else if (continuity_source ==
                   fusion_overlay::MarkerContinuitySource::LatchedConfirmedContinuation) {
            // The selector owns cue-hold geometry and identity. This copy only
            // grants visual continuity; it does not feed control authority.
            render_target.direct_observation = true;
            render_detection_count = 0;
        } else if (continuity_source ==
                   fusion_overlay::MarkerContinuitySource::None) {
            render_target = {};
            render_detection_count = 0;
        }
        if (!visibility) {
            render_target = {};
            render_detection_count = 0;
        }

        // --- render ---
        renderer.render(
            render_target,
            current_detections,
            render_detection_count,
            render_frame_width,
            render_frame_height,
            render_geometry,
            vr.left,
            vr.top,
            visibility ? idle_mode : IdleMode::Hide);
        render_dirty = false;
        rendered_marker_visible =
            visibility &&
            continuity_source != fusion_overlay::MarkerContinuitySource::None;
        if (rendered_marker_visible && qpc_available) {
            const std::uint64_t rendered_timestamp =
                continuity_source ==
                    fusion_overlay::MarkerContinuitySource::LatestDirect
                ? current_timestamp
                : latched_direct_timestamp;
            const double elapsed_ms =
                rendered_timestamp != 0 &&
                    static_cast<std::uint64_t>(qpc_now.QuadPart) >= rendered_timestamp
                ? static_cast<double>(
                      static_cast<std::uint64_t>(qpc_now.QuadPart) - rendered_timestamp) *
                      1000.0 / static_cast<double>(qpc_frequency)
                : static_cast<double>(STALE_DATA_TIMEOUT_MS);
            const double remaining_ms = std::max(
                0.0,
                static_cast<double>(STALE_DATA_TIMEOUT_MS) - elapsed_ms);
            rendered_marker_expiry =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double, std::milli>(remaining_ms));
        }
        if (visibility &&
            continuity_source != fusion_overlay::MarkerContinuitySource::None) {
            latched_direct_pending_render = false;
        }
    }

    // --- cleanup ---
    UnregisterHotKey(nullptr, HOTKEY_TOGGLE);
    UnregisterHotKey(nullptr, HOTKEY_EXIT);
    if (deadline_timer != nullptr) {
        CancelWaitableTimer(deadline_timer);
        CloseHandle(deadline_timer);
        deadline_timer = nullptr;
    }
    renderer.cleanup();
    DestroyWindow(hwnd);
    reader.close();

    log_line("[FusionCanvas] exited cleanly");
    if (g_log_file != nullptr) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
    if (isolation_failed) return 3;
    return wait_failed ? 2 : 0;
}
