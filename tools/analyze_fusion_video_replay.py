from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence

import cv2
import numpy as np


PROJECT_ROOT = Path(__file__).resolve().parent.parent
NATIVE_MODULE_DIR = PROJECT_ROOT / "native" / "vision_native" / "build" / "Release"
DEFAULT_MODEL = PROJECT_ROOT / "models" / "best_480x384.engine"
DEFAULT_CROP_WIDTH = 640
DEFAULT_CROP_HEIGHT = 512
DEFAULT_DECODE_CONFIDENCE = 0.40
DEFAULT_PICKUP_RADIUS_PX = 150.0
REPLAY_TIME_ORIGIN_NS = 1_000_000_000


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def git_state() -> dict[str, Any]:
    try:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=3,
            check=False,
        )
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=3,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return {"revision": None, "dirty": None}
    return {
        "revision": revision.stdout.strip() if revision.returncode == 0 else None,
        "dirty": bool(status.stdout.strip()) if status.returncode == 0 else None,
    }


def load_native_module() -> Any:
    if not NATIVE_MODULE_DIR.exists():
        raise RuntimeError(f"native build output is missing: {NATIVE_MODULE_DIR}")
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(NATIVE_MODULE_DIR))
    sys.path.insert(0, str(NATIVE_MODULE_DIR))
    try:
        import vision_native_cpp  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            f"failed to import vision_native_cpp from {NATIVE_MODULE_DIR}"
        ) from exc
    finally:
        try:
            sys.path.remove(str(NATIVE_MODULE_DIR))
        except ValueError:
            pass
    return vision_native_cpp


def center_crop_bounds(
    source_width: int,
    source_height: int,
    crop_width: int,
    crop_height: int,
) -> tuple[int, int, int, int]:
    if crop_width <= 0 or crop_height <= 0:
        raise ValueError("crop dimensions must be positive")
    if crop_width > source_width or crop_height > source_height:
        raise ValueError(
            f"crop {crop_width}x{crop_height} exceeds source "
            f"{source_width}x{source_height}"
        )
    left = (source_width - crop_width) // 2
    top = (source_height - crop_height) // 2
    return left, top, left + crop_width, top + crop_height


def parse_window(value: str) -> tuple[str, float, float]:
    try:
        label, bounds = value.split("=", 1)
        start_text, end_text = bounds.split(":", 1)
        start = float(start_text)
        end = float(end_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "window must use LABEL=START_SECONDS:END_SECONDS"
        ) from exc
    label = label.strip()
    if not label or start < 0.0 or end <= start:
        raise argparse.ArgumentTypeError("window label and bounds are invalid")
    return label, start, end


def detection_array(detections: Sequence[dict[str, Any]]) -> np.ndarray:
    rows = [
        [
            float(item.get("x1", 0.0)),
            float(item.get("y1", 0.0)),
            float(item.get("x2", 0.0)),
            float(item.get("y2", 0.0)),
            float(item.get("conf", 0.0)),
            float(item.get("class_id", 0)),
        ]
        for item in detections
    ]
    return np.asarray(rows, dtype=np.float32).reshape((-1, 6))


def true_runs(records: Sequence[dict[str, Any]], field: str) -> list[dict[str, Any]]:
    runs: list[dict[str, Any]] = []
    run_start: dict[str, Any] | None = None
    previous: dict[str, Any] | None = None
    for record in records:
        active = bool(record.get(field, False))
        consecutive = (
            previous is not None
            and int(record["frame_index"]) == int(previous["frame_index"]) + 1
        )
        if active and (run_start is None or not consecutive):
            if run_start is not None and previous is not None:
                runs.append(_run_record(run_start, previous))
            run_start = record
        elif not active and run_start is not None:
            if previous is not None:
                runs.append(_run_record(run_start, previous))
            run_start = None
        previous = record
    if run_start is not None and previous is not None:
        runs.append(_run_record(run_start, previous))
    return runs


