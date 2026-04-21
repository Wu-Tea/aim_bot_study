#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vision_native {

struct TensorInfo {
    std::string name;
    std::string mode;
    std::string dtype;
    std::vector<int64_t> shape;
};

struct EngineInfo {
    std::string engine_path;
    uint64_t container_size_bytes = 0;
    uint64_t metadata_prefix_bytes = 0;
    uint64_t engine_size_bytes = 0;
    int32_t num_io_tensors = 0;
    std::vector<TensorInfo> tensors;
};

EngineInfo inspect_engine(const std::string& engine_path);
std::string engine_info_to_json(const EngineInfo& info);
std::string build_info();

} // namespace vision_native
