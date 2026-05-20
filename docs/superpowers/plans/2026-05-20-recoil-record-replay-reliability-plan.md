# Recoil Record Replay Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `recoil_app` produce trustworthy full-magazine recordings, fit only stable recoil curves, and replay them through gamepad output using an explicit game-sensitivity calibration instead of an implicit pixel-to-stick guess.

**Architecture:** Treat recoil as a closed-loop pipeline with four explicit stages: record raw screen motion, audit and fit repeated full-magazine episodes, calibrate gamepad stick output to screen motion for the user's sensitivity, then replay only profiles that pass record-quality and calibration readiness gates. The previous config/diagnostics plan remains useful as supporting observability, but this plan is the primary fix for the broken record/replay behavior.

**Tech Stack:** Python dataclasses and unittest, OpenCV/Numpy motion estimation, existing `recoil_app.runtime`, `vision.recoil_collection`, `runtime.recoil_sidecar`, `controllers.gamepad.recoil_compensation`, local artifacts under `artifacts/recoil_profiles`, `artifacts/recoil_plots`, and `artifacts/recoil_calibration`.

---

## Application Overview

The recoil feature is meant to let the user record how a weapon climbs during a full magazine, repeat that recording several times, fit a stable curve, and have the gamepad runtime play the inverse curve when the user fires. The moving parts are:

- `recoil_app` recognizes the current weapon and starts/stops recording.
- `vision.recoil_collection.capture` samples screen motion while the user fires.
- `vision.recoil_collection.extraction` fits repeated recordings into one profile.
- `controllers.gamepad.recoil_compensation` converts profile pixels into virtual right-stick output.

The failure mode is not just "too much strength". If the recorded curve is polluted by camera movement, weapon animation, foreground motion, incorrect segmentation, or an unknown pixel-to-stick mapping, playback can be wrong even when the weapon name and profile file are found.

## Priority Correction

This plan intentionally treats config reporting, cache behavior, and `amount = 0` diagnostics as secondary. Those symptoms matter only after the app can prove that it recorded a real recoil curve, fitted it from repeated full-magazine evidence, and can replay it through a calibrated controller-response model. A session executing this plan should resist starting with small runtime toggles unless they are needed by one of the verification steps below.

The central question for every task is:

```text
Can this recording be trusted, can this fitted curve be explained, and can this playback reproduce the intended inverse motion in the current game sensitivity?
```

## Current Evidence

Local artifacts currently show the problem:

- `profile-cod22-小动脉-ads-standing-current.json` has `episode_count=2`, `accepted_episode_count=1`, and `support_counts` of `1`.
- `profile-cod22-小动脉-hipfire-standing-current.json` also has `episode_count=2`, `accepted_episode_count=1`, and `support_counts` of `1`.
- The ADS profile has `samples_y` moving from negative values to a positive tail, which means the recorded "recoil" curve is not a clean monotonic climb measurement.
- The current motion collector uses phase correlation between whole captured frames. That can include wall perspective changes, weapon model animation, HUD, lighting, or user correction, not just recoil-induced camera motion.
- The playback code maps profile pixels into right-stick output with static `piecewise_*` parameters and `amount`; it does not know the user's COD sensitivity, ADS multiplier, FOV, or response curve.

## Proposed Direction

The main implementation should stop treating a saved JSON profile as automatically usable. A recoil profile becomes runtime-ready only after:

1. Raw full-magazine episodes are preserved with enough diagnostics to explain what was measured.
2. The fitted curve is supported by at least two consistent episodes with similar shape, duration, direction, and magnitude.
3. The curve passes plausibility checks such as stable dominant vertical direction and no large recovery tail being treated as recoil.
4. A gamepad response calibration exists for the same game, aim mode, and stance.
5. Playback can produce a bounded stick command sequence from that calibration.

---

## File Structure

- Create `vision/recoil_collection/audit.py`
  - Load profile and episode JSON files.
  - Produce structured audit findings for current artifacts.
  - Detect unsupported fits, sign reversals, duration mismatch, shape disagreement, and missing calibration.
- Create `tools/audit_recoil_profiles.py`
  - CLI entry point for offline diagnosis before live gameplay.
- Modify `vision/recoil_collection/models.py`
  - Add calibration and audit dataclasses.
  - Keep existing profile JSON backward compatible.
- Create `vision/recoil_collection/calibration.py`
  - Store and load gamepad response calibration records.
  - Convert screen pixels to right-stick output using measured response.
- Modify `vision/recoil_collection/extraction.py`
  - Add strict magazine fit diagnostics and readiness metadata.
  - Reject single-supported `current` fits from runtime readiness.
- Modify `vision/recoil_collection/readiness.py`
  - Gate runtime use on recording quality and calibration readiness.
