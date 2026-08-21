#include "vision_native/build_family.h"
#include "test_support/native_test_registry.h"

#include <stdexcept>

namespace {
void require(bool value) {
    if (!value) throw std::runtime_error("build family assertion failed");
}
bool fails(vision_native::BuildFamily family, const char* path, int major, int minor) {
    try { vision_native::validate_runtime_artifact_family(family, path, major, minor); }
    catch (const std::runtime_error&) { return true; }
    return false;
}
}
void test_build_family_contract() {
    vision_native::validate_runtime_artifact_family(
        vision_native::BuildFamily::Modern, "legacy-model-without-manifest.engine", 7, 5);
    vision_native::validate_runtime_artifact_family(
        vision_native::BuildFamily::Modern, "any-modern-model.engine", 8, 9);
    require(fails(vision_native::BuildFamily::Modern, "model.engine", 6, 1));
}

void register_build_family_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "vision_build_family_contract", test_build_family_contract);
}
