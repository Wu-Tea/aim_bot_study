from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Sequence

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.benchmark_vision_dataset import _safe_div, _timing_summary


TIMING_KEYS = (
    "preprocess_ms",
    "infer_ms",
    "gpu_total_ms",
    "output_wait_ms",
    "decode_ms",
    "wall_ms",
    "process_cpu_ms",
    "process_rss_mb",
)


def load_frame_records(path: Path) -> tuple[str, dict[str, dict[str, Any]], list[dict[str, Any]]]:
    evaluated: dict[str, dict[str, Any]] = {}
    skipped: list[dict[str, Any]] = []
    candidate_ids: set[str] = set()
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            record = json.loads(line)
            candidate_ids.add(str(record.get("candidate_id", "")))
            sample_key = str(record.get("sample_key", ""))
            if not sample_key:
                raise ValueError(f"{path}:{line_number}: missing sample_key")
            if record.get("status") == "evaluated":
                if sample_key in evaluated:
                    raise ValueError(f"{path}:{line_number}: duplicate sample_key {sample_key}")
                evaluated[sample_key] = record
            else:
                skipped.append(record)
    candidate_ids.discard("")
    if len(candidate_ids) != 1:
        raise ValueError(f"{path}: expected exactly one non-empty candidate_id, got {sorted(candidate_ids)}")
    return next(iter(candidate_ids)), evaluated, skipped


