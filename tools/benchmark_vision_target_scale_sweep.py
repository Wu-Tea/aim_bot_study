from __future__ import annotations

import argparse
import json
import math
import time
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

import benchmark_vision_dataset as bench


DEFAULT_SCALES = (0.80, 1.00, 1.333333, 1.60, 2.00, 2.50, 3.00, 4.00)


def target_centered_crop(
    source_width: int,
    source_height: int,
    target: bench.Box,
    width: int,
    height: int,
) -> bench.CropWindow | None:
    if width > source_width or height > source_height:
        return None
    center_x = (target.x1 + target.x2) * 0.5
    center_y = (target.y1 + target.y2) * 0.5
    left = round(center_x - width * 0.5)
    top = round(center_y - height * 0.5)
    left = max(0, min(source_width - width, left))
    top = max(0, min(source_height - height, top))
    return bench.CropWindow(
        left=left,
        top=top,
        width=width,
        height=height,
        source_width=source_width,
        source_height=source_height,
    )


def scale_name(scale: float) -> str:
    return f"{scale:.6f}".rstrip("0").rstrip(".")


def tensor_height_bucket(height: float) -> str:
    if height < 96.0:
        return "lt_96"
    if height < 160.0:
        return "96_160"
    if height < 240.0:
        return "160_240"
    if height < 320.0:
        return "240_320"
    return "ge_320"


def summarize(records: Sequence[dict[str, Any]], scales: Sequence[float]) -> dict[str, Any]:
    output: dict[str, Any] = {
        "scales": {},
        "fully_paired_scales": {},
        "tensor_height_buckets": {},
        "adjacent_transitions": {},
    }
    names = [scale_name(scale) for scale in scales]
    fully_paired = [
        record for record in records
        if all(name in record["scales"] for name in names)
    ]
    bucket_outcomes: dict[str, list[dict[str, Any]]] = {}
    for name in names:
        outcomes = [
            record["scales"][name]
            for record in records
            if name in record["scales"]
        ]
        paired_outcomes = [record["scales"][name] for record in fully_paired]
        matched = sum(bool(value["matched"]) for value in outcomes)
        paired_matched = sum(bool(value["matched"]) for value in paired_outcomes)
        tensor_heights = [float(value["tensor_height"]) for value in outcomes]
        output["scales"][name] = {
            "targets": len(outcomes),
            "matched": matched,
            "missed": len(outcomes) - matched,
            "recall": matched / len(outcomes) if outcomes else 0.0,
            "target_tensor_height_p50": bench.percentile(tensor_heights, 0.50),
            "target_tensor_height_p95": bench.percentile(tensor_heights, 0.95),
        }
        output["fully_paired_scales"][name] = {
            "targets": len(paired_outcomes),
            "matched": paired_matched,
            "missed": len(paired_outcomes) - paired_matched,
            "recall": (
                paired_matched / len(paired_outcomes)
                if paired_outcomes else 0.0
            ),
        }
        for value in outcomes:
            bucket_outcomes.setdefault(
                tensor_height_bucket(float(value["tensor_height"])),
                [],
            ).append(value)
    for bucket, outcomes in sorted(bucket_outcomes.items()):
        matched = sum(bool(value["matched"]) for value in outcomes)
        output["tensor_height_buckets"][bucket] = {
            "targets": len(outcomes),
            "matched": matched,
            "missed": len(outcomes) - matched,
            "recall": matched / len(outcomes) if outcomes else 0.0,
        }
    for previous, current in zip(names, names[1:]):
        paired = [
            (record["scales"][previous], record["scales"][current])
            for record in records
            if previous in record["scales"] and current in record["scales"]
        ]
        output["adjacent_transitions"][f"{previous}->{current}"] = {
            "paired_targets": len(paired),
            "gained": sum(not left["matched"] and right["matched"] for left, right in paired),
            "lost": sum(left["matched"] and not right["matched"] for left, right in paired),
        }
    return output