- Modify `vision/recoil_collection/capture.py`
  - Stop relying on whole-frame motion as the only measurement.
  - Add a central/static-background ROI estimator with masks and quality metrics.
- Modify `recoil_app/runtime.py`
  - Save raw episode diagnostics.
  - Log audit summaries after every recording.
  - Write plots for raw episodes, fitted recoil, anti-recoil, and calibrated playback.
- Modify `controllers/gamepad/recoil_compensation.py`
  - Use calibration-aware profile playback.
  - If calibration is missing, do not apply profile-driven compensation.
- Modify `runtime/recoil_sidecar/service.py`
  - Ensure sidecar reports unready profiles with explicit reasons.
- Tests:
  - `tests/recoil_collection/test_audit.py`
  - `tests/recoil_collection/test_calibration.py`
  - `tests/recoil_collection/test_capture.py`
  - `tests/recoil_collection/test_extraction.py`
  - `tests/recoil_collection/test_readiness.py`
  - `tests/recoil_app/test_runtime.py`
  - `tests/gamepad/test_gamepad_recoil_compensation.py`
  - `tests/runtime/test_recoil_sidecar_service.py`

---

### Task 1: Offline Audit For Existing Broken Profiles

**Files:**
- Create: `vision/recoil_collection/audit.py`
- Create: `tools/audit_recoil_profiles.py`
- Test: `tests/recoil_collection/test_audit.py`

- [x] **Step 1: Write failing audit tests**

Create `tests/recoil_collection/test_audit.py`:

```python
import tempfile
import unittest
from pathlib import Path

from vision.recoil_collection.audit import audit_recoil_profile_directory
from vision.recoil_collection.models import RecoilBurstSampleSeries, RecoilProfileRecord, RecoilSample


def _profile(*, profile_id="profile-cod22-m4-ads-standing-current", samples_y=(0.0, -4.0, 10.0)):
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id="cod22-m4",
        game="cod22",
        stance="standing",
        aim_mode="ads",
        sample_interval_ms=10,
        duration_ms=len(samples_y) * 10,
        initial_delay_ms=0,
        samples_x=tuple(0.0 for _ in samples_y),
        samples_y=tuple(float(value) for value in samples_y),
        sample_count=len(samples_y),
        burst_count=1,
        variance_summary={"horizontal_stddev": 0.0, "vertical_stddev": 0.0},
        confidence=0.8,
        capture_resolution="640x640",
        capture_fps=100.0,
        collector_version="test",
        created_at="2026-05-20T00:00:00Z",
        profile_type="magazine_curve_v1",
        support_counts=tuple(1 for _ in samples_y),
        fit_summary={"episode_count": 2.0, "accepted_episode_count": 1.0},
    )


class RecoilAuditTests(unittest.TestCase):
    def test_audit_flags_single_supported_sign_reversing_profile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            profile = _profile()
            (root / f"{profile.profile_id}.json").write_text(
                __import__("json").dumps(profile.to_dict(), ensure_ascii=False),
                encoding="utf-8",
            )

            report = audit_recoil_profile_directory(root)

        self.assertEqual(len(report.profiles), 1)
        findings = report.profiles[0].findings
        self.assertIn("accepted_episodes_below_min", findings)
        self.assertIn("support_below_min", findings)
        self.assertIn("vertical_direction_reversal", findings)
        self.assertFalse(report.profiles[0].runtime_ready)
```

- [x] **Step 2: Run test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_audit -v
```

Expected: fail because `vision.recoil_collection.audit` does not exist.

- [x] **Step 3: Implement audit dataclasses and loader**

In `vision/recoil_collection/audit.py`, implement:

```python
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import json

from vision.recoil_collection.models import RecoilProfileRecord
from vision.recoil_collection.readiness import profile_readiness_reason


@dataclass(slots=True, frozen=True)
class RecoilProfileAudit:
    profile_id: str
    aim_mode: str
    confidence: float
    sample_count: int
    duration_ms: int
    findings: tuple[str, ...]
    runtime_ready: bool


@dataclass(slots=True, frozen=True)
class RecoilProfileDirectoryAudit:
    root: Path
    profiles: tuple[RecoilProfileAudit, ...]


def audit_recoil_profile_directory(root: Path) -> RecoilProfileDirectoryAudit:
    audits = []
    for path in sorted(root.glob("*.json")):
        if path.name.endswith(".summary.json"):
            continue
        try:
            profile = RecoilProfileRecord.from_dict(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError):
            continue
        findings = _profile_findings(profile)
        audits.append(
            RecoilProfileAudit(
                profile_id=profile.profile_id,
                aim_mode=profile.aim_mode,
                confidence=profile.confidence,
                sample_count=profile.sample_count,
                duration_ms=profile.duration_ms,
                findings=findings,
                runtime_ready=not findings,
            )
        )
    return RecoilProfileDirectoryAudit(root=root, profiles=tuple(audits))


