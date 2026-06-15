#include "recoil_visual_model.h"

namespace recoil_native {

RecoilVisualDisplacement DisabledRecoilVisualModel::compute(
    const RecoilVisualInput& /*input*/) const {
    return {};
}

}  // namespace recoil_native
