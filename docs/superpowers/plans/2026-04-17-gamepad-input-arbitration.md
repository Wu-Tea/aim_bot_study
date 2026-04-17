# Gamepad Input Arbitration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `Body Lock` into an input-arbitrating tracking controller that preserves helpful manual input, suppresses harmful input, and proves the behavior with arbitration-aware manual-mix benchmark metrics.

**Architecture:** Keep the existing `Manual / ADS Snap / Body Lock` state machine and scope arbitration to `Body Lock` only. Implement confidence estimation plus vector-based manual-input sanitation inside `controllers/gamepad/ai_aim.py`, expose enough runtime debug state for the benchmark to measure preserved vs. suppressed input, then extend the manual-mix suite and scoreboard to report the new arbitration metrics.

**Tech Stack:** Python 3, dataclasses, `unittest`, existing gamepad controller state machine, manual-mix benchmark runner

---

### Task 1: Lock down the desired `Body Lock` arbitration behavior with failing tests

**Files:**
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_ai_aim_plugin.py`

- [ ] **Step 1: Write the failing arbitration behavior tests**

```python
    def test_body_lock_suppresses_harmful_manual_input_when_confidence_is_high(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_smoothing=0.0,
                body_lock_max_ai_force=1.0,
                body_lock_max_ai_force_y=1.0,
                body_lock_activation_box_px=150.0,
                body_lock_confidence_frames=4,
                body_lock_confidence_min_strong=0.65,
                body_lock_opposing_suppression_max=0.9,
            )
        )
        target = _target(
            aim_point_x=350.0,
            aim_point_y=240.0,
            body_box=(315.0, 210.0, 385.0, 330.0),
        )

        for i, timestamp in enumerate((0.00, 0.02, 0.04, 0.06), start=1):
            warm = _frame(
                aiming=True,
                manual_rx=0,
                manual_ry=0,
                timestamp=timestamp,
                target_revision=i,
                target=target,
            )
            plugin.apply(warm, _output(warm))

        frame = _frame(
            aiming=True,
            manual_rx=-12000,
            manual_ry=0,
            timestamp=0.08,
            target_revision=5,
            target=target,
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)
        self.assertLess(output.right_x, 12000)

    def test_body_lock_preserves_aligned_manual_input(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_smoothing=0.0,
                body_lock_max_ai_force=1.0,
                body_lock_max_ai_force_y=1.0,
                body_lock_helpful_preservation_floor=0.8,
            )
        )
        target = _target(
            aim_point_x=350.0,
            aim_point_y=240.0,
            body_box=(315.0, 210.0, 385.0, 330.0),
        )

        for i, timestamp in enumerate((0.00, 0.02, 0.04, 0.06), start=1):
            warm = _frame(
                aiming=True,
                manual_rx=0,
                manual_ry=0,
                timestamp=timestamp,
                target_revision=i,
                target=target,
            )
            plugin.apply(warm, _output(warm))

        frame = _frame(
            aiming=True,
            manual_rx=9000,
            manual_ry=0,
            timestamp=0.08,
            target_revision=5,
            target=target,
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 9000)

    def test_body_lock_damps_orthogonal_input_more_near_lock_than_far_from_lock(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_smoothing=0.0,
                body_lock_max_ai_force=1.0,
                body_lock_max_ai_force_y=1.0,
                body_lock_near_lock_error_px=18.0,
                body_lock_orthogonal_suppression_max=0.75,
            )
        )
        near_target = _target(
            aim_point_x=328.0,
            aim_point_y=250.0,
            body_box=(293.0, 220.0, 363.0, 340.0),
        )
        far_target = _target(
            aim_point_x=360.0,
            aim_point_y=220.0,
            body_box=(325.0, 190.0, 395.0, 310.0),
        )

        near_frame = _frame(
            aiming=True,
            manual_rx=0,
            manual_ry=8000,
            timestamp=0.10,
            target_revision=1,
            target=near_target,
        )
        far_frame = _frame(
            aiming=True,
            manual_rx=0,
            manual_ry=8000,
            timestamp=0.10,
            target_revision=1,
            target=far_target,
        )

        near_output = _output(near_frame)
        far_output = _output(far_frame)
        plugin.apply(near_frame, near_output)
        plugin.reset()
        plugin.apply(far_frame, far_output)

        self.assertLess(abs(near_output.right_y), abs(far_output.right_y))

    def test_body_lock_releases_suppression_when_confidence_is_not_built(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_smoothing=0.0,
                body_lock_max_ai_force=1.0,
                body_lock_max_ai_force_y=1.0,
                body_lock_confidence_frames=4,
                body_lock_opposing_suppression_max=0.95,
            )
        )
        target = _target(
            aim_point_x=350.0,
            aim_point_y=240.0,
            body_box=(315.0, 210.0, 385.0, 330.0),
        )
        frame = _frame(
            aiming=True,
            manual_rx=-12000,
            manual_ry=0,
            timestamp=0.00,
            target_revision=1,
            target=target,
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertLess(output.right_x, 0)

    def test_leaving_body_lock_resets_confidence_before_the_next_lock(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_smoothing=0.0,
                body_lock_max_ai_force=1.0,
                body_lock_max_ai_force_y=1.0,
                body_lock_confidence_frames=4,
                body_lock_opposing_suppression_max=0.95,
            )
        )
        target = _target(
            aim_point_x=350.0,
            aim_point_y=240.0,
            body_box=(315.0, 210.0, 385.0, 330.0),
        )

        for i, timestamp in enumerate((0.00, 0.02, 0.04, 0.06), start=1):
            warm = _frame(
                aiming=True,
                manual_rx=0,
                manual_ry=0,
                timestamp=timestamp,
                target_revision=i,
                target=target,
            )
            plugin.apply(warm, _output(warm))

        stop_frame = _frame(aiming=False, manual_rx=0, manual_ry=0, timestamp=0.08, target_revision=5, target=None)
        plugin.apply(stop_frame, _output(stop_frame))

        reacquire = _frame(
            aiming=True,
            manual_rx=-12000,
            manual_ry=0,
            timestamp=0.10,
            target_revision=6,
            target=target,
        )
        output = _output(reacquire)
        plugin.apply(reacquire, output)

        self.assertLess(output.right_x, 0)

    def test_ads_snap_path_keeps_raw_manual_passthrough(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                ads_snap_smoothing=0.0,
                ads_snap_max_ai_force=1.0,
                ads_snap_max_ai_force_y=1.0,
            )
        )
        target = _target(
            aim_point_x=420.0,
            aim_point_y=220.0,
            body_box=(385.0, 190.0, 455.0, 310.0),
        )
        frame = _frame(
            aiming=True,
            target_dx=100.0,
            target_dy=-36.0,
            manual_rx=6000,
            manual_ry=-2000,
            timestamp=0.00,
            target_revision=1,
            target=target,
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 6000)
        self.assertLess(output.right_y, -2000)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin -v`
Expected: FAIL because `AIAimConfig` does not expose the new arbitration knobs and the default `Body Lock` path still raw-adds manual input to AI output

- [ ] **Step 3: Adjust helpers only if the failure is caused by missing test fixtures**

```python
def _body_lock_target(*, aim_point_x: float, aim_point_y: float) -> ControllerTarget:
    return _target(
        aim_point_x=aim_point_x,
        aim_point_y=aim_point_y,
        body_box=(aim_point_x - 35.0, aim_point_y - 30.0, aim_point_x + 35.0, aim_point_y + 90.0),
    )
