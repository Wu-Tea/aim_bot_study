#pragma once
// Hardware-only offline benchmark helpers. No runtime scheduling policy changes.
#include <windows.h>
#include <cuda_d3d11_interop.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include "runtime_app/runtime_timing.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace vision_bench {
using Clock = std::chrono::steady_clock;
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
inline void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("D3D HRESULT " + std::to_string(result));
}
inline void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
inline double qpc_seconds() {
    LARGE_INTEGER value{}, frequency{};
    QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
    return static_cast<double>(value.QuadPart) / frequency.QuadPart;
}
inline double milliseconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
struct Device {
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> context;
    Device() {
        Ptr<IDXGIFactory1> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Ptr<IDXGIAdapter1> adapter; int cuda_device = -1;
        for (UINT i=0; ; ++i) {
            const auto result = factory->EnumAdapters1(i, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            check(result);
            if (cudaD3D11GetDevice(&cuda_device, adapter.Get()) == cudaSuccess) break;
            adapter.Reset(); cuda_device = -1;
        }
        if (cuda_device < 0 || !adapter) throw std::runtime_error("No CUDA/D3D11 adapter");
        check(cudaSetDevice(cuda_device));
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context));
        DXGI_ADAPTER_DESC1 desc{}; check(adapter->GetDesc1(&desc));
        std::cout << "ADAPTER " << desc.AdapterLuid.HighPart << " " << desc.AdapterLuid.LowPart
                  << " CUDA " << cuda_device << std::endl;
    }
};
inline void validate_phase(double hz, double seconds) {
    if (!std::isfinite(hz) || !std::isfinite(seconds) || hz <= 0 || hz > 1000 ||
        seconds < 1 || seconds > 180) throw std::runtime_error("Invalid bounded phase");
}
} // namespace vision_bench
