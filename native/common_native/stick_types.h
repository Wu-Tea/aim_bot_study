#pragma once

namespace common_native {

struct Stick2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct StickComponents {
    Stick2f manual;
    Stick2f assist;
    Stick2f dynamics;
    Stick2f recoil;
    Stick2f final_output;
};

}  // namespace common_native
