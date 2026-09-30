from __future__ import annotations

import argparse
import json
import math
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence

import benchmark_vision_dataset as bench


DEFAULT_VIEWPORTS = ("precision=360x312", "normal=480x416", "rescue=600x520")


@dataclass(frozen=True)
class ViewportSpec:
    name: str
    width: int
    height: int


def parse_viewport_spec(value: str) -> ViewportSpec:
    try:
        name, dimensions = value.split("=", 1)
        width_text, height_text = dimensions.lower().split("x", 1)
        width = int(width_text)
        height = int(height_text)
    except (TypeError, ValueError) as exc:
        raise ValueError(
            f"invalid viewport {value!r}; expected NAME=WIDTHxHEIGHT"
        ) from exc
    name = name.strip()
    if not name or width <= 0 or height <= 0:
        raise ValueError(f"invalid viewport {value!r}; dimensions must be positive")
    return ViewportSpec(name=name, width=width, height=height)


def validate_viewports(
    viewports: Sequence[ViewportSpec],
    tensor_width: int,
    tensor_height: int,
) -> None:
    if len(viewports) < 2:
        raise ValueError("viewport sweep requires at least two viewports")
    names = [viewport.name for viewport in viewports]
    if len(set(names)) != len(names):
        raise ValueError("viewport names must be unique")
    previous_area = 0
    for viewport in viewports:
        if viewport.width * tensor_height != viewport.height * tensor_width:
            raise ValueError(
                f"viewport {viewport.name}={viewport.width}x{viewport.height} "
                f"does not match tensor aspect {tensor_width}x{tensor_height}"
            )
        area = viewport.width * viewport.height
        if area <= previous_area:
            raise ValueError("viewports must be ordered from smallest to largest")
        previous_area = area


def target_viewport_result(
    target: bench.CroppedTarget | None,
    *,
    viewport: ViewportSpec,
    tensor_width: int,
    tensor_height: int,
    match_iou: float | None,
    confidence: float | None,
) -> dict[str, Any]:
    if target is None:
        return {
            "visible_fraction": 0.0,
            "visibility_bucket": "outside",
            "tensor_width": 0.0,
            "tensor_height": 0.0,
            "tensor_area": 0.0,
            "size_bucket": "outside",
            "matched": False,
            "matched_iou": None,
            "confidence": None,
        }
    record = bench.build_target_record(
        target,
        crop_width=viewport.width,
        crop_height=viewport.height,
        tensor_width=tensor_width,
        tensor_height=tensor_height,
        matched=match_iou is not None,
        matched_iou=match_iou,
    )
    record["confidence"] = confidence
    return record


def classify_cohort(viewport_results: dict[str, dict[str, Any]], names: Sequence[str]) -> str:
    visibility = [
        float(viewport_results[name]["visible_fraction"])
        for name in names
    ]
    if all(value >= 0.999 for value in visibility):
        return "fully_visible_all"
    if visibility[-1] > 0.0 and visibility[0] < 0.999:
        return "precision_clipped"
    return "other"


def _new_counts() -> dict[str, int]:
    return {"targets": 0, "matched": 0, "missed": 0}


def summarize_records(
    records: Sequence[dict[str, Any]],
    viewports: Sequence[ViewportSpec],
) -> dict[str, Any]:
    names = [viewport.name for viewport in viewports]
    per_cohort: dict[str, dict[str, dict[str, int]]] = {}
    for cohort in ("all_rescue_visible", "fully_visible_all", "precision_clipped"):
        per_cohort[cohort] = {name: _new_counts() for name in names}

    recoveries: dict[str, dict[str, int]] = {}
    smallest = names[0]
    for larger in names[1:]:
        recoveries[larger] = {
            "all_recovered": 0,
            "all_regressed": 0,
            "fully_visible_recovered": 0,
            "fully_visible_regressed": 0,
        }

    for record in records:
        results = record["viewports"]
        cohorts = ["all_rescue_visible"]
        if record["cohort"] in per_cohort:
            cohorts.append(record["cohort"])
        for cohort in cohorts:
            for name in names:
                counts = per_cohort[cohort][name]
                counts["targets"] += 1
                key = "matched" if results[name]["matched"] else "missed"
                counts[key] += 1
        for larger in names[1:]:
            small_hit = bool(results[smallest]["matched"])
            large_hit = bool(results[larger]["matched"])
            if not small_hit and large_hit:
                recoveries[larger]["all_recovered"] += 1
                if record["cohort"] == "fully_visible_all":
                    recoveries[larger]["fully_visible_recovered"] += 1
            if small_hit and not large_hit:
                recoveries[larger]["all_regressed"] += 1
                if record["cohort"] == "fully_visible_all":
                    recoveries[larger]["fully_visible_regressed"] += 1

    metrics: dict[str, Any] = {"cohorts": {}, "transitions_from_smallest": recoveries}
    for cohort, viewport_counts in per_cohort.items():
        metrics["cohorts"][cohort] = {}
        for name, counts in viewport_counts.items():
            metrics["cohorts"][cohort][name] = {
                **counts,
                "recall": (
                    counts["matched"] / counts["targets"]
                    if counts["targets"] else 0.0
                ),
            }
    return metrics


