# Recoil Magazine Curve V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make recoil profiles record and play back a fitted full-magazine curve for full-auto and hold-to-burst automatic weapons, with each new fire press restarting playback at 0ms.

**Architecture:** Keep the current recoil collection pipeline, but add a focused magazine-curve extraction path that treats each full-magazine recording as one episode aligned at trigger-down. Preserve the existing `RecoilProfileRecord` storage contract by adding optional metadata fields for `profile_type`, `support_counts`, and fit quality, and keep controller playback compatible with old profiles while preferring magazine curves. Apply profiles only when identity and profile confidence are both ready.

**Tech Stack:** Python dataclasses, unittest, existing `vision.recoil_collection`, `recoil_app.runtime`, `runtime.recoil_sidecar`, and `controllers.gamepad.recoil_compensation`.

**Implementation status (2026-05-19):** Completed in the working tree. The implementation used the existing `tests/recoil_collection/test_extraction.py` file instead of creating `test_magazine_curve.py`, added `vision/recoil_collection/readiness.py`, and added an episode store under each recoil profile directory's `_episodes` subdirectory so repeated full-magazine recordings can be refit together. Unready profiles now fail closed as `unknown`/`None` rather than being published as ready or degraded.

---

### Task 1: Profile Schema

**Files:**
- Modify: `vision/recoil_collection/models.py`
- Test: `tests/recoil_collection/test_models.py`

- [ ] **Step 1: Write the failing test**

Create `tests/recoil_collection/test_models.py` with tests that construct a magazine profile:

```python
import unittest

from vision.recoil_collection.models import RecoilProfileRecord


class RecoilProfileRecordMagazineTests(unittest.TestCase):
    def test_magazine_profile_round_trips_optional_metadata(self):
        profile = RecoilProfileRecord(
            profile_id="profile-cod22-test-ads-standing-mag",
            canonical_weapon_id="cod22-test",
            game="cod22",
            stance="standing",
            aim_mode="ads",
            sample_interval_ms=10,
            duration_ms=40,
            initial_delay_ms=0,
            samples_x=(0.0, 0.2, 0.4, 0.5),
            samples_y=(0.0, -1.0, -1.0, -2.0),
            sample_count=4,
            burst_count=3,
            variance_summary={"horizontal_stddev": 0.2, "vertical_stddev": 0.3},
            confidence=0.82,
            capture_resolution="1920x1080",
            capture_fps=100.0,
            collector_version="collector-test",
            created_at="2026-05-19T00:00:00Z",
            profile_type="magazine_curve_v1",
            support_counts=(3, 3, 2, 2),
            fit_summary={"episode_count": 3.0, "accepted_episode_count": 2.0},
        )

        restored = RecoilProfileRecord.from_dict(profile.to_dict())

        self.assertEqual(restored.profile_type, "magazine_curve_v1")
        self.assertEqual(restored.support_counts, (3, 3, 2, 2))
        self.assertEqual(restored.fit_summary["episode_count"], 3.0)

    def test_legacy_profile_payload_defaults_to_burst_average_type(self):
        payload = {
            "profile_id": "profile-cod22-test-ads-standing-v1",
            "canonical_weapon_id": "cod22-test",
            "game": "cod22",
            "stance": "standing",
            "aim_mode": "ads",
            "sample_interval_ms": 10,
            "duration_ms": 20,
            "initial_delay_ms": 0,
            "samples_x": [0.0, 0.0],
            "samples_y": [0.0, -1.0],
            "sample_count": 2,
            "burst_count": 3,
            "variance_summary": {"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
            "confidence": 0.9,
            "capture_resolution": "1920x1080",
            "capture_fps": 100.0,
            "collector_version": "collector-test",
            "created_at": "2026-05-19T00:00:00Z",
        }

        profile = RecoilProfileRecord.from_dict(payload)

        self.assertEqual(profile.profile_type, "burst_average_v1")
        self.assertEqual(profile.support_counts, ())
        self.assertEqual(profile.fit_summary, {})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -B -m unittest tests.recoil_collection.test_models -v`
Expected: FAIL because `RecoilProfileRecord` does not accept or emit the new metadata fields.

- [ ] **Step 3: Implement schema support**

Add optional dataclass fields after `created_at`:

```python
profile_type: str = "burst_average_v1"
support_counts: tuple[int, ...] = ()
fit_summary: Mapping[str, float] = MappingProxyType({})
```

Update validation, `to_dict`, and `from_dict` so legacy payloads without these keys still load.

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -B -m unittest tests.recoil_collection.test_models -v`
Expected: PASS.

### Task 2: Magazine Curve Extraction

**Files:**
- Modify: `vision/recoil_collection/extraction.py`
- Test: `tests/recoil_collection/test_magazine_curve.py`

- [ ] **Step 1: Write the failing test**

Create a test that passes three full-magazine episodes into `extract_magazine_recoil_profile()`. The expected fitted curve uses per-time-point median/mean behavior, keeps platform segments from burst-auto weapons, emits `support_counts`, and rejects an obvious outlier episode.

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -B -m unittest tests.recoil_collection.test_magazine_curve -v`
Expected: FAIL because `extract_magazine_recoil_profile` is missing.

