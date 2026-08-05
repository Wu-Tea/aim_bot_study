from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
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
    matches: list["MatchPair"] = field(default_factory=list)


@dataclass(frozen=True)
class MatchPair:
    ground_truth_index: int
    detection_index: int
    iou: float


@dataclass(frozen=True)
class CroppedTarget:
    target_index: int
    original: Box
    cropped: Box
    visible_fraction: float


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
    skipped_undersized: int = 0
    infer_ms: list[float] = field(default_factory=list)
    enqueue_cpu_ms: list[float] = field(default_factory=list)
    gpu_total_ms: list[float] = field(default_factory=list)
    wall_ms: list[float] = field(default_factory=list)
    process_cpu_ms: list[float] = field(default_factory=list)
    process_rss_mb: list[float] = field(default_factory=list)
    preprocess_ms: list[float] = field(default_factory=list)
    output_wait_ms: list[float] = field(default_factory=list)
    decode_ms: list[float] = field(default_factory=list)
    frame_budgets_ms: tuple[float, ...] = (4.167, 8.333, 16.667)
    size_buckets: dict[str, dict[str, int]] = field(default_factory=dict)
    visibility_buckets: dict[str, dict[str, int]] = field(default_factory=dict)

    def add(
        self,
        *,
        label_count: int,
        detection_count: int,
        match: MatchResult,
        timings: dict[str, float],
        target_records: Sequence[dict[str, Any]] = (),
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
        _append_if_number(self.enqueue_cpu_ms, timings.get("enqueue_cpu_ms"))
        _append_if_number(self.gpu_total_ms, timings.get("gpu_total_ms"))
        _append_if_number(self.wall_ms, timings.get("wall_ms"))
        _append_if_number(self.process_cpu_ms, timings.get("process_cpu_ms"))
        _append_if_number(self.process_rss_mb, timings.get("process_rss_mb"))
        _append_if_number(self.preprocess_ms, timings.get("preprocess_ms"))
        _append_if_number(self.output_wait_ms, timings.get("output_wait_ms"))
        _append_if_number(self.decode_ms, timings.get("decode_ms"))
        for target in target_records:
            _add_bucket_result(self.size_buckets, str(target["size_bucket"]), bool(target["matched"]))
            _add_bucket_result(
                self.visibility_buckets,
                str(target["visibility_bucket"]),
                bool(target["matched"]),
            )

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
            "skipped_undersized": self.skipped_undersized,
            "infer_ms": _timing_summary(self.infer_ms),
            "enqueue_cpu_ms": _timing_summary(self.enqueue_cpu_ms),
            "gpu_total_ms": _timing_summary(self.gpu_total_ms),
            "wall_ms": _timing_summary(self.wall_ms),
            "process_cpu_ms": _timing_summary(self.process_cpu_ms),
            "process_rss_mb": _timing_summary(self.process_rss_mb),
            "preprocess_ms": _timing_summary(self.preprocess_ms),
            "output_wait_ms": _timing_summary(self.output_wait_ms),
            "decode_ms": _timing_summary(self.decode_ms),
            "deadline_misses": deadline_miss_summary(self.wall_ms, self.frame_budgets_ms),
            "size_buckets": _finalize_bucket_results(self.size_buckets),
            "visibility_buckets": _finalize_bucket_results(self.visibility_buckets),
        }


def _add_bucket_result(buckets: dict[str, dict[str, int]], name: str, matched: bool) -> None:
    bucket = buckets.setdefault(name, {"targets": 0, "tp": 0, "fn": 0})
    bucket["targets"] += 1
    bucket["tp" if matched else "fn"] += 1