def _target_map(frame: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {int(target["target_index"]): target for target in frame.get("targets", [])}


def _paired_target_metrics(
    baseline_frames: dict[str, dict[str, Any]],
    candidate_frames: dict[str, dict[str, Any]],
    common_samples: Sequence[str],
) -> dict[str, Any]:
    common_count = 0
    common_baseline_tp = 0
    common_candidate_tp = 0
    baseline_only_count = 0
    baseline_only_tp = 0
    candidate_only_count = 0
    candidate_only_tp = 0
    by_baseline_size: dict[str, dict[str, int]] = {}
    by_candidate_size: dict[str, dict[str, int]] = {}
    by_baseline_visibility: dict[str, dict[str, int]] = {}
    by_candidate_visibility: dict[str, dict[str, int]] = {}

    for sample_key in common_samples:
        baseline_targets = _target_map(baseline_frames[sample_key])
        candidate_targets = _target_map(candidate_frames[sample_key])
        baseline_ids = set(baseline_targets)
        candidate_ids = set(candidate_targets)

        for target_id in baseline_ids & candidate_ids:
            common_count += 1
            baseline_target = baseline_targets[target_id]
            candidate_target = candidate_targets[target_id]
            baseline_matched = bool(baseline_target.get("matched"))
            candidate_matched = bool(candidate_target.get("matched"))
            common_baseline_tp += int(baseline_matched)
            common_candidate_tp += int(candidate_matched)
            _add_paired_bucket(
                by_baseline_size,
                str(baseline_target.get("size_bucket", "unknown")),
                baseline_matched,
                candidate_matched,
            )
            _add_paired_bucket(
                by_candidate_size,
                str(candidate_target.get("size_bucket", "unknown")),
                baseline_matched,
                candidate_matched,
            )
            _add_paired_bucket(
                by_baseline_visibility,
                str(baseline_target.get("visibility_bucket", "unknown")),
                baseline_matched,
                candidate_matched,
            )
            _add_paired_bucket(
                by_candidate_visibility,
                str(candidate_target.get("visibility_bucket", "unknown")),
                baseline_matched,
                candidate_matched,
            )
        for target_id in baseline_ids - candidate_ids:
            baseline_only_count += 1
            baseline_only_tp += int(bool(baseline_targets[target_id].get("matched")))
        for target_id in candidate_ids - baseline_ids:
            candidate_only_count += 1
            candidate_only_tp += int(bool(candidate_targets[target_id].get("matched")))

    return {
        "common_visible": {
            "targets": common_count,
            "baseline_tp": common_baseline_tp,
            "candidate_tp": common_candidate_tp,
            "baseline_recall": _safe_div(common_baseline_tp, common_count),
            "candidate_recall": _safe_div(common_candidate_tp, common_count),
            "recall_delta": _safe_div(common_candidate_tp, common_count)
            - _safe_div(common_baseline_tp, common_count),
        },
        "baseline_only_visible": {
            "targets": baseline_only_count,
            "baseline_tp": baseline_only_tp,
            "baseline_recall": _safe_div(baseline_only_tp, baseline_only_count),
        },
        "candidate_only_visible": {
            "targets": candidate_only_count,
            "candidate_tp": candidate_only_tp,
            "candidate_recall": _safe_div(candidate_only_tp, candidate_only_count),
        },
        "common_visible_by_baseline_size": _finalize_paired_buckets(by_baseline_size),
        "common_visible_by_candidate_size": _finalize_paired_buckets(by_candidate_size),
        "common_visible_by_baseline_visibility": _finalize_paired_buckets(by_baseline_visibility),
        "common_visible_by_candidate_visibility": _finalize_paired_buckets(by_candidate_visibility),
    }


def _add_paired_bucket(
    buckets: dict[str, dict[str, int]],
    name: str,
    baseline_matched: bool,
    candidate_matched: bool,
) -> None:
    bucket = buckets.setdefault(name, {"targets": 0, "baseline_tp": 0, "candidate_tp": 0})
    bucket["targets"] += 1
    bucket["baseline_tp"] += int(baseline_matched)
    bucket["candidate_tp"] += int(candidate_matched)


def _finalize_paired_buckets(buckets: dict[str, dict[str, int]]) -> dict[str, dict[str, float | int]]:
    return {
        name: {
            **counts,
            "baseline_recall": _safe_div(counts["baseline_tp"], counts["targets"]),
            "candidate_recall": _safe_div(counts["candidate_tp"], counts["targets"]),
            "recall_delta": _safe_div(counts["candidate_tp"] - counts["baseline_tp"], counts["targets"]),
        }
        for name, counts in sorted(buckets.items())
    }


def _timing_comparison(
    baseline_frames: dict[str, dict[str, Any]],
    candidate_frames: dict[str, dict[str, Any]],
    common_samples: Sequence[str],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key in TIMING_KEYS:
        baseline_values: list[float] = []
        candidate_values: list[float] = []
        for sample_key in common_samples:
            baseline_value = baseline_frames[sample_key].get("timings", {}).get(key)
            candidate_value = candidate_frames[sample_key].get("timings", {}).get(key)
            if not isinstance(baseline_value, (int, float)) or not isinstance(candidate_value, (int, float)):
                continue
            baseline_values.append(float(baseline_value))
            candidate_values.append(float(candidate_value))
        baseline_summary = _timing_summary(baseline_values)
        candidate_summary = _timing_summary(candidate_values)
        result[key] = {
            "baseline": baseline_summary,
            "candidate": candidate_summary,
            "p50_delta_percent": 100.0
            * (_safe_div(candidate_summary["p50"], baseline_summary["p50"]) - 1.0)
            if baseline_summary["p50"] > 0.0
            else 0.0,
            "p95_delta_percent": 100.0
            * (_safe_div(candidate_summary["p95"], baseline_summary["p95"]) - 1.0)
            if baseline_summary["p95"] > 0.0
            else 0.0,
        }
    return result


def compare_candidates(baseline_path: Path, candidate_path: Path) -> dict[str, Any]:
    baseline_id, baseline_frames, baseline_skipped = load_frame_records(baseline_path)
    candidate_id, candidate_frames, candidate_skipped = load_frame_records(candidate_path)
    common_samples = sorted(set(baseline_frames) & set(candidate_frames))
    return {
        "schema_version": 1,
        "baseline_id": baseline_id,
        "candidate_id": candidate_id,
        "baseline_frame_jsonl": str(baseline_path),
        "candidate_frame_jsonl": str(candidate_path),
        "samples": {
            "baseline_evaluated": len(baseline_frames),
            "candidate_evaluated": len(candidate_frames),
            "common_evaluated": len(common_samples),
            "baseline_skipped": len(baseline_skipped),
            "candidate_skipped": len(candidate_skipped),
            "baseline_only_evaluated": len(set(baseline_frames) - set(candidate_frames)),
            "candidate_only_evaluated": len(set(candidate_frames) - set(baseline_frames)),
        },
        "targets": _paired_target_metrics(baseline_frames, candidate_frames, common_samples),
        "timings": _timing_comparison(baseline_frames, candidate_frames, common_samples),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Pair and compare two vision benchmark frame JSONL artifacts.")
    parser.add_argument("--baseline", type=Path, required=True, help="Baseline per-frame JSONL artifact.")
    parser.add_argument("--candidate", type=Path, required=True, help="Candidate per-frame JSONL artifact.")
    parser.add_argument("--output-json", type=Path, default=None, help="Optional paired comparison JSON output.")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = compare_candidates(args.baseline, args.candidate)
    targets = result["targets"]
    common = targets["common_visible"]
    added = targets["candidate_only_visible"]
    print(
        f"{result['baseline_id']} -> {result['candidate_id']} "
        f"common_samples={result['samples']['common_evaluated']} "
        f"common_targets={common['targets']} "
        f"recall={common['baseline_recall']:.4f}->{common['candidate_recall']:.4f} "
        f"newly_visible={added['targets']} newly_visible_recall={added['candidate_recall']:.4f}"
    )
    for key in ("infer_ms", "gpu_total_ms", "wall_ms"):
        timing = result["timings"][key]
        print(
            f"{key} p50={timing['baseline']['p50']:.3f}->{timing['candidate']['p50']:.3f} "
            f"({timing['p50_delta_percent']:+.1f}%) "
            f"p95={timing['baseline']['p95']:.3f}->{timing['candidate']['p95']:.3f} "
            f"({timing['p95_delta_percent']:+.1f}%)"
        )
    if args.output_json is not None:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"wrote {args.output_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
