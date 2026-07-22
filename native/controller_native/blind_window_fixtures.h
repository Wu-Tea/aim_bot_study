#pragma once

#include "blind_window_benchmark.h"

#include <cstdint>

namespace controller_native::blind_window {

BlindTimingProfile timing_100hz_phase_5() noexcept;

BlindFixture bodylock_pending_crossing_fixture(
    std::uint32_t seed,
    BlindTimingProfile timing);

BlindControllerStep stale_proportional_controller();

}  // namespace controller_native::blind_window
