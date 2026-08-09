#include "vision_native/tensorrt_engine.h"
#include "vision_native/types.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string model_path;
    std::string output_json;
    std::string expect_preprocess_mode;
    int width = 480;
    int height = 416;
    int array_width = 0;
    int array_height = 0;
    int warmup = 20;
    int iterations = 200;
    bool bind_tensor_addresses_once = true;
    bool use_high_priority_stream = true;
    bool use_cuda_graph = true;
};

void check_cuda(cudaError_t status, const char* what) {
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(status));
    }
}

void usage() {
    std::cerr
        << "usage: vision_native_bgra_benchmark.exe --model <engine> [--output-json <path>]\n"
        << "       [--width 480] [--height 416] [--warmup 20] [--iterations 200]\n"
        << "       [--array-width 600] [--array-height 520]\n"
        << "       [--tensor-address-binding once|per-inference]\n"
        << "       [--stream-priority high|default]\n"
        << "       [--cuda-graph on|off]\n"
        << "       [--expect-preprocess-mode <mode>]\n";
}

int parse_int(const char* value, const char* name) {
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid integer for ") + name + ": " + value);
    }
}

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--model") {
            options.model_path = require_value("--model");
        } else if (arg == "--output-json") {
            options.output_json = require_value("--output-json");
        } else if (arg == "--expect-preprocess-mode") {
            options.expect_preprocess_mode = require_value("--expect-preprocess-mode");
        } else if (arg == "--width") {
            options.width = parse_int(require_value("--width"), "--width");
        } else if (arg == "--height") {
            options.height = parse_int(require_value("--height"), "--height");
        } else if (arg == "--array-width") {
            options.array_width =
                parse_int(require_value("--array-width"), "--array-width");
        } else if (arg == "--array-height") {
            options.array_height =
                parse_int(require_value("--array-height"), "--array-height");
        } else if (arg == "--warmup") {
            options.warmup = parse_int(require_value("--warmup"), "--warmup");
        } else if (arg == "--iterations") {
            options.iterations = parse_int(require_value("--iterations"), "--iterations");
        } else if (arg == "--tensor-address-binding") {
            const std::string value = require_value("--tensor-address-binding");
            if (value == "once") {
                options.bind_tensor_addresses_once = true;
            } else if (value == "per-inference") {
                options.bind_tensor_addresses_once = false;
            } else {
                throw std::runtime_error("--tensor-address-binding must be once or per-inference");
            }
        } else if (arg == "--stream-priority") {
            const std::string value = require_value("--stream-priority");
            if (value == "high") {
                options.use_high_priority_stream = true;
            } else if (value == "default") {
                options.use_high_priority_stream = false;
            } else {
                throw std::runtime_error("--stream-priority must be high or default");
            }
        } else if (arg == "--cuda-graph") {
            const std::string value = require_value("--cuda-graph");
            if (value == "on") {
                options.use_cuda_graph = true;
            } else if (value == "off") {
                options.use_cuda_graph = false;
            } else {
                throw std::runtime_error("--cuda-graph must be on or off");
            }
        } else if (arg == "--fixtures") {
            (void)require_value("--fixtures");
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.model_path.empty()) {
        throw std::runtime_error("--model is required");
    }
    if (options.width <= 0 || options.height <= 0 || options.warmup < 0 || options.iterations <= 0) {
        throw std::runtime_error("invalid benchmark dimensions or iteration counts");
    }
    if (options.array_width == 0) options.array_width = options.width;
    if (options.array_height == 0) options.array_height = options.height;
    if (options.array_width < options.width || options.array_height < options.height) {
        throw std::runtime_error("array dimensions must contain the benchmark ROI");
    }
    return options;
}