```

Use this only if the new tests need a more concise target helper; do not change production code in this step.

- [ ] **Step 4: Re-run the focused test file and confirm the same behavioral failure**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin -v`
Expected: FAIL on the new arbitration assertions, not on helper errors

- [ ] **Step 5: Commit**

```bash
git add tests/gamepad/test_gamepad_ai_aim_plugin.py
git commit -m "Add body lock arbitration behavior tests"
```

### Task 2: Implement `Body Lock` confidence and manual-input arbitration

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\ai_aim.py`

- [ ] **Step 1: Add the new config knobs and debug state needed by arbitration**

```python
@dataclass(slots=True, frozen=True)
class AIAimConfig:
    smoothing: float = 0.62
    max_pixels: int = 130
    piecewise_mid_pixels: float = 60.0
    piecewise_max_pixels: float = 230.0
    piecewise_mid_ratio: float = 0.56
    piecewise_mid_pixels_y: float = 45.0
    piecewise_max_pixels_y: float = 180.0
    piecewise_mid_ratio_y: float = 0.65
    invert_x: bool = False
    invert_y: bool = False
    max_ai_force: float = 0.64
    max_ai_force_y: float = 0.8
    deadzone_inner: float = 1.5
    deadzone_outer: float = 5.0
    x_deadzone_outer: float = 3.0
    ai_fade_full: int = 8000
    ai_delta_gain: float = 0.7
    ads_snap_window_ms: int = 100
    ads_snap_smoothing: float = 0.0
    ads_snap_max_ai_force: float = 1.0
    ads_snap_max_ai_force_y: float = 1.0
    body_lock_smoothing: float = 0.18
    body_lock_max_ai_force: float = 0.42
    body_lock_max_ai_force_y: float = 0.48
    body_lock_box_tolerance_px: float = 18.0
    body_lock_activation_box_px: float = 150.0
    body_lock_confidence_frames: int = 4
    body_lock_confidence_min_strong: float = 0.65
    body_lock_opposing_suppression_max: float = 0.9
    body_lock_orthogonal_suppression_max: float = 0.75
    body_lock_helpful_preservation_floor: float = 0.8
    body_lock_near_lock_error_px: float = 18.0
    body_lock_vertical_orthogonal_bias: float = 1.15
    body_lock_vertical_deadzone_px: float = 6.0
    body_lock_vertical_tail_inner_px: float = 2.0
    body_lock_vertical_tail_speed_threshold_px_per_sec: float = 90.0
    body_lock_upper_body_ratio: float = 0.38
    body_lock_lead_frames: int = 4
    body_lock_lead_seconds: float = 0.05
    body_lock_vertical_lead_scale: float = 0.95
    body_lock_lead_max_px: float = 18.0
    body_lock_target_match_iou: float = 0.10
    body_lock_target_match_center_px: float = 48.0