def _finalize_bucket_results(buckets: dict[str, dict[str, int]]) -> dict[str, dict[str, float | int]]:
    return {
        name: {
            **counts,
            "recall": _safe_div(counts["tp"], counts["targets"]),
        }
        for name, counts in sorted(buckets.items())
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


def deadline_miss_summary(values: Sequence[float], budgets_ms: Sequence[float]) -> dict[str, dict[str, float | int]]:
    result: dict[str, dict[str, float | int]] = {}
    for budget in budgets_ms:
        misses = sum(1 for value in values if value > budget)
        result[f"{budget:g}"] = {
            "count": misses,
            "rate": _safe_div(misses, len(values)),
        }
    return result


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


def crop_fits_source(
    source_width: int,
    source_height: int,
    crop_width: int | None,
    crop_height: int | None,
) -> bool:
    requested_width = source_width if not crop_width or crop_width <= 0 else crop_width
    requested_height = source_height if not crop_height or crop_height <= 0 else crop_height
    return requested_width <= source_width and requested_height <= source_height


def crop_targets_to_window(
    boxes: Sequence[Box],
    crop: CropWindow,
    min_area: float = 4.0,
) -> list[CroppedTarget]:
    targets: list[CroppedTarget] = []
    crop_right = crop.left + crop.width
    crop_bottom = crop.top + crop.height
    for target_index, box in enumerate(boxes):
        x1 = max(box.x1, float(crop.left))
        y1 = max(box.y1, float(crop.top))
        x2 = min(box.x2, float(crop_right))
        y2 = min(box.y2, float(crop_bottom))
        cropped = Box(
            x1=x1 - crop.left,
            y1=y1 - crop.top,
            x2=x2 - crop.left,
            y2=y2 - crop.top,
            class_id=box.class_id,
            conf=box.conf,
        )
        if cropped.area < min_area:
            continue
        targets.append(
            CroppedTarget(
                target_index=target_index,
                original=box,
                cropped=cropped,
                visible_fraction=_safe_div(cropped.area, box.area),
            )
        )
    return targets


def clip_boxes_to_crop(boxes: Sequence[Box], crop: CropWindow, min_area: float = 4.0) -> list[Box]:
    return [target.cropped for target in crop_targets_to_window(boxes, crop, min_area)]


def target_size_bucket(tensor_width: float, tensor_height: float) -> str:
    area = max(0.0, tensor_width) * max(0.0, tensor_height)
    if area < 16.0**2:
        return "tiny"
    if area < 32.0**2:
        return "small"
    if area < 96.0**2:
        return "medium"
    if area < 160.0**2:
        return "large"
    return "very_large"


def visibility_bucket(visible_fraction: float) -> str:
    if visible_fraction < 0.50:
        return "severe_clip"
    if visible_fraction < 0.90:
        return "partial"
    if visible_fraction < 0.999:
        return "mostly_visible"
    return "full"


def build_target_record(
    target: CroppedTarget,
    *,
    crop_width: int,
    crop_height: int,
    tensor_width: int,
    tensor_height: int,
    matched: bool,
    matched_iou: float | None,
) -> dict[str, Any]:
    scale_x = _safe_div(tensor_width, crop_width)
    scale_y = _safe_div(tensor_height, crop_height)
    target_tensor_width = target.cropped.width * scale_x
    target_tensor_height = target.cropped.height * scale_y
    return {
        "target_index": target.target_index,
        "original": asdict(target.original),
        "cropped": asdict(target.cropped),
        "visible_fraction": target.visible_fraction,
        "visibility_bucket": visibility_bucket(target.visible_fraction),
        "tensor_width": target_tensor_width,
        "tensor_height": target_tensor_height,
        "tensor_area": target_tensor_width * target_tensor_height,
        "size_bucket": target_size_bucket(target_tensor_width, target_tensor_height),
        "matched": matched,
        "matched_iou": matched_iou,
    }


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
    matches: list[MatchPair] = []
    true_positive = 0
    false_positive = 0

    ordered_detections = sorted(enumerate(detections), key=lambda item: item[1].conf, reverse=True)
    for detection_index, detection in ordered_detections:
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
            matches.append(
                MatchPair(
                    ground_truth_index=best_index,
                    detection_index=detection_index,
                    iou=best_iou,
                )
            )
            true_positive += 1
        else:
            false_positive += 1

    false_negative = len(ground_truth) - len(matched_gt)
    return MatchResult(
        tp=true_positive,
        fp=false_positive,
        fn=false_negative,
        matched_ious=matched_ious,
        matches=matches,
    )


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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_identity() -> dict[str, Any]:
    git = shutil.which("git")
    if not git:
        return {"revision": None, "dirty": None}
    try:
        revision = subprocess.run(
            [git, "rev-parse", "HEAD"],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=2.0,
            check=False,
        )
        status = subprocess.run(
            [git, "status", "--porcelain"],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=2.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return {"revision": None, "dirty": None}
    return {
        "revision": revision.stdout.strip() if revision.returncode == 0 else None,
        "dirty": bool(status.stdout.strip()) if status.returncode == 0 else None,
    }


def extract_engine_input_size(engine_info: dict[str, Any]) -> tuple[int, int]:
    for tensor in engine_info.get("tensors", []):
        shape = tensor.get("shape", [])
        if isinstance(shape, (list, tuple)) and len(shape) == 4:
            height = int(shape[-2])
            width = int(shape[-1])
            if width > 0 and height > 0:
                return width, height
    raise ValueError("engine inspection did not expose a positive NCHW input shape")


class ProcessResourceSampler:
    def __init__(self) -> None:
        self._process: Any | None = None
        try:
            import psutil

            self._process = psutil.Process(os.getpid())
        except (ImportError, OSError):
            self._process = None

    def rss_mb(self) -> float | None:
        if self._process is None:
            return None
        try:
            return float(self._process.memory_info().rss) / (1024.0 * 1024.0)
        except (OSError, RuntimeError):
            return None


class FrameRecordWriter:
    def __init__(self, path: Path, candidate_id: str) -> None:
        self.path = path
        self.candidate_id = candidate_id
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.handle = self.path.open("w", encoding="utf-8")

    def close(self) -> None:
        self.handle.close()

    def write(self, record: dict[str, Any]) -> None:
        output = {
            "schema_version": 1,
            "candidate_id": self.candidate_id,
            **record,
        }
        self.handle.write(json.dumps(output, ensure_ascii=False) + "\n")


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
    tensor_width: int,
    tensor_height: int,
    strict_crop_size: bool,
    frame_budgets_ms: Sequence[float],
    failure_writer: "FailureWriter | None" = None,
    frame_writer: "FrameRecordWriter | None" = None,
    process_sampler: "ProcessResourceSampler | None" = None,
) -> DatasetSummary:
    summary = DatasetSummary(
        dataset=dataset.name,
        root=str(dataset.root),
        split=dataset.split,
        frame_budgets_ms=tuple(frame_budgets_ms),
    )
    image_paths = list(iter_image_files(dataset.image_dir))
    if max_images is not None and max_images > 0:
        image_paths = image_paths[:max_images]
    warmed = False

    for image_path in image_paths:
        rgb = load_rgb_array(image_path)
        source_height, source_width = int(rgb.shape[0]), int(rgb.shape[1])
        sample_key = f"{dataset.name}/{dataset.split}/{image_path.relative_to(dataset.image_dir).as_posix()}"
        if strict_crop_size and not crop_fits_source(
            source_width,
            source_height,
            crop_width,
            crop_height,
        ):
            summary.skipped_undersized += 1
            if frame_writer is not None:
                frame_writer.write(
                    {
                        "sample_key": sample_key,
                        "dataset": dataset.name,
                        "split": dataset.split,
                        "image": str(image_path),
                        "status": "skipped",
                        "skip_reason": "source_smaller_than_requested_crop",
                        "source_width": source_width,
                        "source_height": source_height,
                        "requested_crop_width": crop_width,
                        "requested_crop_height": crop_height,
                    }
                )
            continue
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
        cropped_targets = crop_targets_to_window(labels, crop)
        cropped_labels = [target.cropped for target in cropped_targets]

        if not warmed:
            run_warmup(engine, cropped_rgb, conf, warmup)
            warmed = True

        wall_start = time.perf_counter()
        process_start = time.process_time()
        raw_result = engine.infer_rgb(cropped_rgb, conf)
        process_cpu_ms = (time.process_time() - process_start) * 1000.0
        wall_ms = (time.perf_counter() - wall_start) * 1000.0
        detections = [detection_from_native(raw) for raw in raw_result.get("detections", [])]
        match = match_detections(
            cropped_labels,
            detections,
            iou_threshold=iou_threshold,
            class_aware=class_aware,
        )
        matched_by_gt = {pair.ground_truth_index: pair.iou for pair in match.matches}
        target_records = [
            build_target_record(
                target,
                crop_width=crop.width,
                crop_height=crop.height,
                tensor_width=tensor_width,
                tensor_height=tensor_height,
                matched=index in matched_by_gt,
                matched_iou=matched_by_gt.get(index),
            )
            for index, target in enumerate(cropped_targets)
        ]
        timings = {
            "wall_ms": wall_ms,
            "process_cpu_ms": process_cpu_ms,
            "process_rss_mb": process_sampler.rss_mb() if process_sampler is not None else None,
            "preprocess_ms": raw_result.get("preprocess_ms", 0.0),
            "infer_ms": raw_result.get("infer_ms", 0.0),
            "enqueue_cpu_ms": raw_result.get("enqueue_cpu_ms", 0.0),
            "gpu_total_ms": raw_result.get("gpu_total_ms", 0.0),
            "output_wait_ms": raw_result.get("output_wait_ms", 0.0),
            "decode_ms": raw_result.get("decode_ms", 0.0),
        }
        summary.add(
            label_count=len(cropped_labels),
            detection_count=len(detections),
            match=match,
            timings=timings,
            target_records=target_records,
        )
        if frame_writer is not None:
            frame_writer.write(
                {
                    "sample_key": sample_key,
                    "dataset": dataset.name,
                    "split": dataset.split,
                    "image": str(image_path),
                    "status": "evaluated",
                    "source_width": source_width,
                    "source_height": source_height,
                    "crop": asdict(crop),
                    "tensor_width": tensor_width,
                    "tensor_height": tensor_height,
                    "original_labels": [asdict(box) for box in labels],
                    "targets": target_records,
                    "detections": [asdict(box) for box in detections],
                    "match": {
                        "tp": match.tp,
                        "fp": match.fp,
                        "fn": match.fn,
                        "pairs": [asdict(pair) for pair in match.matches],
                    },
                    "timings": timings,
                }
            )
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
    budgets = summaries[0].frame_budgets_ms if summaries else (4.167, 8.333, 16.667)
    overall = DatasetSummary(dataset="OVERALL", root="", split="", frame_budgets_ms=budgets)
    for summary in summaries:
        overall.images += summary.images
        overall.labels += summary.labels
        overall.detections += summary.detections
        overall.tp += summary.tp
        overall.fp += summary.fp
        overall.fn += summary.fn
        overall.failures += summary.failures
        overall.skipped_undersized += summary.skipped_undersized
        overall.infer_ms.extend(summary.infer_ms)
        overall.enqueue_cpu_ms.extend(summary.enqueue_cpu_ms)
        overall.gpu_total_ms.extend(summary.gpu_total_ms)
        overall.wall_ms.extend(summary.wall_ms)
        overall.process_cpu_ms.extend(summary.process_cpu_ms)
        overall.process_rss_mb.extend(summary.process_rss_mb)
        overall.preprocess_ms.extend(summary.preprocess_ms)
        overall.output_wait_ms.extend(summary.output_wait_ms)
        overall.decode_ms.extend(summary.decode_ms)
        for name, counts in summary.size_buckets.items():
            target = overall.size_buckets.setdefault(name, {"targets": 0, "tp": 0, "fn": 0})
            for key in ("targets", "tp", "fn"):
                target[key] += counts[key]
        for name, counts in summary.visibility_buckets.items():
            target = overall.visibility_buckets.setdefault(name, {"targets": 0, "tp": 0, "fn": 0})
            for key in ("targets", "tp", "fn"):
                target[key] += counts[key]
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
        f"{'dataset':34} {'split':7} {'img':>5} {'skip':>5} {'gt':>6} {'det':>6} "
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
            f"{summary.images:5d} {summary.skipped_undersized:5d} "
            f"{summary.labels:6d} {summary.detections:6d} "
            f"{metrics['precision']:6.3f} {metrics['recall']:6.3f} {metrics['f1']:6.3f} "
            f"{infer['p50']:7.3f}/{infer['p95']:<7.3f} "
            f"{gpu['p50']:7.3f}/{gpu['p95']:<7.3f}"
        )


