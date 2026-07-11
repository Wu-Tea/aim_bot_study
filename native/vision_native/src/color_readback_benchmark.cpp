#include "color_readback.h"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(cudaError_t status, const char* what) {
    if (status != cudaSuccess) throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(status));
}
double percentile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    const auto rank = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(q * values.size())));
    return values[rank - 1];
}
struct Result { std::vector<double> ms; const char* mode = ""; };
Result run(cudaArray_t array, cudaStream_t stream, int width, int height, bool pinned, int iterations) {
    vision_native::ColorReadbackBuffer buffer(pinned);
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    if (!buffer.ensure(bytes)) throw std::runtime_error("buffer allocation failed");
    Result result;
    result.mode = vision_native::color_readback_mode_name(buffer.mode());
    result.ms.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        check(cudaMemcpy2DFromArrayAsync(
            buffer.data(), static_cast<std::size_t>(width) * 4, array, 0, 0,
            static_cast<std::size_t>(width) * 4, height, cudaMemcpyDeviceToHost, stream), "copy");
        check(cudaStreamSynchronize(stream), "sync");
        result.ms.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    return result;
}
}

int main(int argc, char** argv) {
    int iterations = 2000;
    std::string output = "color_readback_benchmark.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) iterations = std::stoi(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) output = argv[++i];
    }
    constexpr int width = 320, height = 256;
    std::vector<unsigned char> source(static_cast<std::size_t>(width) * height * 4, 127);
    cudaChannelFormatDesc desc = cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned);
    cudaArray_t array = nullptr;
    cudaStream_t stream = nullptr;
    check(cudaMallocArray(&array, &desc, width, height), "cudaMallocArray");
    check(cudaStreamCreate(&stream), "cudaStreamCreate");
    check(cudaMemcpy2DToArray(array, 0, 0, source.data(), width * 4, width * 4, height,
        cudaMemcpyHostToDevice), "upload");
    const Result pageable = run(array, stream, width, height, false, iterations);
    const Result pinned = run(array, stream, width, height, true, iterations);
    cudaStreamDestroy(stream);
    cudaFreeArray(array);
    const double pageable_p95 = percentile(pageable.ms, .95);
    const double pinned_p95 = percentile(pinned.ms, .95);
    std::ofstream out(output, std::ios::trunc);
    out << "{\n  \"iterations\": " << iterations
        << ",\n  \"bytes\": " << source.size()
        << ",\n  \"pageable_mode\": \"" << pageable.mode << "\""
        << ",\n  \"pinned_mode\": \"" << pinned.mode << "\""
        << ",\n  \"pageable_p95_ms\": " << pageable_p95
        << ",\n  \"pinned_p95_ms\": " << pinned_p95
        << ",\n  \"p95_improvement_percent\": "
        << (pageable_p95 > 0 ? (pageable_p95 - pinned_p95) * 100.0 / pageable_p95 : 0.0)
        << "\n}\n";
    return 0;
}
