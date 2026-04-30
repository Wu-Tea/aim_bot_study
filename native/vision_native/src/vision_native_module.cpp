#include "vision_native/aim_enhancement.h"
#include "vision_native/body_state_tracker.h"
#include "vision_native/center_cue_refiner.h"
#include "vision_native/dxgi_capture.h"
#include "vision_native/ego_motion.h"
#include "vision_native/target_selector.h"
#include "vision_native/vision_engine.h"
#include "vision_native/tensorrt_inspector.h"
#include "vision_native/tensorrt_engine.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <cmath>

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
    result["anchor_confidence"] = result_in.anchor_confidence;
    result["ego_confidence"] = result_in.ego_confidence;
    result["body_state_mode"] = result_in.body_state_mode;
    result["anchor_source"] = result_in.anchor_source;
    result["torso_x1"] = result_in.torso_x1;
    result["torso_y1"] = result_in.torso_y1;
    result["torso_x2"] = result_in.torso_x2;
    result["torso_y2"] = result_in.torso_y2;
    result["yellow_cue_present"] = result_in.yellow_cue_present;
    result["yellow_cue_score"] = result_in.yellow_cue_score;
    result["yellow_cue_x"] = result_in.yellow_cue_x;
    result["yellow_cue_y"] = result_in.yellow_cue_y;
    result["yellow_mask_area"] = result_in.yellow_mask_area;
    result["yellow_roi_x1"] = result_in.yellow_roi_x1;
    result["yellow_roi_y1"] = result_in.yellow_roi_y1;
    result["yellow_roi_x2"] = result_in.yellow_roi_x2;
    result["yellow_roi_y2"] = result_in.yellow_roi_y2;
    result["refiner_applied"] = result_in.refiner_applied;
    result["refined_target_x"] = result_in.refined_target_x;
    result["refined_target_y"] = result_in.refined_target_y;
    result["ego_model"] = result_in.ego_model;
    result["engine_mode"] = result_in.engine_mode;
    result["scan_ran"] = result_in.scan_ran;
    result["scan_age_ms"] = result_in.scan_age_ms;
    result["scan_reason"] = result_in.scan_reason;
    result["keyframe_age_ms"] = result_in.keyframe_age_ms;
    result["prewarm_used"] = result_in.prewarm_used;
    result["debug_search_x1"] = result_in.debug_search_x1;
    result["debug_search_y1"] = result_in.debug_search_y1;
    result["debug_search_x2"] = result_in.debug_search_x2;
    result["debug_search_y2"] = result_in.debug_search_y2;
    result["debug_predicted_x"] = result_in.debug_predicted_x;
    result["debug_predicted_y"] = result_in.debug_predicted_y;
    result["debug_patch_x"] = result_in.debug_patch_x;
    result["debug_patch_y"] = result_in.debug_patch_y;
    result["debug_patch_valid"] = result_in.debug_patch_valid;
    result["debug_template_w"] = result_in.debug_template_w;
    result["debug_template_h"] = result_in.debug_template_h;
    result["debug_track_points"] = result_in.debug_track_points;
    result["debug_scan_boxes"] = result_in.debug_scan_boxes;
    result["acquire_ms"] = result_in.acquire_ms;
    result["copy_ms"] = result_in.copy_ms;
    result["capture_ms"] = result_in.capture_ms;
    result["wait_ms"] = result_in.wait_ms;
    result["preprocess_ms"] = result_in.preprocess_ms;
    result["infer_ms"] = result_in.infer_ms;
    result["decode_ms"] = result_in.decode_ms;
    result["post_ms"] = result_in.post_ms;
    result["age_ms"] = result_in.age_ms;
    result["boxes_seen"] = result_in.boxes_seen;
    return result;
}

py::dict ego_warp_to_dict(const vision_native::EgoWarp& warp) {
    py::dict result;
    result["a00"] = warp.a00;
    result["a01"] = warp.a01;
    result["a10"] = warp.a10;
    result["a11"] = warp.a11;
    result["dx"] = warp.tx;
    result["dy"] = warp.ty;
    result["confidence"] = warp.confidence;
    result["valid_points"] = warp.valid_points;
    result["inlier_points"] = warp.inlier_points;
    result["model"] = warp.model;
    return result;
}