@dataclass(slots=True, frozen=True)
class BodyLockManualResolution:
    sanitized_manual_x: float
    sanitized_manual_y: float
    helpful_preserved_ratio: float
    harmful_suppressed_ratio: float
    orthogonal_suppressed_ratio: float
```

Also initialize runtime/debug fields in `_reset_runtime_state()`:

```python
        self._last_lock_confidence = 0.0
        self._last_sanitized_manual_x = 0.0
        self._last_sanitized_manual_y = 0.0
        self._last_helpful_preserved_ratio = 1.0
        self._last_harmful_suppressed_ratio = 0.0
        self._last_orthogonal_suppressed_ratio = 0.0
```

- [ ] **Step 2: Run the focused controller tests to keep the suite red**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin -v`
Expected: still FAIL because the new fields exist but the controller has not changed behavior yet

- [ ] **Step 3: Implement confidence estimation and manual-vector decomposition**

```python
    def _body_lock_confidence(self, frame: GamepadFrame, lock_dx: float, lock_dy: float) -> float:
        if frame.target is None or frame.target.body_box is None:
            return 0.0

        activation_half = max(1.0, self.config.body_lock_activation_box_px * 0.5)
        activation_distance = max(abs(lock_dx), abs(lock_dy))
        activation_ratio = max(0.0, 1.0 - min(1.0, activation_distance / activation_half))
        continuity = min(1.0, self._motion_frames / max(1, self.config.body_lock_confidence_frames))
        motion_ready = 1.0 if self._motion_frames >= 2 else 0.0
        valid = 1.0 if self._should_body_lock(frame) else 0.0

        return max(
            0.0,
            min(
                1.0,
                (0.40 * continuity)
                + (0.30 * activation_ratio)
                + (0.20 * valid)
                + (0.10 * motion_ready),
            ),
        )

    def _resolve_body_lock_manual(
        self,
        frame: GamepadFrame,
        *,
        desired_ai_x: float,
        desired_ai_y: float,
        lock_confidence: float,
    ) -> BodyLockManualResolution:
        manual_x = float(frame.manual_right_x)
        manual_y = float(frame.manual_right_y)
        desired_norm = math.hypot(desired_ai_x, desired_ai_y)
        if desired_norm < 1.0 or lock_confidence <= 0.0:
            return BodyLockManualResolution(manual_x, manual_y, 1.0, 0.0, 0.0)

        ux = desired_ai_x / desired_norm
        uy = desired_ai_y / desired_norm
        parallel = (manual_x * ux) + (manual_y * uy)
        parallel_x = ux * parallel
        parallel_y = uy * parallel
        orth_x = manual_x - parallel_x
        orth_y = manual_y - parallel_y

        helpful = max(0.0, parallel)
        harmful = max(0.0, -parallel)
        error_radius = math.hypot(frame.target_dx, frame.target_dy)
        near_lock_ratio = max(
            0.0,
            1.0 - min(1.0, error_radius / max(1.0, self.config.body_lock_near_lock_error_px)),
        )
        confidence_ratio = max(
            0.0,
            min(
                1.0,
                (lock_confidence - self.config.body_lock_confidence_min_strong)
                / max(0.001, 1.0 - self.config.body_lock_confidence_min_strong),
            ),
        )

        helpful_scale = min(
            1.0,
            self.config.body_lock_helpful_preservation_floor
            + ((1.0 - near_lock_ratio) * (1.0 - self.config.body_lock_helpful_preservation_floor)),
        )
        harmful_suppression = min(
            self.config.body_lock_opposing_suppression_max,
            self.config.body_lock_opposing_suppression_max * confidence_ratio,
        )
        orthogonal_suppression = min(
            self.config.body_lock_orthogonal_suppression_max,
            self.config.body_lock_orthogonal_suppression_max * max(lock_confidence, near_lock_ratio),
        )

        sanitized_parallel = (helpful * helpful_scale) - (harmful * (1.0 - harmful_suppression))
        sanitized_orth_x = orth_x * (1.0 - orthogonal_suppression)
        sanitized_orth_y = orth_y * (
            1.0
            - min(1.0, orthogonal_suppression * self.config.body_lock_vertical_orthogonal_bias)
        )
        return BodyLockManualResolution(
            sanitized_manual_x=(ux * sanitized_parallel) + sanitized_orth_x,
            sanitized_manual_y=(uy * sanitized_parallel) + sanitized_orth_y,
            helpful_preserved_ratio=1.0 if helpful <= 1.0 else min(1.0, (helpful * helpful_scale) / helpful),
            harmful_suppressed_ratio=0.0 if harmful <= 1.0 else min(1.0, (harmful * harmful_suppression) / harmful),
            orthogonal_suppressed_ratio=0.0 if math.hypot(orth_x, orth_y) <= 1.0 else orthogonal_suppression,
        )
```

