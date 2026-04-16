# Gamepad Controller Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the default gamepad always-on blended assist with a controller-side `Manual` / `ADS Snap` / `Body Lock` state machine while preserving a legacy compatibility path for the old plugin-chain behavior.

**Architecture:** Keep the old sub-plugin pipeline available when `AIAimPlugin` is constructed with explicit `sub_plugins`, but make the default gamepad controller path use a new state-machine executor. Pass compact target metadata from `vision.runner` into the controller so `Body Lock` can reason about body boxes, upper-body lock points, and a central `200x200` activation window without redesigning vision target selection.

**Tech Stack:** Python 3, `unittest`, dataclasses, existing controller plugin pipeline, existing vision target selection output

---

### Task 1: Pass target metadata through the controller contract

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\base_controller.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\state.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad_controller.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\mouse_controller.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\kbm_controller.py`
- Modify: `D:\work\AI\yolo-study-001\vision\runner.py`
- Modify: `D:\work\AI\yolo-study-001\tests\test_vision_runner.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_controller_host.py`

- [ ] **Step 1: Write the failing contract tests**

```python
def test_build_frame_keeps_controller_target_metadata(self):
    controller = GamepadController.__new__(GamepadController)
    controller.lock = threading.Lock()
    controller.target_dx = 6.0
    controller.target_dy = -4.0
    controller.target_revision = 3
    controller.target_timestamp = 12.5
    controller.target_info = ControllerTarget(
        aim_point_x=320.0,
        aim_point_y=200.0,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=(280.0, 120.0, 360.0, 300.0),
    )
```

- [ ] **Step 2: Run the focused contract tests and verify they fail**

Run: `python -m unittest tests.test_vision_runner tests.gamepad.test_gamepad_controller_host -v`
Expected: FAIL because the controller contract does not yet carry target metadata.

- [ ] **Step 3: Implement the metadata contract**

```python
@dataclass(slots=True, frozen=True)
class ControllerTarget:
    aim_point_x: float
    aim_point_y: float
    screen_center_x: float
    screen_center_y: float
    body_box: tuple[float, float, float, float] | None = None
```

```python
def update(self, dx: float, dy: float, target: ControllerTarget | None = None):
    with self.lock:
        self.target_dx = dx
        self.target_dy = dy
        self.target_info = target
        self.target_revision += 1
        self.target_timestamp = time.perf_counter()
```

- [ ] **Step 4: Re-run the focused contract tests until they pass**

Run: `python -m unittest tests.test_vision_runner tests.gamepad.test_gamepad_controller_host -v`
Expected: PASS.

### Task 2: Add the new default `AIAimPlugin` state machine with TDD

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\ai_aim.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\state.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_ai_aim_plugin.py`

- [ ] **Step 1: Write failing state-machine tests**

```python
def test_default_plugin_only_snaps_once_per_ads_session(self):
    plugin = AIAimPlugin(AIAimConfig(smoothing=0.0))
    first = _frame(aiming=True, target_dx=60.0, target_dy=0.0, timestamp=1.00)
    second = _frame(aiming=True, target_dx=55.0, target_dy=0.0, timestamp=1.04)

    first_output = _output(first)
    second_output = _output(second)
```

```python
def test_body_lock_requires_near_target_region_and_center_activation_window(self):
    target = _target(
        aim_point_x=350.0,
        aim_point_y=220.0,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=(290.0, 150.0, 370.0, 320.0),
    )
```

- [ ] **Step 2: Run the gamepad AI aim tests and verify they fail**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin -v`
Expected: FAIL because the default plugin still behaves like continuous blended assist.

- [ ] **Step 3: Implement the minimal state machine**

```python
class AimExecutionMode(str, Enum):
    MANUAL = "manual"
    ADS_SNAP = "ads_snap"
    BODY_LOCK = "body_lock"
```

```python
if explicit_sub_plugins_were_provided:
    return self._apply_legacy_pipeline(frame, output)

if not frame.is_aiming:
    self._reset_ads_session()
    output.right_x = frame.manual_right_x
    output.right_y = frame.manual_right_y
    return
```

```python
if self._should_trigger_ads_snap(frame):
    mode = AimExecutionMode.ADS_SNAP
elif self._should_enter_or_stay_body_lock(frame):
    mode = AimExecutionMode.BODY_LOCK
else:
    mode = AimExecutionMode.MANUAL
```

- [ ] **Step 4: Re-run the AI aim tests until they pass**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin -v`
Expected: PASS.

