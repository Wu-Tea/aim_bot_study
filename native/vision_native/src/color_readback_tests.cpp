#include "color_readback.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
void require(bool value, int line) {
    if (!value) { std::cerr << "require failed at line " << line << '\n'; std::abort(); }
}
#define REQUIRE(v) require((v), __LINE__)

void test_pageable_buffer_reuses_high_watermark() {
    vision_native::ColorReadbackBuffer buffer(false);
    REQUIRE(buffer.ensure(1024));
    auto* first = buffer.data();
    REQUIRE(first != nullptr);
    REQUIRE(buffer.capacity() >= 1024);
    REQUIRE(buffer.mode() == vision_native::ColorReadbackMode::Pageable);
    REQUIRE(buffer.ensure(512));
    REQUIRE(buffer.data() == first);
}

void test_pinned_failure_falls_back_to_pageable() {
    vision_native::ColorReadbackBuffer buffer(true, true);
    REQUIRE(buffer.ensure(2048));
    REQUIRE(buffer.data() != nullptr);
    REQUIRE(buffer.mode() == vision_native::ColorReadbackMode::PageableFallback);
    REQUIRE(buffer.pinned_failures() == 1);
}

void test_transfer_failure_can_force_pageable_fallback() {
    vision_native::ColorReadbackBuffer buffer(false);
    REQUIRE(buffer.ensure(512));
    REQUIRE(buffer.fallback_to_pageable(2048));
    REQUIRE(buffer.data() != nullptr);
    REQUIRE(buffer.capacity() >= 2048);
    REQUIRE(buffer.mode() == vision_native::ColorReadbackMode::PageableFallback);
}
}

int main() {
    test_pageable_buffer_reuses_high_watermark();
    test_pinned_failure_falls_back_to_pageable();
    test_transfer_failure_can_force_pageable_fallback();
    return 0;
}