def _run_record(first: dict[str, Any], last: dict[str, Any]) -> dict[str, Any]:
    frame_period_ms = float(first["frame_period_ms"])
    frame_count = int(last["frame_index"]) - int(first["frame_index"]) + 1
    return {
        "start_frame": int(first["frame_index"]),
        "end_frame": int(last["frame_index"]),
        "start_s": round(float(first["time_s"]), 6),
        "end_s": round(float(last["time_s"]), 6),
        "frame_count": frame_count,
        "duration_ms": round(frame_count * frame_period_ms, 3),
    }


def internal_false_runs(
    records: Sequence[dict[str, Any]],
    field: str,
) -> list[dict[str, Any]]:
    active_indices = [index for index, record in enumerate(records) if record.get(field)]
    if len(active_indices) < 2:
        return []
    first = active_indices[0]
    last = active_indices[-1]
    inverted = [
        {**record, "_gap": not bool(record.get(field, False))}
        for record in records[first : last + 1]
    ]
    return true_runs(inverted, "_gap")


def first_time(records: Sequence[dict[str, Any]], field: str) -> float | None:
    for record in records:
        if record.get(field):
            return round(float(record["time_s"]), 6)
    return None


def summarize_window(
    all_records: Sequence[dict[str, Any]],
    label: str,
    start_s: float,
    end_s: float,
    marker_field: str = "fusion_marker_current_policy",
) -> dict[str, Any]:
    records = [
        record
        for record in all_records
        if start_s <= float(record["time_s"]) < end_s
    ]
    if not records:
        return {
            "label": label,
            "start_s": start_s,
            "end_s": end_s,
            "frame_count": 0,
        }
    detector_runs = true_runs(records, "detector_positive")
    direct_runs = true_runs(records, marker_field)
    direct_gaps = internal_false_runs(records, marker_field)
    first_detector = first_time(records, "detector_positive")
    first_direct = first_time(records, marker_field)
    detector_to_direct_ms = None
    if first_detector is not None and first_direct is not None:
        detector_to_direct_ms = round((first_direct - first_detector) * 1000.0, 3)
    maximum_confidence = max(
        (float(record["max_detection_confidence"]) for record in records),
        default=0.0,
    )
    direct_count = sum(bool(record[marker_field]) for record in records)
    return {
        "label": label,
        "marker_field": marker_field,
        "start_s": start_s,
        "end_s": end_s,
        "frame_count": len(records),
        "detector_positive_frames": sum(bool(record["detector_positive"]) for record in records),
        "selector_target_frames": sum(bool(record["has_target"]) for record in records),
        "direct_observation_frames": direct_count,
        "direct_observation_ratio": round(direct_count / len(records), 6),
        "enemy_confirmed_frames": sum(
            bool(record["enemy_identity_confirmed"]) for record in records
        ),
        "first_detector_s": first_detector,
        "first_direct_observation_s": first_direct,
        "detector_to_direct_observation_ms": detector_to_direct_ms,
        "max_detection_confidence": round(maximum_confidence, 6),
        "detector_runs": detector_runs,
        "direct_observation_runs": direct_runs,
        "internal_direct_observation_gaps": direct_gaps,
        "max_internal_gap_ms": max(
            (float(gap["duration_ms"]) for gap in direct_gaps),
            default=0.0,
        ),
        "gaps_over_120ms": sum(
            float(gap["duration_ms"]) > 120.0 for gap in direct_gaps
        ),
    }


