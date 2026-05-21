import argparse
import csv
import math
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


DEFAULT_TELEMETRY_DIR = Path("artifacts") / "mouse_telemetry"
OUTPUT_EPSILON = 0.05
INJECTED_ECHO_WINDOW_SECONDS = 0.020
MANUAL_OVERRIDE_TRIGGER_PX = 9.0


@dataclass(slots=True)
class MouseTelemetrySummary:
    path: Path
    rows: int
    duration_s: float
    aiming_rows: int
    target_rows: int
    target_revision_changes: int
    target_update_hz: float
    phase_counts: Counter
    injection_backend_counts: Counter
    snap_rows: int
    max_target_radius_px: float
    manual_left_pressed_rows: int
    manual_override_rows: int
    injected_echo_manual_rows: int
    reverse_injected_echo_rows: int
    nonzero_output_rows: int
    nonzero_move_rows: int
    output_without_move_rows: int
    repeated_revision_output_rows: int
    same_revision_move_rows: int
    max_same_revision_move_burst: float
    max_same_revision_move_burst_ms: float
    controller_loop_hz: float
    mouse_move_hz: float
    mouse_move_gap_p95_ms: float
    mouse_move_gap_max_ms: float
    target_age_p95_ms: float
    target_age_max_ms: float
    vision_handoff_p95_ms: float
    controller_consume_age_p95_ms: float
    physical_aim_sync_resets: int
    physical_aim_sync_presses: int
    injection_errors: int
    response_px_per_input_last: float
    response_input_scale_min: float
    response_input_scale_max: float
    diagnosis: list[str]


