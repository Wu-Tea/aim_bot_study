#include "color_readback.h"
#include "test_support/native_test_registry.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, int line) {
    if (!value) throw std::runtime_error(
        "color readback assertion failed at line " + std::to_string(line));
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

void register_color_readback_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "pageable_buffer_reuses_high_watermark", test_pageable_buffer_reuses_high_watermark);
    registry.add_case("BaseContracts", "pinned_failure_falls_back_to_pageable", test_pinned_failure_falls_back_to_pageable);
    registry.add_case("BaseContracts", "transfer_failure_forces_pageable_fallback", test_transfer_failure_can_force_pageable_fallback);
}
