# Native C++ Runtime Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the live game runtime with a single C++ executable that owns entrypoint, config loading, native vision, controller state, ai_aim, recoil, auto-fire, perf logging, and virtual gamepad output without requiring Python during gameplay.

**Architecture:** Build a new native runtime executable beside the existing `vision_native` library, then migrate controller behavior module-by-module while keeping the Python runtime as a fallback. The C++ runtime should pass `VisionResult` directly from native vision into a native controller core through shared in-process structures, avoiding Python dict conversion, Python controller threads, and Python virtual gamepad calls on the live path.

**Tech Stack:** C++17, CMake, CUDA, TensorRT, DXGI/D3D11, XInput, ViGEmClient, existing `native/vision_native` code, Python unittest-based scaffold tests, existing `tools/build_native_vision.ps1`.

---

## New Session Prompt

Use this prompt to start the implementation session:

```text
Read AGENTS.md, .agent-context/handoff.md, and docs/superpowers/plans/2026-06-06-native-cpp-runtime-migration.md first.

Objective: implement the C++ live runtime migration plan task-by-task. Do not rewrite the Python runtime first. Keep Python as fallback, add a new C++ executable, and migrate entrypoint/config/controller output loop before porting ai_aim/recoil internals.

Important constraints:
- Do not revert unrelated dirty worktree changes.
- Keep current config.toml field names compatible.
- Keep current native vision behavior and target authority gates.
- Do not smooth final gamepad output after recoil.
- Preserve recoil profile timing and target-aware recoil yield semantics.
- Use tests first for each behavior change.
- Stop for user gameplay验收 after each milestone that can run in-game.
```

## Application Overview

The live application helps a controller-driven FPS/COD workflow by detecting a person target near the screen center, converting target deltas into right-stick assistance, combining that assistance with recoil compensation, and writing the final output to a virtual gamepad. Today, TensorRT vision and target selection already run in C++, but Python still owns the live entrypoint, config parsing, controller state, plugin pipeline, recoil playback, and virtual gamepad output. The desired end state is a single C++ runtime process where native vision and native controller exchange data in memory and the only Python usage left is tooling such as training, export, plotting, and fallback debugging.

## Current State To Preserve

- Native vision source lives under `native/vision_native`.
- Python runtime entrypoint is `main.py`.
- Python controller implementation is mostly in `controllers/gamepad_controller.py` and `controllers/gamepad/*`.
- Runtime config defaults are defined in `config/loader.py`, with local overrides in `config.toml`.
- Recoil profiles live under `artifacts/recoil_profiles`.
- Recent native perf logs include:
  - `gpu_total`
  - `d2h_copy`
  - `sync_wait`
  - `box samples/near/edge/near_edge`
- Current controller plugin order is:
  - `AIAimPlugin`
  - `AutoFirePlugin`
  - `AimAssistDynamicsPlugin`
  - `RecoilCompensationPlugin`

## Design Boundaries

Keep these boundaries unless the user explicitly changes the scope:

- Python remains available as fallback and tooling.
- The live C++ runtime gets a new executable instead of replacing `main.py` in the first milestone.
- `config.toml` remains the shared config source.
- The C++ runtime must not depend on Python packages at gameplay time.
- Native vision remains the source of target authority fields.
- Controller behavior is migrated in small pieces with golden tests against Python behavior.
- The first production-capable target is gamepad only. Mouse can be migrated after the gamepad runtime is stable.

## File Map

Create or modify these areas:

- Modify: `native/vision_native/CMakeLists.txt`
  - Add the new runtime executable and native controller source files.
- Create: `native/controller_native/`
  - C++ controller core, config structs, XInput input reader, ViGEm output backend, recoil and ai_aim modules.
- Create: `native/runtime_app/`
  - Native executable entrypoint, runtime loop, perf logger, shutdown handling.
- Create: `tests/test_native_cpp_runtime_scaffold.py`
  - Static/scaffold tests that verify new C++ files and CMake targets exist.
- Create: `tests/test_native_controller_behavior.py`
  - Python-side golden tests for controller math fixtures when bindings or test executables exist.
- Modify: `tools/build_native_vision.ps1`
  - Keep one build script capable of building the existing native module, smoke exe, debug exe, and new runtime exe.
