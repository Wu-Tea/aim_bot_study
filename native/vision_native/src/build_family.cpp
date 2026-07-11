#include "vision_native/build_family.h"

#include <filesystem>
#include <fstream>
#include <regex>
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
    const std::filesystem::path manifest_path = engine_path + ".runtime.json";
    std::string artifact_family;
    std::uintmax_t declared_size = 0;
    if (std::filesystem::exists(manifest_path)) {
        std::ifstream input(manifest_path);
        const std::string text(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::smatch match;
        if (!std::regex_search(
                text, match,
                std::regex("\\\"build_family\\\"\\s*:\\s*\\\"(modern|pascal)\\\""))) {
            throw std::runtime_error("engine runtime manifest has no valid build_family");
        }
        artifact_family = match[1].str();
        if (!std::regex_search(
                text, match, std::regex("\\\"engine_size_bytes\\\"\\s*:\\s*([0-9]+)"))) {
            throw std::runtime_error("engine runtime manifest has no engine_size_bytes signature");
        }
        declared_size = static_cast<std::uintmax_t>(std::stoull(match[1].str()));
        if (!std::filesystem::exists(engine_path) ||
            std::filesystem::file_size(engine_path) != declared_size) {
            throw std::runtime_error("engine artifact does not match its runtime manifest signature");
        }
    }
    if (family == BuildFamily::Pascal) {
        if (compute_major != 6 || compute_minor != 1) {
            throw std::runtime_error(
                "pascal build requires an SM 6.1 GPU and a separately built Pascal engine");
        }
        if (artifact_family.empty()) {
            throw std::runtime_error(
                "pascal build requires <engine>.runtime.json build metadata");
        }
        if (artifact_family != "pascal") {
            throw std::runtime_error("pascal build refuses a non-Pascal engine artifact");
        }
    } else {
        if (compute_major < 7 || (compute_major == 7 && compute_minor < 5)) {
            throw std::runtime_error(
                "modern build requires Turing/SM 7.5 or newer; use the Pascal build for SM 6.1");
        }
        if (!artifact_family.empty() && artifact_family != "modern") {
            throw std::runtime_error("modern build refuses a Pascal/SM61 engine artifact");
        }
    }
}

} // namespace vision_native
