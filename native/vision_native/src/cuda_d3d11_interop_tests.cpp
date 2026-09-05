#include "vision_native/cuda_graphics_mapping.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cuda_d3d11_interop.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void check(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
void check(HRESULT status) {
    if (FAILED(status)) throw std::runtime_error("D3D11 fixture setup failed");
}

struct GpuResources {
    cudaStream_t stream = nullptr;
    cudaGraphicsResource_t resource = nullptr;
    void* device = nullptr;
    void* host = nullptr;
    ~GpuResources() {
        if (stream) (void)cudaStreamSynchronize(stream);
        if (resource) (void)cudaGraphicsUnregisterResource(resource);
        if (device) (void)cudaFree(device);
        if (host) (void)cudaFreeHost(host);
        if (stream) (void)cudaStreamDestroy(stream);
    }
};

}  // namespace

int main() {
    try {
        using Microsoft::WRL::ComPtr;
        ComPtr<IDXGIFactory1> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        int device_index = -1;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            if (cudaD3D11GetDevice(&device_index, adapter.Get()) == cudaSuccess) break;
            adapter.Reset();
            device_index = -1;
        }
        if (device_index < 0) {
            std::cerr << "[INVALID] No CUDA/D3D11 adapter for interop verification\n";
            return 3;
        }
        check(cudaSetDevice(device_index));
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &device, nullptr, &context));
        constexpr UINT width = 256, height = 256;
        constexpr std::size_t bytes = width * height * sizeof(std::uint32_t);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        check(device->CreateTexture2D(&desc, nullptr, &texture));
        GpuResources gpu;
        check(cudaStreamCreateWithFlags(&gpu.stream, cudaStreamNonBlocking));
        check(cudaGraphicsD3D11RegisterResource(&gpu.resource, texture.Get(), cudaGraphicsRegisterFlagsNone));
        check(cudaMalloc(&gpu.device, bytes));
        check(cudaHostAlloc(&gpu.host, bytes, cudaHostAllocDefault));

        bool rejected_default_stream = false;
        try { vision_native::CudaGraphicsMapping invalid(gpu.resource, nullptr); }
        catch (const std::invalid_argument&) { rejected_default_stream = true; }
        if (!rejected_default_stream) throw std::runtime_error("mapping accepted an unspecified consumer stream");

        std::vector<std::uint32_t> pixels(width * height);
        constexpr unsigned frames = 512;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned frame = 1; frame <= frames; ++frame) {
            for (unsigned i = 0; i < pixels.size(); ++i)
                pixels[i] = 0xff000000u | ((frame * 65537u + i * 12347u) & 0x00ffffffu);
            context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), width * 4, 0);
            context->Flush();
            {
                // The same production mapping owner used by VisionEngine.
                vision_native::CudaGraphicsMapping mapping(gpu.resource, gpu.stream);
                check(cudaMemcpy2DFromArrayAsync(gpu.device, width * 4, mapping.array(), 0, 0,
                                                width * 4, height, cudaMemcpyDeviceToDevice, gpu.stream));
                check(cudaMemcpyAsync(gpu.host, gpu.device, bytes, cudaMemcpyDeviceToHost, gpu.stream));
                // Exercise both explicit close and automatic scope cleanup.
                if ((frame % 2) == 0) mapping.unmap();
            }
            check(cudaStreamSynchronize(gpu.stream));
            const auto* observed = static_cast<const std::uint32_t*>(gpu.host);
            if (!std::equal(pixels.begin(), pixels.end(), observed))
                throw std::runtime_error("D3D/CUDA frame content mismatch at frame " + std::to_string(frame));
        }
        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        std::cout << "[PASS] CudaD3D11Interop frames=" << frames
                  << " pixels_per_frame=" << pixels.size()
                  << " mismatches=0 elapsed_ms=" << ms << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] CudaD3D11Interop: " << error.what() << '\n';
        return 1;
    }
}
