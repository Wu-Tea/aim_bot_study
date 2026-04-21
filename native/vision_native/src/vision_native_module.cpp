#include "vision_native/tensorrt_inspector.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace {

py::dict tensor_to_dict(const vision_native::TensorInfo& tensor) {
    py::dict result;
    result["name"] = tensor.name;
    result["mode"] = tensor.mode;
    result["dtype"] = tensor.dtype;
    result["shape"] = tensor.shape;
    return result;
}

py::dict engine_to_dict(const vision_native::EngineInfo& info) {
    py::dict result;
    result["engine_path"] = info.engine_path;
    result["container_size_bytes"] = info.container_size_bytes;
    result["metadata_prefix_bytes"] = info.metadata_prefix_bytes;
    result["engine_size_bytes"] = info.engine_size_bytes;
    result["num_io_tensors"] = info.num_io_tensors;

    py::list tensors;
    for (const auto& tensor : info.tensors) {
        tensors.append(tensor_to_dict(tensor));
    }
    result["tensors"] = tensors;
    return result;
}

} // namespace

PYBIND11_MODULE(vision_native_cpp, module) {
    module.doc() = "Native vision TensorRT scaffold for engine-loading smoke tests.";
    module.def("build_info", &vision_native::build_info);
    module.def("inspect_engine", [](const std::string& engine_path) {
        return engine_to_dict(vision_native::inspect_engine(engine_path));
    });
}
