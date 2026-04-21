#include "vision_native/aim_enhancement.h"
#include "vision_native/dxgi_capture.h"
#include "vision_native/target_selector.h"
#include "vision_native/vision_engine.h"
#include "vision_native/tensorrt_inspector.h"
#include "vision_native/tensorrt_engine.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>

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
    result["color_bonus"] = detection.color_bonus;
    result["is_friendly"] = detection.is_friendly;
    result["color_classified"] = detection.color_classified;
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

const char* pixel_format_to_string(vision_native::PixelFormat format) {
    switch (format) {
    case vision_native::PixelFormat::RGB8:
        return "RGB8";
    case vision_native::PixelFormat::BGRA8:
        return "BGRA8";
    default:
        return "unknown";
    }
}

const char* memory_kind_to_string(vision_native::MemoryKind kind) {
    switch (kind) {
    case vision_native::MemoryKind::CpuHwc:
        return "CpuHwc";
    case vision_native::MemoryKind::D3D11Texture:
        return "D3D11Texture";
    default:
        return "unknown";
    }
}

py::dict frame_packet_to_dict(const vision_native::FramePacket& frame) {
    py::dict result;
    result["frame_id"] = frame.frame_id;
    result["captured_at_ns"] = frame.captured_at_ns;
    result["width"] = frame.width;
    result["height"] = frame.height;
    result["format"] = pixel_format_to_string(frame.format);
    result["memory_kind"] = memory_kind_to_string(frame.memory_kind);
    result["row_pitch"] = frame.row_pitch;
    result["has_data"] = frame.data != nullptr;
    return result;
}

py::dict capture_metadata_to_dict(const vision_native::DxgiCaptureMetadata& metadata) {
    py::dict result;
    result["updated"] = metadata.updated;
    result["frame"] = frame_packet_to_dict(metadata.frame);
    result["memory_kind"] = memory_kind_to_string(metadata.frame.memory_kind);
    result["format"] = pixel_format_to_string(metadata.frame.format);
    result["roi_left"] = metadata.roi_left;
    result["roi_top"] = metadata.roi_top;
    result["output_width"] = metadata.output_width;
    result["output_height"] = metadata.output_height;
    result["adapter_index"] = metadata.adapter_index;
    result["output_index"] = metadata.output_index;
    result["acquire_ms"] = metadata.acquire_ms;
    result["copy_ms"] = metadata.copy_ms;
    return result;
}