def write_json(
    path: Path,
    summaries: Sequence[DatasetSummary],
    args: argparse.Namespace,
    *,
    engine_info: dict[str, Any],
    model_sha256: str,
    gpu_resource: dict[str, Any] | None = None,
) -> None:
    overall = aggregate_summaries(summaries).metrics()
    output = {
        "schema_version": 2,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "candidate_id": args.candidate_id,
        "provenance": {
            "engine": {
                "path": str(resolve_project_path(args.model)),
                "size_bytes": resolve_project_path(args.model).stat().st_size,
                "sha256": model_sha256,
                "inspection": engine_info,
            },
            "benchmark": git_identity(),
        },
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
            "strict_crop_size": args.strict_crop_size,
            "tensor_address_binding": args.tensor_address_binding,
            "stream_priority": args.stream_priority,
            "cuda_graph": args.cuda_graph,
            "warmup": args.warmup,
            "frame_budgets_ms": args.frame_budget_ms,
            "frame_jsonl": str(resolve_project_path(args.frame_jsonl)) if args.frame_jsonl else None,
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
    parser.add_argument(
        "--strict-crop-size",
        action="store_true",
        help="Skip and record images smaller than the requested crop instead of silently clamping.",
    )
    parser.add_argument(
        "--tensor-address-binding",
        choices=("once", "per-inference"),
        default="once",
        help="Bind fixed TensorRT tensor addresses once or repeat the calls for every inference.",
    )
    parser.add_argument(
        "--stream-priority",
        choices=("high", "default"),
        default="high",
        help="Use the production high-priority non-blocking CUDA stream or the legacy default stream.",
    )
    parser.add_argument(
        "--cuda-graph",
        choices=("on", "off"),
        default="on",
        help="Enable or disable the fixed-shape TensorRT CUDA Graph replay path.",
    )
    parser.add_argument(
        "--candidate-id",
        default="default",
        help="Stable candidate name written to artifacts, e.g. fixed480 or wide320.",
    )
    parser.add_argument(
        "--frame-jsonl",
        type=Path,
        default=None,
        help="Optional per-frame JSONL evidence including targets, matches, timings, and skipped images.",
    )
    parser.add_argument(
        "--frame-budget-ms",
        type=float,
        nargs="+",
        default=[4.167, 8.333, 16.667],
        help="Wall-time budgets used to report deadline miss counts and rates.",
    )
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
    if not args.candidate_id.strip():
        parser.error("--candidate-id must not be empty")
        return 2
    if any(not math.isfinite(value) or value <= 0.0 for value in args.frame_budget_ms):
        parser.error("--frame-budget-ms values must be positive finite numbers")
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
    try:
        tensor_width, tensor_height = extract_engine_input_size(engine_info)
    except ValueError as exc:
        print(f"Invalid engine input contract: {exc}", file=sys.stderr)
        return 2
    model_sha256 = sha256_file(model_path)
    print(
        f"model={model_path} input={engine_info.get('tensors', [{}])[0].get('shape', '?')} "
        f"conf={args.conf} iou={args.iou} crop={args.crop_width or '-'}x{args.crop_height or '-'} "
        f"binding={args.tensor_address_binding} priority={args.stream_priority} graph={args.cuda_graph}"
    )
    if selected_classes:
        print(f"target_classes={','.join(selected_classes)} class_aware={args.class_aware}")
    engine = vision_native_cpp.NativeEngine(
        str(model_path),
        args.tensor_address_binding == "once",
        args.stream_priority == "high",
        args.cuda_graph == "on",
    )

    failure_writer = FailureWriter(resolve_project_path(args.save_failures), args.max_failures) if args.save_failures else None
    frame_writer = (
        FrameRecordWriter(resolve_project_path(args.frame_jsonl), args.candidate_id)
        if args.frame_jsonl
        else None
    )
    process_sampler = ProcessResourceSampler()
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
                    tensor_width=tensor_width,
                    tensor_height=tensor_height,
                    strict_crop_size=args.strict_crop_size,
                    frame_budgets_ms=args.frame_budget_ms,
                    failure_writer=failure_writer,
                    frame_writer=frame_writer,
                    process_sampler=process_sampler,
                )
            )
    finally:
        if gpu_monitor is not None:
            gpu_resource = gpu_monitor.stop()
        if failure_writer is not None:
            failure_writer.close()
        if frame_writer is not None:
            frame_writer.close()

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
        write_json(
            output_json,
            summaries,
            args,
            engine_info=engine_info,
            model_sha256=model_sha256,
            gpu_resource=gpu_resource,
        )
        print(f"wrote {output_json}")
    if frame_writer is not None:
        print(f"wrote frame evidence to {frame_writer.path}")
    if failure_writer is not None:
        print(f"wrote failures to {failure_writer.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
