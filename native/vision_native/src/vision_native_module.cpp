#include "vision_native/tensorrt_inspector.h"
#include "vision_native/tensorrt_engine.h"

#include <pybind11/numpy.h>
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

py::dict detection_to_dict(const vision_native::Detection& detection) {
    py::dict result;
    result["x1"] = detection.x1;
    result["y1"] = detection.y1;
    result["x2"] = detection.x2;
    result["y2"] = detection.y2;
    result["conf"] = detection.conf;
    result["class_id"] = detection.class_id;
    return result;
}

py::dict batch_to_dict(const vision_native::DetectionBatch& batch) {
    py::dict result;
    result["frame_id"] = batch.frame_id;
    result["captured_at_ns"] = batch.captured_at_ns;
    result["inferred_at_ns"] = batch.inferred_at_ns;
    result["frame_width"] = batch.frame_width;
    result["frame_height"] = batch.frame_height;
    result["preprocess_ms"] = batch.preprocess_ms;
    result["infer_ms"] = batch.infer_ms;
    result["decode_ms"] = batch.decode_ms;

    py::list detections;
    for (const auto& detection : batch.detections) {
        detections.append(detection_to_dict(detection));
    }
    result["detections"] = detections;
    result["boxes_seen"] = batch.detections.size();
    return result;
}

vision_native::DetectionBatch infer_array(
    vision_native::TensorRTEngine& engine,
    py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
    float conf_threshold) {
    py::buffer_info buffer = frame.request();
    if (buffer.ndim != 3 || buffer.shape[2] != 3) {
        throw std::runtime_error("run_inference_rgb expects an RGB uint8 array with shape [H,W,3]");
    }

    const int height = static_cast<int>(buffer.shape[0]);
    const int width = static_cast<int>(buffer.shape[1]);
    const int row_pitch = static_cast<int>(buffer.strides[0]);
    const auto* data = static_cast<const uint8_t*>(buffer.ptr);

    py::gil_scoped_release release;
    return engine.infer_rgb(data, width, height, row_pitch, conf_threshold);
}

} // namespace

PYBIND11_MODULE(vision_native_cpp, module) {
    module.doc() = "Native vision TensorRT scaffold for engine-loading smoke tests.";
    module.def("build_info", &vision_native::build_info);
    module.def("inspect_engine", [](const std::string& engine_path) {
        return engine_to_dict(vision_native::inspect_engine(engine_path));
    });
    module.def(
        "run_inference_rgb",
        [](const std::string& engine_path,
           py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
           float conf_threshold) {
            vision_native::TensorRTEngine engine(engine_path);
            return batch_to_dict(infer_array(engine, frame, conf_threshold));
        },
        py::arg("engine_path"),
        py::arg("frame"),
        py::arg("conf_threshold") = 0.4f);

    py::class_<vision_native::TensorRTEngine>(module, "NativeEngine")
        .def(py::init<std::string>())
        .def_property_readonly("input_width", &vision_native::TensorRTEngine::input_width)
        .def_property_readonly("input_height", &vision_native::TensorRTEngine::input_height)
        .def_property_readonly("output_rows", &vision_native::TensorRTEngine::output_rows)
        .def_property_readonly("output_cols", &vision_native::TensorRTEngine::output_cols)
        .def(
            "infer_rgb",
            [](vision_native::TensorRTEngine& engine,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float conf_threshold) {
                return batch_to_dict(infer_array(engine, frame, conf_threshold));
            },
            py::arg("frame"),
            py::arg("conf_threshold") = 0.4f);
}
