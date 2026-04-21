# Native Vision Phase 3 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Phase 3 foundation by introducing a native `VisionEngine` / `VisionResult` boundary and a standalone `vision_native_debug` executable without switching the production Python runner.

**Architecture:** Keep Python `vision.runner` unchanged for now, but define the native runtime boundary in C++ so later targeting and parity work can accumulate behind one stable API. The first foundation milestone is allowed to be behavior-light as long as the public contracts, build targets, pybind entry points, and debug harness are real and verified.

**Tech Stack:** C++, pybind11, D3D11 Desktop Duplication, TensorRT, CUDA, Python unittest, PowerShell build/smoke scripts

## Status Update

- Phase 3 foundation is complete.
- Follow-on Phase 3A is also complete: `VisionEngine` now bridges native DXGI ROI capture into native TensorRT inference through CUDA D3D11 interop.
- Phase 3B has started: native target selection now exists as its own C++ component and is wired into `VisionEngine`.
- Native friendly/enemy color classification is now also wired into the Phase 3B selector path.
- Native occlusion compensation is now wired into the Phase 3B selector path, including partial-box reconstruction, two-frame short prediction, and `observed` / `reconstructed` / `predicted` source parity.
- Native auto-fire gating is now wired into the Phase 3B path with selected-target `fire_zone` checks and release grace.
- Native aim enhancement is now wired into `VisionEngine`, including lead prediction, catchup boost, and near-target damping.
- The debug executable now prints real capture/inference timing instead of placeholder-only `VisionResult` fields.
- Remaining migration work is no longer "make the engine real"; it is "validate native parity/perf against Python, optimize the current host-side color sampling path if needed, then plan Phase 4 startup integration."

---

## File Map

- Modify: `docs/project/NATIVE_VISION.md`
- Modify: `docs/superpowers/specs/2026-04-20-cpp-vision-engine-design.md`
- Modify: `native/vision_native/CMakeLists.txt`
- Modify: `native/vision_native/include/vision_native/types.h`
- Modify: `native/vision_native/src/vision_native_module.cpp`
- Create: `native/vision_native/include/vision_native/vision_engine.h`
- Create: `native/vision_native/src/vision_engine.cpp`
- Create: `native/vision_native/src/vision_debug_main.cpp`
- Create: `tools/run_native_vision_debug.ps1`
- Modify: `tests/test_native_vision_scaffold.py`

### Task 1: Lock the Phase 3 scope in tests

**Files:**
- Modify: `tests/test_native_vision_scaffold.py`

- [ ] **Step 1: Write the failing test**

Add assertions that require:

- `VisionResult` to exist in native source
- `VisionEngine` to exist in native source
- `vision_debug_main.cpp` to be declared in CMake
- `run_native_vision_debug.ps1` to exist and reference `vision_native_debug`
- pybind to expose `NativeVisionEngine`

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: FAIL because the Phase 3 symbols and debug harness do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create the smallest native API surface necessary to satisfy the new contract:

- `VisionResult` struct in `types.h`
- `VisionEngine` class declaration/definition
- `vision_native_debug` executable target
- pybind exposure for `NativeVisionEngine`

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tests/test_native_vision_scaffold.py native/vision_native/CMakeLists.txt native/vision_native/include/vision_native/types.h native/vision_native/include/vision_native/vision_engine.h native/vision_native/src/vision_engine.cpp native/vision_native/src/vision_debug_main.cpp native/vision_native/src/vision_native_module.cpp tools/run_native_vision_debug.ps1 docs/project/NATIVE_VISION.md docs/superpowers/specs/2026-04-20-cpp-vision-engine-design.md
git commit -m "Add Phase 3 native vision engine foundation"
```

### Task 2: Add the native engine boundary

**Files:**
- Modify: `native/vision_native/include/vision_native/types.h`
- Create: `native/vision_native/include/vision_native/vision_engine.h`
- Create: `native/vision_native/src/vision_engine.cpp`

- [ ] **Step 1: Write the failing test**

Require `VisionResult` fields for:

- frame ids and timestamps
- target booleans
- dx/dy
- body box fields
- perf timing fields

Require `VisionEngine` methods for:

- constructor
- `set_aiming(bool)`
- `poll_once()`
- `reset()`

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: FAIL with missing `VisionResult` or `VisionEngine` symbols.

- [ ] **Step 3: Write minimal implementation**

Implement `VisionEngine` as a long-lived native object that:

- owns capture/inference dependencies
- exposes a `poll_once()` method returning `VisionResult`
- returns a safe empty result while targeting parity is still unfinished

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add native/vision_native/include/vision_native/types.h native/vision_native/include/vision_native/vision_engine.h native/vision_native/src/vision_engine.cpp tests/test_native_vision_scaffold.py
git commit -m "Add native VisionEngine result boundary"
```

