from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Iterable, Sequence

try:
    import yaml
except ImportError:  # pragma: no cover - PyYAML is present in the project env.
    yaml = None


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DATASETS = (
    PROJECT_ROOT / "models" / "train",
    Path("D:/datasets/roboflow_candidates"),
)
NATIVE_MODULE_DIR = PROJECT_ROOT / "native" / "vision_native" / "build" / "Release"
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
SPLIT_ALIASES = {
    "val": ("valid", "val"),
    "valid": ("valid", "val"),
    "train": ("train",),
    "test": ("test",),
}


@dataclass(frozen=True)
class Box:
    x1: float
    y1: float
    x2: float
    y2: float
    class_id: int = 0
    conf: float = 1.0

    @property
    def width(self) -> float:
        return max(0.0, self.x2 - self.x1)

    @property
    def height(self) -> float:
        return max(0.0, self.y2 - self.y1)

    @property
    def area(self) -> float:
        return self.width * self.height


@dataclass(frozen=True)
class CropWindow:
    left: int
    top: int
    width: int
    height: int
    source_width: int
    source_height: int


@dataclass(frozen=True)
class DatasetSplit:
    root: Path
    name: str
    split: str
    image_dir: Path
    label_dir: Path
    class_names: tuple[str, ...]


@dataclass
class MatchResult:
    tp: int
    fp: int
    fn: int
    matched_ious: list[float] = field(default_factory=list)


@dataclass(frozen=True)
class GpuSample:
    timestamp: float
    index: int
    utilization_gpu_percent: float | None = None
    utilization_memory_percent: float | None = None
    memory_used_mb: float | None = None
    memory_total_mb: float | None = None
    power_draw_w: float | None = None
    temperature_c: float | None = None


@dataclass
class DatasetSummary:
    dataset: str
    root: str
    split: str
    images: int = 0
    labels: int = 0
    detections: int = 0
    tp: int = 0
    fp: int = 0
    fn: int = 0
    failures: int = 0
    infer_ms: list[float] = field(default_factory=list)
    gpu_total_ms: list[float] = field(default_factory=list)
    wall_ms: list[float] = field(default_factory=list)
    preprocess_ms: list[float] = field(default_factory=list)
    output_wait_ms: list[float] = field(default_factory=list)
    decode_ms: list[float] = field(default_factory=list)

    def add(
        self,
        *,
        label_count: int,
        detection_count: int,
        match: MatchResult,
        timings: dict[str, float],
    ) -> None:
        self.images += 1
        self.labels += label_count
        self.detections += detection_count
        self.tp += match.tp
        self.fp += match.fp
        self.fn += match.fn
        if match.fp or match.fn:
            self.failures += 1
        _append_if_number(self.infer_ms, timings.get("infer_ms"))
        _append_if_number(self.gpu_total_ms, timings.get("gpu_total_ms"))
        _append_if_number(self.wall_ms, timings.get("wall_ms"))
        _append_if_number(self.preprocess_ms, timings.get("preprocess_ms"))
        _append_if_number(self.output_wait_ms, timings.get("output_wait_ms"))
        _append_if_number(self.decode_ms, timings.get("decode_ms"))

    def metrics(self) -> dict[str, Any]:
        return {
            "dataset": self.dataset,
            "root": self.root,
            "split": self.split,
            "images": self.images,
            "labels": self.labels,
            "detections": self.detections,
            "tp": self.tp,
            "fp": self.fp,
            "fn": self.fn,
            "precision": _safe_div(self.tp, self.tp + self.fp),
            "recall": _safe_div(self.tp, self.tp + self.fn),
            "f1": _f1(self.tp, self.fp, self.fn),
            "failure_images": self.failures,
            "infer_ms": _timing_summary(self.infer_ms),
            "gpu_total_ms": _timing_summary(self.gpu_total_ms),
            "wall_ms": _timing_summary(self.wall_ms),
            "preprocess_ms": _timing_summary(self.preprocess_ms),
            "output_wait_ms": _timing_summary(self.output_wait_ms),
            "decode_ms": _timing_summary(self.decode_ms),
        }


def _append_if_number(values: list[float], value: Any) -> None:
    if isinstance(value, (int, float)) and math.isfinite(float(value)):
        values.append(float(value))


def _append_optional_number(values: list[float], value: float | None) -> None:
    if value is not None and math.isfinite(value):
        values.append(value)


