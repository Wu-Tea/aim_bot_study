#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vision_native {

enum class ColorReadbackMode { Pageable, Pinned, PageableFallback };

const char* color_readback_mode_name(ColorReadbackMode mode);

class ColorReadbackBuffer {
public:
    explicit ColorReadbackBuffer(bool prefer_pinned, bool force_pinned_failure_for_test = false);
    ~ColorReadbackBuffer();
    ColorReadbackBuffer(const ColorReadbackBuffer&) = delete;
    ColorReadbackBuffer& operator=(const ColorReadbackBuffer&) = delete;

    bool ensure(std::size_t bytes) noexcept;
    std::uint8_t* data() noexcept;
    std::size_t capacity() const noexcept;
    ColorReadbackMode mode() const noexcept;
    std::uint64_t pinned_failures() const noexcept;

private:
    void release_pinned() noexcept;
    bool prefer_pinned_ = false;
    bool force_pinned_failure_for_test_ = false;
    void* pinned_ = nullptr;
    std::vector<std::uint8_t> pageable_;
    std::size_t capacity_ = 0;
    ColorReadbackMode mode_ = ColorReadbackMode::Pageable;
    std::uint64_t pinned_failures_ = 0;
};

} // namespace vision_native