def _truthy(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _float(row: dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        return float(row.get(key, default) or default)
    except ValueError:
        return default


def _int(row: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(float(row.get(key, default) or default))
    except ValueError:
        return default


def _magnitude(row: dict[str, str], x_key: str, y_key: str) -> float:
    return math.hypot(_float(row, x_key), _float(row, y_key))


def _is_reverse_axis_echo(row: dict[str, str], observed_key: str, pending_key: str) -> bool:
    observed = _float(row, observed_key)
    pending = _float(row, pending_key)
    return observed != 0.0 and pending != 0.0 and observed * pending < 0.0


def _is_active_target_row(row: dict[str, str]) -> bool:
    return _truthy(row.get("is_aiming", "")) and bool(row.get("target_source"))


def _has_axis_overlap(
    row: dict[str, str],
    previous_move_row: dict[str, str],
) -> bool:
    return (
        _float(row, "manual_dx") != 0.0
        and _float(previous_move_row, "move_x") != 0.0
    ) or (
        _float(row, "manual_dy") != 0.0
        and _float(previous_move_row, "move_y") != 0.0
    )


def _count_recent_injected_echo_manual_rows(rows: list[dict[str, str]]) -> int:
    count = 0
    previous_move_row = None
    for row in rows:
        if (
            previous_move_row is not None
            and _is_active_target_row(row)
            and _truthy(row.get("manual_override_active", ""))
            and _magnitude(row, "manual_dx", "manual_dy") >= MANUAL_OVERRIDE_TRIGGER_PX
            and _float(row, "timestamp") - _float(previous_move_row, "timestamp")
            <= INJECTED_ECHO_WINDOW_SECONDS
            and _has_axis_overlap(row, previous_move_row)
        ):
            count += 1
        if _magnitude(row, "move_x", "move_y") > OUTPUT_EPSILON:
            previous_move_row = row
    return count


def _series(rows: list[dict[str, str]], key: str) -> list[float]:
    return [_float(row, key) for row in rows if row.get(key, "") != ""]


def _percentile(values: list[float], percentile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * max(0.0, min(100.0, percentile)) / 100.0
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def latest_telemetry_file(directory: Path = DEFAULT_TELEMETRY_DIR) -> Path:
    files = sorted(directory.glob("mouse-controller-*.csv"), key=lambda item: item.stat().st_mtime)
    if not files:
        raise FileNotFoundError(f"No mouse telemetry CSV found under {directory}")
    return files[-1]


def summarize(path: Path) -> MouseTelemetrySummary:
    rows = load_rows(path)
    if not rows:
        return MouseTelemetrySummary(
            path=path,
            rows=0,
            duration_s=0.0,
            aiming_rows=0,
            target_rows=0,
            target_revision_changes=0,
            target_update_hz=0.0,
            phase_counts=Counter(),
            injection_backend_counts=Counter(),
            snap_rows=0,
            max_target_radius_px=0.0,
            manual_left_pressed_rows=0,
            manual_override_rows=0,
            injected_echo_manual_rows=0,
            reverse_injected_echo_rows=0,
            nonzero_output_rows=0,
            nonzero_move_rows=0,
            output_without_move_rows=0,
            repeated_revision_output_rows=0,
            same_revision_move_rows=0,
            max_same_revision_move_burst=0.0,
            max_same_revision_move_burst_ms=0.0,
            controller_loop_hz=0.0,
            mouse_move_hz=0.0,
            mouse_move_gap_p95_ms=0.0,
            mouse_move_gap_max_ms=0.0,
            target_age_p95_ms=0.0,
            target_age_max_ms=0.0,
            vision_handoff_p95_ms=0.0,
            controller_consume_age_p95_ms=0.0,
            physical_aim_sync_resets=0,
            physical_aim_sync_presses=0,
            injection_errors=0,
            response_px_per_input_last=0.0,
            response_input_scale_min=0.0,
            response_input_scale_max=0.0,
            diagnosis=["Telemetry file is empty."],
        )

    first_time = _float(rows[0], "timestamp")
    last_time = _float(rows[-1], "timestamp")
    duration_s = max(0.0, last_time - first_time)
    controller_loop_hz = (
        (len(rows) - 1) / duration_s if duration_s > 0.0 and len(rows) > 1 else 0.0
    )
    phase_counts = Counter(row.get("ai_phase", "") or "blank" for row in rows)
    snap_rows = phase_counts.get("snap", 0)
    injection_backend_counts = Counter(
        row.get("injection_backend", "") or "unknown" for row in rows
    )
    aiming_rows = sum(1 for row in rows if _truthy(row.get("is_aiming", "")))
    target_rows = sum(1 for row in rows if row.get("target_source"))
    target_radii = [
        _magnitude(row, "target_dx", "target_dy")
        for row in rows
        if row.get("target_source")
    ]
    max_target_radius_px = max(target_radii) if target_radii else 0.0
    manual_override_rows = sum(
        1 for row in rows if _truthy(row.get("manual_override_active", ""))
    )
    manual_left_pressed_rows = sum(
        1 for row in rows if _truthy(row.get("manual_left_pressed", ""))
    )
    reverse_injected_echo_rows = sum(
        1
        for row in rows
        if _truthy(row.get("is_aiming", ""))
        and row.get("target_source")
        and _truthy(row.get("manual_override_active", ""))
        and _magnitude(row, "manual_dx", "manual_dy") >= 9.0
        and (
            _is_reverse_axis_echo(row, "manual_dx", "pending_injected_dx")
            or _is_reverse_axis_echo(row, "manual_dy", "pending_injected_dy")
        )
    )
    injected_echo_manual_rows = _count_recent_injected_echo_manual_rows(rows)
    nonzero_output_rows = sum(
        1 for row in rows if _magnitude(row, "output_dx", "output_dy") > OUTPUT_EPSILON
    )
    nonzero_move_rows = sum(
        1 for row in rows if _magnitude(row, "move_x", "move_y") > OUTPUT_EPSILON
    )
    move_timestamps = [
        _float(row, "timestamp")
        for row in rows
        if _magnitude(row, "move_x", "move_y") > OUTPUT_EPSILON
    ]
    if len(move_timestamps) >= 2:
        move_duration_s = max(0.0, move_timestamps[-1] - move_timestamps[0])
        mouse_move_hz = (
            (len(move_timestamps) - 1) / move_duration_s
            if move_duration_s > 0.0
            else 0.0
        )
        move_gaps_ms = [
            (current - previous) * 1000.0
            for previous, current in zip(move_timestamps, move_timestamps[1:])
        ]
    else:
        mouse_move_hz = 0.0
        move_gaps_ms = []
    mouse_move_gap_p95_ms = _percentile(move_gaps_ms, 95.0)
    mouse_move_gap_max_ms = max(move_gaps_ms) if move_gaps_ms else 0.0
    active_target_rows = [
        row
        for row in rows
        if _truthy(row.get("is_aiming", "")) and row.get("target_source")
    ]
    target_age_values = [
        _float(row, "target_age_ms")
        for row in active_target_rows
        if row.get("target_age_ms", "") != ""
    ]
    vision_handoff_values = _series(rows, "vision_handoff_ms")
    controller_consume_age_values = [
        _float(row, "controller_consume_age_ms")
        for row in active_target_rows
        if row.get("controller_consume_age_ms", "") != ""
    ]
    target_age_p95_ms = _percentile(target_age_values, 95.0)
    target_age_max_ms = max(target_age_values) if target_age_values else 0.0
    vision_handoff_p95_ms = _percentile(vision_handoff_values, 95.0)
    controller_consume_age_p95_ms = _percentile(
        controller_consume_age_values,
        95.0,
    )
    output_without_move_rows = sum(
        1
        for row in rows
        if _magnitude(row, "output_dx", "output_dy") > OUTPUT_EPSILON
        and _magnitude(row, "move_x", "move_y") <= OUTPUT_EPSILON
    )
    response_scales = [
        _float(row, "response_input_scale", 1.0)
        for row in rows
        if row.get("response_input_scale", "") != ""
    ]
    response_px_per_input_last = _float(rows[-1], "response_px_per_input", 0.0)
    physical_aim_sync_resets = _int(rows[-1], "physical_aim_sync_resets", 0)
    physical_aim_sync_presses = _int(rows[-1], "physical_aim_sync_presses", 0)
    injection_errors = _int(rows[-1], "injection_errors", 0)
    response_input_scale_min = min(response_scales) if response_scales else 1.0
    response_input_scale_max = max(response_scales) if response_scales else 1.0

    target_revision_changes = 0
    repeated_revision_output_rows = 0
    same_revision_move_rows = 0
    max_same_revision_move_burst = 0.0
    max_same_revision_move_burst_ms = 0.0
    previous_revision = None
    burst_revision = None
    burst_start_time = 0.0
    burst_end_time = 0.0
    burst_move_rows = 0
    burst_sum_x = 0.0
    burst_sum_y = 0.0

    def finish_move_burst() -> None:
        nonlocal max_same_revision_move_burst, max_same_revision_move_burst_ms
        if burst_move_rows < 2:
            return
        magnitude = math.hypot(burst_sum_x, burst_sum_y)
        if magnitude > max_same_revision_move_burst:
            max_same_revision_move_burst = magnitude
            max_same_revision_move_burst_ms = max(
                0.0,
                (burst_end_time - burst_start_time) * 1000.0,
            )

    for row in rows:
        revision = _int(row, "target_revision")
        has_output = _magnitude(row, "output_dx", "output_dy") > OUTPUT_EPSILON
        has_move = _magnitude(row, "move_x", "move_y") > OUTPUT_EPSILON
        if previous_revision is None:
            previous_revision = revision
            burst_revision = revision
        elif revision != previous_revision:
            target_revision_changes += 1
            finish_move_burst()
            burst_revision = revision
            burst_start_time = 0.0
            burst_end_time = 0.0
            burst_move_rows = 0
            burst_sum_x = 0.0
            burst_sum_y = 0.0
        elif has_output:
            repeated_revision_output_rows += 1
        if has_move:
            timestamp = _float(row, "timestamp")
            if burst_revision != revision:
                finish_move_burst()
                burst_revision = revision
                burst_start_time = 0.0
                burst_end_time = 0.0
                burst_move_rows = 0
                burst_sum_x = 0.0
                burst_sum_y = 0.0
            if burst_move_rows == 0:
                burst_start_time = timestamp
            elif revision == previous_revision:
                same_revision_move_rows += 1
            burst_end_time = timestamp
            burst_move_rows += 1
            burst_sum_x += _float(row, "move_x")
            burst_sum_y += _float(row, "move_y")
        previous_revision = revision
    finish_move_burst()

    target_update_hz = (
        target_revision_changes / duration_s if duration_s > 0.0 else 0.0
    )
    diagnosis = diagnose(
        rows=len(rows),
        aiming_rows=aiming_rows,
        target_rows=target_rows,
        snap_rows=snap_rows,
        max_target_radius_px=max_target_radius_px,
        manual_left_pressed_rows=manual_left_pressed_rows,
        nonzero_output_rows=nonzero_output_rows,
        nonzero_move_rows=nonzero_move_rows,
        output_without_move_rows=output_without_move_rows,
        manual_override_rows=manual_override_rows,
        injected_echo_manual_rows=injected_echo_manual_rows,
        reverse_injected_echo_rows=reverse_injected_echo_rows,
        repeated_revision_output_rows=repeated_revision_output_rows,
        same_revision_move_rows=same_revision_move_rows,
        max_same_revision_move_burst=max_same_revision_move_burst,
        response_input_scale_max=response_input_scale_max,
        duration_s=duration_s,
        controller_loop_hz=controller_loop_hz,
        mouse_move_gap_p95_ms=mouse_move_gap_p95_ms,
        target_age_p95_ms=target_age_p95_ms,
        physical_aim_sync_resets=physical_aim_sync_resets,
        physical_aim_sync_presses=physical_aim_sync_presses,
        injection_errors=injection_errors,
    )

    return MouseTelemetrySummary(
        path=path,
        rows=len(rows),
        duration_s=duration_s,
        aiming_rows=aiming_rows,
        target_rows=target_rows,
        target_revision_changes=target_revision_changes,
        target_update_hz=target_update_hz,
        phase_counts=phase_counts,
        injection_backend_counts=injection_backend_counts,
        snap_rows=snap_rows,
        max_target_radius_px=max_target_radius_px,
        manual_left_pressed_rows=manual_left_pressed_rows,
        manual_override_rows=manual_override_rows,
        injected_echo_manual_rows=injected_echo_manual_rows,
        reverse_injected_echo_rows=reverse_injected_echo_rows,
        nonzero_output_rows=nonzero_output_rows,
        nonzero_move_rows=nonzero_move_rows,
        output_without_move_rows=output_without_move_rows,
        repeated_revision_output_rows=repeated_revision_output_rows,
        same_revision_move_rows=same_revision_move_rows,
        max_same_revision_move_burst=max_same_revision_move_burst,
        max_same_revision_move_burst_ms=max_same_revision_move_burst_ms,
        controller_loop_hz=controller_loop_hz,
        mouse_move_hz=mouse_move_hz,
        mouse_move_gap_p95_ms=mouse_move_gap_p95_ms,
        mouse_move_gap_max_ms=mouse_move_gap_max_ms,
        target_age_p95_ms=target_age_p95_ms,
        target_age_max_ms=target_age_max_ms,
        vision_handoff_p95_ms=vision_handoff_p95_ms,
        controller_consume_age_p95_ms=controller_consume_age_p95_ms,
        physical_aim_sync_resets=physical_aim_sync_resets,
        physical_aim_sync_presses=physical_aim_sync_presses,
        injection_errors=injection_errors,
        response_px_per_input_last=response_px_per_input_last,
        response_input_scale_min=response_input_scale_min,
        response_input_scale_max=response_input_scale_max,
        diagnosis=diagnosis,
    )


def diagnose(
    *,
    rows: int,
    aiming_rows: int,
    target_rows: int,
    snap_rows: int = 0,
    max_target_radius_px: float = 0.0,
    manual_left_pressed_rows: int = 0,
    nonzero_output_rows: int,
    nonzero_move_rows: int,
    output_without_move_rows: int,
    manual_override_rows: int,
    injected_echo_manual_rows: int = 0,
    reverse_injected_echo_rows: int = 0,
    repeated_revision_output_rows: int,
    same_revision_move_rows: int = 0,
    max_same_revision_move_burst: float = 0.0,
    response_input_scale_max: float = 1.0,
    duration_s: float = 0.0,
    controller_loop_hz: float = 0.0,
    mouse_move_gap_p95_ms: float = 0.0,
    target_age_p95_ms: float = 0.0,
    physical_aim_sync_resets: int = 0,
    physical_aim_sync_presses: int = 0,
    injection_errors: int = 0,
) -> list[str]:
    if rows <= 0:
        return ["No telemetry rows were captured."]
    if aiming_rows <= 0:
        return ["No aiming rows: mouse button state did not reach the controller."]
    if target_rows <= 0:
        return ["No controller target rows: inspect vision/native target delivery first."]
    if nonzero_output_rows <= 0:
        return ["Targets exist but AI output is zero: inspect ai_phase/config gates."]
    if repeated_revision_output_rows <= 0:
        return ["AI only outputs on new target revisions: snap/acquire continuity is still broken."]
    if nonzero_move_rows <= 0:
        return ["AI output exists but no integer mouse move is emitted: output scale is too small."]

    notes = []
    if injected_echo_manual_rows > 0:
        notes.append(
            "Manual takeover is being triggered immediately after a recent injected move."
        )
    if reverse_injected_echo_rows > 0:
        notes.append(
            "Synthetic mouse echo is reaching manual arbitration: injected movement is being mistaken for user input."
        )
    if manual_override_rows / rows >= 0.20:
        notes.append("Manual override is active in many rows: input arbitration is suppressing AI.")
    if output_without_move_rows / max(1, nonzero_output_rows) >= 0.65:
        notes.append("Most AI output stays fractional: increase output scale or horizon aggressiveness.")
    if response_input_scale_max >= 8.0:
        notes.append(
            "Adaptive mouse response scale climbed high: the game is seeing much less movement than requested."
        )
    if max_same_revision_move_burst >= 180.0:
        notes.append(
            "Large mouse movement was emitted on the same target revision: stale vision error is being over-consumed."
        )
    if duration_s >= 0.25 and controller_loop_hz < 90.0:
        notes.append("Controller loop cadence is below the 120Hz-class mouse target.")
    if nonzero_move_rows >= 5 and mouse_move_gap_p95_ms >= 35.0:
        notes.append("Mouse move cadence is sparse: large gaps can feel like stepped pulling.")
    if target_age_p95_ms >= 100.0:
        notes.append("Controller target age is high: vision input may already be stale.")
    if injection_errors > 0:
        notes.append("Mouse injection failures were recorded: inspect the Windows/game input layer.")
    if max_target_radius_px >= 32.0 and snap_rows <= 0:
        notes.append("Far target rows exist but snap phase never engaged: fast snap did not start.")
    if physical_aim_sync_resets > 0:
        notes.append(
            "A physical right-button fail-closed reset happened: the listener may have missed an ADS release."
        )
    if physical_aim_sync_presses > 0:
        notes.append(
            "A physical right-button recovery happened: the listener may have missed an ADS press."
        )
    if manual_left_pressed_rows > 0:
        notes.append("Manual left-click aim activation reached the controller.")
    if not notes:
        notes.append(
            "Controller emits mouse moves. If the game still does not move, inspect the Windows/game input injection layer."
        )
    return notes


def health_failures(summary: MouseTelemetrySummary) -> list[str]:
    failures = []
    if summary.rows <= 0:
        failures.append("No telemetry rows were captured.")
    if summary.aiming_rows <= 0:
        failures.append("No aiming rows were captured; mouse button state did not reach the controller.")
    if summary.target_rows <= 0:
        failures.append("No controller target rows were captured.")
    if summary.nonzero_output_rows <= 0:
        failures.append("Targets exist but AI output is zero.")
    if summary.repeated_revision_output_rows <= 0:
        failures.append("AI did not continue output across repeated target revisions.")
    if summary.nonzero_move_rows <= 0:
        failures.append("AI output exists but no integer mouse move is emitted.")
    if (
        summary.nonzero_output_rows > 0
        and summary.output_without_move_rows / summary.nonzero_output_rows >= 0.65
    ):
        failures.append("Most AI output remains fractional instead of becoming mouse moves.")
    if summary.duration_s >= 0.25 and summary.controller_loop_hz < 90.0:
        failures.append("The controller loop cadence is below the 120Hz-class mouse target.")
    if summary.nonzero_move_rows >= 5 and summary.mouse_move_gap_p95_ms >= 35.0:
        failures.append("The mouse move cadence has large gaps and can feel stepped.")
    if summary.target_age_p95_ms >= 100.0:
        failures.append("The controller target age is high; vision input may be stale.")
    if summary.injection_errors > 0:
        failures.append("Mouse injection failures were recorded.")
    if summary.injected_echo_manual_rows > 0:
        failures.append("Manual takeover is triggered immediately after a recent injected move.")
    if summary.reverse_injected_echo_rows > 0:
        failures.append("Synthetic mouse echo is reaching manual arbitration.")
    if summary.max_same_revision_move_burst >= 180.0:
        failures.append("Large mouse movement was emitted on the same target revision.")
    if summary.target_rows > 0 and summary.max_target_radius_px >= 32.0 and summary.snap_rows <= 0:
        failures.append("Far target rows were captured but snap phase never engaged.")
    if summary.rows > 0 and summary.manual_override_rows / summary.rows >= 0.20:
        failures.append("Manual override is active in too many telemetry rows.")
    return failures


def format_summary(summary: MouseTelemetrySummary) -> str:
    phase_text = ", ".join(
        f"{phase}:{count}" for phase, count in summary.phase_counts.most_common()
    )
    backend_text = ", ".join(
        f"{backend}:{count}"
        for backend, count in summary.injection_backend_counts.most_common()
    )
    diagnosis_text = "\n".join(f"- {item}" for item in summary.diagnosis)
    return "\n".join(
        (
            f"file: {summary.path}",
            f"rows: {summary.rows}",
            f"duration_s: {summary.duration_s:.3f}",
            f"aiming_rows: {summary.aiming_rows}",
            f"target_rows: {summary.target_rows}",
            f"target_revision_changes: {summary.target_revision_changes}",
            f"target_update_hz: {summary.target_update_hz:.2f}",
            f"controller_loop_hz: {summary.controller_loop_hz:.2f}",
            f"mouse_move_hz: {summary.mouse_move_hz:.2f}",
            f"mouse_move_gap_p95_ms: {summary.mouse_move_gap_p95_ms:.2f}",
            f"mouse_move_gap_max_ms: {summary.mouse_move_gap_max_ms:.2f}",
            f"target_age_p95_ms: {summary.target_age_p95_ms:.2f}",
            f"target_age_max_ms: {summary.target_age_max_ms:.2f}",
            f"vision_handoff_p95_ms: {summary.vision_handoff_p95_ms:.2f}",
            f"controller_consume_age_p95_ms: {summary.controller_consume_age_p95_ms:.2f}",
            f"physical_aim_sync_resets: {summary.physical_aim_sync_resets}",
            f"physical_aim_sync_presses: {summary.physical_aim_sync_presses}",
            f"injection_errors: {summary.injection_errors}",
            f"phase_counts: {phase_text}",
            f"injection_backend_counts: {backend_text}",
            f"snap_rows: {summary.snap_rows}",
            f"max_target_radius_px: {summary.max_target_radius_px:.2f}",
            f"manual_left_pressed_rows: {summary.manual_left_pressed_rows}",
            f"manual_override_rows: {summary.manual_override_rows}",
            f"injected_echo_manual_rows: {summary.injected_echo_manual_rows}",
            f"reverse_injected_echo_rows: {summary.reverse_injected_echo_rows}",
            f"nonzero_output_rows: {summary.nonzero_output_rows}",
            f"nonzero_move_rows: {summary.nonzero_move_rows}",
            f"output_without_move_rows: {summary.output_without_move_rows}",
            f"repeated_revision_output_rows: {summary.repeated_revision_output_rows}",
            f"same_revision_move_rows: {summary.same_revision_move_rows}",
            f"max_same_revision_move_burst: {summary.max_same_revision_move_burst:.2f}",
            f"max_same_revision_move_burst_ms: {summary.max_same_revision_move_burst_ms:.2f}",
            f"response_px_per_input_last: {summary.response_px_per_input_last:.4f}",
            f"response_input_scale_min: {summary.response_input_scale_min:.3f}",
            f"response_input_scale_max: {summary.response_input_scale_max:.3f}",
            "diagnosis:",
            diagnosis_text,
        )
    )


def _parse_args(argv=None):
    parser = argparse.ArgumentParser(description="Summarize mouse controller telemetry.")
    parser.add_argument("path", nargs="?", help="Telemetry CSV path. Defaults to latest.")
    parser.add_argument(
        "--assert-healthy",
        action="store_true",
        help="Return a nonzero exit code when telemetry shows a broken control chain.",
    )
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = _parse_args(argv)
    path = Path(args.path) if args.path else latest_telemetry_file()
    summary = summarize(path)
    print(format_summary(summary))
    if args.assert_healthy:
        failures = health_failures(summary)
        if failures:
            print("health: FAIL")
            for failure in failures:
                print(f"- {failure}")
            return 2
        print("health: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