def matched_target_maps(
    targets: Sequence[bench.CroppedTarget],
    detections: Sequence[bench.Box],
    match: bench.MatchResult,
) -> tuple[dict[int, float], dict[int, float]]:
    ious: dict[int, float] = {}
    confidences: dict[int, float] = {}
    for pair in match.matches:
        target_index = targets[pair.ground_truth_index].target_index
        ious[target_index] = pair.iou
        confidences[target_index] = detections[pair.detection_index].conf
    return ious, confidences


def iter_limited(paths: Iterable[Path], max_images: int | None) -> Iterable[Path]:
    for index, path in enumerate(paths):
        if max_images is not None and max_images > 0 and index >= max_images:
            break
        yield path


def run_sweep(args: argparse.Namespace) -> dict[str, Any]:
    splits = bench.normalize_splits(args.split)
    datasets = bench.discover_yolo_splits(
        [bench.resolve_project_path(path) for path in args.datasets],
        splits,
    )
    if not datasets:
        raise RuntimeError("no YOLO dataset splits found")

    model_path = bench.resolve_project_path(args.model)
    vision_native_cpp = bench.ensure_native_module()
    engine_info = vision_native_cpp.inspect_engine(str(model_path))
    tensor_width, tensor_height = bench.extract_engine_input_size(engine_info)
    viewports = [parse_viewport_spec(value) for value in args.viewports]
    validate_viewports(viewports, tensor_width, tensor_height)
    selected_classes = bench.parse_target_classes(args.target_classes)
    engine = vision_native_cpp.NativeEngine(str(model_path))

    records: list[dict[str, Any]] = []
    skipped_undersized = 0
    evaluated_images = 0
    warmed = False
    largest = viewports[-1]

    for dataset in datasets:
        image_paths = sorted(bench.iter_image_files(dataset.image_dir))
        for image_path in iter_limited(image_paths, args.max_images):
            rgb = bench.load_rgb_array(image_path)
            source_height, source_width = int(rgb.shape[0]), int(rgb.shape[1])
            if source_width < largest.width or source_height < largest.height:
                skipped_undersized += 1
                continue
            labels = bench.parse_yolo_label_file(
                bench.label_path_for_image(image_path, dataset.label_dir),
                source_width,
                source_height,
                class_names=dataset.class_names,
                selected_classes=selected_classes,
            )
            if not labels:
                continue
            sample_key = (
                f"{dataset.name}/{dataset.split}/"
                f"{image_path.relative_to(dataset.image_dir).as_posix()}"
            )
            per_viewport: dict[str, dict[int, dict[str, Any]]] = {}
            for viewport in viewports:
                crop = bench.center_crop_window(
                    source_width,
                    source_height,
                    viewport.width,
                    viewport.height,
                )
                cropped_rgb = bench.crop_rgb_array(rgb, crop)
                targets = bench.crop_targets_to_window(labels, crop)
                target_boxes = [target.cropped for target in targets]
                if not warmed:
                    bench.run_warmup(engine, cropped_rgb, args.conf, args.warmup)
                    warmed = True
                raw = engine.infer_rgb(cropped_rgb, args.conf)
                detections = [
                    bench.detection_from_native(value)
                    for value in raw.get("detections", [])
                ]
                match = bench.match_detections(
                    target_boxes,
                    detections,
                    iou_threshold=args.iou,
                    class_aware=args.class_aware,
                )
                ious, confidences = matched_target_maps(targets, detections, match)
                target_by_index = {target.target_index: target for target in targets}
                per_viewport[viewport.name] = {
                    target_index: target_viewport_result(
                        target_by_index.get(target_index),
                        viewport=viewport,
                        tensor_width=tensor_width,
                        tensor_height=tensor_height,
                        match_iou=ious.get(target_index),
                        confidence=confidences.get(target_index),
                    )
                    for target_index in range(len(labels))
                }

            for target_index, label in enumerate(labels):
                viewport_results = {
                    viewport.name: per_viewport[viewport.name][target_index]
                    for viewport in viewports
                }
                if viewport_results[largest.name]["visible_fraction"] <= 0.0:
                    continue
                records.append(
                    {
                        "sample_key": sample_key,
                        "target_index": target_index,
                        "original": asdict(label),
                        "cohort": classify_cohort(
                            viewport_results,
                            [viewport.name for viewport in viewports],
                        ),
                        "viewports": viewport_results,
                    }
                )
            evaluated_images += 1

    return {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "candidate_id": args.candidate_id,
        "provenance": {
            "engine": {
                "path": str(model_path),
                "sha256": bench.sha256_file(model_path),
                "inspection": engine_info,
            },
            "benchmark": bench.git_identity(),
            "tool": {
                "path": str(Path(__file__).resolve()),
                "sha256": bench.sha256_file(Path(__file__).resolve()),
            },
        },
        "parameters": {
            "datasets": [str(bench.resolve_project_path(path)) for path in args.datasets],
            "split": list(args.split),
            "max_images_per_dataset": args.max_images,
            "conf": args.conf,
            "iou": args.iou,
            "target_classes": args.target_classes,
            "class_aware": args.class_aware,
            "viewports": [asdict(viewport) for viewport in viewports],
            "tensor_width": tensor_width,
            "tensor_height": tensor_height,
        },
        "evaluated_images": evaluated_images,
        "skipped_undersized": skipped_undersized,
        "target_records": records,
        "metrics": summarize_records(records, viewports),
    }


