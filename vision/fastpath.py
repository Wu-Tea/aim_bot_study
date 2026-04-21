import time
from dataclasses import dataclass

import numpy as np
import torch
from ultralytics import YOLO

from .targeting import ParsedDetections


def _load_model(config):
    try:
        model = YOLO(config.model_path, task=config.model_task)
        print(f"Loaded model: {config.model_path}")
        return model
    except Exception as exc:
        print(f"Failed to load {config.model_path}: {exc}. Falling back to {config.fallback_model_path}.")
        return YOLO(config.fallback_model_path, task=config.model_task)


def _resolve_autobackend(model):
    predictor = getattr(model, "predictor", None)
    backend = getattr(predictor, "model", None) if predictor is not None else None
    if backend is None:
        backend = getattr(model, "model", None)
    return None if isinstance(backend, str) else backend


def _describe_model_backend(model):
    backend = _resolve_autobackend(model)
    if backend is None:
        print(f"[Vision] backend unresolved; model.model={type(getattr(model, 'model', None)).__name__}")
        return
    print(
        "[Vision] backend "
        f"{{'class': '{type(backend).__name__}', 'pt': {getattr(backend, 'pt', None)}, "
        f"'engine': {getattr(backend, 'engine', None)}, 'onnx': {getattr(backend, 'onnx', None)}, "
        f"'fp16': {getattr(backend, 'fp16', None)}, 'device': '{getattr(backend, 'device', '?')}'}}"
    )