py::dict vision_result_to_dict(const vision_native::VisionResult& result_in) {
    py::dict result;
    result["frame_id"] = result_in.frame_id;
    result["captured_at_ns"] = result_in.captured_at_ns;
    result["inferred_at_ns"] = result_in.inferred_at_ns;
    result["result_at_ns"] = result_in.result_at_ns;
    result["has_target"] = result_in.has_target;
    result["auto_fire"] = result_in.auto_fire;
    result["dx"] = result_in.dx;
    result["dy"] = result_in.dy;
    result["target_x"] = result_in.target_x;
    result["target_y"] = result_in.target_y;
    result["screen_center_x"] = result_in.screen_center_x;
    result["screen_center_y"] = result_in.screen_center_y;
    result["has_body_box"] = result_in.has_body_box;
    result["body_x1"] = result_in.body_x1;
    result["body_y1"] = result_in.body_y1;
    result["body_x2"] = result_in.body_x2;
    result["body_y2"] = result_in.body_y2;
    result["target_source"] = result_in.target_source;
    result["wait_ms"] = result_in.wait_ms;
    result["preprocess_ms"] = result_in.preprocess_ms;
    result["infer_ms"] = result_in.infer_ms;
    result["post_ms"] = result_in.post_ms;
    result["age_ms"] = result_in.age_ms;
    result["boxes_seen"] = result_in.boxes_seen;
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

vision_native::DxgiCaptureMetadata grab_capture(vision_native::DxgiRoiCapture& capture) {
    py::gil_scoped_release release;
    return capture.grab();
}

vision_native::VisionResult poll_engine_once(vision_native::VisionEngine& engine) {
    py::gil_scoped_release release;
    return engine.poll_once();
}

vision_native::DetectionBatch batch_from_xyxy_array(
    py::array_t<float, py::array::c_style | py::array::forcecast> detections) {
    py::buffer_info buffer = detections.request();
    if (buffer.ndim != 2 || (buffer.shape[1] != 6 && buffer.shape[1] != 8)) {
        throw std::runtime_error("select_xyxy expects a float32 array with shape [N,6] or [N,8]");
    }

    vision_native::DetectionBatch batch;
    const auto* rows = static_cast<const float*>(buffer.ptr);
    const py::ssize_t count = buffer.shape[0];
    const py::ssize_t cols = buffer.shape[1];
    batch.detections.reserve(static_cast<size_t>(count));
    for (py::ssize_t index = 0; index < count; ++index) {
        const float* row = rows + (index * cols);
        vision_native::Detection detection;
        detection.x1 = row[0];
        detection.y1 = row[1];
        detection.x2 = row[2];
        detection.y2 = row[3];
        detection.conf = row[4];
        detection.class_id = static_cast<int>(row[5]);
        if (cols >= 8) {
            detection.color_bonus = row[6];
            detection.is_friendly = row[7] != 0.0f;
            detection.color_classified = true;
        }
        batch.detections.push_back(detection);
    }
    return batch;
}

vision_native::VisionTargetSelector::ColorFrameView rgb_frame_view(
    py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame) {
    py::buffer_info buffer = frame.request();
    if (buffer.ndim != 3 || buffer.shape[2] != 3) {
        throw std::runtime_error("select_xyxy_rgb expects an RGB uint8 array with shape [H,W,3]");
    }

    vision_native::VisionTargetSelector::ColorFrameView view;
    view.data = static_cast<const uint8_t*>(buffer.ptr);
    view.width = static_cast<int>(buffer.shape[1]);
    view.height = static_cast<int>(buffer.shape[0]);
    view.row_pitch = static_cast<int>(buffer.strides[0]);
    view.format = vision_native::PixelFormat::RGB8;
    return view;
}

std::optional<vision_native::AimSlowZone> parse_slow_zone(const py::object& value) {
    if (value.is_none()) {
        return std::nullopt;
    }

    const auto zone = value.cast<std::array<float, 4>>();
    return vision_native::AimSlowZone{zone[0], zone[1], zone[2], zone[3]};
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

    py::class_<vision_native::DxgiRoiCapture>(module, "NativeDxgiCapture")
        .def(
            py::init<int, int, int, int, int>(),
            py::arg("width"),
            py::arg("height"),
            py::arg("adapter_index") = 0,
            py::arg("output_index") = -1,
            py::arg("timeout_ms") = 0)
        .def_property_readonly("width", &vision_native::DxgiRoiCapture::width)
        .def_property_readonly("height", &vision_native::DxgiRoiCapture::height)
        .def_property_readonly("output_width", &vision_native::DxgiRoiCapture::output_width)
        .def_property_readonly("output_height", &vision_native::DxgiRoiCapture::output_height)
        .def_property_readonly("roi_left", &vision_native::DxgiRoiCapture::roi_left)
        .def_property_readonly("roi_top", &vision_native::DxgiRoiCapture::roi_top)
        .def("grab", [](vision_native::DxgiRoiCapture& capture) {
            return capture_metadata_to_dict(grab_capture(capture));
        });

    py::class_<vision_native::VisionEngine>(module, "NativeVisionEngine")
        .def(
            py::init<int, int, int, int, int>(),
            py::arg("width"),
            py::arg("height"),
            py::arg("adapter_index") = 0,
            py::arg("output_index") = -1,
            py::arg("timeout_ms") = 0)
        .def_property_readonly("width", &vision_native::VisionEngine::width)
        .def_property_readonly("height", &vision_native::VisionEngine::height)
        .def("set_aiming", &vision_native::VisionEngine::set_aiming, py::arg("aiming"))
        .def("reset", &vision_native::VisionEngine::reset)
        .def("poll_once", [](vision_native::VisionEngine& engine) {
            return vision_result_to_dict(poll_engine_once(engine));
        });

    py::class_<vision_native::AimEnhancementPipeline>(module, "NativeAimEnhancer")
        .def(py::init<>())
        .def("reset", &vision_native::AimEnhancementPipeline::reset)
        .def(
            "process",
            [](vision_native::AimEnhancementPipeline& enhancer,
               float target_x,
               float target_y,
               float screen_center_x,
               float screen_center_y,
               py::object slow_zone,
               const std::string& source,
               double timestamp) {
                vision_native::VisionResult target;
                target.has_target = true;
                target.target_x = target_x;
                target.target_y = target_y;
                target.screen_center_x = screen_center_x;
                target.screen_center_y = screen_center_y;
                target.dx = target_x - screen_center_x;
                target.dy = target_y - screen_center_y;
                target.target_source = source == "predicted" ? "predicted" : "observed";
                const vision_native::VisionResult enhanced = enhancer.process(
                    target,
                    timestamp,
                    parse_slow_zone(slow_zone));
                return vision_result_to_dict(enhanced);
            },
            py::arg("target_x"),
            py::arg("target_y"),
            py::arg("screen_center_x"),
            py::arg("screen_center_y"),
            py::arg("slow_zone"),
            py::arg("source"),
            py::arg("timestamp"));

    py::class_<vision_native::VisionTargetSelector>(module, "NativeTargetSelector")
        .def(py::init<int, int>(), py::arg("width"), py::arg("height"))
        .def("reset", &vision_native::VisionTargetSelector::reset)
        .def(
            "select_xyxy",
            [](vision_native::VisionTargetSelector& selector,
               py::array_t<float, py::array::c_style | py::array::forcecast> detections) {
                return vision_result_to_dict(selector.select(batch_from_xyxy_array(detections)));
            },
            py::arg("detections"))
        .def(
            "select_xyxy_rgb",
            [](vision_native::VisionTargetSelector& selector,
               py::array_t<float, py::array::c_style | py::array::forcecast> detections,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame) {
                return vision_result_to_dict(
                    selector.select_with_frame(
                        batch_from_xyxy_array(detections),
                        rgb_frame_view(frame)));
            },
            py::arg("detections"),
            py::arg("frame"));
}