- [ ] **Step 4: Compose sanitized manual input only in `Body Lock`**

Replace the final composition block in `_apply_state_machine()` with:

```python
        if mode == "body_lock":
            lock_confidence = self._body_lock_confidence(frame, desired_dx, desired_dy)
            resolved_manual = self._resolve_body_lock_manual(
                frame,
                desired_ai_x=desired_ai_x,
                desired_ai_y=desired_ai_y,
                lock_confidence=lock_confidence,
            )
            manual_x = resolved_manual.sanitized_manual_x
            manual_y = resolved_manual.sanitized_manual_y
            self._last_lock_confidence = lock_confidence
            self._last_sanitized_manual_x = manual_x
            self._last_sanitized_manual_y = manual_y
            self._last_helpful_preserved_ratio = resolved_manual.helpful_preserved_ratio
            self._last_harmful_suppressed_ratio = resolved_manual.harmful_suppressed_ratio
            self._last_orthogonal_suppressed_ratio = resolved_manual.orthogonal_suppressed_ratio
        else:
            manual_x = float(frame.manual_right_x)
            manual_y = float(frame.manual_right_y)
            self._last_lock_confidence = 0.0
            self._last_sanitized_manual_x = manual_x
            self._last_sanitized_manual_y = manual_y
            self._last_helpful_preserved_ratio = 1.0
            self._last_harmful_suppressed_ratio = 0.0
            self._last_orthogonal_suppressed_ratio = 0.0

        output.right_x = int(manual_x + self.ai_stick_x)
        output.right_y = int(manual_y + self.ai_stick_y)
```

