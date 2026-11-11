#pragma once

#include "aim_response_curve_plugin.h"

namespace controller_native {

// Compatibility for previously saved profiles. New configurations use generic LUTs.
inline constexpr auto& kCodLinearReferenceStick = curve_library_data::cod_linear_legacy_0;
inline constexpr auto& kCodLinearReferenceResponse = curve_library_data::cod_linear_legacy_1;

inline float cod_linear_reference_magnitude(float value, bool decode) noexcept {
    return decode
        ? aim_response_curve_detail::interpolate_monotonic(value,
            kCodLinearReferenceStick, kCodLinearReferenceResponse)
        : aim_response_curve_detail::interpolate_monotonic(value,
            kCodLinearReferenceResponse, kCodLinearReferenceStick);
}

} // namespace controller_native