def draw_diagnostic_frame(
    roi_bgr: np.ndarray,
    record: dict[str, Any],
    marker_field: str = "fusion_marker_current_policy",
    policy_label: str = "current",
) -> np.ndarray:
    output = roi_bgr.copy()
    selected_index = int(record.get("selected_detection_index", -1))
    for index, detection in enumerate(record.get("detections", [])):
        x1 = int(round(float(detection.get("x1", 0.0))))
        y1 = int(round(float(detection.get("y1", 0.0))))
        x2 = int(round(float(detection.get("x2", 0.0))))
        y2 = int(round(float(detection.get("y2", 0.0))))
        color = (0, 220, 255) if index != selected_index else (255, 200, 0)
        cv2.rectangle(output, (x1, y1), (x2, y2), color, 1, cv2.LINE_AA)
        cv2.putText(
            output,
            f"{float(detection.get('conf', 0.0)):.2f}",
            (max(0, x1), max(14, y1 - 4)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            color,
            1,
            cv2.LINE_AA,
        )

    if record.get(marker_field):
        body_x1 = float(record["body_box"][0])
        body_y1 = float(record["body_box"][1])
        body_x2 = float(record["body_box"][2])
        marker_x = int(round((body_x1 + body_x2) * 0.5))
        marker_y = int(round(body_y1 - 14.0))
        cv2.circle(output, (marker_x, marker_y), 11, (0, 0, 0), -1, cv2.LINE_AA)
        cv2.circle(output, (marker_x, marker_y), 9, (255, 255, 255), -1, cv2.LINE_AA)
        cv2.circle(output, (marker_x, marker_y), 6, (80, 255, 80), -1, cv2.LINE_AA)

    status = "VISIBLE" if record.get(marker_field) else "HIDDEN"
    if record.get("has_target") and status == "HIDDEN":
        status = "HELD/HIDDEN"
    status_color = (80, 255, 80) if status == "DIRECT" else (80, 180, 255)
    label = (
        f"t={float(record['time_s']):06.3f}s  det={int(record['detection_count'])}  "
        f"fusion={status} ({policy_label})  source={record.get('target_source') or 'none'}"
    )
    cv2.rectangle(output, (0, output.shape[0] - 28), (output.shape[1], output.shape[0]), (0, 0, 0), -1)
    cv2.putText(
        output,
        label,
        (8, output.shape[0] - 9),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.46,
        status_color,
        1,
        cv2.LINE_AA,
    )
    return output


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Replay recorded gameplay through the production TensorRT engine, "
            "native selector, and current Fusion direct-observation display rule."
        )
    )
    parser.add_argument("video", type=Path)
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--crop-width", type=int, default=DEFAULT_CROP_WIDTH)
    parser.add_argument("--crop-height", type=int, default=DEFAULT_CROP_HEIGHT)
    parser.add_argument("--conf", type=float, default=DEFAULT_DECODE_CONFIDENCE)
    parser.add_argument("--pickup-radius-px", type=float, default=DEFAULT_PICKUP_RADIUS_PX)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--max-frames", type=int, default=None)
    parser.add_argument(
        "--window",
        action="append",
        type=parse_window,
        default=[],
        metavar="LABEL=START:END",
    )
    parser.add_argument(
        "--no-video",
        action="store_true",
        help="Do not write the diagnostic ROI video.",
    )
    return parser