- [ ] **Step 5: Run the focused controller tests and make them pass**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin -v`
Expected: PASS for the new arbitration behavior tests and the existing ADS/body-lock regression tests

- [ ] **Step 6: Commit**

```bash
git add controllers/gamepad/ai_aim.py tests/gamepad/test_gamepad_ai_aim_plugin.py
git commit -m "Implement body lock input arbitration"
```

### Task 3: Wire the arbitration knobs through config loading and examples

**Files:**
- Modify: `D:\work\AI\yolo-study-001\config\loader.py`
- Modify: `D:\work\AI\yolo-study-001\config.toml.example`
- Modify: `D:\work\AI\yolo-study-001\tests\test_config_loader.py`

- [ ] **Step 1: Write the failing config-loader assertions**

```python
    def test_overrides_applied_per_section(self):
        toml = textwrap.dedent(
            """
            [gamepad.ai_aim]
            body_lock_confidence_frames = 6
            body_lock_confidence_min_strong = 0.72
            body_lock_opposing_suppression_max = 0.94
            body_lock_orthogonal_suppression_max = 0.81
            body_lock_helpful_preservation_floor = 0.77
            body_lock_near_lock_error_px = 20.0
            body_lock_vertical_orthogonal_bias = 1.25
            """
        ).strip()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text(toml, encoding="utf-8")
            config = load_tuning_config(path)
        self.assertEqual(config.gamepad_ai_aim.body_lock_confidence_frames, 6)
        self.assertEqual(config.gamepad_ai_aim.body_lock_confidence_min_strong, 0.72)
        self.assertEqual(config.gamepad_ai_aim.body_lock_opposing_suppression_max, 0.94)
        self.assertEqual(config.gamepad_ai_aim.body_lock_orthogonal_suppression_max, 0.81)
        self.assertEqual(config.gamepad_ai_aim.body_lock_helpful_preservation_floor, 0.77)
        self.assertEqual(config.gamepad_ai_aim.body_lock_near_lock_error_px, 20.0)
        self.assertEqual(config.gamepad_ai_aim.body_lock_vertical_orthogonal_bias, 1.25)
