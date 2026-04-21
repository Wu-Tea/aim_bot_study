#include "vision_native/engine_io.h"

#include <cstddef>
#include <fstream>
#include <stdexcept>

namespace vision_native {
namespace {

uint32_t read_little_endian_u32(const std::vector<char>& bytes) {
    return static_cast<uint32_t>(static_cast<unsigned char>(bytes[0]))
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[1])) << 8U)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[2])) << 16U)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[3])) << 24U);
}

bool looks_like_ultralytics_metadata(const std::vector<char>& bytes, uint32_t metadata_len) {
    if (metadata_len == 0 || metadata_len > 1024U * 1024U) {
        return false;
    }
    if (bytes.size() <= static_cast<size_t>(4U + metadata_len)) {
        return false;
    }

    const char* metadata = bytes.data() + 4;
    const char* metadata_end = metadata + metadata_len;
    while (metadata < metadata_end && (*metadata == ' ' || *metadata == '\n' || *metadata == '\r' || *metadata == '\t')) {
        ++metadata;
    }
    return metadata < metadata_end && *metadata == '{';
}

} // namespace

SerializedEngine read_serialized_engine(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("failed to open engine file: " + path);
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("engine file is empty: " + path);
    }

    std::vector<char> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(bytes.data(), size)) {
        throw std::runtime_error("failed to read engine file: " + path);
    }

    SerializedEngine result;
    result.container_size_bytes = static_cast<uint64_t>(bytes.size());

    if (bytes.size() > 4) {
        const uint32_t metadata_len = read_little_endian_u32(bytes);
        if (looks_like_ultralytics_metadata(bytes, metadata_len)) {
            result.metadata_prefix_bytes = static_cast<uint64_t>(4U + metadata_len);
        }
    }

    result.plan.assign(bytes.begin() + static_cast<std::ptrdiff_t>(result.metadata_prefix_bytes), bytes.end());
    if (result.plan.empty()) {
        throw std::runtime_error("engine payload is empty after metadata prefix: " + path);
    }
    return result;
}

} // namespace vision_native
