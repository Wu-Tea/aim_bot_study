#include "vision_native/dxgi_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace vision_native {
namespace {

using Microsoft::WRL::ComPtr;

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

double now_ms() {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count())
        / 1'000'000.0;
}

std::string hresult_hex(HRESULT hr) {
    std::ostringstream out;
    out << "0x" << std::hex << static_cast<unsigned long>(hr);
    return out.str();
}

void check_hresult(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + " failed: " + hresult_hex(hr));
    }
}

bool output_contains_origin(const DXGI_OUTPUT_DESC& desc) {
    const RECT& rect = desc.DesktopCoordinates;
    return rect.left <= 0 && rect.top <= 0 && rect.right > 0 && rect.bottom > 0;
}

} // namespace

struct DxgiRoiCapture::Impl {
    int requested_width = 0;
    int requested_height = 0;
    int requested_adapter_index = 0;
    int requested_output_index = -1;
    int timeout_ms = 0;

    int selected_adapter_index = 0;
    int selected_output_index = 0;
    int output_width = 0;
    int output_height = 0;
    int roi_left = 0;
    int roi_top = 0;
    uint64_t next_frame_id = 1;

    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    ComPtr<IDXGIOutput1> output1;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> roi_texture;

    Impl(int width, int height, int adapter_index, int output_index, int timeout)
        : requested_width(width),
          requested_height(height),
          requested_adapter_index(adapter_index),
          requested_output_index(output_index),
          timeout_ms(timeout) {
        if (requested_width <= 0 || requested_height <= 0) {
            throw std::runtime_error("DxgiRoiCapture width and height must be positive");
        }
        initialize();
    }

    void initialize() {
        select_output();
        create_device();
        create_duplication();
        create_roi_texture();
    }

    void select_output() {
        ComPtr<IDXGIFactory1> factory;
        check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

        ComPtr<IDXGIAdapter1> current_adapter;
        for (UINT adapter_idx = 0; factory->EnumAdapters1(adapter_idx, &current_adapter) != DXGI_ERROR_NOT_FOUND; ++adapter_idx) {
            if (static_cast<int>(adapter_idx) != requested_adapter_index) {
                current_adapter.Reset();
                continue;
            }

            ComPtr<IDXGIOutput> first_attached;
            DXGI_OUTPUT_DESC first_desc{};
            int first_index = -1;

            ComPtr<IDXGIOutput> current_output;
            for (UINT output_idx = 0; current_adapter->EnumOutputs(output_idx, &current_output) != DXGI_ERROR_NOT_FOUND; ++output_idx) {
                DXGI_OUTPUT_DESC desc{};
                check_hresult(current_output->GetDesc(&desc), "IDXGIOutput::GetDesc");
                if (!desc.AttachedToDesktop) {
                    current_output.Reset();
                    continue;
                }

                const bool requested = requested_output_index >= 0 && static_cast<int>(output_idx) == requested_output_index;
                const bool primary_like = requested_output_index < 0 && output_contains_origin(desc);
                if (requested || primary_like) {
                    adapter = current_adapter;
                    output = current_output;
                    selected_adapter_index = static_cast<int>(adapter_idx);
                    selected_output_index = static_cast<int>(output_idx);
                    set_output_geometry(desc);
                    return;
                }

                if (first_index < 0) {
                    first_attached = current_output;
                    first_desc = desc;
                    first_index = static_cast<int>(output_idx);
                }
                current_output.Reset();
            }

            if (first_attached) {
                adapter = current_adapter;
                output = first_attached;
                selected_adapter_index = static_cast<int>(adapter_idx);
                selected_output_index = first_index;
                set_output_geometry(first_desc);
                return;
            }
        }

        throw std::runtime_error("no attached DXGI output found for requested adapter/output");
    }

    void set_output_geometry(const DXGI_OUTPUT_DESC& desc) {
        const RECT& rect = desc.DesktopCoordinates;
        output_width = rect.right - rect.left;
        output_height = rect.bottom - rect.top;
        if (requested_width > output_width || requested_height > output_height) {
            throw std::runtime_error("requested ROI is larger than the selected output");
        }
        roi_left = (output_width - requested_width) / 2;
        roi_top = (output_height - requested_height) / 2;
    }