def run(args: argparse.Namespace) -> dict[str, Any]:
    video_path = args.video.resolve()
    model_path = args.model.resolve()
    output_dir = args.output_dir.resolve()
    if not video_path.is_file():
        raise FileNotFoundError(video_path)
    if not model_path.is_file():
        raise FileNotFoundError(model_path)
    output_dir.mkdir(parents=True, exist_ok=True)

    native = load_native_module()
    native_module_path = Path(native.__file__).resolve()
    engine = native.NativeEngine(str(model_path))
    selector = native.NativeTargetSelector(
        args.crop_width,
        args.crop_height,
        args.pickup_radius_px,
    )

    capture = cv2.VideoCapture(str(video_path))
    if not capture.isOpened():
        raise RuntimeError(f"failed to open video: {video_path}")
    source_width = int(round(capture.get(cv2.CAP_PROP_FRAME_WIDTH)))
    source_height = int(round(capture.get(cv2.CAP_PROP_FRAME_HEIGHT)))
    source_fps = float(capture.get(cv2.CAP_PROP_FPS))
    declared_frame_count = int(round(capture.get(cv2.CAP_PROP_FRAME_COUNT)))
    if source_fps <= 0.0:
        raise RuntimeError("video FPS is unavailable")
    crop_left, crop_top, crop_right, crop_bottom = center_crop_bounds(
        source_width,
        source_height,
        args.crop_width,
        args.crop_height,
    )

    video_writer: cv2.VideoWriter | None = None
    candidate_video_writer: cv2.VideoWriter | None = None
    diagnostic_video_path = output_dir / "replay-current-policy-roi.mp4"
    candidate_video_path = output_dir / "replay-candidate-policy-roi.mp4"
    if not args.no_video:
        video_writer = cv2.VideoWriter(
            str(diagnostic_video_path),
            cv2.VideoWriter_fourcc(*"mp4v"),
            source_fps,
            (args.crop_width, args.crop_height),
        )
        if not video_writer.isOpened():
            raise RuntimeError(f"failed to create diagnostic video: {diagnostic_video_path}")
        candidate_video_writer = cv2.VideoWriter(
            str(candidate_video_path),
            cv2.VideoWriter_fourcc(*"mp4v"),
            source_fps,
            (args.crop_width, args.crop_height),
        )
        if not candidate_video_writer.isOpened():
            raise RuntimeError(f"failed to create candidate video: {candidate_video_path}")

    records_path = output_dir / "frame-records.jsonl"
    records: list[dict[str, Any]] = []
    replay_started = time.perf_counter()
    frame_index = 0
    warmed = False
    last_direct_time_s: float | None = None
    last_direct_generation = 0
    with records_path.open("w", encoding="utf-8") as records_file:
        while True:
            if args.max_frames is not None and frame_index >= args.max_frames:
                break
            ok, source_bgr = capture.read()
            if not ok:
                break
            roi_bgr = source_bgr[crop_top:crop_bottom, crop_left:crop_right]
            roi_rgb = cv2.cvtColor(roi_bgr, cv2.COLOR_BGR2RGB)
            if not warmed:
                for _ in range(max(0, args.warmup)):
                    engine.infer_rgb(roi_rgb, args.conf)
                warmed = True

            inference = engine.infer_rgb(roi_rgb, args.conf)
            raw_detections = list(inference.get("detections", []))
            time_s = frame_index / source_fps
            captured_at_ns = REPLAY_TIME_ORIGIN_NS + round(time_s * 1_000_000_000.0)
            selection = selector.select_xyxy_rgb_at(
                detection_array(raw_detections),
                roi_rgb,
                captured_at_ns,
                frame_index + 1,
            )
            has_selected_detection = bool(selection.get("has_selected_detection", False))
            has_body_box = bool(selection.get("has_body_box", False))
            direct_observation = (
                bool(selection.get("frame_updated", False))
                and has_selected_detection
                and has_body_box
            )
            fusion_marker = (
                bool(selection.get("has_target", False))
                and has_body_box
                and direct_observation
            )
            target_generation = int(selection.get("selector_target_generation", 0))
            if fusion_marker:
                last_direct_time_s = time_s
                last_direct_generation = target_generation
            confirmed_continuation = (
                last_direct_time_s is not None
                and target_generation == last_direct_generation
                and (time_s - last_direct_time_s) <= 0.120000001
                and bool(selection.get("has_target", False))
                and has_body_box
                and bool(selection.get("enemy_identity_confirmed", False))
            )
            candidate_marker = fusion_marker or confirmed_continuation
            body_box = [
                float(selection.get("body_x1", 0.0)),
                float(selection.get("body_y1", 0.0)),
                float(selection.get("body_x2", 0.0)),
                float(selection.get("body_y2", 0.0)),
            ]
            annotated_detections = list(selection.get("detections", raw_detections))
            record = {
                "schema_version": 1,
                "frame_index": frame_index,
                "frame_id": int(selection.get("frame_id", frame_index + 1)),
                "time_s": round(time_s, 9),
                "frame_period_ms": 1000.0 / source_fps,
                "detection_count": len(raw_detections),
                "detector_positive": bool(raw_detections),
                "max_detection_confidence": max(
                    (float(item.get("conf", 0.0)) for item in raw_detections),
                    default=0.0,
                ),
                "has_target": bool(selection.get("has_target", False)),
                "has_selected_detection": has_selected_detection,
                "selected_detection_index": (
                    int(selection.get("selected_detection_index", 0))
                    if has_selected_detection
                    else -1
                ),
                "has_body_box": has_body_box,
                "direct_observation": direct_observation,
                "fusion_marker_current_policy": fusion_marker,
                "fusion_marker_candidate_policy": candidate_marker,
                "enemy_cue_current": bool(selection.get("enemy_cue_current", False)),
                "enemy_identity_confirmed": bool(
                    selection.get("enemy_identity_confirmed", False)
                ),
                "selector_target_generation": target_generation,
                "selector_target_changed": bool(
                    selection.get("selector_target_changed", False)
                ),
                "target_source": str(selection.get("target_source", "")),
                "target_tier": str(selection.get("target_tier", "none")),
                "target_confidence": float(selection.get("target_confidence", 0.0)),
                "body_box": body_box,
                "detections": annotated_detections,
                "timing_ms": {
                    "preprocess": float(inference.get("preprocess_ms", 0.0)),
                    "infer": float(inference.get("infer_ms", 0.0)),
                    "decode": float(inference.get("decode_ms", 0.0)),
                },
            }
            records.append(record)
            records_file.write(json.dumps(record, ensure_ascii=False) + "\n")
            if video_writer is not None:
                video_writer.write(draw_diagnostic_frame(roi_bgr, record))
            if candidate_video_writer is not None:
                candidate_video_writer.write(
                    draw_diagnostic_frame(
                        roi_bgr,
                        record,
                        marker_field="fusion_marker_candidate_policy",
                        policy_label="candidate",
                    )
                )
            frame_index += 1

    capture.release()
    if video_writer is not None:
        video_writer.release()
    if candidate_video_writer is not None:
        candidate_video_writer.release()

    windows = list(args.window)
    clip_duration_s = frame_index / source_fps
    if not windows:
        windows = [("full_clip", 0.0, clip_duration_s)]
    elif not any(label == "full_clip" for label, _, _ in windows):
        windows.insert(0, ("full_clip", 0.0, clip_duration_s))
    summary = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "outcome": "INSUFFICIENT_EVIDENCE_FOR_LIVE_CAUSE",
        "provenance": {
            "video": str(video_path),
            "video_sha256": sha256_file(video_path),
            "model": str(model_path),
            "model_sha256": sha256_file(model_path),
            "native_module": str(native_module_path),
            "native_module_sha256": sha256_file(native_module_path),
            "git": git_state(),
        },
        "source": {
            "width": source_width,
            "height": source_height,
            "fps": source_fps,
            "declared_frame_count": declared_frame_count,
            "processed_frame_count": frame_index,
            "processed_duration_s": round(clip_duration_s, 6),
        },
        "replay_contract": {
            "crop": {
                "left": crop_left,
                "top": crop_top,
                "width": args.crop_width,
                "height": args.crop_height,
            },
            "engine_input_width": int(engine.input_width),
            "engine_input_height": int(engine.input_height),
            "decode_confidence_floor": args.conf,
            "pickup_base_radius_px": args.pickup_radius_px,
            "fusion_marker_rule": (
                "has_target && has_body_box && frame_updated && "
                "has_selected_detection"
            ),
            "candidate_marker_continuation": (
                "current direct observation, or same-generation selector-confirmed "
                "target continuation within 120 ms of the last direct observation"
            ),
        },
        "fidelity": {
            "measured": [
                "recorded 120 FPS game pixels",
                "current best_480x384 TensorRT engine",
                "current native VisionTargetSelector with source timestamps",
                "current Fusion direct-observation visibility predicate",
            ],
            "inferred": [
                "centered 640x512 ROI matches static production capture geometry",
            ],
            "unknown_or_unavailable": [
                "Fusion pixels are intentionally absent from the recording",
                "controller ADS/assist activation timeline",
                "live DXGI 200 FPS frames between recorded 120 FPS frames",
                "identity of the runtime binary used during recording",
                "live end-to-end publish and render timestamps",
                "host RGB replay is not the live DXGI BGRA CUDA preprocess path",
            ],
        },
        "windows": [
            summarize_window(records, label, start_s, end_s)
            for label, start_s, end_s in windows
        ],
        "candidate_windows": [
            summarize_window(
                records,
                label,
                start_s,
                end_s,
                marker_field="fusion_marker_candidate_policy",
            )
            for label, start_s, end_s in windows
        ],
        "artifacts": {
            "frame_records": str(records_path),
            "diagnostic_video": (
                str(diagnostic_video_path) if video_writer is not None else None
            ),
            "candidate_video": (
                str(candidate_video_path) if candidate_video_writer is not None else None
            ),
        },
        "replay_wall_time_s": round(time.perf_counter() - replay_started, 3),
    }
    summary_path = output_dir / "summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return summary


def main(argv: Iterable[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    try:
        summary = run(args)
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
