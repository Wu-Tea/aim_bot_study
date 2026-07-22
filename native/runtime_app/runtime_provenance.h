#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace runtime_app {

std::string sha256_file_with_context(
    const std::filesystem::path& path,
    std::string_view context = {});

}  // namespace runtime_app
