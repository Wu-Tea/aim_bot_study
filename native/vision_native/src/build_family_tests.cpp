#include "vision_native/build_family.h"

#include <cstdlib>
#include <stdexcept>

namespace {
void require(bool value) { if (!value) std::abort(); }
bool fails(vision_native::BuildFamily family, const char* path, int major, int minor) {
    try { vision_native::validate_runtime_artifact_family(family, path, major, minor); }
    catch (const std::runtime_error&) { return true; }
    return false;
}
}
int main() {
    vision_native::validate_runtime_artifact_family(
        vision_native::BuildFamily::Modern, "legacy-model-without-manifest.engine", 7, 5);
    vision_native::validate_runtime_artifact_family(
        vision_native::BuildFamily::Modern, "any-modern-model.engine", 8, 9);
    require(fails(vision_native::BuildFamily::Modern, "model.engine", 6, 1));
    return 0;
}