py::dict body_state_result_to_dict(const vision_native::BodyStateResult& result_in) {
    py::dict result;
    result["has_target"] = result_in.has_target;
    result["has_body_box"] = result_in.has_body_box;
    result["target_x"] = result_in.target_x;
    result["target_y"] = result_in.target_y;
    result["anchor_confidence"] = result_in.anchor_confidence;
    result["body_x1"] = result_in.body_x1;
    result["body_y1"] = result_in.body_y1;
    result["body_x2"] = result_in.body_x2;
    result["body_y2"] = result_in.body_y2;
    result["torso_x1"] = result_in.torso_x1;
    result["torso_y1"] = result_in.torso_y1;
    result["torso_x2"] = result_in.torso_x2;
    result["torso_y2"] = result_in.torso_y2;
    result["body_state_mode"] = result_in.body_state_mode;
    result["anchor_source"] = result_in.anchor_source;
    result["debug_search_x1"] = result_in.debug_search_x1;
    result["debug_search_y1"] = result_in.debug_search_y1;
    result["debug_search_x2"] = result_in.debug_search_x2;
    result["debug_search_y2"] = result_in.debug_search_y2;
    result["debug_predicted_x"] = result_in.debug_predicted_x;
    result["debug_predicted_y"] = result_in.debug_predicted_y;
    result["debug_patch_x"] = result_in.debug_patch_x;
    result["debug_patch_y"] = result_in.debug_patch_y;
    result["debug_patch_valid"] = result_in.debug_patch_valid;
    result["debug_template_w"] = result_in.debug_template_w;
    result["debug_template_h"] = result_in.debug_template_h;
    result["debug_track_points"] = result_in.debug_track_points;
    return result;
}

py::dict center_cue_result_to_dict(const vision_native::CenterCueResult& result_in) {
    py::dict result;
    result["yellow_cue_present"] = result_in.yellow_cue_present;
    result["yellow_cue_score"] = result_in.yellow_cue_score;
    result["yellow_cue_x"] = result_in.yellow_cue_x;
    result["yellow_cue_y"] = result_in.yellow_cue_y;
    result["yellow_mask_area"] = result_in.yellow_mask_area;
    result["yellow_roi_x1"] = result_in.yellow_roi_x1;
    result["yellow_roi_y1"] = result_in.yellow_roi_y1;
    result["yellow_roi_x2"] = result_in.yellow_roi_x2;
    result["yellow_roi_y2"] = result_in.yellow_roi_y2;
    result["refiner_applied"] = result_in.refiner_applied;
    result["refined_target_x"] = result_in.refined_target_x;
    result["refined_target_y"] = result_in.refined_target_y;
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

py::array_t<uint8_t> debug_frame_bgr_array(const vision_native::VisionEngine& engine) {
    const std::vector<uint8_t>& bgra = engine.last_debug_frame_bgra();
    if (bgra.empty()) {
        return py::array_t<uint8_t>({0, 0, 3});
    }

    const int width = engine.width();
    const int height = engine.height();
    py::array_t<uint8_t> output({height, width, 3});
    py::buffer_info info = output.request();
    auto* dst = static_cast<uint8_t*>(info.ptr);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t src_index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
            const size_t dst_index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            dst[dst_index + 0] = bgra[src_index + 0];
            dst[dst_index + 1] = bgra[src_index + 1];
            dst[dst_index + 2] = bgra[src_index + 2];
        }
    }
    return output;
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

vision_native::CenterCueFrameView center_cue_rgb_frame_view(
    py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame) {
    py::buffer_info buffer = frame.request();
    if (buffer.ndim != 3 || buffer.shape[2] != 3) {
        throw std::runtime_error("center-cue RGB frame must be a uint8 array with shape [H,W,3]");
    }

    vision_native::CenterCueFrameView view;
    view.data = static_cast<const uint8_t*>(buffer.ptr);
    view.width = static_cast<int>(buffer.shape[1]);
    view.height = static_cast<int>(buffer.shape[0]);
    view.row_pitch = static_cast<int>(buffer.strides[0]);
    view.format = vision_native::PixelFormat::RGB8;
    return view;
}

vision_native::EgoFrameView ego_rgb_frame_view(
    py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame) {
    py::buffer_info buffer = frame.request();
    if (buffer.ndim != 3 || buffer.shape[2] != 3) {
        throw std::runtime_error("RGB frame must be a uint8 array with shape [H,W,3]");
    }

    vision_native::EgoFrameView view;
    view.data = static_cast<const uint8_t*>(buffer.ptr);
    view.width = static_cast<int>(buffer.shape[1]);
    view.height = static_cast<int>(buffer.shape[0]);
    view.row_pitch = static_cast<int>(buffer.strides[0]);
    view.format = vision_native::PixelFormat::RGB8;
    return view;
}

vision_native::EgoWarp simple_ego_warp(float dx, float dy, float confidence) {
    vision_native::EgoWarp warp;
    warp.tx = dx;
    warp.ty = dy;
    warp.confidence = confidence;
    warp.model = (std::fabs(dx) > 0.01f || std::fabs(dy) > 0.01f) ? "translation" : "identity";
    return warp;
}