- [ ] **Step 3: Implement minimal extraction**

Implement `extract_magazine_recoil_profile(session, episodes, profile_id, created_at, config)` in `vision/recoil_collection/extraction.py`. Reuse `RecoilBurstSampleSeries` as the episode container, align each episode to its first sample, choose a target horizon by median supported sample count, resample each episode, reject outliers with the existing `_reject_outliers`, compute averaged `samples_x/y`, and save:

```python
profile_type="magazine_curve_v1"
support_counts=(...)
fit_summary={
    "episode_count": float(total),
    "accepted_episode_count": float(clean),
    "rejected_episode_count": float(rejected),
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -B -m unittest tests.recoil_collection.test_magazine_curve -v`
Expected: PASS.

### Task 3: Recoil App Learning Uses Magazine Profiles

**Files:**
- Modify: `vision/recoil_collection/capture.py`
- Modify: `recoil_app/runtime.py`
- Test: `tests/recoil_app/test_runtime.py`

- [ ] **Step 1: Write the failing test**

Add a test that records mode calls the collector and stores a profile with `profile_type == "magazine_curve_v1"` when learning starts.

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -B -m unittest tests.recoil_app.test_runtime -v`
Expected: FAIL until the runtime path requests magazine extraction.

- [ ] **Step 3: Implement collector wiring**

Add `profile_type: str = "magazine_curve_v1"` to `RecoilCollectorConfig`. In `collect_recoil_profile()`, call `extract_magazine_recoil_profile()` when the config profile type is magazine, while keeping the legacy extractor available for old tests and explicit compatibility.

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -B -m unittest tests.recoil_app.test_runtime -v`
Expected: PASS.

### Task 4: Playback Semantics

**Files:**
- Modify: `controllers/gamepad/recoil_compensation.py`
- Test: `tests/gamepad/test_gamepad_recoil_compensation.py`

- [ ] **Step 1: Write the failing test**

Add tests that a magazine curve with platform samples emits no extra downward delta during a burst-auto internal pause, and every new fire activation restarts playback from 0ms.

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -B -m unittest tests.gamepad.test_gamepad_recoil_compensation -v`
Expected: FAIL if platform/restart behavior is not explicit.

- [ ] **Step 3: Implement focused playback helpers**

Keep the existing cumulative-difference playback but add interpolation by elapsed time and preserve reset-on-new-fire semantics. This should be backward compatible for legacy profiles.

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -B -m unittest tests.gamepad.test_gamepad_recoil_compensation -v`
Expected: PASS.

### Task 5: Profile Readiness Gate

**Files:**
- Modify: `runtime/recoil_sidecar/service.py`
- Modify: `recoil_app/runtime.py`
- Test: `tests/runtime/test_recoil_sidecar_service.py`
- Test: `tests/recoil_app/test_runtime.py`

- [ ] **Step 1: Write the failing test**

Add tests that a profile with low `confidence` does not publish as ready and `recoil_app` does not return low-confidence active profiles.

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -B -m unittest tests.runtime.test_recoil_sidecar_service tests.recoil_app.test_runtime -v`
Expected: FAIL because profile confidence is currently not a hard readiness gate.

- [ ] **Step 3: Implement readiness gate**

Use a default `ready_profile_threshold` of `0.70`. `publish_active_profile()` should emit `degraded` for low profile confidence, and `RecoilRuntime.get_active_profile()` should return `None` for low confidence profiles in recoil mode.

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -B -m unittest tests.runtime.test_recoil_sidecar_service tests.recoil_app.test_runtime -v`
Expected: PASS.

### Task 6: Final Verification

**Files:**
- Verify: recoil collection, recoil app, runtime sidecar, weapon identity, and gamepad recoil tests.

- [ ] **Step 1: Run focused recoil suites**

Run:

```powershell
py -3 -B -m unittest tests.recoil_collection.test_models tests.recoil_collection.test_magazine_curve tests.recoil_collection.test_extraction tests.recoil_collection.test_segmentation tests.recoil_app.test_runtime tests.recoil_app.test_console tests.runtime.test_recoil_sidecar_service tests.gamepad.test_gamepad_recoil_compensation tests.weapon_identity.test_text -v
```

Expected: all tests pass.

- [ ] **Step 2: Run compile and diff checks**

Run:

```powershell
py -3 -B -m py_compile vision\recoil_collection\models.py vision\recoil_collection\extraction.py vision\recoil_collection\capture.py recoil_app\runtime.py runtime\recoil_sidecar\service.py controllers\gamepad\recoil_compensation.py
git diff --check -- vision\recoil_collection\models.py vision\recoil_collection\extraction.py vision\recoil_collection\capture.py recoil_app\runtime.py runtime\recoil_sidecar\service.py controllers\gamepad\recoil_compensation.py tests\recoil_collection tests\recoil_app\test_runtime.py tests\runtime\test_recoil_sidecar_service.py tests\gamepad\test_gamepad_recoil_compensation.py
```

Expected: compile passes; diff check has no whitespace errors except existing LF/CRLF warnings.
