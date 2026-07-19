# Native Vision 480x416 Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore `480x416` and its matching TensorRT engine as the canonical live native-vision default without changing synthetic benchmark coordinate systems.

**Architecture:** Treat `VisionRuntimeConfig` as the compiled fallback contract and the tracked example as the config-generation contract. Keep `VisionEngine`'s direct fallback aligned with both, while allowing explicit user values to override all defaults.

**Tech Stack:** C++17, TOML-like native config loader, CMake/MSBuild, PowerShell verification.

---

### Task 1: Lock the default and template contract with tests

**Files:**
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Test: `native/controller_native/runtime_config_tests.cpp`

- [ ] **Step 1: Write the failing default-contract test**

Add assertions to `test_vision_gpu_service_defaults_are_enabled()`:

```cpp
require(config.vision.capture_width == 480);
require(config.vision.capture_height == 416);
require(config.vision.model_path ==
    "models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine");
```

Add assertions to `test_normal_template_preserves_controller_baseline()`:

```cpp
require(config.vision.capture_width == 480);
require(config.vision.capture_height == 416);
require(config.vision.model_path ==
    "models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine");
require(std::filesystem::exists(config.vision.model_path));
```

- [ ] **Step 2: Run the test and witness RED**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime_config_tests
native/vision_native/build/Release/cod_native_runtime_config_tests.exe
```

Expected: process exits non-zero because current defaults and template resolve to `640x512`.

### Task 2: Restore the four runtime sources of truth

**Files:**
- Modify: `config.toml`
- Modify: `config.native.example.toml`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/vision_native/src/vision_engine.cpp`

- [ ] **Step 1: Update local and tracked configuration**

Set both configuration files to:

```toml
capture_width = 480
capture_height = 416
model_path = "models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine"
```

- [ ] **Step 2: Update compiled runtime defaults**

Set `VisionRuntimeConfig` defaults to:

```cpp
int capture_width = 480;
int capture_height = 416;
std::string model_path =
    "models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine";
```

Set `VisionEngine`'s `kDefaultEnginePath` to the same engine path.

- [ ] **Step 3: Run focused tests and witness GREEN**

Run the same runtime-config test command. Expected: exit code `0`.

- [ ] **Step 4: Verify explicit alternate configuration still wins**

Keep `test_vision_gpu_service_config_values_parse()` and existing parser tests green; the loader must continue honoring explicit `capture_width`, `capture_height`, and `model_path` assignments.

### Task 3: Build and inspect the live runtime contract

**Files:**
- Verify: `native/vision_native/build/Release/cod_native_runtime.exe`

- [ ] **Step 1: Build the native runtime**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime
```

Expected: build succeeds and produces the canonical runtime executable.

- [ ] **Step 2: Verify all live boundaries agree**

Search only the four runtime sources and confirm each contains `480`, `416`, and the `480x416` engine. Confirm the engine exists on disk. Do not treat `640x512` benchmark fixtures as failures.

- [ ] **Step 3: Check diff and commit**

```powershell
git diff --check
git add config.native.example.toml native/controller_native/runtime_config.h native/controller_native/runtime_config_tests.cpp native/vision_native/src/vision_engine.cpp
git commit -m "fix: restore native vision 480x416 contract"
```

`config.toml` is intentionally untracked; verify its contents separately rather than forcing it into Git.