```

- [ ] **Step 2: Run the config-loader tests to verify they fail**

Run: `python -m unittest tests.test_config_loader -v`
Expected: FAIL because the new keys are ignored by `GAMEPAD_AI_AIM_KEYS`

- [ ] **Step 3: Add the allowed keys and document them in the example config**

```python
GAMEPAD_AI_AIM_KEYS = frozenset(
    {
        "smoothing",
        "max_pixels",
        "max_ai_force",
        "max_ai_force_y",
        "ai_delta_gain",
        "piecewise_mid_pixels_y",
        "piecewise_max_pixels_y",
        "piecewise_mid_ratio_y",
        "ads_snap_window_ms",
        "ads_snap_smoothing",
        "ads_snap_max_ai_force",
        "ads_snap_max_ai_force_y",
        "body_lock_smoothing",
        "body_lock_max_ai_force",
        "body_lock_max_ai_force_y",
        "body_lock_box_tolerance_px",
        "body_lock_activation_box_px",
        "body_lock_confidence_frames",
        "body_lock_confidence_min_strong",
        "body_lock_opposing_suppression_max",
        "body_lock_orthogonal_suppression_max",
        "body_lock_helpful_preservation_floor",
        "body_lock_near_lock_error_px",
        "body_lock_vertical_orthogonal_bias",
        "body_lock_vertical_deadzone_px",
        "body_lock_vertical_tail_inner_px",
        "body_lock_vertical_tail_speed_threshold_px_per_sec",
        "body_lock_upper_body_ratio",
        "body_lock_lead_frames",
        "body_lock_lead_seconds",
        "body_lock_vertical_lead_scale",
        "body_lock_lead_max_px",
        "body_lock_target_match_iou",
        "body_lock_target_match_center_px",
    }
)
```

Add these example entries to `config.toml.example`:

```toml
# Frames of stable target continuity needed before body-lock suppression gets aggressive.
body_lock_confidence_frames = 4
# Confidence threshold above which opposing manual input is strongly cut down.
body_lock_confidence_min_strong = 0.65
# Maximum fraction of harmful manual input that can be removed while lock confidence is high.
body_lock_opposing_suppression_max = 0.9
# Maximum damping applied to orthogonal wobble near the lock point.
body_lock_orthogonal_suppression_max = 0.75
# Minimum fraction of aligned manual input that still survives arbitration.
body_lock_helpful_preservation_floor = 0.8
# Near-lock radius where orthogonal damping ramps up.
body_lock_near_lock_error_px = 18.0
# Slight extra damping on vertical orthogonal wobble around the upper-body point.
body_lock_vertical_orthogonal_bias = 1.15
```

- [ ] **Step 4: Run the config-loader tests again and make them pass**

Run: `python -m unittest tests.test_config_loader -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add config/loader.py config.toml.example tests/test_config_loader.py
git commit -m "Expose body lock arbitration tuning knobs"
```

### Task 4: Extend the manual-mix benchmark to measure arbitration rather than raw yield

**Files:**
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\manual_mix_metrics.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_manual_mix_metrics.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\benchmark_scoreboard.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_benchmark_scoreboard.py`
- Modify: `D:\work\AI\yolo-study-001\docs\project\GAMEPAD_MANUAL_MIX_BENCHMARKS.md`

- [ ] **Step 1: Write the failing metric tests for arbitration-aware aggregates**

```python
    def test_harmful_input_suppression_ratio_reflects_removed_opposing_manual_input(self):
        frames = (
            ManualMixFrameRecord(
                frame=0,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=9.0,
                manual_x=-6000,
                manual_y=0,
                sanitized_manual_x=-1200,
                sanitized_manual_y=0,
                output_x=4200,
                output_y=0,
                ai_x=5400,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.8,
                orthogonal_suppressed_ratio=0.0,
            ),
        )
        self.assertAlmostEqual(_harmful_input_suppression_ratio(frames), 0.8)

    def test_aligned_input_preservation_ratio_reflects_surviving_helpful_input(self):
        frames = (
            ManualMixFrameRecord(
                frame=0,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=12.0,
                manual_x=6000,
                manual_y=0,
                sanitized_manual_x=5400,
                sanitized_manual_y=0,
                output_x=11400,
                output_y=0,
                ai_x=6000,
                ai_y=0,
                manual_mode="aligned_follow",
                controller_mode="body_lock",
                in_opposing_burst=False,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=0.9,
                harmful_suppressed_ratio=0.0,
                orthogonal_suppressed_ratio=0.0,
            ),
        )
        self.assertAlmostEqual(_aligned_input_preservation_ratio(frames), 0.9)

    def test_lock_survival_rate_counts_bursts_that_remain_in_body_lock_and_recover(self):
        frames = (
            ManualMixFrameRecord(0, "s00", 1, "turn_then_decel", 0.0, 0.0, 10.0, -5000, 0, -1000, 0, 4000, 0, "opposing_burst", "body_lock", True, True, 0.9, 1.0, 0.8, 0.0),
            ManualMixFrameRecord(1, "s00", 1, "turn_then_decel", 0.0, 0.0, 9.0, -5000, 0, -800, 0, 4200, 0, "opposing_burst", "body_lock", True, True, 0.9, 1.0, 0.8, 0.0),
            ManualMixFrameRecord(2, "s00", 1, "turn_then_decel", 0.0, 0.0, 7.0, 2000, 0, 4500, 0, 2500, 0, "overshoot_recover", "body_lock", False, True, 0.8, 0.9, 0.0, 0.0),
        )
        self.assertAlmostEqual(_lock_survival_rate(frames, recovery_threshold_px=8.0), 1.0)
```

