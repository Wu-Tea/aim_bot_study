> 当前 dev 合并约定：runtime、Vision 和构建走 C++；GUI 沿用 dev 的 Python/Tk 配置工作室。新 C++ GUI 未合入。本文原生助手相关旧检查不作为当前要求，界面操作见 [桌面助手](DESKTOP_ASSISTANT.md)。

# Native C++ Runtime

This is the maintained reference for the default gamepad live path. The
runtime is one C++ process: `cod_native_runtime.exe`. The retained dev Python/Tk desktop workspace owns configuration editing and background process control. Python gameplay and vision fallback paths are retired.

## Launch

Default launcher:

```powershell
scripts\launch\gamepad_start.bat
```

Direct native launcher:

```powershell
scripts\launch\gamepad_native_cpp_start.bat
```

Direct executable and one-tick smoke:

```powershell
native\build\Release\cod_native_runtime.exe --config config.toml --perf-log
native\build\Release\cod_native_runtime.exe --config config.toml --perf-log --once
```

The desktop entry is `启动助手.vbs`. Native mouse tools remain separate; Python mouse and KBM-to-gamepad paths are retired. See [Desktop Assistant](DESKTOP_ASSISTANT.md).

## What the Process Owns

- native DXGI capture, CUDA preprocessing and TensorRT inference;
- native target selection and source authority;
- physical SDL/XInput gamepad input;
- one target plan and one manual/AI authority state machine;
- ADS acquisition, BodyLock follow and one dynamics shaper;
- AutoFire safety and physical-fire passthrough;
- recoil feed-forward;
- ViGEm Xbox 360 output;
- optional lightweight performance summaries and structured telemetry.

## Vision-to-Output Contract

```text
VisionEngine
  -> VisionService latest snapshot
  -> VisionDeliveryGate
  -> NativeGamepadController
       -> TargetCoordinator / TargetPlan
       -> ADS acquisition OR BodyLock follow
       -> AimDynamicsShaper
       -> AssistControlStateMachine
       -> AutoFire gate
       -> recoil feed-forward
  -> ViGEm
```

`VisionDeliveryGate` accepts only a unique, increasing and sufficiently recent
capture. A 1 kHz controller tick without a new capture may continue the last
immutable source-owned plan, but it does not create a projected detector
observation or renew target authority. A fresh no-target capture releases
generic aim authority.

The native selector publishes only current evidence:

- `observed`: strong direct person evidence; may grant aim and fire authority;
- `associated_weak` / `weak_observed`: direct but reduced evidence; aim only;
- `cue_hold`: visible same-generation cue continuation; bounded aim only;
- no target: no authority.

Unknown and retired `predicted/projected` source labels fail closed.

## Control Ownership

`TargetCoordinator` is the identity, lifecycle and mode owner. ADS and BodyLock
are mutually exclusive target-relative solvers. `AimDynamicsShaper` shapes the
chosen proposal once. `AssistControlStateMachine` then owns Track,
HandoverSeek, Capture and Manual and emits the one pre-recoil right-stick
command.

Manual input is intent evidence, not a separately protected additive force.
The state machine may use helpful single-target input, request an eligible
multi-target handover, or pass an axis through when AI is materially idle. No
legacy axis/vector fuser, carry/brake layer or benchmark-selectable output mode
exists in Release.

AutoFire owns only its synthetic contribution. Physical RB/RT is always passed
through. Recoil runs afterward as explicit feed-forward and must not own target
identity, generic continuity or selector policy.

## Removed Compatibility

The low-rate cleanup intentionally broke old native config/build compatibility:

- tracker backend/projection/coasting keys;
- legacy manual-preservation and alternate mix keys;
- W3/W5/pending/causal/rollout controls;
- the synchronous AimPerf logger and `VISION_AIM_PERF_*` aliases;
- benchmark-only legacy control binaries.

Unknown retired keys produce diagnostics instead of silently changing current
behavior. Historical code remains available from Git history, not from a
second archive tree in the build.

The complete rationale is in
[Legacy Control Stack Cleanup](LEGACY_CONTROL_STACK_CLEANUP_20260810.md).

## Recoil Boundary

Recoil is implemented behind
`native/recoil_native/RecoilCompensationPolicy`. Profile selection,
calibration, despike, fixed/adaptive fallback and playback remain explicit
recoil concerns. Any target-relative firing intent is applied to the current
target error before the single target solve; it is not raw manual-plus-AI
stacking.

Do not add target projection, tracker correction or another output hold inside
recoil. New coupling requires a reproduced incident and a native contract test.

## Telemetry

Lightweight normal-run statistics:

```toml
[runtime.performance]
enabled = true
interval_ms = 5000
directory = "runs/perf_summary"
stdout_enabled = false
```

Bounded structured diagnostics:

```toml
[runtime.telemetry]
enabled = true
directory = "runs/native_perf"
manual_controller_hz = 100
```

`runtime.telemetry.enabled` is the sole detailed-telemetry switch and
`runtime.telemetry.directory` is its sole root directory. When disabled, the
runtime does not start the structured writer/session pipeline. Do not use the
retired AimPerf environment variables.

## Build and Test

Repository helper:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_runtime.ps1
```

Direct CMake workflow:

```powershell
cmake -S native -B native\build
cmake --build native\build --config Release --parallel 8
ctest --test-dir native\build -C Release --output-on-failure
```

Pipeline check:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check_native_cpp_gamepad_runtime.ps1
```

## Acceptance Checklist

- full Release build and all current CTest targets pass;
- retained dev GUI configuration/workspace/IPC checks pass;
- physical input remains intact and process exit sends neutral ViGEm output;
- no-target and unknown-source input fail closed;
- multi-target flick handover and centered capture contracts remain green;
- AutoFire never gains authority from weak/cue/no-target evidence;
- live validation uses the same executable, model, config, game cadence and
  logging conditions for both cohorts;
- synthetic smoke PASS is not described as a live performance win.

## Supported Native Vision Build

The native build targets CUDA 13.x, TensorRT 10.x and SM 7.5 or newer. Existing
compatible TensorRT `.engine` files load directly. Pascal/GTX 1060 deployment
is outside the supported scope.
