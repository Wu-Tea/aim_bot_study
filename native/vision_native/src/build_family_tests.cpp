#include "vision_native/build_family.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    const auto directory = std::filesystem::temp_directory_path() / "cod_build_family_tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto modern = directory / "renamed.engine";
    const auto pascal = directory / "also-renamed.engine";
    { std::ofstream out(modern); out << "modern-plan"; }
    { std::ofstream out(pascal); out << "pascal-plan"; }
    { std::ofstream out(modern.string() + ".runtime.json"); out << R"({"build_family":"modern","engine_size_bytes":11})"; }
    { std::ofstream out(pascal.string() + ".runtime.json"); out << R"({"build_family":"pascal","engine_size_bytes":11})"; }
    vision_native::validate_runtime_artifact_family(
        vision_native::BuildFamily::Modern, modern.string(), 7, 5);
    require(fails(vision_native::BuildFamily::Modern, pascal.string().c_str(), 8, 9));
    require(fails(vision_native::BuildFamily::Modern, "model.engine", 6, 1));
    vision_native::validate_runtime_artifact_family(
        vision_native::BuildFamily::Pascal, pascal.string(), 6, 1);
    require(fails(vision_native::BuildFamily::Pascal, "model.engine", 6, 1));
    require(fails(vision_native::BuildFamily::Pascal, modern.string().c_str(), 6, 1));
    require(fails(vision_native::BuildFamily::Pascal, pascal.string().c_str(), 7, 5));
    { std::ofstream out(pascal, std::ios::app); out << "tampered"; }
    require(fails(vision_native::BuildFamily::Pascal, pascal.string().c_str(), 6, 1));
    std::filesystem::remove_all(directory);
    return 0;
}
