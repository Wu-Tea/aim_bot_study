#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vision_native {

struct SerializedEngine {
    std::vector<char> plan;
    uint64_t container_size_bytes = 0;
    uint64_t metadata_prefix_bytes = 0;
};

SerializedEngine read_serialized_engine(const std::string& path);

} // namespace vision_native