def _safe_div(numerator: float, denominator: float) -> float:
    if denominator <= 0:
        return 0.0
    return numerator / denominator


def _f1(tp: int, fp: int, fn: int) -> float:
    precision = _safe_div(tp, tp + fp)
    recall = _safe_div(tp, tp + fn)
    if precision + recall <= 0:
        return 0.0
    return 2.0 * precision * recall / (precision + recall)


def percentile(values: Sequence[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = (len(ordered) - 1) * pct
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return ordered[low]
    fraction = rank - low
    return ordered[low] * (1.0 - fraction) + ordered[high] * fraction


def _timing_summary(values: Sequence[float]) -> dict[str, float]:
    if not values:
        return {"count": 0, "avg": 0.0, "p50": 0.0, "p90": 0.0, "p95": 0.0, "p99": 0.0, "max": 0.0}
    return {
        "count": len(values),
        "avg": sum(values) / len(values),
        "p50": percentile(values, 0.50),
        "p90": percentile(values, 0.90),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": max(values),
    }


def _parse_optional_float(value: str) -> float | None:
    cleaned = value.strip()
    if not cleaned or cleaned.upper() in {"N/A", "[N/A]", "NOT SUPPORTED", "[NOT SUPPORTED]"}:
        return None
    cleaned = cleaned.replace("%", "").replace("MiB", "").replace("W", "").replace("C", "").strip()
    try:
        parsed = float(cleaned)
    except ValueError:
        return None
    return parsed if math.isfinite(parsed) else None


def parse_nvidia_smi_sample(line: str, timestamp: float) -> GpuSample | None:
    parts = [part.strip() for part in line.split(",")]
    if len(parts) < 7:
        return None
    index = _parse_optional_float(parts[0])
    if index is None:
        return None
    return GpuSample(
        timestamp=timestamp,
        index=int(index),
        utilization_gpu_percent=_parse_optional_float(parts[1]),
        utilization_memory_percent=_parse_optional_float(parts[2]),
        memory_used_mb=_parse_optional_float(parts[3]),
        memory_total_mb=_parse_optional_float(parts[4]),
        power_draw_w=_parse_optional_float(parts[5]),
        temperature_c=_parse_optional_float(parts[6]),
    )


def summarize_gpu_samples(samples: Sequence[GpuSample], duration_seconds: float, error: str | None = None) -> dict[str, Any]:
    gpu_util: list[float] = []
    mem_util: list[float] = []
    mem_used: list[float] = []
    mem_total: list[float] = []
    power_draw: list[float] = []
    temperature: list[float] = []
    indexes = sorted({sample.index for sample in samples})
    for sample in samples:
        _append_optional_number(gpu_util, sample.utilization_gpu_percent)
        _append_optional_number(mem_util, sample.utilization_memory_percent)
        _append_optional_number(mem_used, sample.memory_used_mb)
        _append_optional_number(mem_total, sample.memory_total_mb)
        _append_optional_number(power_draw, sample.power_draw_w)
        _append_optional_number(temperature, sample.temperature_c)
    avg_power = sum(power_draw) / len(power_draw) if power_draw else 0.0
    energy_joules = avg_power * max(0.0, duration_seconds)
    return {
        "enabled": True,
        "duration_seconds": max(0.0, duration_seconds),
        "sample_count": len(samples),
        "gpu_indexes": indexes,
        "error": error,
        "utilization_gpu_percent": _timing_summary(gpu_util),
        "utilization_memory_percent": _timing_summary(mem_util),
        "memory_used_mb": _timing_summary(mem_used),
        "memory_total_mb": max(mem_total) if mem_total else 0.0,
        "power_draw_w": _timing_summary(power_draw),
        "energy_joules": energy_joules,
        "energy_wh": energy_joules / 3600.0,
        "temperature_c": _timing_summary(temperature),
    }


def build_efficiency_metrics(overall: dict[str, Any], gpu_resource: dict[str, Any] | None) -> dict[str, Any]:
    duration = float(gpu_resource.get("duration_seconds", 0.0)) if gpu_resource else 0.0
    energy_joules = float(gpu_resource.get("energy_joules", 0.0)) if gpu_resource else 0.0
    energy_kj = energy_joules / 1000.0
    gpu_p50 = float(overall.get("gpu_total_ms", {}).get("p50", 0.0))
    gpu_p95 = float(overall.get("gpu_total_ms", {}).get("p95", 0.0))
    images = float(overall.get("images", 0.0))
    tp = float(overall.get("tp", 0.0))
    fp = float(overall.get("fp", 0.0))
    f1 = float(overall.get("f1", 0.0))
    return {
        "images_per_second": _safe_div(images, duration),
        "true_positive_per_second": _safe_div(tp, duration),
        "false_positive_per_second": _safe_div(fp, duration),
        "images_per_kj": _safe_div(images, energy_kj),
        "true_positive_per_kj": _safe_div(tp, energy_kj),
        "f1_per_kj": _safe_div(f1, energy_kj),
        "f1_per_gpu_total_ms_p50": _safe_div(f1, gpu_p50),
        "f1_per_gpu_total_ms_p95": _safe_div(f1, gpu_p95),
    }


class GpuResourceMonitor:
    def __init__(self, *, interval_seconds: float, gpu_index: int) -> None:
        self.interval_seconds = max(0.05, interval_seconds)
        self.gpu_index = gpu_index
        self.samples: list[GpuSample] = []
        self.error: str | None = None
        self.start_time = 0.0
        self.end_time = 0.0
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._nvidia_smi = shutil.which("nvidia-smi")

    def start(self) -> None:
        self.start_time = time.perf_counter()
        if not self._nvidia_smi:
            self.error = "nvidia-smi not found on PATH"
            return
        self._thread = threading.Thread(target=self._run, name="gpu-resource-monitor", daemon=True)
        self._thread.start()

    def stop(self) -> dict[str, Any]:
        self.end_time = time.perf_counter()
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=max(1.0, self.interval_seconds * 2.0))
        duration = (self.end_time or time.perf_counter()) - (self.start_time or time.perf_counter())
        return summarize_gpu_samples(self.samples, duration, self.error)

    def _run(self) -> None:
        while not self._stop.is_set():
            self._sample_once()
            self._stop.wait(self.interval_seconds)

    def _sample_once(self) -> None:
        if not self._nvidia_smi:
            return
        cmd = [
            self._nvidia_smi,
            f"--id={self.gpu_index}",
            "--query-gpu=index,utilization.gpu,utilization.memory,memory.used,memory.total,power.draw,temperature.gpu",
            "--format=csv,noheader,nounits",
        ]
        try:
            completed = subprocess.run(cmd, capture_output=True, text=True, timeout=2.0, check=False)
        except (OSError, subprocess.TimeoutExpired) as exc:
            if self.error is None:
                self.error = str(exc)
            return
        if completed.returncode != 0:
            if self.error is None:
                self.error = completed.stderr.strip() or f"nvidia-smi exited with {completed.returncode}"
            return
        timestamp = time.perf_counter()
        for line in completed.stdout.splitlines():
            sample = parse_nvidia_smi_sample(line, timestamp)
            if sample is not None:
                self.samples.append(sample)


def load_data_yaml(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    content = path.read_text(encoding="utf-8")
    if yaml is not None:
        loaded = yaml.safe_load(content)
        return loaded if isinstance(loaded, dict) else {}
    result: dict[str, Any] = {}
    for line in content.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if key == "names":
            result[key] = [part.strip().strip("'\"") for part in value.strip("[]").split(",") if part.strip()]
        else:
            result[key] = value
    return result


def class_names_from_yaml(path: Path) -> tuple[str, ...]:
    names = load_data_yaml(path).get("names", [])
    if isinstance(names, dict):
        ordered = [names[key] for key in sorted(names, key=lambda value: int(value))]
        return tuple(str(name) for name in ordered)
    if isinstance(names, list):
        return tuple(str(name) for name in names)
    return ()


def normalize_splits(values: Sequence[str]) -> tuple[str, ...]:
    if not values:
        return ("valid",)
    expanded: list[str] = []
    for value in values:
        for part in str(value).split(","):
            split = part.strip().lower()
            if not split:
                continue
            if split == "all":
                for candidate in ("train", "valid", "test"):
                    if candidate not in expanded:
                        expanded.append(candidate)
                continue
            if split not in SPLIT_ALIASES:
                raise ValueError(f"unknown split '{split}', expected train/valid/val/test/all")
            canonical = "valid" if split == "val" else split
            if canonical not in expanded:
                expanded.append(canonical)
    return tuple(expanded or ["valid"])


def discover_dataset_roots(roots: Sequence[Path]) -> list[Path]:
    discovered: list[Path] = []
    seen: set[Path] = set()
    for root in roots:
        root = root.expanduser().resolve()
        candidates = [root] if (root / "data.yaml").is_file() else []
        if root.is_dir():
            candidates.extend(path.parent for path in root.rglob("data.yaml"))
        for candidate in candidates:
            resolved = candidate.resolve()
            if resolved not in seen:
                seen.add(resolved)
                discovered.append(resolved)
    return sorted(discovered, key=lambda value: str(value).lower())


def discover_yolo_splits(roots: Sequence[Path], splits: Sequence[str]) -> list[DatasetSplit]:
    requested = normalize_splits(splits)
    datasets: list[DatasetSplit] = []
    for root in discover_dataset_roots(roots):
        data_yaml = root / "data.yaml"
        class_names = class_names_from_yaml(data_yaml)
        for split in requested:
            dirs = _find_split_dirs(root, split)
            if dirs is None:
                continue
            image_dir, label_dir, actual_split = dirs
            if not any(iter_image_files(image_dir)):
                continue
            datasets.append(
                DatasetSplit(
                    root=root,
                    name=root.name,
                    split=actual_split,
                    image_dir=image_dir,
                    label_dir=label_dir,
                    class_names=class_names,
                )
            )
    return datasets


def _find_split_dirs(root: Path, split: str) -> tuple[Path, Path, str] | None:
    for actual_split in SPLIT_ALIASES[split]:
        image_dir = root / actual_split / "images"
        label_dir = root / actual_split / "labels"
        if image_dir.is_dir() and label_dir.is_dir():
            return image_dir, label_dir, actual_split
    return None


def iter_image_files(image_dir: Path) -> Iterable[Path]:
    for path in sorted(image_dir.iterdir(), key=lambda value: value.name.lower()):
        if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS:
            yield path


def label_path_for_image(image_path: Path, label_dir: Path) -> Path:
    return label_dir / f"{image_path.stem}.txt"


def parse_target_classes(value: str | None) -> tuple[str, ...]:
    if not value:
        return ()
    return tuple(part.strip().lower() for part in value.split(",") if part.strip())


def class_is_selected(class_id: int, class_names: Sequence[str], selected: Sequence[str]) -> bool:
    if not selected:
        return True
    class_name = class_names[class_id].lower() if 0 <= class_id < len(class_names) else ""
    for token in selected:
        if token == class_name:
            return True
        if token.isdigit() and int(token) == class_id:
            return True
    return False


def parse_yolo_label_file(
    path: Path,
    image_width: int,
    image_height: int,
    *,
    class_names: Sequence[str] = (),
    selected_classes: Sequence[str] = (),
) -> list[Box]:
    if not path.is_file():
        return []
    boxes: list[Box] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 5:
            continue
        try:
            class_id = int(float(parts[0]))
            cx, cy, width, height = (float(parts[index]) for index in range(1, 5))
        except ValueError:
            continue
        if not class_is_selected(class_id, class_names, selected_classes):
            continue
        x1 = (cx - width / 2.0) * image_width
        y1 = (cy - height / 2.0) * image_height
        x2 = (cx + width / 2.0) * image_width
        y2 = (cy + height / 2.0) * image_height
        boxes.append(
            Box(
                x1=max(0.0, min(float(image_width), x1)),
                y1=max(0.0, min(float(image_height), y1)),
                x2=max(0.0, min(float(image_width), x2)),
                y2=max(0.0, min(float(image_height), y2)),
                class_id=class_id,
            )
        )
    return [box for box in boxes if box.area > 0.0]


def center_crop_window(
    source_width: int,
    source_height: int,
    crop_width: int | None,
    crop_height: int | None,
) -> CropWindow:
    width = source_width if not crop_width or crop_width <= 0 else min(crop_width, source_width)
    height = source_height if not crop_height or crop_height <= 0 else min(crop_height, source_height)
    left = max(0, (source_width - width) // 2)
    top = max(0, (source_height - height) // 2)
    return CropWindow(left=left, top=top, width=width, height=height, source_width=source_width, source_height=source_height)


def clip_boxes_to_crop(boxes: Sequence[Box], crop: CropWindow, min_area: float = 4.0) -> list[Box]:
    clipped: list[Box] = []
    crop_right = crop.left + crop.width
    crop_bottom = crop.top + crop.height
    for box in boxes:
        x1 = max(box.x1, float(crop.left))
        y1 = max(box.y1, float(crop.top))
        x2 = min(box.x2, float(crop_right))
        y2 = min(box.y2, float(crop_bottom))
        candidate = Box(
            x1=x1 - crop.left,
            y1=y1 - crop.top,
            x2=x2 - crop.left,
            y2=y2 - crop.top,
            class_id=box.class_id,
            conf=box.conf,
        )
        if candidate.area >= min_area:
            clipped.append(candidate)
    return clipped


def box_iou(left: Box, right: Box) -> float:
    ix1 = max(left.x1, right.x1)
    iy1 = max(left.y1, right.y1)
    ix2 = min(left.x2, right.x2)
    iy2 = min(left.y2, right.y2)
    intersection = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    union = left.area + right.area - intersection
    if union <= 0.0:
        return 0.0
    return intersection / union


def match_detections(
    ground_truth: Sequence[Box],
    detections: Sequence[Box],
    *,
    iou_threshold: float,
    class_aware: bool = False,
) -> MatchResult:
    matched_gt: set[int] = set()
    matched_ious: list[float] = []
    true_positive = 0
    false_positive = 0

    for detection in sorted(detections, key=lambda box: box.conf, reverse=True):
        best_index = -1
        best_iou = 0.0
        for index, truth in enumerate(ground_truth):
            if index in matched_gt:
                continue
            if class_aware and detection.class_id != truth.class_id:
                continue
            iou = box_iou(truth, detection)
            if iou > best_iou:
                best_iou = iou
                best_index = index
        if best_index >= 0 and best_iou >= iou_threshold:
            matched_gt.add(best_index)
            matched_ious.append(best_iou)
            true_positive += 1
        else:
            false_positive += 1

    false_negative = len(ground_truth) - len(matched_gt)
    return MatchResult(tp=true_positive, fp=false_positive, fn=false_negative, matched_ious=matched_ious)


def detection_from_native(raw: dict[str, Any]) -> Box:
    return Box(
        x1=float(raw.get("x1", 0.0)),
        y1=float(raw.get("y1", 0.0)),
        x2=float(raw.get("x2", 0.0)),
        y2=float(raw.get("y2", 0.0)),
        class_id=int(raw.get("class_id", 0)),
        conf=float(raw.get("conf", 0.0)),
    )


def load_rgb_array(path: Path) -> Any:
    import numpy as np
    from PIL import Image

    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"))


def crop_rgb_array(rgb: Any, crop: CropWindow) -> Any:
    return rgb[crop.top : crop.top + crop.height, crop.left : crop.left + crop.width, :]


def ensure_native_module() -> Any:
    if str(NATIVE_MODULE_DIR) not in sys.path:
        sys.path.insert(0, str(NATIVE_MODULE_DIR))
    try:
        import vision_native_cpp
    except ImportError as exc:
        raise RuntimeError(
            f"failed to import vision_native_cpp from {NATIVE_MODULE_DIR}; build native vision first"
        ) from exc
    return vision_native_cpp


def resolve_project_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return PROJECT_ROOT / path


def run_warmup(engine: Any, sample_rgb: Any, conf: float, count: int) -> None:
    for _ in range(max(0, count)):
        engine.infer_rgb(sample_rgb, conf)


def benchmark_split(
    engine: Any,
    dataset: DatasetSplit,
    *,
    conf: float,
    iou_threshold: float,
    selected_classes: Sequence[str],
    class_aware: bool,
    crop_width: int | None,
    crop_height: int | None,
    max_images: int | None,
    warmup: int,
    failure_writer: "FailureWriter | None" = None,
) -> DatasetSummary:
    summary = DatasetSummary(dataset=dataset.name, root=str(dataset.root), split=dataset.split)
    image_paths = list(iter_image_files(dataset.image_dir))
    if max_images is not None and max_images > 0:
        image_paths = image_paths[:max_images]
    warmed = False

    for image_path in image_paths:
        rgb = load_rgb_array(image_path)
        source_height, source_width = int(rgb.shape[0]), int(rgb.shape[1])
        crop = center_crop_window(source_width, source_height, crop_width, crop_height)
        cropped_rgb = crop_rgb_array(rgb, crop)
        label_path = label_path_for_image(image_path, dataset.label_dir)
        labels = parse_yolo_label_file(
            label_path,
            source_width,
            source_height,
            class_names=dataset.class_names,
            selected_classes=selected_classes,
        )
        cropped_labels = clip_boxes_to_crop(labels, crop)

        if not warmed:
            run_warmup(engine, cropped_rgb, conf, warmup)
            warmed = True

        wall_start = time.perf_counter()
        raw_result = engine.infer_rgb(cropped_rgb, conf)
        wall_ms = (time.perf_counter() - wall_start) * 1000.0
        detections = [detection_from_native(raw) for raw in raw_result.get("detections", [])]
        match = match_detections(
            cropped_labels,
            detections,
            iou_threshold=iou_threshold,
            class_aware=class_aware,
        )
        timings = {
            "wall_ms": wall_ms,
            "preprocess_ms": raw_result.get("preprocess_ms", 0.0),
            "infer_ms": raw_result.get("infer_ms", 0.0),
            "gpu_total_ms": raw_result.get("gpu_total_ms", 0.0),
            "output_wait_ms": raw_result.get("output_wait_ms", 0.0),
            "decode_ms": raw_result.get("decode_ms", 0.0),
        }
        summary.add(label_count=len(cropped_labels), detection_count=len(detections), match=match, timings=timings)
        if failure_writer is not None and (match.fp or match.fn):
            failure_writer.write(
                dataset=dataset,
                image_path=image_path,
                crop=crop,
                rgb=cropped_rgb,
                ground_truth=cropped_labels,
                detections=detections,
                match=match,
            )
    return summary


class FailureWriter:
    def __init__(self, output_dir: Path, max_failures: int) -> None:
        self.output_dir = output_dir
        self.max_failures = max(0, max_failures)
        self.count = 0
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.manifest_path = self.output_dir / "failures.jsonl"
        self.manifest = self.manifest_path.open("w", encoding="utf-8")

    def close(self) -> None:
        self.manifest.close()

    def write(
        self,
        *,
        dataset: DatasetSplit,
        image_path: Path,
        crop: CropWindow,
        rgb: Any,
        ground_truth: Sequence[Box],
        detections: Sequence[Box],
        match: MatchResult,
    ) -> None:
        if self.count >= self.max_failures:
            return
        self.count += 1
        safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", f"{dataset.name}_{dataset.split}_{image_path.stem}")
        output_image = self.output_dir / f"{self.count:04d}_{safe_name}.jpg"
        self._save_annotated_image(output_image, rgb, ground_truth, detections)
        record = {
            "dataset": dataset.name,
            "split": dataset.split,
            "image": str(image_path),
            "crop": asdict(crop),
            "output_image": str(output_image),
            "tp": match.tp,
            "fp": match.fp,
            "fn": match.fn,
            "labels": len(ground_truth),
            "detections": len(detections),
        }
        self.manifest.write(json.dumps(record, ensure_ascii=False) + "\n")

    @staticmethod
    def _save_annotated_image(output_image: Path, rgb: Any, ground_truth: Sequence[Box], detections: Sequence[Box]) -> None:
        from PIL import Image, ImageDraw

        image = Image.fromarray(rgb)
        draw = ImageDraw.Draw(image)
        for box in ground_truth:
            draw.rectangle((box.x1, box.y1, box.x2, box.y2), outline=(0, 255, 0), width=2)
        for box in detections:
            draw.rectangle((box.x1, box.y1, box.x2, box.y2), outline=(255, 0, 0), width=2)
        image.save(output_image, quality=90)


def aggregate_summaries(summaries: Sequence[DatasetSummary]) -> DatasetSummary:
    overall = DatasetSummary(dataset="OVERALL", root="", split="")
    for summary in summaries:
        overall.images += summary.images
        overall.labels += summary.labels
        overall.detections += summary.detections
        overall.tp += summary.tp
        overall.fp += summary.fp
        overall.fn += summary.fn
        overall.failures += summary.failures
        overall.infer_ms.extend(summary.infer_ms)
        overall.gpu_total_ms.extend(summary.gpu_total_ms)
        overall.wall_ms.extend(summary.wall_ms)
        overall.preprocess_ms.extend(summary.preprocess_ms)
        overall.output_wait_ms.extend(summary.output_wait_ms)
        overall.decode_ms.extend(summary.decode_ms)
    return overall


def print_discovery(datasets: Sequence[DatasetSplit], max_images: int | None = None) -> None:
    if not datasets:
        print("No YOLO dataset splits found.")
        return
    print(f"{'dataset':42} {'split':7} {'images':>7} classes")
    print("-" * 90)
    for dataset in datasets:
        count = sum(1 for _ in iter_image_files(dataset.image_dir))
        shown_count = min(count, max_images) if max_images and max_images > 0 else count
        classes = ", ".join(dataset.class_names) if dataset.class_names else "-"
        print(f"{dataset.name[:42]:42} {dataset.split:7} {shown_count:7d} {classes}")


def print_summary(summaries: Sequence[DatasetSummary]) -> None:
    header = (
        f"{'dataset':34} {'split':7} {'img':>5} {'gt':>6} {'det':>6} "
        f"{'P':>6} {'R':>6} {'F1':>6} {'infer p50/p95':>18} {'gpu p50/p95':>18}"
    )
    print(header)
    print("-" * len(header))
    for summary in list(summaries) + [aggregate_summaries(summaries)]:
        metrics = summary.metrics()
        infer = metrics["infer_ms"]
        gpu = metrics["gpu_total_ms"]
        print(
            f"{summary.dataset[:34]:34} {summary.split or '-':7} "
            f"{summary.images:5d} {summary.labels:6d} {summary.detections:6d} "
            f"{metrics['precision']:6.3f} {metrics['recall']:6.3f} {metrics['f1']:6.3f} "
            f"{infer['p50']:7.3f}/{infer['p95']:<7.3f} "
            f"{gpu['p50']:7.3f}/{gpu['p95']:<7.3f}"
        )


def write_json(
    path: Path,
    summaries: Sequence[DatasetSummary],
    args: argparse.Namespace,
    *,
    gpu_resource: dict[str, Any] | None = None,
) -> None:
    overall = aggregate_summaries(summaries).metrics()
    output = {
        "parameters": {
            "model": str(resolve_project_path(args.model)),
            "datasets": [str(resolve_project_path(path)) for path in args.datasets],
            "split": args.split,
            "max_images": args.max_images,
            "conf": args.conf,
            "iou": args.iou,
            "target_classes": args.target_classes,
            "class_aware": args.class_aware,
            "crop_width": args.crop_width,
            "crop_height": args.crop_height,
            "warmup": args.warmup,
            "gpu_monitor": args.gpu_monitor,
            "gpu_monitor_interval_ms": args.gpu_monitor_interval_ms,
            "gpu_index": args.gpu_index,
        },
        "datasets": [summary.metrics() for summary in summaries],
        "overall": overall,
    }
    if gpu_resource is not None:
        output["gpu_resource"] = gpu_resource
        output["efficiency"] = build_efficiency_metrics(overall, gpu_resource)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(output, indent=2, ensure_ascii=False), encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Benchmark native TensorRT vision on YOLO-format datasets.",
    )
    parser.add_argument("--model", type=Path, default=Path("models/best.engine"), help="TensorRT .engine path.")
    parser.add_argument(
        "--datasets",
        type=Path,
        nargs="+",
        default=list(DEFAULT_DATASETS),
        help="Dataset roots or parent folders containing Roboflow/YOLO data.yaml files.",
    )
    parser.add_argument(
        "--split",
        nargs="+",
        default=["valid"],
        help="Splits to benchmark: train, valid/val, test, all. Comma-separated values are also accepted.",
    )
    parser.add_argument("--max-images", type=int, default=None, help="Max images per discovered dataset split.")
    parser.add_argument("--conf", type=float, default=0.25, help="Native detector confidence threshold.")
    parser.add_argument("--iou", type=float, default=0.50, help="IoU threshold for TP matching.")
    parser.add_argument(
        "--target-classes",
        default=None,
        help="Comma-separated GT class names or ids to evaluate, e.g. body,enemy,target,0.",
    )
    parser.add_argument(
        "--class-aware",
        action="store_true",
        help="Require prediction class id to equal GT class id. Off by default because source datasets use incompatible class ids.",
    )
    parser.add_argument("--crop-width", type=int, default=None, help="Optional centered ROI crop width before inference.")
    parser.add_argument("--crop-height", type=int, default=None, help="Optional centered ROI crop height before inference.")
    parser.add_argument("--warmup", type=int, default=3, help="Warmup inferences on the first image of each dataset.")
    parser.add_argument("--discover-only", action="store_true", help="Only print discovered datasets; do not load TensorRT.")
    parser.add_argument("--output-json", type=Path, default=None, help="Optional summary JSON output path.")
    parser.add_argument("--save-failures", type=Path, default=None, help="Optional directory for annotated FP/FN images.")
    parser.add_argument("--max-failures", type=int, default=100, help="Max annotated failure images to save.")
    parser.add_argument("--gpu-monitor", action="store_true", help="Sample GPU utilization, memory, power, and energy via nvidia-smi.")
    parser.add_argument("--gpu-index", type=int, default=0, help="GPU index to sample when --gpu-monitor is enabled.")
    parser.add_argument(
        "--gpu-monitor-interval-ms",
        type=int,
        default=500,
        help="GPU resource sampling interval in milliseconds when --gpu-monitor is enabled.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        splits = normalize_splits(args.split)
    except ValueError as exc:
        parser.error(str(exc))
        return 2

    datasets = discover_yolo_splits([resolve_project_path(path) for path in args.datasets], splits)
    if args.discover_only:
        print_discovery(datasets, args.max_images)
        return 0
    if not datasets:
        print("No YOLO dataset splits found for the requested roots/splits.", file=sys.stderr)
        return 2

    model_path = resolve_project_path(args.model)
    if not model_path.is_file():
        print(f"Model not found: {model_path}", file=sys.stderr)
        return 2

    selected_classes = parse_target_classes(args.target_classes)
    vision_native_cpp = ensure_native_module()
    engine_info = vision_native_cpp.inspect_engine(str(model_path))
    print(
        f"model={model_path} input={engine_info.get('tensors', [{}])[0].get('shape', '?')} "
        f"conf={args.conf} iou={args.iou} crop={args.crop_width or '-'}x{args.crop_height or '-'}"
    )
    if selected_classes:
        print(f"target_classes={','.join(selected_classes)} class_aware={args.class_aware}")
    engine = vision_native_cpp.NativeEngine(str(model_path))

    failure_writer = FailureWriter(resolve_project_path(args.save_failures), args.max_failures) if args.save_failures else None
    gpu_monitor = (
        GpuResourceMonitor(interval_seconds=args.gpu_monitor_interval_ms / 1000.0, gpu_index=args.gpu_index)
        if args.gpu_monitor
        else None
    )
    gpu_resource: dict[str, Any] | None = None
    summaries: list[DatasetSummary] = []
    try:
        if gpu_monitor is not None:
            gpu_monitor.start()
        for dataset in datasets:
            summaries.append(
                benchmark_split(
                    engine,
                    dataset,
                    conf=args.conf,
                    iou_threshold=args.iou,
                    selected_classes=selected_classes,
                    class_aware=args.class_aware,
                    crop_width=args.crop_width,
                    crop_height=args.crop_height,
                    max_images=args.max_images,
                    warmup=args.warmup,
                    failure_writer=failure_writer,
                )
            )
    finally:
        if gpu_monitor is not None:
            gpu_resource = gpu_monitor.stop()
        if failure_writer is not None:
            failure_writer.close()

    print_summary(summaries)
    if gpu_resource is not None:
        gpu_util = gpu_resource["utilization_gpu_percent"]
        power = gpu_resource["power_draw_w"]
        print(
            "gpu_resource "
            f"samples={gpu_resource['sample_count']} "
            f"duration={gpu_resource['duration_seconds']:.3f}s "
            f"util avg/p95={gpu_util['avg']:.1f}/{gpu_util['p95']:.1f}% "
            f"power avg/p95={power['avg']:.1f}/{power['p95']:.1f}W "
            f"energy={gpu_resource['energy_joules']:.1f}J"
        )
        if gpu_resource.get("error"):
            print(f"gpu_resource warning: {gpu_resource['error']}", file=sys.stderr)
    if args.output_json is not None:
        output_json = resolve_project_path(args.output_json)
        write_json(output_json, summaries, args, gpu_resource=gpu_resource)
        print(f"wrote {output_json}")
    if failure_writer is not None:
        print(f"wrote failures to {failure_writer.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