- [ ] **Step 2: Run the manual-mix metric tests and keep them red**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_metrics -v`
Expected: FAIL because `ManualMixFrameRecord` and the aggregate dataclasses do not include arbitration-aware fields yet

- [ ] **Step 3: Extend the frame record, scenario metrics, and aggregation logic**

```python
@dataclass(frozen=True, slots=True)
class ManualMixScenarioMetrics:
    scenario_key: str
    manual_seed: int
    kind: str
    mean_error_px: float
    p95_error_px: float
    p99_error_px: float
    overshoot_events: int
    max_overshoot_px: float
    mean_recovery_frames_after_turn: float | None
    mean_settle_frames_after_decel: float | None
    conflict_frames_ratio: float
    wrong_input_recovery_frames: float | None
    manual_yield_score: float | None
    harmful_input_suppression_ratio: float | None
    aligned_input_preservation_ratio: float | None
    opposing_burst_hold_error_px: float | None
    lock_survival_rate: float | None


@dataclass(frozen=True, slots=True)
class ManualMixFrameRecord:
    frame: int
    scenario_key: str
    manual_seed: int
    kind: str
    error_x: float
    error_y: float
    radial_error_px: float
    manual_x: int
    manual_y: int
    sanitized_manual_x: int
    sanitized_manual_y: int
    output_x: int
    output_y: int
    ai_x: int
    ai_y: int
    manual_mode: str
    controller_mode: str
    in_opposing_burst: bool
    measured: bool
    lock_confidence: float
    helpful_preserved_ratio: float
    harmful_suppressed_ratio: float
    orthogonal_suppressed_ratio: float
```

In `_simulate_manual_mix_closed_loop()`, record the plugin debug fields instead of inferring AI only from `output - raw_manual`:

```python
        sanitized_manual_x = int(round(plugin._last_sanitized_manual_x))
        sanitized_manual_y = int(round(plugin._last_sanitized_manual_y))
        ai_x = int(round(plugin.ai_stick_x))
        ai_y = int(round(plugin.ai_stick_y))
        records.append(
            ManualMixFrameRecord(
                frame=state.frame,
                scenario_key=manifest.scenario_key,
                manual_seed=manual_seed,
                kind=manifest.kind,
                error_x=error_x,
                error_y=error_y,
                radial_error_px=math.hypot(error_x, error_y),
                manual_x=manual_input.manual_right_x,
                manual_y=manual_input.manual_right_y,
                sanitized_manual_x=sanitized_manual_x,
                sanitized_manual_y=sanitized_manual_y,
                output_x=output_x,
                output_y=output_y,
                ai_x=ai_x,
                ai_y=ai_y,
                manual_mode=manual_input.mode,
                controller_mode=plugin._mode,
                in_opposing_burst=manual_input.in_opposing_burst,
                measured=state.frame >= config.measure_from_frame,
                lock_confidence=plugin._last_lock_confidence,
                helpful_preserved_ratio=plugin._last_helpful_preserved_ratio,
                harmful_suppressed_ratio=plugin._last_harmful_suppressed_ratio,
                orthogonal_suppressed_ratio=plugin._last_orthogonal_suppressed_ratio,
            )
        )
```

Add metric helpers:

```python
def _harmful_input_suppression_ratio(records):
    values = [record.harmful_suppressed_ratio for record in records if record.in_opposing_burst and record.controller_mode == "body_lock" and record.measured]
    return None if not values else mean(values)


def _aligned_input_preservation_ratio(records):
    values = [record.helpful_preserved_ratio for record in records if record.controller_mode == "body_lock" and record.measured]
    return None if not values else mean(values)


def _opposing_burst_hold_error_px(records):
    values = [record.radial_error_px for record in records if record.in_opposing_burst and record.measured]
    return None if not values else mean(values)


def _lock_survival_rate(records, *, recovery_threshold_px: float):
    burst_windows = _group_burst_windows(records)
    if not burst_windows:
        return None
    survivors = 0
    for window in burst_windows:
        if all(record.controller_mode == "body_lock" for record in window) and min(record.radial_error_px for record in window) <= recovery_threshold_px:
            survivors += 1
    return survivors / len(burst_windows)