- Create: `scripts/launch/gamepad_native_cpp_start.bat`
  - New C++ runtime launcher.
- Keep: `scripts/launch/gamepad_start.bat`
  - Python fallback until the user accepts the C++ runtime as default.

---

### Task 1: Add Native Runtime Scaffold Tests

**Files:**
- Create: `tests/test_native_cpp_runtime_scaffold.py`
- Modify: none
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Write the failing scaffold test**

Create `tests/test_native_cpp_runtime_scaffold.py` with:

```python
from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
NATIVE_DIR = PROJECT_ROOT / "native"
VISION_NATIVE_DIR = NATIVE_DIR / "vision_native"
CONTROLLER_NATIVE_DIR = NATIVE_DIR / "controller_native"
RUNTIME_APP_DIR = NATIVE_DIR / "runtime_app"
CMAKE_FILE = VISION_NATIVE_DIR / "CMakeLists.txt"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class NativeCppRuntimeScaffoldTests(unittest.TestCase):
    def test_native_runtime_directories_exist(self):
        self.assertTrue(CONTROLLER_NATIVE_DIR.exists())
        self.assertTrue(RUNTIME_APP_DIR.exists())

    def test_cmake_declares_native_runtime_executable(self):
        content = _read(CMAKE_FILE)
        self.assertIn("cod_native_runtime", content)
        self.assertIn("native/controller_native", content)
        self.assertIn("native/runtime_app", content)

    def test_runtime_entrypoint_and_core_headers_exist(self):
        expected = [
            RUNTIME_APP_DIR / "main.cpp",
            RUNTIME_APP_DIR / "runtime_loop.h",
            RUNTIME_APP_DIR / "runtime_loop.cpp",
            RUNTIME_APP_DIR / "perf_logger.h",
            RUNTIME_APP_DIR / "perf_logger.cpp",
            CONTROLLER_NATIVE_DIR / "runtime_config.h",
            CONTROLLER_NATIVE_DIR / "runtime_config.cpp",
            CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h",
            CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp",
            CONTROLLER_NATIVE_DIR / "virtual_gamepad.h",
            CONTROLLER_NATIVE_DIR / "virtual_gamepad.cpp",
            CONTROLLER_NATIVE_DIR / "xinput_reader.h",
            CONTROLLER_NATIVE_DIR / "xinput_reader.cpp",
        ]
        for path in expected:
            with self.subTest(path=path):
                self.assertTrue(path.exists(), path)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold -v
```

Expected: FAIL because `native/controller_native`, `native/runtime_app`, and `cod_native_runtime` do not exist yet.

- [ ] **Step 3: Add empty directories and minimal source/header files**

Create the listed files with minimal compilable C++ skeletons. Each `.h` should use `#pragma once`; each `.cpp` should include its header.

- [ ] **Step 4: Add CMake target skeleton**

Modify `native/vision_native/CMakeLists.txt` to include a new executable:

```cmake
add_executable(cod_native_runtime
    ../runtime_app/main.cpp
    ../runtime_app/runtime_loop.cpp
    ../runtime_app/perf_logger.cpp
    ../controller_native/runtime_config.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/virtual_gamepad.cpp
    ../controller_native/xinput_reader.cpp
)

target_include_directories(cod_native_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(cod_native_runtime PRIVATE
    vision_native_core
    d3d11
    dxgi
    xinput
)
```

If the existing CMake file uses different library variables for CUDA/TensorRT, follow its current pattern and keep this target beside `vision_native_debug` and `vision_native_smoke`.