std::vector<std::uint8_t> fixture_bgra(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            const bool edge = (x % 41 == 0) || (y % 31 == 0);
            pixels[offset + 0] = edge ? 240 : static_cast<std::uint8_t>((x * 255) / std::max(1, width - 1));
            pixels[offset + 1] = edge ? 40 : static_cast<std::uint8_t>((y * 255) / std::max(1, height - 1));
            pixels[offset + 2] = edge ? 220 : static_cast<std::uint8_t>(((x + y) * 255) / std::max(1, width + height - 2));
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

std::string json_string(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

float percentile(std::vector<float> values, double p) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const double index = (static_cast<double>(values.size() - 1) * p);
    const size_t lo = static_cast<size_t>(index);
    const size_t hi = std::min(values.size() - 1, lo + 1);
    const double t = index - static_cast<double>(lo);
    return static_cast<float>((static_cast<double>(values[lo]) * (1.0 - t)) + (static_cast<double>(values[hi]) * t));
}

void write_metric(std::ostream& out, const char* name, const std::vector<float>& values, bool comma) {
    out << "  \"" << name << "\": {"
        << "\"p50\": " << percentile(values, 0.50)
        << ", \"p95\": " << percentile(values, 0.95)
        << ", \"p99\": " << percentile(values, 0.99)
        << "}";
    if (comma) {
        out << ",";
    }
    out << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        const std::vector<std::uint8_t> host_bgra =
            fixture_bgra(options.array_width, options.array_height);

        cudaArray_t bgra_array = nullptr;
        const cudaChannelFormatDesc desc = cudaCreateChannelDesc<uchar4>();
        check_cuda(
            cudaMallocArray(
                &bgra_array,
                &desc,
                static_cast<size_t>(options.array_width),
                static_cast<size_t>(options.array_height)),
            "cudaMallocArray");
        check_cuda(
            cudaMemcpy2DToArray(
                bgra_array,
                0,
                0,
                host_bgra.data(),
                static_cast<size_t>(options.array_width) * 4,
                static_cast<size_t>(options.array_width) * 4,
                static_cast<size_t>(options.array_height),
                cudaMemcpyHostToDevice),
            "cudaMemcpy2DToArray");

        vision_native::TensorRTEngineOptions engine_options;
        engine_options.bind_tensor_addresses_once = options.bind_tensor_addresses_once;
        engine_options.use_high_priority_stream = options.use_high_priority_stream;
        engine_options.use_cuda_graph = options.use_cuda_graph;
        vision_native::TensorRTEngine engine(options.model_path, engine_options);
        std::vector<float> preprocess_ms;
        std::vector<float> infer_ms;
        std::vector<float> gpu_total_ms;
        std::vector<float> enqueue_cpu_ms;
        std::vector<float> output_copy_sync_ms;
        std::vector<float> output_copy_ms;
        std::vector<float> output_wait_ms;
        std::vector<float> decode_ms;
        std::vector<float> wall_ms;
        std::map<size_t, int> detection_counts;
        vision_native::DetectionBatch last_batch;

        for (int i = 0; i < options.warmup + options.iterations; ++i) {
            const auto wall_start = std::chrono::steady_clock::now();
            vision_native::DetectionBatch batch = engine.infer_bgra_array_roi(
                bgra_array,
                options.array_width,
                options.array_height,
                (options.array_width - options.width) / 2,
                (options.array_height - options.height) / 2,
                options.width,
                options.height,
                0.20f);
            const auto wall_end = std::chrono::steady_clock::now();
            if (i >= options.warmup) {
                preprocess_ms.push_back(batch.preprocess_ms);
                infer_ms.push_back(batch.infer_ms);
                gpu_total_ms.push_back(batch.gpu_total_ms);
                enqueue_cpu_ms.push_back(batch.enqueue_cpu_ms);
                output_copy_sync_ms.push_back(batch.output_copy_sync_ms);
                output_copy_ms.push_back(batch.output_copy_ms);
                output_wait_ms.push_back(batch.output_wait_ms);
                decode_ms.push_back(batch.decode_ms);
                wall_ms.push_back(std::chrono::duration<float, std::milli>(wall_end - wall_start).count());
                detection_counts[batch.detections.size()] += 1;
                last_batch = std::move(batch);
            }
        }

        check_cuda(cudaFreeArray(bgra_array), "cudaFreeArray");

        const std::string actual_mode = vision_native::preprocess_mode_name(last_batch.preprocess_mode);
        if (!options.expect_preprocess_mode.empty() && actual_mode != options.expect_preprocess_mode) {
            std::ostringstream message;
            message << "expected preprocess_mode " << options.expect_preprocess_mode
                    << " but saw " << actual_mode;
            throw std::runtime_error(message.str());
        }
        std::ostream* out = &std::cout;
        std::ofstream file;
        if (!options.output_json.empty()) {
            file.open(options.output_json, std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
                throw std::runtime_error("failed to open output json: " + options.output_json);
            }
            out = &file;
        }

        *out << std::fixed << std::setprecision(6);
        *out << "{\n"
             << "  \"model\": " << json_string(options.model_path) << ",\n"
             << "  \"width\": " << options.width << ",\n"
             << "  \"height\": " << options.height << ",\n"
             << "  \"array_width\": " << options.array_width << ",\n"
             << "  \"array_height\": " << options.array_height << ",\n"
             << "  \"warmup\": " << options.warmup << ",\n"
             << "  \"iterations\": " << options.iterations << ",\n"
             << "  \"tensor_address_binding\": \""
             << (options.bind_tensor_addresses_once ? "once" : "per-inference") << "\",\n"
             << "  \"stream_priority\": \""
             << (options.use_high_priority_stream ? "high" : "default") << "\",\n"
             << "  \"cuda_graph\": " << (options.use_cuda_graph ? "true" : "false") << ",\n"
             << "  \"preprocess_mode\": \""
             << vision_native::preprocess_mode_name(last_batch.preprocess_mode) << "\",\n";
        write_metric(*out, "preprocess_ms", preprocess_ms, true);
        write_metric(*out, "infer_ms", infer_ms, true);
        write_metric(*out, "gpu_total_ms", gpu_total_ms, true);
        write_metric(*out, "enqueue_cpu_ms", enqueue_cpu_ms, true);
        write_metric(*out, "output_copy_sync_ms", output_copy_sync_ms, true);
        write_metric(*out, "output_copy_ms", output_copy_ms, true);
        write_metric(*out, "output_wait_ms", output_wait_ms, true);
        write_metric(*out, "decode_ms", decode_ms, true);
        write_metric(*out, "wall_ms", wall_ms, true);
        *out << "  \"detection_counts\": {";
        bool first = true;
        for (const auto& [count, seen] : detection_counts) {
            if (!first) {
                *out << ", ";
            }
            first = false;
            *out << "\"" << count << "\": " << seen;
        }
        *out << "}\n}\n";

        if (file.is_open()) {
            std::cout << "[VisionBgraBenchmark] wrote " << options.output_json << "\n";
        }
    } catch (const std::exception& exc) {
        std::cerr << "[VisionBgraBenchmark] FAIL " << exc.what() << "\n";
        usage();
        return 1;
    }
    return 0;
}
