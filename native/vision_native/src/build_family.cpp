#include "vision_native/build_family.h"

#include <stdexcept>

namespace vision_native {

BuildFamily compiled_build_family() {
    return BuildFamily::Modern;
}

const char* build_family_name(BuildFamily) {
    return "modern";
}

void validate_runtime_artifact_family(
    BuildFamily,
    const std::string&,
    int compute_major,
    int compute_minor) {
    if (compute_major < 7 || (compute_major == 7 && compute_minor < 5)) {
        throw std::runtime_error(
            "native vision runtime requires Turing/SM 7.5 or newer");
    }
}

} // namespace vision_native