    void create_device() {
        static constexpr D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D_FEATURE_LEVEL selected_level{};
        check_hresult(
            D3D11CreateDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                feature_levels,
                ARRAYSIZE(feature_levels),
                D3D11_SDK_VERSION,
                &device,
                &selected_level,
                &context),
            "D3D11CreateDevice");
    }

    void create_duplication() {
        check_hresult(output.As(&output1), "IDXGIOutput1 query");
        check_hresult(output1->DuplicateOutput(device.Get(), &duplication), "IDXGIOutput1::DuplicateOutput");
    }

    void create_roi_texture() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(requested_width);
        desc.Height = static_cast<UINT>(requested_height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        check_hresult(device->CreateTexture2D(&desc, nullptr, &roi_texture), "ID3D11Device::CreateTexture2D ROI");
    }

    void rebuild_duplication() {
        duplication.Reset();
        output1.Reset();
        create_duplication();
    }

    DxgiCaptureMetadata empty_metadata(float acquire_ms = 0.0f) const {
        DxgiCaptureMetadata metadata;
        metadata.updated = false;
        metadata.frame.width = requested_width;
        metadata.frame.height = requested_height;
        metadata.frame.format = PixelFormat::BGRA8;
        metadata.frame.memory_kind = MemoryKind::D3D11Texture;
        metadata.roi_left = roi_left;
        metadata.roi_top = roi_top;
        metadata.output_width = output_width;
        metadata.output_height = output_height;
        metadata.adapter_index = selected_adapter_index;
        metadata.output_index = selected_output_index;
        metadata.acquire_ms = acquire_ms;
        return metadata;
    }

    DxgiCaptureMetadata grab() {
        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> desktop_resource;

        const double acquire_start = now_ms();
        HRESULT hr = duplication->AcquireNextFrame(static_cast<UINT>(timeout_ms), &frame_info, &desktop_resource);
        const double acquire_end = now_ms();
        const float acquire_elapsed = static_cast<float>(acquire_end - acquire_start);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return empty_metadata(acquire_elapsed);
        }
        if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
            rebuild_duplication();
            return empty_metadata(acquire_elapsed);
        }
        check_hresult(hr, "IDXGIOutputDuplication::AcquireNextFrame");

        bool frame_acquired = true;
        try {
            ComPtr<ID3D11Texture2D> desktop_texture;
            check_hresult(desktop_resource.As(&desktop_texture), "IDXGIResource texture query");

            D3D11_BOX source_box{};
            source_box.left = static_cast<UINT>(roi_left);
            source_box.top = static_cast<UINT>(roi_top);
            source_box.front = 0;
            source_box.right = static_cast<UINT>(roi_left + requested_width);
            source_box.bottom = static_cast<UINT>(roi_top + requested_height);
            source_box.back = 1;

            const double copy_start = now_ms();
            context->CopySubresourceRegion(
                roi_texture.Get(),
                0,
                0,
                0,
                0,
                desktop_texture.Get(),
                0,
                &source_box);
            context->Flush();
            const double copy_end = now_ms();

            duplication->ReleaseFrame();
            frame_acquired = false;

            DxgiCaptureMetadata metadata = empty_metadata(acquire_elapsed);
            metadata.updated = true;
            metadata.frame.frame_id = next_frame_id++;
            metadata.frame.captured_at_ns = now_ns();
            metadata.frame.width = requested_width;
            metadata.frame.height = requested_height;
            metadata.frame.format = PixelFormat::BGRA8;
            metadata.frame.memory_kind = MemoryKind::D3D11Texture;
            metadata.frame.row_pitch = 0;
            metadata.frame.data = roi_texture.Get();
            metadata.copy_ms = static_cast<float>(copy_end - copy_start);
            return metadata;
        } catch (...) {
            if (frame_acquired) {
                duplication->ReleaseFrame();
            }
            throw;
        }
    }
};

DxgiRoiCapture::DxgiRoiCapture(
    int width,
    int height,
    int adapter_index,
    int output_index,
    int timeout_ms)
    : impl_(std::make_unique<Impl>(width, height, adapter_index, output_index, timeout_ms)) {}

DxgiRoiCapture::~DxgiRoiCapture() = default;

DxgiCaptureMetadata DxgiRoiCapture::grab() {
    return impl_->grab();
}

int DxgiRoiCapture::width() const {
    return impl_->requested_width;
}

int DxgiRoiCapture::height() const {
    return impl_->requested_height;
}

int DxgiRoiCapture::output_width() const {
    return impl_->output_width;
}

int DxgiRoiCapture::output_height() const {
    return impl_->output_height;
}

int DxgiRoiCapture::roi_left() const {
    return impl_->roi_left;
}

int DxgiRoiCapture::roi_top() const {
    return impl_->roi_top;
}

void* DxgiRoiCapture::d3d11_device() const {
    return impl_->device.Get();
}

void* DxgiRoiCapture::texture() const {
    return impl_->roi_texture.Get();
}

} // namespace vision_native
