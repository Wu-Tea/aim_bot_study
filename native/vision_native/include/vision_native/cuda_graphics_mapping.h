#pragma once

#include <cuda_runtime_api.h>
#include <stdexcept>
#include <string>

namespace vision_native {

// Map, all array reads, and unmap share the engine stream. A default-stream
// map cannot order reads submitted to TensorRT's cudaStreamNonBlocking stream.
class CudaGraphicsMapping {
public:
    CudaGraphicsMapping(cudaGraphicsResource_t resource, cudaStream_t stream)
        : resource_(resource), stream_(stream) {
        if (resource_ == nullptr || stream_ == nullptr) {
            throw std::invalid_argument("CUDA graphics mapping requires the consuming engine stream");
        }
        check(cudaGraphicsMapResources(1, &resource_, stream_), "cudaGraphicsMapResources");
        mapped_ = true;
        const auto status = cudaGraphicsSubResourceGetMappedArray(&array_, resource_, 0, 0);
        if (status != cudaSuccess) {
            (void)cudaGraphicsUnmapResources(1, &resource_, stream_);
            mapped_ = false;
            check(status, "cudaGraphicsSubResourceGetMappedArray");
        }
    }

    ~CudaGraphicsMapping() {
        if (mapped_) (void)cudaGraphicsUnmapResources(1, &resource_, stream_);
    }
    CudaGraphicsMapping(const CudaGraphicsMapping&) = delete;
    CudaGraphicsMapping& operator=(const CudaGraphicsMapping&) = delete;

    cudaArray_t array() const noexcept { return array_; }
    void unmap() {
        check(cudaGraphicsUnmapResources(1, &resource_, stream_), "cudaGraphicsUnmapResources");
        mapped_ = false;
    }

private:
    static void check(cudaError_t status, const char* operation) {
        if (status != cudaSuccess) {
            throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
        }
    }
    cudaGraphicsResource_t resource_ = nullptr;
    cudaStream_t stream_ = nullptr;
    cudaArray_t array_ = nullptr;
    bool mapped_ = false;
};

}  // namespace vision_native