### Task 3: Add Body Lock motion compensation and benchmark metadata support

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\ai_aim.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\benchmark_metrics.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_benchmark_metrics.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_ai_aim_plugin.py`

- [ ] **Step 1: Write failing motion-compensation tests**

```python
def test_body_lock_can_lead_a_stably_moving_target_after_n_frames(self):
    plugin = AIAimPlugin(AIAimConfig(body_lock_lead_frames=4, body_lock_lead_max_px=18.0))
```

```python
def test_closed_loop_benchmark_builds_controller_target_metadata(self):
    records = _simulate_closed_loop(manifest, config=BenchmarkMetricsConfig())
    self.assertGreater(records[-1].stick_x, 0)
```

- [ ] **Step 2: Run the focused tests and verify they fail**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_benchmark_metrics -v`
Expected: FAIL because motion compensation and benchmark target metadata are missing.

- [ ] **Step 3: Implement lead compensation and benchmark support**

```python
lead_dx = clamp(velocity_x * self.config.body_lock_lead_seconds, self.config.body_lock_lead_max_px)
lock_point_x = upper_body_x + lead_dx
```

```python
target = ControllerTarget(
    aim_point_x=screen_center_x + state.target_x,
    aim_point_y=screen_center_y + state.target_y,
    screen_center_x=screen_center_x,
    screen_center_y=screen_center_y,
    body_box=(state.target_x - 28.0 + screen_center_x, state.target_y - 72.0 + screen_center_y,
              state.target_x + 28.0 + screen_center_x, state.target_y + 72.0 + screen_center_y),
)
```

- [ ] **Step 4: Re-run the focused tests until they pass**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_benchmark_metrics -v`
Expected: PASS.

### Task 4: Wire the new default gamepad host path and update docs/config

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad_controller.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\__init__.py`
- Modify: `D:\work\AI\yolo-study-001\config\loader.py`
- Modify: `D:\work\AI\yolo-study-001\config.toml.example`
- Modify: `D:\work\AI\yolo-study-001\docs\project\GAMEPAD_OVERVIEW.md`

- [ ] **Step 1: Write failing host/config expectations**

```python
def test_gamepad_controller_uses_default_state_machine_plugin_path(self):
    controller = GamepadController.__new__(GamepadController)
    plugin = AIAimPlugin(AIAimConfig())
    self.assertIsNone(plugin._legacy_sub_plugins)
```

- [ ] **Step 2: Run the focused regression slice and verify it catches missing wiring**

Run: `python -m unittest tests.gamepad.test_gamepad_controller_host tests.test_config_loader -v`
Expected: FAIL until the new config keys and host wiring are in place.

- [ ] **Step 3: Implement the host/config/docs updates**

```python
GAMEPAD_AI_AIM_KEYS = frozenset({
    "smoothing",
    "max_pixels",
    "max_ai_force",
    "max_ai_force_y",
    "ai_delta_gain",
    "ads_snap_window_ms",
    "body_lock_activation_box_px",
    "body_lock_box_tolerance_px",
    "body_lock_lead_frames",
    "body_lock_lead_max_px",
})
```

```python
self.plugins = list(plugins) if plugins is not None else [
    AIAimPlugin(ai_aim_config),
    AutoFirePlugin(AutoFireConfig(fire_output=auto_fire_output)),
    RecoilCompensationPlugin(RecoilCompensationConfig(amount=0.30)),
]
```

- [ ] **Step 4: Re-run the focused host/config regression slice**

Run: `python -m unittest tests.gamepad.test_gamepad_controller_host tests.test_config_loader -v`
Expected: PASS.

### Task 5: Run the final verification slice

**Files:**
- Verify only: updated controller, vision, config, docs, and gamepad tests

- [ ] **Step 1: Run the targeted gamepad and vision regression slice**

Run: `python -m unittest tests.test_vision_runner tests.gamepad.test_gamepad_controller_host tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_benchmark_metrics -v`
Expected: PASS.

- [ ] **Step 2: Run the broader gamepad regression slice**

Run: `python -m unittest discover -s tests/gamepad -p "test_*.py" -v`
Expected: PASS.

- [ ] **Step 3: Run syntax verification**

Run: `python -m py_compile controllers\base_controller.py controllers\gamepad\state.py controllers\gamepad\ai_aim.py controllers\gamepad_controller.py controllers\mouse_controller.py controllers\kbm_controller.py vision\runner.py tests\test_vision_runner.py tests\gamepad\test_gamepad_controller_host.py tests\gamepad\test_gamepad_ai_aim_plugin.py tests\gamepad\benchmark_metrics.py tests\gamepad\test_gamepad_benchmark_metrics.py`
Expected: no output, exit code 0.