def _bench_once(fn, iters: int):
    timings = []
    for _ in range(iters):
        t0 = time.perf_counter()
        fn()
        timings.append((time.perf_counter() - t0) * 1000.0)
    timings.sort()
    return timings[iters // 10], timings[iters // 2], timings[(iters * 9) // 10]


def _warmup_model(model, config, predict_kwargs: dict, bench: bool = False):
    print("Warming up model...")
    dummy_frame = np.zeros((config.capture_height, config.capture_width, 3), dtype=np.uint8)
    for _ in range(config.warmup_iterations):
        model.predict(source=dummy_frame, **predict_kwargs)

    _describe_model_backend(model)
    fast_path = _init_fast_path(model, config)

    if bench:
        bench_iters = 30
        p10, median, p90 = _bench_once(lambda: model.predict(source=dummy_frame, **predict_kwargs), bench_iters)
        print(f"[Vision] bench predict() {bench_iters} iters: p10={p10:.2f}ms median={median:.2f}ms p90={p90:.2f}ms")
        if fast_path is not None:
            p10, median, p90 = _bench_once(lambda: _fast_predict(fast_path, dummy_frame), bench_iters)
            print(f"[Vision] bench _fast_predict {bench_iters} iters: p10={p10:.2f}ms median={median:.2f}ms p90={p90:.2f}ms")

    print("Warmup complete.")
    return fast_path


@dataclass(slots=True)
class FastPath:
    backend: object
    gpu_input: torch.Tensor
    conf_thr: float
    max_det: int
    output_kind: str | None = None


def _detect_output_kind(raw) -> str:
    pred = raw[0] if isinstance(raw, (tuple, list)) else raw
    if not isinstance(pred, torch.Tensor):
        raise TypeError(f"Unexpected backend output type: {type(pred)}")
    if pred.ndim == 2:
        pred = pred.unsqueeze(0)
    if pred.ndim != 3:
        raise ValueError(f"Unexpected backend output ndim: {tuple(pred.shape)}")

    _, d1, d2 = pred.shape
    if d2 == 57:
        return "nms_in_engine"
    if d1 == 56 and d2 >= 1000:
        return "raw"
    return "nms_in_engine" if d1 <= 300 and d2 >= 6 else "raw"


def _init_fast_path(model, config):
    backend = _resolve_autobackend(model)
    if backend is None:
        print("[Vision] Fast path disabled: could not resolve AutoBackend after warmup.")
        return None

    fast_path = FastPath(
        backend=backend,
        gpu_input=torch.empty(
            (1, 3, config.capture_height, config.capture_width),
            dtype=_fast_path_input_dtype(backend, config.half),
            device=torch.device(f"cuda:{config.device}"),
        ),
        conf_thr=float(config.conf),
        max_det=10,
    )

    with torch.inference_mode():
        fast_path.output_kind = _detect_output_kind(backend(fast_path.gpu_input))

    print(f"[Vision] Fast path ready: output_kind={fast_path.output_kind}")
    return fast_path


def _fast_path_input_dtype(backend, config_half: bool):
    if getattr(backend, "fp16", False):
        return torch.float16
    if config_half and not hasattr(backend, "fp16"):
        return torch.float16
    return torch.float32


def _fast_predict(fast_path: FastPath, frame_rgb: np.ndarray):
    if not frame_rgb.flags.c_contiguous:
        frame_rgb = np.ascontiguousarray(frame_rgb)

    cpu_u8 = torch.from_numpy(frame_rgb)
    gpu_u8 = cpu_u8.to(fast_path.gpu_input.device)
    fast_path.gpu_input[0].copy_(gpu_u8.permute(2, 0, 1).to(fast_path.gpu_input.dtype).div_(255.0))

    with torch.inference_mode():
        raw = fast_path.backend(fast_path.gpu_input)

    output_kind = fast_path.output_kind or _detect_output_kind(raw)
    if fast_path.output_kind is None:
        fast_path.output_kind = output_kind

    if output_kind == "nms_in_engine":
        return _decode_nms_in_engine(raw, fast_path.conf_thr, fast_path.max_det)
    return _decode_raw_pose(raw, fast_path.conf_thr, fast_path.max_det)


def _decode_nms_in_engine(raw, conf_thr: float, max_det: int):
    pred = raw[0] if isinstance(raw, (tuple, list)) else raw
    if pred.ndim == 3:
        pred = pred[0]
    if pred.ndim != 2 or pred.shape[1] < 6:
        return []

    pred = pred[pred[:, 4] >= conf_thr]
    if pred.numel() == 0:
        return []
    if pred.shape[0] > max_det:
        pred = pred[torch.topk(pred[:, 4], max_det, largest=True, sorted=False).indices]

    pred_np = pred.detach().to(torch.float32).cpu().numpy()
    keypoints = None
    if pred_np.shape[1] - 6 >= 17 * 3:
        keypoints = pred_np[:, 6:57].reshape(-1, 17, 3).astype(np.float32, copy=False)

    return [
        ParsedDetections(
            boxes=np.ascontiguousarray(pred_np[:, :4], dtype=np.float32),
            confs=np.ascontiguousarray(pred_np[:, 4], dtype=np.float32),
            keypoints=keypoints,
        )
    ]


def _decode_raw_pose(raw, conf_thr: float, max_det: int):
    pred = raw[0] if isinstance(raw, (tuple, list)) else raw
    if pred.ndim == 3:
        pred = pred[0]
    if pred.ndim != 2 or pred.shape[0] < 6:
        return []

    pred_t = pred.transpose(0, 1).contiguous()
    cand = pred_t[pred_t[:, 4] >= conf_thr]
    if cand.numel() == 0:
        return []

    if cand.shape[0] > 50:
        cand = cand[torch.topk(cand[:, 4], 50, largest=True, sorted=False).indices]

    cx, cy, w, h = cand[:, 0], cand[:, 1], cand[:, 2], cand[:, 3]
    boxes = torch.stack((cx - w * 0.5, cy - h * 0.5, cx + w * 0.5, cy + h * 0.5), dim=1)

    from torchvision.ops import nms as tv_nms

    keep = tv_nms(boxes.float(), cand[:, 4].float(), 0.45)
    if keep.numel() == 0:
        return []
    keep = keep[:max_det]

    boxes_np = boxes[keep].detach().to(torch.float32).cpu().numpy().astype(np.float32, copy=False)
    scores_np = cand[keep, 4].detach().to(torch.float32).cpu().numpy().astype(np.float32, copy=False)
    kpts_np = cand[keep, 5:56].detach().to(torch.float32).cpu().numpy().reshape(-1, 17, 3).astype(np.float32, copy=False)
    return [ParsedDetections(boxes=boxes_np, confs=scores_np, keypoints=kpts_np)]


def _extract_detections(results):
    parsed: list[ParsedDetections] = []
    for result in results:
        if result.boxes is None or len(result.boxes) == 0:
            continue

        result_cpu = result.cpu()
        boxes_obj = result_cpu.boxes
        keypoints = None
        if result_cpu.keypoints is not None and result_cpu.keypoints.data is not None and len(result_cpu.keypoints.data) > 0:
            keypoints = result_cpu.keypoints.data.numpy()

        parsed.append(
            ParsedDetections(
                boxes=boxes_obj.xyxy.numpy(),
                confs=boxes_obj.conf.numpy() if boxes_obj.conf is not None else np.ones(len(boxes_obj), dtype=np.float32),
                keypoints=keypoints,
            )
        )

    return parsed