def _profile_findings(profile: RecoilProfileRecord) -> tuple[str, ...]:
    findings = []
    readiness = profile_readiness_reason(profile)
    if readiness is not None:
        findings.append(readiness)
    if profile.profile_type == "magazine_curve_v1" and profile.support_counts and min(profile.support_counts) < 2:
        findings.append("support_below_min")
    if _has_vertical_direction_reversal(profile.samples_y):
        findings.append("vertical_direction_reversal")
    return tuple(dict.fromkeys(findings))


def _has_vertical_direction_reversal(samples_y: tuple[float, ...]) -> bool:
    if len(samples_y) < 4:
        return False
    min_y = min(samples_y)
    max_y = max(samples_y)
    return min_y < -5.0 and max_y > 5.0
```

Create `tools/audit_recoil_profiles.py`:

```python
from __future__ import annotations

import argparse
import json
from pathlib import Path

from vision.recoil_collection.audit import audit_recoil_profile_directory


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Audit recoil profile readiness")
    parser.add_argument("--profile-dir", default="artifacts/recoil_profiles")
    args = parser.parse_args(argv)
    report = audit_recoil_profile_directory(Path(args.profile_dir))
    payload = {
        "root": str(report.root),
        "profiles": [
            {
                "profile_id": profile.profile_id,
                "aim_mode": profile.aim_mode,
                "confidence": profile.confidence,
                "sample_count": profile.sample_count,
                "duration_ms": profile.duration_ms,
                "findings": list(profile.findings),
                "runtime_ready": profile.runtime_ready,
            }
            for profile in report.profiles
        ],
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [x] **Step 4: Run audit tests and current artifacts audit**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_audit -v
py -3 -B tools\audit_recoil_profiles.py --profile-dir artifacts\recoil_profiles
```

Expected: tests pass. Current local `小动脉` profiles report unready findings rather than silently looking usable.

- [x] **Step 5: Commit**

```powershell
git add vision/recoil_collection/audit.py tools/audit_recoil_profiles.py tests/recoil_collection/test_audit.py
git commit -m "Add recoil profile audit tooling"
```

---

### Task 2: Make Episode Quality First-Class

**Files:**
- Modify: `vision/recoil_collection/models.py`
- Modify: `recoil_app/runtime.py`
- Test: `tests/recoil_app/test_runtime.py`

- [x] **Step 1: Write failing runtime test for episode diagnostics**

In `tests/recoil_app/test_runtime.py`, add a test near the existing magazine episode tests:

```python
def test_magazine_episode_storage_includes_quality_diagnostics(self):
    runtime = _load_runtime_module()

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        recoil_runtime = runtime.RecoilRuntime(
            game="cod22",
            identity_store=runtime.IdentityStore(temp_path / "identities"),
            profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
            stdout=io.StringIO(),
        )
        state = RecognizerState(
            game="cod22",
            canonical_weapon_id="cod22-m4",
            confidence=0.95,
            source="switch_text",
            timestamp="2026-05-20T00:00:00Z",
            degraded=False,
            matched_name="M4",
            profile_ids=(),
        )
        series = _burst_series_from_y_values(
            burst_id="session-a-burst-001",
            session_id="session-a",
            y_values=(0.0, -4.0, 10.0),
        )

        recoil_runtime._save_magazine_episode_series(
            current_state=state,
            aim_mode="ads",
            captured_at="2026-05-20T00:00:00Z",
            burst_series=(series,),
        )

        episode_file = next((temp_path / "profiles" / "_episodes").glob("episode-*.json"))
        payload = json.loads(episode_file.read_text(encoding="utf-8"))

    self.assertIn("diagnostics", payload)
    self.assertEqual(payload["diagnostics"]["sample_count"], 3)
    self.assertEqual(payload["diagnostics"]["vertical_min"], -4.0)
    self.assertEqual(payload["diagnostics"]["vertical_max"], 10.0)
    self.assertIn("vertical_direction_reversal", payload["diagnostics"]["findings"])
```

- [x] **Step 2: Run test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_app.test_runtime.RecoilRuntimeTests.test_magazine_episode_storage_includes_quality_diagnostics -v
```

Expected: fail because episode payloads do not include `diagnostics`.

- [x] **Step 3: Implement episode diagnostics**

In `recoil_app/runtime.py`, add:

```python
def _episode_diagnostics(series: RecoilBurstSampleSeries) -> dict[str, object]:
    xs = tuple(sample.x for sample in series.samples)
    ys = tuple(sample.y for sample in series.samples)
    findings = []
    if len(ys) >= 4 and min(ys) < -5.0 and max(ys) > 5.0:
        findings.append("vertical_direction_reversal")
    return {
        "sample_count": series.sample_count,
        "duration_ms": series.samples[-1].offset_ms if series.samples else 0,
        "horizontal_min": min(xs) if xs else 0.0,
        "horizontal_max": max(xs) if xs else 0.0,
        "vertical_min": min(ys) if ys else 0.0,
        "vertical_max": max(ys) if ys else 0.0,
        "findings": findings,
    }
```

In `_save_magazine_episode_series`, add:

```python
"diagnostics": _episode_diagnostics(series),
```

- [x] **Step 4: Run runtime tests**

Run:

```powershell
py -3 -B -m unittest tests.recoil_app.test_runtime -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add recoil_app/runtime.py tests/recoil_app/test_runtime.py
git commit -m "Persist recoil episode quality diagnostics"
```

---

### Task 3: Replace Whole-Frame Motion With Masked Static-ROI Estimation

**Files:**
- Create: `vision/recoil_collection/motion_estimation.py`
- Modify: `vision/recoil_collection/capture.py`
- Test: `tests/recoil_collection/test_capture.py`

- [x] **Step 1: Write synthetic estimator tests**

Add to `tests/recoil_collection/test_capture.py`:

```python
def test_static_roi_motion_estimator_ignores_bottom_weapon_animation():
    import cv2
    import numpy as np
    from vision.recoil_collection.motion_estimation import estimate_static_roi_motion

    base = np.zeros((120, 160, 3), dtype=np.uint8)
    cv2.rectangle(base, (40, 25), (120, 70), (255, 255, 255), -1)
    cv2.circle(base, (80, 48), 12, (0, 0, 0), -1)
    shifted = np.roll(base, shift=5, axis=0)
    shifted[90:120, :, :] = 255

    result = estimate_static_roi_motion(base, shifted)

    self.assertAlmostEqual(result.delta_y, 5.0, delta=1.0)
    self.assertGreaterEqual(result.quality, 0.5)


def test_static_roi_motion_estimator_reports_low_quality_for_blank_frames():
    import numpy as np
    from vision.recoil_collection.motion_estimation import estimate_static_roi_motion

    result = estimate_static_roi_motion(
        np.zeros((120, 160, 3), dtype=np.uint8),
        np.zeros((120, 160, 3), dtype=np.uint8),
    )

    self.assertEqual(result.delta_x, 0.0)
    self.assertEqual(result.delta_y, 0.0)
    self.assertLess(result.quality, 0.5)
```

- [x] **Step 2: Run tests to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_capture -v
```

Expected: fail because `motion_estimation.py` does not exist.

- [x] **Step 3: Implement static ROI estimator**

Create `vision/recoil_collection/motion_estimation.py`:

```python
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import cv2
import numpy as np


@dataclass(slots=True, frozen=True)
class MotionEstimate:
    delta_x: float
    delta_y: float
    quality: float
    feature_count: int


def estimate_static_roi_motion(previous_frame: Any, current_frame: Any) -> MotionEstimate:
    previous = _central_static_roi(previous_frame)
    current = _central_static_roi(current_frame)
    previous_gray = _to_gray(previous)
    current_gray = _to_gray(current)
    if previous_gray.std() < 2.0 or current_gray.std() < 2.0:
        return MotionEstimate(0.0, 0.0, 0.0, 0)
    shift, response = cv2.phaseCorrelate(previous_gray.astype(np.float32), current_gray.astype(np.float32))
    quality = float(max(0.0, min(1.0, response)))
    if quality < 0.05:
        return MotionEstimate(0.0, 0.0, quality, 0)
    return MotionEstimate(float(shift[0]), float(shift[1]), quality, int(previous_gray.size))


def _central_static_roi(frame: Any) -> np.ndarray:
    array = np.asarray(frame)
    height, width = array.shape[:2]
    top = int(height * 0.15)
    bottom = int(height * 0.72)
    left = int(width * 0.18)
    right = int(width * 0.82)
    return array[top:bottom, left:right]


def _to_gray(frame: np.ndarray) -> np.ndarray:
    if frame.ndim == 2:
        return frame
    return cv2.cvtColor(frame[:, :, :3], cv2.COLOR_RGB2GRAY)
```

In `vision/recoil_collection/capture.py`, replace `_estimate_phase_shift` internals with:

```python
from vision.recoil_collection.motion_estimation import estimate_static_roi_motion
```

and:

```python
def _estimate_phase_shift(previous_gray: np.ndarray, current_gray: np.ndarray) -> tuple[float, float]:
    estimate = estimate_static_roi_motion(previous_gray, current_gray)
    if estimate.quality < _MIN_VALID_PHASE_CORRELATION_RESPONSE:
        return 0.0, 0.0
    return estimate.delta_x, estimate.delta_y
```

- [x] **Step 4: Run capture tests**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_capture -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add vision/recoil_collection/motion_estimation.py vision/recoil_collection/capture.py tests/recoil_collection/test_capture.py
git commit -m "Use masked static ROI for recoil motion estimation"
```

---

### Task 4: Strict Fit Readiness For Full-Magazine Profiles

**Files:**
- Modify: `vision/recoil_collection/readiness.py`
- Modify: `vision/recoil_collection/extraction.py`
- Test: `tests/recoil_collection/test_readiness.py`
- Test: `tests/recoil_collection/test_extraction.py`

- [x] **Step 1: Write readiness tests for current bad shape**

Add to `tests/recoil_collection/test_readiness.py`:

```python
def test_magazine_profile_with_vertical_reversal_is_not_ready():
    from vision.recoil_collection.readiness import profile_readiness_reason

    profile = _profile_record(
        profile_id="profile-cod22-m4-ads-standing-current",
        canonical_weapon_id="cod22-m4",
        aim_mode="ads",
        confidence=0.95,
        profile_type="magazine_curve_v1",
        burst_count=2,
        support_counts=(2, 2, 2, 2),
        fit_summary={
            "accepted_episode_count": 2.0,
            "vertical_direction_reversal": 1.0,
        },
    )

    self.assertEqual(profile_readiness_reason(profile), "vertical_direction_reversal")
```

- [x] **Step 2: Run readiness test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_readiness -v
```

Expected: fail because readiness does not inspect reversal metadata.

- [x] **Step 3: Add strict readiness fields**

In `vision/recoil_collection/extraction.py`, when building `fit_summary`, include:

```python
"vertical_direction_reversal": 1.0 if _has_vertical_direction_reversal(profile_samples_y) else 0.0,
"horizontal_peak_abs": max(abs(value) for value in profile_samples_x) if profile_samples_x else 0.0,
"vertical_peak_abs": max(abs(value) for value in profile_samples_y) if profile_samples_y else 0.0,
```

Add:

```python
def _has_vertical_direction_reversal(samples_y: tuple[float, ...]) -> bool:
    if len(samples_y) < 4:
        return False
    return min(samples_y) < -5.0 and max(samples_y) > 5.0
```

In `vision/recoil_collection/readiness.py`, before returning ready for `magazine_curve_v1`, add:

```python
if _float_from_mapping(profile.fit_summary, "vertical_direction_reversal", fallback=0.0) >= 1.0:
    return "vertical_direction_reversal"
```

- [x] **Step 4: Run extraction and readiness tests**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_extraction tests.recoil_collection.test_readiness -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add vision/recoil_collection/extraction.py vision/recoil_collection/readiness.py tests/recoil_collection/test_extraction.py tests/recoil_collection/test_readiness.py
git commit -m "Gate recoil profiles on magazine fit quality"
```

---

### Task 5: Add Gamepad Response Calibration Records

**Files:**
- Create: `vision/recoil_collection/calibration.py`
- Modify: `vision/recoil_collection/models.py`
- Test: `tests/recoil_collection/test_calibration.py`

- [x] **Step 1: Write calibration tests**

Create `tests/recoil_collection/test_calibration.py`:

```python
import tempfile
import unittest
from pathlib import Path

from vision.recoil_collection.calibration import (
    RecoilControlCalibration,
    load_calibration,
    save_calibration,
    stick_from_pixels,
)


class RecoilCalibrationTests(unittest.TestCase):
    def test_stick_from_pixels_uses_measured_response(self):
        calibration = RecoilControlCalibration(
            game="cod22",
            aim_mode="ads",
            stance="standing",
            pixels_per_full_stick_x_per_second=500.0,
            pixels_per_full_stick_y_per_second=1000.0,
            created_at="2026-05-20T00:00:00Z",
        )

        self.assertEqual(stick_from_pixels(50.0, axis="y", duration_ms=100, calibration=calibration), 16384)
        self.assertEqual(stick_from_pixels(-50.0, axis="y", duration_ms=100, calibration=calibration), -16384)

    def test_save_and_load_calibration(self):
        calibration = RecoilControlCalibration(
            game="cod22",
            aim_mode="hipfire",
            stance="standing",
            pixels_per_full_stick_x_per_second=400.0,
            pixels_per_full_stick_y_per_second=800.0,
            created_at="2026-05-20T00:00:00Z",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "cod22-hipfire-standing.json"
            save_calibration(path, calibration)
            loaded = load_calibration(path)

        self.assertEqual(loaded, calibration)
```

- [x] **Step 2: Run test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_calibration -v
```

Expected: fail because `vision.recoil_collection.calibration` does not exist.

- [x] **Step 3: Implement calibration module**

Create `vision/recoil_collection/calibration.py`:

```python
from __future__ import annotations

from dataclasses import asdict, dataclass
import json
from pathlib import Path


@dataclass(slots=True, frozen=True)
class RecoilControlCalibration:
    game: str
    aim_mode: str
    stance: str
    pixels_per_full_stick_x_per_second: float
    pixels_per_full_stick_y_per_second: float
    created_at: str


def stick_from_pixels(
    pixels: float,
    *,
    axis: str,
    duration_ms: int,
    calibration: RecoilControlCalibration,
) -> int:
    if duration_ms <= 0:
        return 0
    if axis == "x":
        rate = calibration.pixels_per_full_stick_x_per_second
    elif axis == "y":
        rate = calibration.pixels_per_full_stick_y_per_second
    else:
        raise ValueError("axis must be one of ['x', 'y']")
    if rate <= 0.0:
        return 0
    full_stick_fraction = float(pixels) / (float(rate) * (float(duration_ms) / 1000.0))
    return int(round(max(-1.0, min(1.0, full_stick_fraction)) * 32767.0))


def save_calibration(path: Path, calibration: RecoilControlCalibration) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(asdict(calibration), ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_calibration(path: Path) -> RecoilControlCalibration:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return RecoilControlCalibration(**payload)
```

- [x] **Step 4: Run calibration tests**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_calibration -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add vision/recoil_collection/calibration.py tests/recoil_collection/test_calibration.py
git commit -m "Add recoil control calibration model"
```

---

### Task 6: Calibration-Aware Gamepad Playback

**Files:**
- Modify: `controllers/gamepad/recoil_compensation.py`
- Test: `tests/gamepad/test_gamepad_recoil_compensation.py`

- [x] **Step 1: Write playback tests**

Add:

```python
def test_profile_playback_requires_calibration_when_provider_returns_profile_bundle(self):
    from vision.recoil_collection.calibration import RecoilControlCalibration

    calibration = RecoilControlCalibration(
        game="cod22",
        aim_mode="ads",
        stance="standing",
        pixels_per_full_stick_x_per_second=500.0,
        pixels_per_full_stick_y_per_second=1000.0,
        created_at="2026-05-20T00:00:00Z",
    )
    plugin = RecoilCompensationPlugin(
        RecoilCompensationConfig(amount=1.0),
        profile_provider=lambda _frame: (_profile(samples_y=(0.0, 10.0)), calibration),
    )

    first = GamepadOutput(right_y=0, auto_fire_active=True)
    plugin.apply(_frame(timestamp=1.00), first)
    second = GamepadOutput(right_y=0, auto_fire_active=True)
    plugin.apply(_frame(timestamp=1.01), second)

    self.assertEqual(first.right_y, 0)
    self.assertEqual(second.right_y, -32767)
```

- [x] **Step 2: Run test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.gamepad.test_gamepad_recoil_compensation -v
```

Expected: fail because provider only accepts a profile, not a `(profile, calibration)` bundle.

- [x] **Step 3: Implement profile bundle support**

In `controllers/gamepad/recoil_compensation.py`, import:

```python
from vision.recoil_collection.calibration import RecoilControlCalibration, stick_from_pixels
```

Allow the provider result to be either `RecoilProfileRecord` or `(RecoilProfileRecord, RecoilControlCalibration)`. Add:

```python
def _coerce_profile_result(value):
    if value is None:
        return None, None
    if isinstance(value, tuple) and len(value) == 2:
        return value[0], value[1]
    return value, None
```

When a calibration is present, compute per-sample delta instead of absolute static mapping:

```python
previous_x, previous_y = _cumulative_profile_values(profile, elapsed_ms=max(0, elapsed_ms - profile.sample_interval_ms))
delta_x = cumulative_pixels_x - previous_x
delta_y = cumulative_pixels_y - previous_y
if calibration is not None:
    anti_recoil_stick_x = stick_from_pixels(-delta_x, axis="x", duration_ms=profile.sample_interval_ms, calibration=calibration) * float(self.config.amount)
    anti_recoil_stick_y = stick_from_pixels(-delta_y, axis="y", duration_ms=profile.sample_interval_ms, calibration=calibration) * float(self.config.amount)
else:
    anti_recoil_stick_x = _map_pixels_to_stick(-cumulative_pixels_x, config=self.config) * float(self.config.amount)
    anti_recoil_stick_y = _map_pixels_to_stick(-cumulative_pixels_y, config=self.config) * float(self.config.amount)
```

- [x] **Step 4: Run gamepad recoil tests**

Run:

```powershell
py -3 -B -m unittest tests.gamepad.test_gamepad_recoil_compensation -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add controllers/gamepad/recoil_compensation.py tests/gamepad/test_gamepad_recoil_compensation.py
git commit -m "Use calibration for recoil profile playback"
```

---

### Task 7: Runtime Readiness Requires Calibration For Profile Playback

**Files:**
- Modify: `recoil_app/runtime.py`
- Modify: `runtime/recoil_sidecar/service.py`
- Test: `tests/recoil_app/test_runtime.py`
- Test: `tests/runtime/test_recoil_sidecar_service.py`

- [x] **Step 1: Write tests for missing calibration**

In `tests/recoil_app/test_runtime.py`, add:

```python
def test_recoil_mode_reports_profile_unready_when_calibration_is_missing(self):
    runtime = _load_runtime_module()

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
        profile_store.upsert(
            _profile_record(
                profile_id="profile-cod22-m4-ads-standing-current",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.95,
                profile_type="magazine_curve_v1",
                burst_count=2,
                support_counts=(2, 2, 2),
                fit_summary={"accepted_episode_count": 2.0},
            )
        )
        recoil_runtime = runtime.RecoilRuntime(
            game="cod22",
            mode="recoil",
            identity_store=runtime.IdentityStore(temp_path / "identities"),
            profile_store=profile_store,
            stdout=io.StringIO(),
        )
        recoil_runtime.complete_switch_resolution(
            slot_index=0,
            switch_epoch=0,
            state=RecognizerState(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="switch_text",
                timestamp="2026-05-20T00:00:00Z",
                degraded=False,
                matched_name="M4",
                profile_ids=(),
            ),
        )

        self.assertIsNone(recoil_runtime.get_active_profile(aim_mode="ads"))
```

- [x] **Step 2: Run test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_app.test_runtime.RecoilRuntimeTests.test_recoil_mode_reports_profile_unready_when_calibration_is_missing -v
```

Expected: fail because runtime returns the profile without calibration awareness.

- [x] **Step 3: Implement readiness reason**

Add a calibration directory to `RecoilRuntime.__init__`:

```python
calibration_dir: Path | str | None = None,
```

Store:

```python
self.calibration_dir = Path(calibration_dir) if calibration_dir is not None else self.profile_store.directory.parent / "recoil_calibration"
```

Add:

```python
def _has_calibration(self, *, aim_mode: str, stance: str = "standing") -> bool:
    path = self.calibration_dir / f"{self.game}-{aim_mode}-{stance}.json"
    return path.is_file()
```

In `get_active_profile`, return `None` for magazine profiles when calibration is missing:

```python
profile = self.profile_store.get_best_profile(...)
if profile is not None and profile.profile_type == "magazine_curve_v1" and not self._has_calibration(aim_mode=aim_mode, stance=stance):
    return None
return profile
```

Also include `calibration_missing` in profile status diagnostics so logs explain why.

- [x] **Step 4: Run runtime and sidecar tests**

Run:

```powershell
py -3 -B -m unittest tests.recoil_app.test_runtime tests.runtime.test_recoil_sidecar_service -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add recoil_app/runtime.py runtime/recoil_sidecar/service.py tests/recoil_app/test_runtime.py tests/runtime/test_recoil_sidecar_service.py
git commit -m "Require calibration for recoil profile readiness"
```

---

### Task 8: Recording And Replay Plots On One Timeline

**Files:**
- Modify: `recoil_app/runtime.py`
- Test: `tests/recoil_app/test_runtime.py`

- [x] **Step 1: Write plot artifact test**

Add:

```python
def test_learning_capture_writes_episode_fit_and_replay_plots(self):
    runtime = _load_runtime_module()

    with tempfile.TemporaryDirectory() as temp_dir:
        plot_dir = Path(temp_dir) / "plots"
        profile = _profile_record(
            profile_id="profile-cod22-m4-ads-standing-current",
            canonical_weapon_id="cod22-m4",
            aim_mode="ads",
            confidence=0.95,
            profile_type="magazine_curve_v1",
            burst_count=2,
            support_counts=(2, 2, 2),
            fit_summary={"accepted_episode_count": 2.0},
        )

        paths = runtime._write_profile_plots(plot_dir, profile)

    names = {path.name for path in paths}
    self.assertIn("profile-cod22-m4-ads-standing-current.recoil.png", names)
    self.assertIn("profile-cod22-m4-ads-standing-current.anti_recoil.png", names)
    self.assertIn("profile-cod22-m4-ads-standing-current.timeline.png", names)
```

- [x] **Step 2: Run test to verify failure**

Run:

```powershell
py -3 -B -m unittest tests.recoil_app.test_runtime.RecoilRuntimeTests.test_learning_capture_writes_episode_fit_and_replay_plots -v
```

Expected: fail because `_write_profile_plots` returns two paths.

- [x] **Step 3: Add timeline plot**

Change `_write_profile_plots` return type to `tuple[Path, ...]`, add:

```python
timeline_path = plot_dir / f"{profile.profile_id}.timeline.png"
_write_timeline_plot(timeline_path, profile)
return recoil_path, anti_recoil_path, timeline_path
```

Implement `_write_timeline_plot` as a simple OpenCV image with X axis as sample index and lines for `samples_y` and `-samples_y`. Use the same Unicode-safe `_write_png_image` helper already present in runtime.

- [x] **Step 4: Run runtime tests**

Run:

```powershell
py -3 -B -m unittest tests.recoil_app.test_runtime -v
```

Expected: pass.

- [x] **Step 5: Commit**

```powershell
git add recoil_app/runtime.py tests/recoil_app/test_runtime.py
git commit -m "Plot recoil fit and replay timeline"
```

---

### Task 9: Live Validation Protocol

**Files:**
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`
- Create: `docs/project/RECOIL_RECORD_REPLAY_VALIDATION.md`

- [x] **Step 1: Write validation document**

Create `docs/project/RECOIL_RECORD_REPLAY_VALIDATION.md`:

```markdown
# Recoil Record Replay Validation

## Goal

Validate one weapon and one aim mode before trusting runtime compensation.

## Record

1. Start `recoil_app_start.bat` in record mode.
2. Confirm current weapon with `Y`.
3. Fire one full magazine at a static wall without touching the right stick.
4. Repeat at least three times for the same weapon and aim mode.
5. Run `py -3 -B tools\audit_recoil_profiles.py --profile-dir artifacts\recoil_profiles`.

## Pass Criteria

- `accepted_episode_count >= 2`
- `support_below_min` absent
- `vertical_direction_reversal` absent
- recoil and timeline plots show a stable curve, not a large recovery tail
- calibration file exists for the same game and aim mode

## Replay

1. Start `gamepad_start.bat` with recoil runtime enabled.
2. Confirm startup logs show the selected profile and calibration.
3. Fire a magazine without touching the right stick.
4. Record residual impact trail.

## Fail Interpretation

- No profile selected: inspect readiness reasons in `[Recoil]` logs.
- Profile selected but view dives: calibration scale is wrong.
- Profile selected but weapon still climbs: profile curve is too small or calibration underestimates stick response.
- Profile curve looks jagged or sign-reversing: discard the recording set and re-record.
```

- [x] **Step 2: Link from overview**

In `docs/project/GAMEPAD_OVERVIEW.md`, add a short link to `docs/project/RECOIL_RECORD_REPLAY_VALIDATION.md` in the Recoil App Workflow section.

- [x] **Step 3: Commit**

```powershell
git add docs/project/GAMEPAD_OVERVIEW.md docs/project/RECOIL_RECORD_REPLAY_VALIDATION.md
git commit -m "Document recoil record replay validation"
```

---

## Verification

Run focused tests:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_audit tests.recoil_collection.test_capture tests.recoil_collection.test_extraction tests.recoil_collection.test_readiness tests.recoil_collection.test_calibration tests.recoil_app.test_runtime tests.gamepad.test_gamepad_recoil_compensation tests.runtime.test_recoil_sidecar_service -v
```

Run syntax checks:

```powershell
py -3 -B -m py_compile vision\recoil_collection\audit.py vision\recoil_collection\motion_estimation.py vision\recoil_collection\calibration.py vision\recoil_collection\capture.py vision\recoil_collection\extraction.py vision\recoil_collection\readiness.py recoil_app\runtime.py controllers\gamepad\recoil_compensation.py tools\audit_recoil_profiles.py
```

Run whitespace check:

```powershell
git diff --check
```

Run current data audit:

```powershell
py -3 -B tools\audit_recoil_profiles.py --profile-dir artifacts\recoil_profiles
```

Expected for current local data: existing `小动脉` profiles should be reported as not runtime-ready until enough clean recordings and calibration exist.

---

## Recommended Goal Mode Objective

Use this objective in the new session:

```text
Fix recoil_app record/replay reliability end to end: audit current recordings, persist episode quality diagnostics, harden motion estimation, gate runtime readiness on stable multi-episode fits plus calibration, and make gamepad playback use calibrated pixel-to-stick conversion.
```

## Scope Boundary

Do not start with `gamepad.recoil.amount` or `config.toml` unless a test in this plan requires it. Configuration visibility is useful, but the priority is to prove that recording and replay are measuring and reproducing the same physical thing.