### Task 3: Add the standalone debug executable

**Files:**
- Modify: `native/vision_native/CMakeLists.txt`
- Create: `native/vision_native/src/vision_debug_main.cpp`
- Create: `tools/run_native_vision_debug.ps1`

- [ ] **Step 1: Write the failing test**

Require:

- CMake target `vision_native_debug`
- script `run_native_vision_debug.ps1`
- script output to mention `VisionResult` fields or perf fields

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: FAIL because the executable target and script do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Implement a standalone executable that:

- creates `VisionEngine`
- optionally sets aiming on
- polls several iterations
- prints a compact debug line containing `has_target`, `dx`, `dy`, `auto_fire`, `wait_ms`, `infer_ms`, `post_ms`, `age_ms`, and `boxes_seen`

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add native/vision_native/CMakeLists.txt native/vision_native/src/vision_debug_main.cpp tools/run_native_vision_debug.ps1 tests/test_native_vision_scaffold.py
git commit -m "Add native vision debug executable"
```

### Task 4: Expose the foundation through pybind

**Files:**
- Modify: `native/vision_native/src/vision_native_module.cpp`

- [ ] **Step 1: Write the failing test**

Require pybind symbols:

- `NativeVisionEngine`
- `poll_once`
- `set_aiming`
- `reset`

- [ ] **Step 2: Run test to verify it fails**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: FAIL because pybind has not exposed the engine yet.

- [ ] **Step 3: Write minimal implementation**

Expose `VisionEngine` and serialize `VisionResult` into a Python dict so Python can inspect the native result contract without touching the production runner.

- [ ] **Step 4: Run test to verify it passes**

Run: `py -3 -m unittest tests.test_native_vision_scaffold -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add native/vision_native/src/vision_native_module.cpp tests/test_native_vision_scaffold.py
git commit -m "Expose native vision engine through pybind"
```

### Task 5: Build and verify the foundation

**Files:**
- Verify: `native/vision_native/build/...`
- Verify: `tools/run_native_vision_smoke.ps1`
- Verify: `tools/run_native_vision_infer_smoke.ps1`
- Verify: `tools/run_native_vision_capture_smoke.ps1`
- Verify: `tools/run_native_vision_debug.ps1`

- [ ] **Step 1: Run Python regression**

Run: `py -3 -m unittest tests.test_startup_scripts tests.test_native_vision_scaffold tests.test_vision_capture tests.test_vision_fastpath tests.test_vision_inference tests.test_vision_runner tests.test_vision_runner_config tests.test_vision_targeting -v`
Expected: PASS

- [ ] **Step 2: Run native build**

Run: `powershell.exe -ExecutionPolicy Bypass -File .\tools\build_native_vision.ps1`
Expected: build succeeds and produces `vision_native_debug.exe`

- [ ] **Step 3: Run native smokes**

Run:

- `powershell.exe -ExecutionPolicy Bypass -File .\tools\run_native_vision_smoke.ps1`
- `powershell.exe -ExecutionPolicy Bypass -File .\tools\run_native_vision_infer_smoke.ps1`
- `powershell.exe -ExecutionPolicy Bypass -File .\tools\run_native_vision_capture_smoke.ps1`
- `powershell.exe -ExecutionPolicy Bypass -File .\tools\run_native_vision_debug.ps1`

Expected:

- engine inspect succeeds
- inference smoke succeeds
- capture smoke succeeds under normal desktop permissions
- debug executable prints `VisionResult`/perf fields without touching the production runner

- [ ] **Step 4: Commit**

```bash
git add docs/project/NATIVE_VISION.md docs/superpowers/specs/2026-04-20-cpp-vision-engine-design.md docs/superpowers/plans/2026-04-21-native-vision-phase3-foundation.md
git commit -m "Document native vision phase 3 foundation"
```
