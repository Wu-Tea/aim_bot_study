#include "color_readback.h"

#include <cuda_runtime_api.h>

namespace vision_native {

const char* color_readback_mode_name(ColorReadbackMode mode) {
    switch (mode) {
    case ColorReadbackMode::Pinned: return "pinned";
    case ColorReadbackMode::PageableFallback: return "pageable_fallback";
    case ColorReadbackMode::Pageable:
    default: return "pageable";
    }
}

ColorReadbackBuffer::ColorReadbackBuffer(
    bool prefer_pinned, bool force_pinned_failure_for_test)
    : prefer_pinned_(prefer_pinned),
      force_pinned_failure_for_test_(force_pinned_failure_for_test) {}

ColorReadbackBuffer::~ColorReadbackBuffer() { release_pinned(); }

void ColorReadbackBuffer::release_pinned() noexcept {
    if (pinned_ != nullptr) cudaFreeHost(pinned_);
    pinned_ = nullptr;
}

bool ColorReadbackBuffer::ensure(std::size_t bytes) noexcept {
    if (bytes <= capacity_ && data() != nullptr) return true;
    if (prefer_pinned_) {
        release_pinned();
        void* candidate = nullptr;
        const cudaError_t status = force_pinned_failure_for_test_
            ? cudaErrorMemoryAllocation
            : cudaMallocHost(&candidate, bytes);
        if (status == cudaSuccess && candidate != nullptr) {
            pinned_ = candidate;
            pageable_.clear();
            capacity_ = bytes;
            mode_ = ColorReadbackMode::Pinned;
            return true;
        }
        ++pinned_failures_;
        mode_ = ColorReadbackMode::PageableFallback;
    } else {
        mode_ = ColorReadbackMode::Pageable;
    }
    try {
        pageable_.resize(bytes);
        capacity_ = pageable_.size();
        return true;
    } catch (...) {
        capacity_ = 0;
        return false;
    }
}

std::uint8_t* ColorReadbackBuffer::data() noexcept {
    return pinned_ != nullptr ? static_cast<std::uint8_t*>(pinned_) :
        (pageable_.empty() ? nullptr : pageable_.data());
}

std::size_t ColorReadbackBuffer::capacity() const noexcept { return capacity_; }
ColorReadbackMode ColorReadbackBuffer::mode() const noexcept { return mode_; }
std::uint64_t ColorReadbackBuffer::pinned_failures() const noexcept { return pinned_failures_; }

} // namespace vision_native
