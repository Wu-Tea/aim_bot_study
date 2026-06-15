#pragma once

#include "../common_native/screen_geometry.h"

namespace recoil_native {

struct RecoilDebugComponents {
    bool recoil_active = false;
    common_native::Vec2f recoil_stick;
    common_native::Vec2f visual_stick;
};

}  // namespace recoil_native
