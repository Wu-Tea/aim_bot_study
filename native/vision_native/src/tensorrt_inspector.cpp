#include "vision_native/tensorrt_inspector.h"

#include <NvInfer.h>
#include <NvInferPlugin.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace vision_native {
namespace {

class Logger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, char const* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            last_warning_ = msg ? msg : "";
        }
    }

    std::string last_warning_;
};

struct EngineBytes {
    std::vector<char> plan;
    uint64_t container_size_bytes = 0;
    uint64_t metadata_prefix_bytes = 0;
};

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

EngineBytes read_engine_file(const std::string& path) {
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

    EngineBytes result;
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

std::string dtype_to_string(nvinfer1::DataType dtype) {
    switch (dtype) {
    case nvinfer1::DataType::kFLOAT:
        return "float32";
    case nvinfer1::DataType::kHALF:
        return "float16";
    case nvinfer1::DataType::kINT8:
        return "int8";
    case nvinfer1::DataType::kINT32:
        return "int32";
    case nvinfer1::DataType::kBOOL:
        return "bool";
    case nvinfer1::DataType::kUINT8:
        return "uint8";
    case nvinfer1::DataType::kFP8:
        return "fp8";
    case nvinfer1::DataType::kBF16:
        return "bf16";
    case nvinfer1::DataType::kINT64:
        return "int64";
    case nvinfer1::DataType::kINT4:
        return "int4";
    case nvinfer1::DataType::kFP4:
        return "fp4";
    case nvinfer1::DataType::kE8M0:
        return "e8m0";
    default:
        return "unknown";
    }
}

std::string mode_to_string(nvinfer1::TensorIOMode mode) {
    switch (mode) {
    case nvinfer1::TensorIOMode::kINPUT:
        return "input";
    case nvinfer1::TensorIOMode::kOUTPUT:
        return "output";
    case nvinfer1::TensorIOMode::kNONE:
        return "none";
    default:
        return "unknown";
    }
}

std::vector<int64_t> dims_to_vector(nvinfer1::Dims const& dims) {
    std::vector<int64_t> result;
    result.reserve(static_cast<size_t>(std::max(dims.nbDims, 0)));
    for (int32_t i = 0; i < dims.nbDims; ++i) {
        result.push_back(static_cast<int64_t>(dims.d[i]));
    }
    return result;
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

} // namespace

EngineInfo inspect_engine(const std::string& engine_path) {
    Logger logger;
    initLibNvInferPlugins(&logger, "");

    EngineBytes bytes = read_engine_file(engine_path);
    std::unique_ptr<nvinfer1::IRuntime> runtime(nvinfer1::createInferRuntime(logger));
    if (!runtime) {
        throw std::runtime_error("failed to create TensorRT runtime");
    }

    std::unique_ptr<nvinfer1::ICudaEngine> engine(
        runtime->deserializeCudaEngine(bytes.plan.data(), bytes.plan.size()));
    if (!engine) {
        throw std::runtime_error("failed to deserialize TensorRT engine: " + engine_path);
    }

    EngineInfo info;
    info.engine_path = engine_path;
    info.container_size_bytes = bytes.container_size_bytes;
    info.metadata_prefix_bytes = bytes.metadata_prefix_bytes;
    info.engine_size_bytes = static_cast<uint64_t>(bytes.plan.size());
    info.num_io_tensors = engine->getNbIOTensors();

    for (int32_t i = 0; i < info.num_io_tensors; ++i) {
        char const* name = engine->getIOTensorName(i);
        if (name == nullptr) {
            continue;
        }

        TensorInfo tensor;
        tensor.name = name;
        tensor.mode = mode_to_string(engine->getTensorIOMode(name));
        tensor.dtype = dtype_to_string(engine->getTensorDataType(name));
        tensor.shape = dims_to_vector(engine->getTensorShape(name));
        info.tensors.push_back(std::move(tensor));
    }

    return info;
}

std::string engine_info_to_json(const EngineInfo& info) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"engine_path\": \"" << json_escape(info.engine_path) << "\",\n";
    out << "  \"container_size_bytes\": " << info.container_size_bytes << ",\n";
    out << "  \"metadata_prefix_bytes\": " << info.metadata_prefix_bytes << ",\n";
    out << "  \"engine_size_bytes\": " << info.engine_size_bytes << ",\n";
    out << "  \"num_io_tensors\": " << info.num_io_tensors << ",\n";
    out << "  \"tensors\": [\n";
    for (size_t i = 0; i < info.tensors.size(); ++i) {
        const TensorInfo& tensor = info.tensors[i];
        out << "    {\"name\": \"" << json_escape(tensor.name) << "\", ";
        out << "\"mode\": \"" << tensor.mode << "\", ";
        out << "\"dtype\": \"" << tensor.dtype << "\", ";
        out << "\"shape\": [";
        for (size_t j = 0; j < tensor.shape.size(); ++j) {
            if (j > 0) {
                out << ", ";
            }
            out << tensor.shape[j];
        }
        out << "]}";
        if (i + 1 < info.tensors.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::string build_info() {
    std::ostringstream out;
    out << "vision_native_cpp scaffold";
    out << " | TensorRT " << NV_TENSORRT_MAJOR << "." << NV_TENSORRT_MINOR << "." << NV_TENSORRT_PATCH;
    return out.str();
}

} // namespace vision_native
