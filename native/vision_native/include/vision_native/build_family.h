#pragma once

#include <string>

namespace vision_native {

enum class BuildFamily { Modern, Pascal };

BuildFamily compiled_build_family();
const char* build_family_name(BuildFamily family);
void validate_runtime_artifact_family(
    BuildFamily family,
    const std::string& engine_path,
    int compute_major,
    int compute_minor);

} // namespace vision_native