```

- [ ] **Step 4: Update scoreboard labeling for the new manual-mix metrics**

Add the history titles:

```python
    titles = {
        "mean_error_px": "Mean Error Delta",
        "p95_error_px": "P95 Delta",
        "p99_error_px": "P99 Delta",
        "overshoot_events": "Overshoot Delta",
        "max_overshoot_px": "Max Overshoot Delta",
        "mean_recovery_frames_after_turn": "Turn Recovery Delta",
        "mean_settle_frames_after_decel": "Decel Settle Delta",
        "conflict_frames_ratio": "Conflict Delta",
        "wrong_input_recovery_frames": "Wrong Input Recovery Delta",
        "manual_yield_score": "Manual Yield Delta",
        "harmful_input_suppression_ratio": "Harmful Suppression Delta",
        "aligned_input_preservation_ratio": "Aligned Preservation Delta",
        "opposing_burst_hold_error_px": "Burst Hold Error Delta",
        "lock_survival_rate": "Lock Survival Delta",
    }
```

Also update the scoreboard test entry to include the new metrics in `aggregate_metrics` and `delta_metrics` so Markdown rendering is exercised.

- [ ] **Step 5: Run the focused benchmark/reporting tests and make them pass**

Run: `python -m unittest tests.gamepad.test_gamepad_manual_mix_metrics tests.gamepad.test_gamepad_benchmark_scoreboard tests.gamepad.test_gamepad_benchmark_runner -v`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add tests/gamepad/manual_mix_metrics.py tests/gamepad/test_gamepad_manual_mix_metrics.py tests/gamepad/benchmark_scoreboard.py tests/gamepad/test_gamepad_benchmark_scoreboard.py docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md
git commit -m "Add arbitration-aware manual-mix metrics"
```

### Task 5: Run full verification and refresh the manual-mix benchmark output

**Files:**
- Modify: `D:\work\AI\yolo-study-001\docs\project\GAMEPAD_MANUAL_MIX_BENCHMARKS.md`
- Modify: `D:\work\AI\yolo-study-001\artifacts\benchmarks\gamepad_manual_mix\*.json`

- [ ] **Step 1: Run the focused regression suite for controller, config, and manual-mix**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_manual_mix_metrics tests.gamepad.test_gamepad_benchmark_runner tests.gamepad.test_gamepad_benchmark_scoreboard tests.test_config_loader -v`
Expected: PASS

- [ ] **Step 2: Run syntax verification on the touched Python files**

Run: `python -m py_compile controllers\gamepad\ai_aim.py config\loader.py tests\gamepad\manual_mix_metrics.py tests\gamepad\test_gamepad_ai_aim_plugin.py tests\gamepad\test_gamepad_manual_mix_metrics.py tests\gamepad\benchmark_scoreboard.py tests\gamepad\test_gamepad_benchmark_scoreboard.py tools\run_gamepad_benchmark.py`
Expected: no output

- [ ] **Step 3: Run the manual-mix suite against the committed baseline**

Run: `python tools/run_gamepad_benchmark.py --suite manual-mix --replay-run-key manual-mix-baseline-20260417`
Expected: JSON replay output with the new aggregate metrics present

- [ ] **Step 4: Run a fresh manual-mix benchmark with the arbitration implementation**

Run: `python tools/run_gamepad_benchmark.py --suite manual-mix`
Expected: a new artifact in `artifacts/benchmarks/gamepad_manual_mix/` and an updated `docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md`

- [ ] **Step 5: Sanity-check the outcome against the spec targets**

Confirm in the fresh run that:
- `harmful_input_suppression_ratio` is meaningfully above `0`
- `aligned_input_preservation_ratio` remains comfortably above `0.5`
- `opposing_burst_hold_error_px` and `wrong_input_recovery_frames` improve or at least do not regress badly versus baseline
- `lock_survival_rate` is reported and non-zero

- [ ] **Step 6: Commit**

```bash
git add docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md artifacts/benchmarks/gamepad_manual_mix
git commit -m "Benchmark body lock input arbitration"
```