- [ ] **Step 5: Run scaffold test again**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold -v
```

Expected: PASS.

- [ ] **Step 6: Build native project**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected: build succeeds and produces `native\vision_native\build\Release\cod_native_runtime.exe`.

---

### Task 2: Implement C++ Runtime Config Loader

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add config parser expectations to the scaffold test**

Append this test to `NativeCppRuntimeScaffoldTests`:

```python
    def test_runtime_config_loader_reads_current_config_toml(self):
        header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        self.assertIn("struct VisionRuntimeConfig", header)
        self.assertIn("struct GamepadRuntimeConfig", header)
        self.assertIn("load_runtime_config", header)
        self.assertIn("config.toml", source)
        self.assertIn("gamepad.ai_aim", source)
        self.assertIn("gamepad.recoil", source)
        self.assertIn("aim_assist_dynamics", source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_runtime_config_loader_reads_current_config_toml -v
```

Expected: FAIL because the config structs and loader do not exist.

- [ ] **Step 3: Implement config structs**

Define at least these structs in `native/controller_native/runtime_config.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace controller_native {

struct VisionRuntimeConfig {
    int capture_width = 640;
    int capture_height = 512;
    int capture_fps = 140;
    std::string model_path = "models/best.engine";
    bool perf_log = false;
};

struct GamepadAiAimConfig {
    float weak_target_body_lock_force_scale = 0.55f;
    float cue_hold_body_lock_force_scale = 0.45f;
};

struct GamepadAimAssistDynamicsConfig {
    bool enabled = true;
};

struct GamepadRecoilConfig {
    bool enabled = true;
    bool selection_log_enabled = false;
    bool target_direction_yield_enabled = true;
    bool profile_despike_enabled = true;
    float profile_despike_threshold_px = 8.0f;
    float profile_despike_ratio = 0.5f;
};

struct GamepadRuntimeConfig {
    GamepadAiAimConfig ai_aim;
    GamepadAimAssistDynamicsConfig aim_assist_dynamics;
    GamepadRecoilConfig recoil;
};

struct RuntimeConfig {
    VisionRuntimeConfig vision;
    GamepadRuntimeConfig gamepad;
};

RuntimeConfig load_runtime_config(const std::filesystem::path& path);

}  // namespace controller_native
```

- [ ] **Step 4: Implement a minimal TOML subset loader**

Implement `native/controller_native/runtime_config.cpp` with a small parser that supports:

- section headers such as `[gamepad.recoil]`
- `#` comments
- quoted strings
- booleans `true` and `false`
- integers
- floats

The first version should read only fields needed by the C++ runtime and keep defaults for all others. Do not make gameplay runtime depend on Python to parse config.

- [ ] **Step 5: Run tests**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold -v
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected: tests and native build pass.

---

### Task 3: Add Native Runtime Entrypoint And Perf Shell

**Files:**
- Modify: `native/runtime_app/main.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/perf_logger.h`
- Modify: `native/runtime_app/perf_logger.cpp`
- Modify: `tools/build_native_vision.ps1`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for CLI behavior**

Append:

```python
    def test_runtime_entrypoint_exposes_config_and_perf_log_flags(self):
        main_cpp = _read(RUNTIME_APP_DIR / "main.cpp")
        self.assertIn("--config", main_cpp)
        self.assertIn("--perf-log", main_cpp)
        self.assertIn("load_runtime_config", main_cpp)
        self.assertIn("RuntimeLoop", main_cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_runtime_entrypoint_exposes_config_and_perf_log_flags -v
```

Expected: FAIL until `main.cpp` contains the flags and runtime loop.

- [ ] **Step 3: Implement `main.cpp`**

Implement a simple executable that:

- accepts `--config <path>`
- accepts `--perf-log`
- prints startup config summary
- constructs `RuntimeLoop`
- calls `run()`
- catches `std::exception`

- [ ] **Step 4: Implement `PerfLogger` shell**

Start with a simple logger that prints:

```text
[Perf][CPP] loop=... FPS | ctrl_loop=...ms | out_age=...ms
```

The first logger may use initial zero-valued metrics from the runtime loop, but it must be C++ runtime-owned and not use Python.

- [ ] **Step 5: Add build script awareness**

Modify `tools/build_native_vision.ps1` only if it filters target names. If it builds the whole CMake project already, no script change is needed.

- [ ] **Step 6: Build and run help**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log
```

Expected: exe starts, prints config summary, then exits cleanly or runs an empty loop that can be stopped with the configured quit key.

---

### Task 4: Implement XInput Physical Input Reader

**Files:**
- Modify: `native/controller_native/xinput_reader.h`
- Modify: `native/controller_native/xinput_reader.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for XInput reader contract**

Append:

```python
    def test_xinput_reader_contract_exists(self):
        header = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.h")
        source = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.cpp")
        self.assertIn("struct PhysicalGamepadState", header)
        self.assertIn("class XInputReader", header)
        self.assertIn("read", header)
        self.assertIn("XInputGetState", source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_xinput_reader_contract_exists -v
```

Expected: FAIL until the contract exists.

- [ ] **Step 3: Implement input state**

Use this shape:

```cpp
struct PhysicalGamepadState {
    bool connected = false;
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool rb = false;
    bool lb = false;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
};
```

Normalize stick values to `[-1.0, 1.0]` and trigger values to `[0.0, 1.0]`. Apply deadzone handling in this layer only if the Python controller currently does the same for physical input.

- [ ] **Step 4: Build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected: build passes.

---

### Task 5: Implement Native Virtual Gamepad Backend

**Files:**
- Modify: `native/controller_native/virtual_gamepad.h`
- Modify: `native/controller_native/virtual_gamepad.cpp`
- Modify: `native/vision_native/CMakeLists.txt`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for backend contract**

Append:

```python
    def test_virtual_gamepad_backend_contract_exists(self):
        header = _read(CONTROLLER_NATIVE_DIR / "virtual_gamepad.h")
        source = _read(CONTROLLER_NATIVE_DIR / "virtual_gamepad.cpp")
        self.assertIn("struct GamepadOutputState", header)
        self.assertIn("class VirtualGamepad", header)
        self.assertIn("update", header)
        self.assertTrue("ViGEm" in source or "vigem" in source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_virtual_gamepad_backend_contract_exists -v
```

Expected: FAIL until the backend contract exists.

- [ ] **Step 3: Implement backend interface**

Use this output shape:

```cpp
struct GamepadOutputState {
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool rb = false;
    bool lb = false;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
};
```

Implement `VirtualGamepad::update(const GamepadOutputState&)` with ViGEmClient when headers/libs are available. If ViGEm discovery fails on the first pass, provide a `LoggingVirtualGamepad` implementation and keep CMake option `NATIVE_ENABLE_VIGEM=OFF` as the default for tests. Do not claim production gamepad output works until ViGEm output is verified on the user's machine.

- [ ] **Step 4: Build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected: build passes. If ViGEm is not configured, the executable should build with logging backend and print a clear startup warning.

---

### Task 6: Implement Native Gamepad Controller Pass-Through

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for controller loop ownership**

Append:

```python
    def test_native_gamepad_controller_owns_pass_through_pipeline(self):
        header = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h")
        source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        self.assertIn("class NativeGamepadController", header)
        self.assertIn("submit_vision_state", header)
        self.assertIn("build_output", source)
        self.assertIn("PhysicalGamepadState", source)
        self.assertIn("GamepadOutputState", source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_gamepad_controller_owns_pass_through_pipeline -v
```

Expected: FAIL until the controller contract exists.

- [ ] **Step 3: Implement pass-through**

Implement `build_output` so physical sticks, triggers, and buttons pass through unchanged. Do not add ai_aim or recoil in this task.

- [ ] **Step 4: Wire runtime loop**

The runtime loop should:

```text
read physical input
call controller.build_output
call virtual_gamepad.update
record perf sample
sleep or wait for next controller tick
```

Use a fixed loop target of 250 Hz for the first pass. Make the tick rate configurable after the pass-through loop is stable.

- [ ] **Step 5: Gameplay smoke**

Run:

```powershell
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log
```

Expected: manual controller pass-through works with no AI assistance and no recoil. This is the first user验收 checkpoint.

---

### Task 7: Directly Connect Native Vision To Native Controller

**Files:**
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for direct `VisionResult` handoff**

Append:

```python
    def test_runtime_connects_vision_result_directly_to_controller(self):
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        controller_header = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h")
        self.assertIn("VisionEngine", runtime_source)
        self.assertIn("poll_once", runtime_source)
        self.assertIn("submit_vision_result", controller_header)
        self.assertIn("VisionResult", controller_header)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_runtime_connects_vision_result_directly_to_controller -v
```

Expected: FAIL until the direct connection exists.

- [ ] **Step 3: Implement latest vision state**

Use a latest-state model:

```cpp
struct NativeControllerVisionState {
    bool has_target = false;
    float dx = 0.0f;
    float dy = 0.0f;
    bool aim_authority = false;
    bool fire_authority = false;
    std::string target_tier = "none";
    double observed_at_seconds = 0.0;
};
```

`NativeGamepadController::submit_vision_result(const vision_native::VisionResult&)` should copy only the fields needed for controller logic into an internal latest state. Keep this copy cheap and bounded.

- [ ] **Step 4: Wire runtime**

The runtime loop should call:

```cpp
vision_native::VisionResult result = vision.poll_once();
controller.submit_vision_result(result);
```

The controller output may still ignore target deltas until Task 8.

- [ ] **Step 5: Build and smoke**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log
```

Expected: native vision starts, perf logs print, manual pass-through still works.

---

### Task 8: Port Auto-Fire Authority Gate

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for auto-fire gate terms**

Append:

```python
    def test_native_controller_contains_auto_fire_authority_gate(self):
        source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        self.assertIn("fire_authority", source)
        self.assertIn("auto_fire_requested", source)
        self.assertIn("aiming", source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_controller_contains_auto_fire_authority_gate -v
```

Expected: FAIL until auto-fire gate logic is present.

- [ ] **Step 3: Implement gate**

Implement the same fail-closed rule as Python:

```text
auto-fire can output only when:
- current physical/controller state is aiming
- native vision requested fire
- target exists
- fire_authority is true
```

Weak, cue, predicted, and no-authority targets must never trigger fire output.

- [ ] **Step 4: Add native perf counters**

Perf logger should count:

```text
fire req
fire ok
fire block
```

- [ ] **Step 5: Gameplay smoke**

Run the C++ runtime in a controlled scene and compare fire behavior to Python fallback. This is a user验收 checkpoint.

---

### Task 9: Port AI Aim Baseline

**Files:**
- Create: `native/controller_native/ai_aim.h`
- Create: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for ai_aim module**

Append:

```python
    def test_native_ai_aim_module_exists(self):
        header = CONTROLLER_NATIVE_DIR / "ai_aim.h"
        source = CONTROLLER_NATIVE_DIR / "ai_aim.cpp"
        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        self.assertIn("class NativeAiAim", _read(header))
        self.assertIn("compute", _read(header))
        self.assertIn("weak_target_body_lock_force_scale", _read(source))
        self.assertIn("cue_hold_body_lock_force_scale", _read(source))
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_ai_aim_module_exists -v
```

Expected: FAIL until files and terms exist.

- [ ] **Step 3: Implement first ai_aim core**

Implement a C++ version that covers:

```text
observed strong target
associated weak target force scale
cue_hold force scale
manual opposing input guard
target max age
```

Use the existing Python code in `controllers/gamepad/ai_aim.py` and `controllers/gamepad_controller.py` as the behavioral source. Keep the first C++ version simple enough to compare with live feel.

- [ ] **Step 4: Wire into controller output**

The output merge order should be:

```text
manual right stick
+ ai_aim assist
```

Recoil stays off in this task.

- [ ] **Step 5: Gameplay smoke**

Run C++ runtime with recoil disabled and compare target acquisition against Python fallback. This is a user验收 checkpoint.

---

### Task 10: Port Aim Assist Dynamics

**Files:**
- Create: `native/controller_native/aim_assist_dynamics.h`
- Create: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for dynamics module**

Append:

```python
    def test_native_aim_assist_dynamics_module_exists(self):
        header = CONTROLLER_NATIVE_DIR / "aim_assist_dynamics.h"
        source = CONTROLLER_NATIVE_DIR / "aim_assist_dynamics.cpp"
        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        self.assertIn("class NativeAimAssistDynamics", _read(header))
        self.assertIn("sign", _read(source))
        self.assertIn("recoil", _read(source))
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_aim_assist_dynamics_module_exists -v
```

Expected: FAIL until module exists.

- [ ] **Step 3: Implement dynamics**

Port the behavior from `controllers/gamepad/aim_assist_dynamics.py`:

```text
guard small sign flips on X and Y
operate on AI assist delta
do not smooth manual input
do not smooth final recoil output
strengthen protection while recoil/manual fire is active
```

- [ ] **Step 4: Wire order**

Controller pipeline order should become:

```text
manual
ai_aim
aim_assist_dynamics
```

- [ ] **Step 5: Gameplay smoke**

Run recoil disabled and test target tracking. Confirm large target direction changes still respond quickly.

---

### Task 11: Port Recoil Profile Loading And Playback

**Files:**
- Create: `native/controller_native/recoil_profile.h`
- Create: `native/controller_native/recoil_profile.cpp`
- Create: `native/controller_native/recoil_compensation.h`
- Create: `native/controller_native/recoil_compensation.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for recoil modules**

Append:

```python
    def test_native_recoil_modules_exist(self):
        expected = [
            CONTROLLER_NATIVE_DIR / "recoil_profile.h",
            CONTROLLER_NATIVE_DIR / "recoil_profile.cpp",
            CONTROLLER_NATIVE_DIR / "recoil_compensation.h",
            CONTROLLER_NATIVE_DIR / "recoil_compensation.cpp",
        ]
        for path in expected:
            with self.subTest(path=path):
                self.assertTrue(path.exists(), path)
        recoil_source = _read(CONTROLLER_NATIVE_DIR / "recoil_compensation.cpp")
        self.assertIn("profile_despike", recoil_source)
        self.assertIn("target_direction_yield", recoil_source)
        self.assertIn("timeline", recoil_source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_recoil_modules_exist -v
```

Expected: FAIL until recoil files exist.

- [ ] **Step 3: Implement profile JSON loading**

Load current profile JSON fields used by Python recoil playback:

```text
profile id/name fields
sample_interval_ms
samples_x
samples_y
duration_ms
initial_delay_ms
```

Use a C++ JSON parser that is vendored or already available to the build. If adding a single-header dependency, place it under `native/third_party/` and make CMake include it explicitly.

- [ ] **Step 4: Implement playback**

Preserve Python semantics:

```text
timeline advances while firing
profile despike creates playback cache only
raw profile JSON is not modified
target direction yield scales or suppresses output
timeline does not pause because target direction conflicts
X and Y both participate
```

- [ ] **Step 5: Wire order**

Controller pipeline order should become:

```text
manual
ai_aim
aim_assist_dynamics
recoil_compensation
final clamp
virtual gamepad update
```

- [ ] **Step 6: Gameplay smoke**

Run C++ runtime with one known recoil profile and compare:

```text
opening 50-80ms recoil feel
ai_aim + recoil overlap
horizontal correction
upward target-follow while recoil is active
```

This is a user验收 checkpoint.

---

### Task 12: Port Perf Logging Parity

**Files:**
- Modify: `native/runtime_app/perf_logger.h`
- Modify: `native/runtime_app/perf_logger.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for perf parity fields**

Append:

```python
    def test_native_perf_logger_keeps_python_parity_fields(self):
        source = _read(RUNTIME_APP_DIR / "perf_logger.cpp")
        required = [
            "[Perf][CPP]",
            "loop=",
            "native=",
            "consume=",
            "out_age=",
            "gpu_total=",
            "sync_wait=",
            "tier",
            "fire req",
            "box samples",
        ]
        for token in required:
            with self.subTest(token=token):
                self.assertIn(token, source)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_perf_logger_keeps_python_parity_fields -v
```

Expected: FAIL until perf logger includes parity fields.

- [ ] **Step 3: Implement perf windows**

Match the Python log shape where practical:

```text
[Perf][CPP] loop=... FPS | native=... | consume=... | out_age=... | detail gpu_total=... sync_wait=... | tier ... | fire req=... | box samples=...
```

Add C++-specific fields:

```text
ctrl_loop
ctrl_pipeline
vigem_update
```

- [ ] **Step 4: Compare logs**

Run Python fallback and C++ runtime in similar scenes. The user should compare:

```text
out_age
consume
controller loop stability
fire gate counts
target authority mix
```

---

### Task 13: Add Native C++ Launch Script

**Files:**
- Create: `scripts/launch/gamepad_native_cpp_start.bat`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Add static test for launcher**

Append:

```python
    def test_native_cpp_launcher_exists(self):
        launcher = PROJECT_ROOT / "scripts" / "launch" / "gamepad_native_cpp_start.bat"
        self.assertTrue(launcher.exists())
        content = _read(launcher)
        self.assertIn("cod_native_runtime.exe", content)
        self.assertIn("--config", content)
        self.assertIn("config.toml", content)
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold.NativeCppRuntimeScaffoldTests.test_native_cpp_launcher_exists -v
```

Expected: FAIL until the launcher exists.

- [ ] **Step 3: Create launcher**

Create:

```bat
@echo off
setlocal
cd /d "%~dp0..\.."
set "EXE=native\vision_native\build\Release\cod_native_runtime.exe"
if not exist "%EXE%" (
  echo Native C++ runtime not found. Run tools\build_native_vision.ps1 first.
  exit /b 1
)
"%EXE%" --config config.toml --perf-log
endlocal
```

- [ ] **Step 4: Keep Python fallback**

Do not overwrite `scripts/launch/gamepad_start.bat` in this task. The user should have both launchers available.

---

### Task 14: Default Runtime Switch After User Acceptance

**Files:**
- Modify: `scripts/launch/gamepad_start.bat`
- Create or modify: `docs/project/NATIVE_CPP_RUNTIME.md`
- Test: `tests/test_native_cpp_runtime_scaffold.py`

- [ ] **Step 1: Wait for user acceptance**

Do not start this task until the user reports that C++ runtime gameplay is acceptable for:

```text
manual pass-through
ai_aim
recoil
ai_aim + recoil overlap
auto-fire gate
perf stability
```

- [ ] **Step 2: Add docs**

Create `docs/project/NATIVE_CPP_RUNTIME.md` with:

```markdown
# Native C++ Runtime

The live runtime can run as `cod_native_runtime.exe` from `scripts/launch/gamepad_native_cpp_start.bat`.

Python remains available for training, export, plots, recoil tooling, and fallback gameplay debugging.

Use the Python fallback launcher when comparing behavior or bisecting regressions.
```

- [ ] **Step 3: Switch default launcher**

Modify `scripts/launch/gamepad_start.bat` to call the C++ launcher by default and keep a clear fallback path to Python.

- [ ] **Step 4: Run final checks**

Run:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold -v
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected: tests and native build pass.

---

## Verification Matrix

Run these checks as milestones complete:

```powershell
py -3 -B -m unittest tests.test_native_cpp_runtime_scaffold -v
py -3 -B -m unittest tests.test_native_vision_scaffold tests.test_performance_tracker tests.test_native_vision_runner -v
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
powershell -ExecutionPolicy Bypass -File tools\run_native_vision_smoke.ps1
git diff --check
```

Manual gameplay验收 checkpoints:

```text
M1: C++ exe starts and exits cleanly
M2: manual pass-through works
M3: native vision feeds native controller without Python runtime
M4: auto-fire authority gate matches Python fallback
M5: ai_aim feel is close enough for live testing
M6: recoil profile playback matches Python fallback
M7: ai_aim + recoil overlap no longer has severe jitter
M8: C++ launcher can replace Python launcher
```

## Risks And Controls

- Risk: C++ runtime improves structure but changes tuned hand feel.
  - Control: keep Python fallback and stop for user gameplay验收 after ai_aim and recoil milestones.
- Risk: ViGEm C/C++ setup differs from Python `vgamepad`.
  - Control: make virtual gamepad backend isolated and test pass-through before enabling AI/recoil.
- Risk: config drift between Python and C++.
  - Control: keep `config.toml` field names and defaults aligned; add static tests for key sections.
- Risk: recoil profile semantics change.
  - Control: preserve timeline advancement, despike cache, target-aware yield, and raw JSON immutability.
- Risk: migration scope grows into mouse/runtime tooling.
  - Control: first production target is gamepad only; Python remains tooling/fallback.

## Self-Review

- Spec coverage: the plan covers entrypoint, config, native vision integration, controller loop, virtual gamepad output, auto-fire, ai_aim, aim dynamics, recoil, perf logging, launcher, verification, and fallback strategy.
- Open item scan: the plan avoids vague fill-in work and gives concrete file paths, expected tests, commands, and behavior gates.
- Type consistency: `RuntimeConfig`, `VisionRuntimeConfig`, `GamepadRuntimeConfig`, `NativeGamepadController`, `NativeControllerVisionState`, `PhysicalGamepadState`, and `GamepadOutputState` are named consistently across tasks.
- Scope control: mouse runtime migration is explicitly out of the first production target; Python remains available for tooling and fallback.
