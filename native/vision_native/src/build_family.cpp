#include "vision_native/build_family.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vision_native {

BuildFamily compiled_build_family() {
#if defined(VISION_BUILD_FAMILY_PASCAL)
    return BuildFamily::Pascal;
#else
    return BuildFamily::Modern;
#endif
}

const char* build_family_name(BuildFamily family) {
    return family == BuildFamily::Pascal ? "pascal" : "modern";
}

void validate_runtime_artifact_family(
    BuildFamily family,
    const std::string& engine_path,
    int compute_major,
    int compute_minor) {
    std::string lower = engine_path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const bool tagged_pascal = lower.find("pascal") != std::string::npos ||
        lower.find("sm61") != std::string::npos;
    if (family == BuildFamily::Pascal) {
        if (compute_major != 6 || compute_minor != 1) {
            throw std::runtime_error(
                "pascal build requires an SM 6.1 GPU and a separately built Pascal engine");
        }
        if (!tagged_pascal) {
            throw std::runtime_error(
                "pascal build requires an engine path tagged 'pascal' or 'sm61'");
        }
    } else {
        if (compute_major < 7 || (compute_major == 7 && compute_minor < 5)) {
            throw std::runtime_error(
                "modern build requires Turing/SM 7.5 or newer; use the Pascal build for SM 6.1");
        }
        if (tagged_pascal) {
            throw std::runtime_error("modern build refuses a Pascal/SM61 engine artifact");
        }
    }
}

} // namespace vision_native