def run(args: argparse.Namespace) -> dict[str, Any]:
    datasets = bench.discover_yolo_splits(
        [bench.resolve_project_path(path) for path in args.datasets],
        bench.normalize_splits(args.split),
    )
    if not datasets:
        raise RuntimeError("no YOLO dataset splits found")
    model_path = bench.resolve_project_path(args.model)
    native = bench.ensure_native_module()
    engine_info = native.inspect_engine(str(model_path))
    tensor_width, tensor_height = bench.extract_engine_input_size(engine_info)
    scales = sorted(set(float(value) for value in args.scales))
    if not scales or any(not math.isfinite(value) or value <= 0.0 for value in scales):
        raise ValueError("scales must be positive finite numbers")
    selected_classes = bench.parse_target_classes(args.target_classes)
    engine = native.NativeEngine(str(model_path))
    records: list[dict[str, Any]] = []
    warmed = False
    evaluated_images = 0

    for dataset in datasets:
        image_paths = sorted(bench.iter_image_files(dataset.image_dir))
        if args.max_images is not None and args.max_images > 0:
            image_paths = image_paths[: args.max_images]
        for image_path in image_paths:
            rgb = bench.load_rgb_array(image_path)
            source_height, source_width = int(rgb.shape[0]), int(rgb.shape[1])
            labels = bench.parse_yolo_label_file(
                bench.label_path_for_image(image_path, dataset.label_dir),
                source_width,
                source_height,
                class_names=dataset.class_names,
                selected_classes=selected_classes,
            )
            sample_key = (
                f"{dataset.name}/{dataset.split}/"
                f"{image_path.relative_to(dataset.image_dir).as_posix()}"
            )
            for target_index, label in enumerate(labels):
                scale_results: dict[str, Any] = {}
                for scale in scales:
                    crop_width = max(32, round(args.base_width / scale))
                    crop_height = max(32, round(args.base_height / scale))
                    crop = target_centered_crop(
                        source_width,
                        source_height,
                        label,
                        crop_width,
                        crop_height,
                    )
                    if crop is None:
                        continue
                    cropped_targets = bench.crop_targets_to_window([label], crop)
                    if not cropped_targets or cropped_targets[0].visible_fraction < 0.999:
                        continue
                    cropped_rgb = bench.crop_rgb_array(rgb, crop)
                    if not warmed:
                        bench.run_warmup(engine, cropped_rgb, args.conf, args.warmup)
                        warmed = True
                    raw = engine.infer_rgb(cropped_rgb, args.conf)
                    detections = [
                        bench.detection_from_native(value)
                        for value in raw.get("detections", [])
                    ]
                    match = bench.match_detections(
                        [cropped_targets[0].cropped],
                        detections,
                        iou_threshold=args.iou,
                        class_aware=args.class_aware,
                    )
                    matched_iou = match.matches[0].iou if match.matches else None
                    confidence = (
                        detections[match.matches[0].detection_index].conf
                        if match.matches else None
                    )
                    value = bench.build_target_record(
                        cropped_targets[0],
                        crop_width=crop.width,
                        crop_height=crop.height,
                        tensor_width=tensor_width,
                        tensor_height=tensor_height,
                        matched=bool(match.matches),
                        matched_iou=matched_iou,
                    )
                    value["confidence"] = confidence
                    value["crop"] = asdict(crop)
                    scale_results[scale_name(scale)] = value
                if scale_results:
                    records.append(
                        {
                            "sample_key": sample_key,
                            "target_index": target_index,
                            "original": asdict(label),
                            "scales": scale_results,
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
            "target_classes": args.target_classes,
            "conf": args.conf,
            "iou": args.iou,
            "base_width": args.base_width,
            "base_height": args.base_height,
            "scales": scales,
            "tensor_width": tensor_width,
            "tensor_height": tensor_height,
        },
        "evaluated_images": evaluated_images,
        "target_records": records,
        "metrics": summarize(records, scales),
    }


def print_summary(result: dict[str, Any]) -> None:
    print(
        f"images={result['evaluated_images']} "
        f"paired_targets={len(result['target_records'])}"
    )
    print("scale      targets  matched  recall  target_h_p50  target_h_p95")
    for name, value in result["metrics"]["scales"].items():
        print(
            f"{name:10} {value['targets']:7d} {value['matched']:8d} "
            f"{value['recall']:7.3f} {value['target_tensor_height_p50']:13.1f} "
            f"{value['target_tensor_height_p95']:13.1f}"
        )
    for transition, value in result["metrics"]["adjacent_transitions"].items():
        print(
            f"{transition}: paired={value['paired_targets']} "
            f"gained={value['gained']} lost={value['lost']}"
        )
    print("fully paired recall:")
    for name, value in result["metrics"]["fully_paired_scales"].items():
        print(
            f"  scale={name} targets={value['targets']} "
            f"matched={value['matched']} recall={value['recall']:.3f}"
        )
    print("tensor target-height buckets:")
    for name, value in result["metrics"]["tensor_height_buckets"].items():
        print(
            f"  {name}: targets={value['targets']} "
            f"matched={value['matched']} recall={value['recall']:.3f}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Scale the same target-centered scene through a fixed TensorRT "
            "engine to find the detector's near-distance size limit."
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
    parser.add_argument("--max-images", type=int, default=100)
    parser.add_argument("--target-classes", default=None)
    parser.add_argument("--conf", type=float, default=0.25)
    parser.add_argument("--iou", type=float, default=0.50)
    parser.add_argument("--class-aware", action="store_true")
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--base-width", type=int, default=480)
    parser.add_argument("--base-height", type=int, default=416)
    parser.add_argument("--scales", type=float, nargs="+", default=list(DEFAULT_SCALES))
    parser.add_argument("--candidate-id", default="target-centered-scale-sweep")
    parser.add_argument("--output-json", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.base_width <= 0 or args.base_height <= 0:
        parser.error("base dimensions must be positive")
    started = time.perf_counter()
    try:
        result = run(args)
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
