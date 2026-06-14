#pragma once

#include "../common_native/screen_geometry.h"

namespace recoil_native {

struct RecoilVisualInput {
    bool recoil_active = false;
    common_native::Vec2f recoil_stick;
};

struct RecoilVisualDisplacement {
    common_native::Vec2f stick;
};

class DisabledRecoilVisualModel {
public:
    RecoilVisualDisplacement compute(const RecoilVisualInput& input) const;
};

}  // namespace recoil_native
