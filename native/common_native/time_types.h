#pragma once

namespace common_native {

struct TimeSeconds {
    double value = 0.0;
};

struct DurationSeconds {
    double value = 0.0;
};

inline double duration_ms(DurationSeconds duration) {
    return duration.value * 1000.0;
}

inline DurationSeconds operator-(TimeSeconds newer, TimeSeconds older) {
    return DurationSeconds{newer.value - older.value};
}

}  // namespace common_native