vision_native::TargetKeyframe simple_keyframe(
    float x1,
    float y1,
    float x2,
    float y2,
    const char* source = "observed") {
    vision_native::TargetKeyframe keyframe;
    keyframe.body_x1 = x1;
    keyframe.body_y1 = y1;
    keyframe.body_x2 = x2;
    keyframe.body_y2 = y2;
    const float box_w = x2 - x1;
    const float box_h = y2 - y1;
    keyframe.torso_x1 = x1 + (box_w * 0.22f);
    keyframe.torso_y1 = y1 + (box_h * 0.18f);
    keyframe.torso_x2 = x2 - (box_w * 0.22f);
    keyframe.torso_y2 = y2 - (box_h * 0.20f);
    keyframe.anchor_prior_x = (x1 + x2) * 0.5f;
    keyframe.anchor_prior_y = y1 + (box_h * 0.38f);
    keyframe.target_source = source;
    return keyframe;
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
            py::init<int, int, int, int, int, float, float, float, float>(),
            py::arg("width"),
            py::arg("height"),
            py::arg("adapter_index") = 0,
            py::arg("output_index") = -1,
            py::arg("timeout_ms") = 0,
            py::arg("track_interval_ms") = 0.0f,
            py::arg("warm_scan_interval_ms") = 50.0f,
            py::arg("active_normal_scan_interval_ms") = 12.0f,
            py::arg("active_recovery_scan_interval_ms") = 8.0f)
        .def_property_readonly("width", &vision_native::VisionEngine::width)
        .def_property_readonly("height", &vision_native::VisionEngine::height)
        .def(
            "set_mode",
            [](vision_native::VisionEngine& engine, const std::string& mode) {
                if (mode == "idle") {
                    engine.set_mode(vision_native::VisionEngine::Mode::Idle);
                } else if (mode == "warm_scan") {
                    engine.set_mode(vision_native::VisionEngine::Mode::WarmScan);
                } else if (mode == "active_track") {
                    engine.set_mode(vision_native::VisionEngine::Mode::ActiveTrack);
                } else {
                    throw std::runtime_error("set_mode expects idle, warm_scan, or active_track");
                }
            },
            py::arg("mode"))
        .def("set_aiming", &vision_native::VisionEngine::set_aiming, py::arg("aiming"))
        .def("reset", &vision_native::VisionEngine::reset)
        .def("poll_once", [](vision_native::VisionEngine& engine) {
            return vision_result_to_dict(poll_engine_once(engine));
        })
        .def("get_debug_frame_bgr", [](const vision_native::VisionEngine& engine) {
            return debug_frame_bgr_array(engine);
        });

    py::class_<vision_native::EgoMotionEstimator>(module, "NativeEgoMotionEstimator")
        .def(py::init<int, int>(), py::arg("width"), py::arg("height"))
        .def("reset", &vision_native::EgoMotionEstimator::reset)
        .def(
            "estimate_rgb",
            [](vision_native::EgoMotionEstimator& estimator,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               py::array_t<float, py::array::c_style | py::array::forcecast> detections) {
                return ego_warp_to_dict(
                    estimator.estimate(
                        ego_rgb_frame_view(frame),
                        batch_from_xyxy_array(detections)));
            },
            py::arg("frame"),
            py::arg("detections"));

    py::class_<vision_native::BodyStateTracker>(module, "NativeBodyStateTracker")
        .def(py::init<int, int>(), py::arg("width"), py::arg("height"))
        .def("reset", &vision_native::BodyStateTracker::reset)
        .def("has_active_target", &vision_native::BodyStateTracker::has_active_target)
        .def(
            "prime_keyframe_rgb",
            [](vision_native::BodyStateTracker& tracker,
               float x1,
               float y1,
               float x2,
               float y2,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame) {
                tracker.prime_from_keyframe(
                    simple_keyframe(x1, y1, x2, y2),
                    ego_rgb_frame_view(frame));
            },
            py::arg("x1"),
            py::arg("y1"),
            py::arg("x2"),
            py::arg("y2"),
            py::arg("frame"))
        .def(
            "update_selected_rgb",
            [](vision_native::BodyStateTracker& tracker,
               float x1,
               float y1,
               float x2,
               float y2,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float ego_dx,
               float ego_dy,
               float ego_confidence) {
                vision_native::VisionResult selected_target;
                selected_target.has_target = true;
                selected_target.has_body_box = true;
                selected_target.body_x1 = x1;
                selected_target.body_y1 = y1;
                selected_target.body_x2 = x2;
                selected_target.body_y2 = y2;
                selected_target.target_x = (x1 + x2) * 0.5f;
                selected_target.target_y = y1 + ((y2 - y1) * 0.38f);
                return body_state_result_to_dict(
                    tracker.update_selected(
                        selected_target,
                        ego_rgb_frame_view(frame),
                        simple_ego_warp(ego_dx, ego_dy, ego_confidence)));
            },
            py::arg("x1"),
            py::arg("y1"),
            py::arg("x2"),
            py::arg("y2"),
            py::arg("frame"),
            py::arg("ego_dx"),
            py::arg("ego_dy"),
            py::arg("ego_confidence"))
        .def(
            "update_interframe_rgb",
            [](vision_native::BodyStateTracker& tracker,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float ego_dx,
               float ego_dy,
               float ego_confidence) {
                return body_state_result_to_dict(
                    tracker.update_interframe(
                        ego_rgb_frame_view(frame),
                        simple_ego_warp(ego_dx, ego_dy, ego_confidence)));
            },
            py::arg("frame"),
            py::arg("ego_dx"),
            py::arg("ego_dy"),
            py::arg("ego_confidence"))
        .def(
            "update_scan_miss_rgb",
            [](vision_native::BodyStateTracker& tracker,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float ego_dx,
               float ego_dy,
               float ego_confidence) {
                return body_state_result_to_dict(
                    tracker.update_scan_miss(
                        ego_rgb_frame_view(frame),
                        simple_ego_warp(ego_dx, ego_dy, ego_confidence)));
            },
            py::arg("frame"),
            py::arg("ego_dx"),
            py::arg("ego_dy"),
            py::arg("ego_confidence"))
        .def(
            "update_missing_rgb",
            [](vision_native::BodyStateTracker& tracker,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float ego_dx,
               float ego_dy,
               float ego_confidence) {
                return body_state_result_to_dict(
                    tracker.update_missing(
                        ego_rgb_frame_view(frame),
                        simple_ego_warp(ego_dx, ego_dy, ego_confidence)));
            },
            py::arg("frame"),
            py::arg("ego_dx"),
            py::arg("ego_dy"),
            py::arg("ego_confidence"));

    py::class_<vision_native::CenterCueRefiner>(module, "NativeCenterCueRefiner")
        .def(py::init<int, int>(), py::arg("width"), py::arg("height"))
        .def("reset", &vision_native::CenterCueRefiner::reset)
        .def(
            "refine_rgb",
            [](vision_native::CenterCueRefiner& refiner,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float target_x,
               float target_y,
               float screen_center_x,
               float screen_center_y,
               float body_x1,
               float body_y1,
               float body_x2,
               float body_y2,
               float torso_x1,
               float torso_y1,
               float torso_x2,
               float torso_y2,
               const std::string& body_state_mode) {
                return center_cue_result_to_dict(
                    refiner.refine(
                        center_cue_rgb_frame_view(frame),
                        target_x,
                        target_y,
                        screen_center_x,
                        screen_center_y,
                        body_x1,
                        body_y1,
                        body_x2,
                        body_y2,
                        torso_x1,
                        torso_y1,
                        torso_x2,
                        torso_y2,
                        body_state_mode.c_str()));
            },
            py::arg("frame"),
            py::arg("target_x"),
            py::arg("target_y"),
            py::arg("screen_center_x"),
            py::arg("screen_center_y"),
            py::arg("body_x1"),
            py::arg("body_y1"),
            py::arg("body_x2"),
            py::arg("body_y2"),
            py::arg("torso_x1"),
            py::arg("torso_y1"),
            py::arg("torso_x2"),
            py::arg("torso_y2"),
            py::arg("body_state_mode"));

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
            "select_xyxy_with_ego",
            [](vision_native::VisionTargetSelector& selector,
               py::array_t<float, py::array::c_style | py::array::forcecast> detections,
               float ego_dx,
               float ego_dy,
               float ego_confidence) {
                return vision_result_to_dict(
                    selector.select(
                        batch_from_xyxy_array(detections),
                        simple_ego_warp(ego_dx, ego_dy, ego_confidence)));
            },
            py::arg("detections"),
            py::arg("ego_dx"),
            py::arg("ego_dy"),
            py::arg("ego_confidence"))
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
            py::arg("frame"))
        .def(
            "select_xyxy_rgb_with_ego",
            [](vision_native::VisionTargetSelector& selector,
               py::array_t<float, py::array::c_style | py::array::forcecast> detections,
               py::array_t<uint8_t, py::array::c_style | py::array::forcecast> frame,
               float ego_dx,
               float ego_dy,
               float ego_confidence) {
                return vision_result_to_dict(
                    selector.select_with_frame(
                        batch_from_xyxy_array(detections),
                        rgb_frame_view(frame),
                        simple_ego_warp(ego_dx, ego_dy, ego_confidence)));
            },
            py::arg("detections"),
            py::arg("frame"),
            py::arg("ego_dx"),
            py::arg("ego_dy"),
            py::arg("ego_confidence"));
}