def print_summary(result: dict[str, Any]) -> None:
    viewports = [ViewportSpec(**value) for value in result["parameters"]["viewports"]]
    metrics = result["metrics"]
    print(
        f"images={result['evaluated_images']} targets={len(result['target_records'])} "
        f"skipped_undersized={result['skipped_undersized']}"
    )
    print("cohort                 viewport       targets  matched  recall")
    for cohort, viewport_metrics in metrics["cohorts"].items():
        for viewport in viewports:
            value = viewport_metrics[viewport.name]
            print(
                f"{cohort:22} {viewport.name:13} "
                f"{value['targets']:7d} {value['matched']:8d} {value['recall']:7.3f}"
            )
    smallest = viewports[0].name
    for viewport in viewports[1:]:
        transition = metrics["transitions_from_smallest"][viewport.name]
        print(
            f"{smallest}->{viewport.name}: "
            f"recovered={transition['all_recovered']} "
            f"regressed={transition['all_regressed']} "
            f"pure_scale_recovered={transition['fully_visible_recovered']} "
            f"pure_scale_regressed={transition['fully_visible_regressed']}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run the same labeled targets through multiple centered viewports "
            "and report paired scale recovery/regression."
        )
    )
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument(
        "--datasets",
        type=Path,
        nargs="+",
        default=list(bench.DEFAULT_DATASETS),
    )
    parser.add_argument("--split", nargs="+", default=["valid"])
    parser.add_argument("--max-images", type=int, default=None)
    parser.add_argument("--conf", type=float, default=0.25)
    parser.add_argument("--iou", type=float, default=0.50)
    parser.add_argument("--target-classes", default=None)
    parser.add_argument("--class-aware", action="store_true")
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--candidate-id", default="dynamic-viewport-scale-sweep")
    parser.add_argument("--viewports", nargs="+", default=list(DEFAULT_VIEWPORTS))
    parser.add_argument("--output-json", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not math.isfinite(args.conf) or args.conf < 0.0:
        parser.error("--conf must be a non-negative finite number")
    if not math.isfinite(args.iou) or args.iou <= 0.0 or args.iou > 1.0:
        parser.error("--iou must be in (0, 1]")
    started = time.perf_counter()
    try:
        result = run_sweep(args)
    except (RuntimeError, ValueError) as exc:
        parser.error(str(exc))
        return 2
    output_path = bench.resolve_project_path(args.output_json)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(result, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print_summary(result)
    print(f"wrote={output_path} elapsed_seconds={time.perf_counter() - started:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
