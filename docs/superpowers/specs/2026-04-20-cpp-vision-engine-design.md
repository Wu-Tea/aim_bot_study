# C++ Vision Engine Design

**Goal:** Replace the current Python `vision` runtime with a C++ TensorRT-based vision engine while preserving the existing Python controller boundary and assisted-play behavior.

**Scope:**
- Move the production `vision` path to a native C++ module loaded from Python.
- Keep the controller layer in Python and preserve the current `BaseController` contract.
- Reuse the current `models/best.engine` deployment artifact as the primary inference format.
- Preserve current behavior for capture cadence, target selection, occlusion compensation, enhancement, and auto-fire recommendation.
- Keep the current Python vision runtime available as a fallback during migration.

**Non-goals:**
- No rewrite of `controllers/*`.
- No attempt to make the first C++ runtime support `best.pt` or Ultralytics fallback.
- No migration of debug overlay or debug frame capture in the first pass.
- No separate C++ worker process or IPC-based architecture.
- No model retraining, detector architecture changes, or benchmark target promises beyond parity-first validation.

## Problem

The current Python vision stack has already been optimized substantially:

- latest-only capture plus inference threading
- ROI-based DXGI capture
- TensorRT `best.engine` fast path
- lazy frame materialization and ROI-only color reads
- short-horizon occlusion compensation

Even after those changes, the hot path still crosses Python-managed boundaries in places where latency and GPU contention matter:

1. capture metadata and lazy frame ownership still originate in Python
2. the CPU fast preprocessor still performs:
   - `torch.from_numpy(...)`
   - CPU-to-GPU upload
   - HWC-to-CHW conversion
   - dtype conversion
   - `/255.0`
3. detector output decoding, color classification, target selection, compensation, and enhancement still execute inside Python object graphs
4. the current "native" path is only scaffolded through `vision/native_fastpath.py`; the actual `vision_native` module does not exist yet

The user wants a cleaner final architecture:

> keep `controller` in Python, but make `vision` a native C++ implementation

That means the migration target is not merely "preprocess in C++." The desired end state is:

- C++ owns capture, inference, targeting, and fire recommendation
- Python owns controller input/output and process-level orchestration

## Current Boundary

Today the runtime boundary already looks like this:

- Python `vision` computes:
  - selected target
  - target delta
  - auto-fire recommendation
- Python `controller` consumes:
  - `is_aiming()`
  - `update(dx, dy, target=ControllerTarget | None)`
  - `set_auto_fire(bool)`
  - `reset()`

This boundary is already narrow enough to make a full C++ `vision` engine practical without redesigning controller code.

## Approaches Considered

### 1. Native preprocess only, keep Python targeting and runner

Implement `vision_native.prepare_into_tensor(...)` and keep the rest of the runtime in Python.

Pros:
- lowest implementation cost
- directly addresses the `GPU -> CPU -> GPU` copy path
- keeps most tests and code structure unchanged

Cons:
- does not achieve the desired final architecture
- leaves targeting, occlusion compensation, enhancement, and autofire state split across Python
- preserves Python-side lifecycle and state-machine complexity

### 2. Full C++ vision engine loaded in-process from Python

Implement a native module, for example `vision_native.pyd`, that owns:

- capture
- preprocess
- TensorRT inference
- detection decode
- target selection
- occlusion compensation
- enhancement
- autofire gate
- perf accounting

Python only interacts with that engine through a narrow result API.

Pros:
- clean final architecture
- keeps controller code unchanged
- removes most Python overhead from the vision hot path
- avoids IPC overhead and cross-process timing jitter

Cons:
- larger initial implementation scope
- requires careful parity validation against the current Python runtime

### 3. C++ vision as a separate process with IPC to Python controller

Pros:
- process isolation
- easier independent crash containment

Cons:
- extra serialization and queueing cost
- more failure modes
- worse fit for low-latency game input flow
- harder to debug and coordinate

## Chosen Design

Choose **Approach 2**.

The new architecture will be:

- **Python**
  - process startup
  - environment/config parsing
  - controller lifecycle
  - `is_aiming()` polling
  - controller actuation
- **C++**
  - DXGI capture
  - GPU-resident preprocessing
  - TensorRT `best.engine`
  - output decode
  - color classification
  - target selection
  - short-horizon occlusion reconstruction and prediction
  - aim enhancement
  - autofire gating
  - performance accounting

This gives a single clear rule:

> Python asks whether the player is aiming and applies the final controller output. C++ decides everything else in the vision chain.

## Runtime Shape

The native module will export a long-lived engine object:

- `VisionEngine`

Python creates it once and keeps it warm across ADS sessions, just like the current threaded Python runtime keeps the model warm.

### Python-side runtime flow

The new Python orchestration should become:

1. create the controller
2. create `VisionEngine(config)`
3. start the engine
4. in the main loop:
   - read `controller.is_aiming()`
   - pass aiming state to the engine
   - poll the latest vision result
   - apply `controller.update(...)` or `controller.reset()`
   - apply `controller.set_auto_fire(...)`
5. stop engine and controller on shutdown

Python will no longer:

- own capture threads
- own inference threads
- own detection decode
- own target selection state

## Python/C++ Communication Contract

Communication must stay small and result-oriented.

### Python -> C++

Required calls:

- `start()`
- `stop()`
- `set_aiming(bool)`
- `poll_latest(timeout_ms: int) -> VisionResult | None`

Optional future calls:

- `reset()`
- `get_perf_snapshot()`
- `set_capture_fps(active_fps, idle_fps)`

### C++ -> Python

The engine returns a compact `VisionResult` payload. The first version should include:

- `frame_id`
- `captured_at_ns`
- `inferred_at_ns`
- `has_target`
- `has_delta`
- `auto_fire`
- `dx`
- `dy`
- `aim_point_x`
- `aim_point_y`
- `screen_center_x`
- `screen_center_y`
- `has_body_box`
- `body_x1`
- `body_y1`
- `body_x2`
- `body_y2`
- `target_source`
- `wait_ms`
- `infer_ms`
- `post_ms`
- `age_ms`
- `boxes_seen`

Python converts the body box fields into the existing `ControllerTarget` dataclass and forwards them to the controller.

### Important rule

The C++ engine must not return image tensors or detection tensors to Python in the hot path.

If Python keeps receiving full frames, the migration has failed architecturally even if inference moved to C++.

## Native Module Form

The engine should be shipped as an in-process Python extension:

- `vision_native.pyd`

Recommended implementation stack:

- `pybind11`
- D3D11 Desktop Duplication
- CUDA
- TensorRT
- MSVC / CMake on Windows

This is preferred over embedding Python into a standalone C++ app because:

- the controller is already Python
- the current project and scripts are already Python-first
- the in-process boundary is smaller and lower-latency than IPC

## C++ Module Responsibilities

### `capture`

Responsibilities:

- own Desktop Duplication setup
- capture a centered ROI directly
- maintain latest-only capture semantics
- expose GPU texture handles to the preprocess stage
- support low idle capture cadence and high ADS capture cadence

### `preprocess`

Responsibilities:

- convert BGRA ROI texture into model input
- do color conversion, layout conversion, normalization, and dtype conversion on GPU
- write directly into TensorRT input memory

The goal is to remove:

- `torch.from_numpy(...)`
- CPU staging for the main detector path
- Python-side HWC-to-CHW conversion

### `infer`

Responsibilities:

- load and own `models/best.engine`
- manage bindings, CUDA stream usage, and enqueue
- decode the current model output contract

The first version should support only the currently deployed engine output format used by the project.

### `targeting`

Responsibilities:

- implement the current `TargetSelector` behavior
- preserve current pickup, hold, and switch confirmation rules
- preserve current friendly filtering and enemy color bonuses
- preserve current `fire_zone` / `slow_zone` semantics

### `occlusion compensation`

Responsibilities:

- preserve the current `observed / reconstructed / predicted` target-source model
- preserve the current short reconstruction and prediction budget

### `enhancement`

Responsibilities:

- preserve current `LeadPredictor`
- preserve current `CatchupBoost`
- preserve current `NearTargetDamping`

### `runtime`

Responsibilities:

- own threads and state transitions
- own `set_aiming(...)` behavior
- own result publication and freshness metrics
- ensure no stale results leak across ADS transitions

## Thread Model

The first C++ engine should use two worker threads:

1. `CaptureThread`
2. `InferenceThread`

The main Python thread remains the controller host and engine consumer.

### Why only two native threads

This matches the current optimized Python design and keeps the first native runtime easier to reason about:

- capture publishes latest-only frame state
- inference consumes the latest frame and produces the latest result

Targeting, compensation, and enhancement should run in the inference/result stage rather than on a third coordination thread. That keeps all vision state in one place.

## Configuration Model

The first native engine should accept a config structure aligned with current `VisionConfig`:

- `capture_width`
- `capture_height`
- `capture_fps`
- `idle_capture_fps`
- `conf`
- `half`
- `device`
- `frame_timeout`
- `perf_log_interval`

Deferred from the first native engine:

- `best.pt` fallback
- debug overlay flags
- debug save options

The Python runtime should continue to read environment variables and CLI flags, then build the native config object from them.

## Compatibility Strategy

The migration must preserve an escape hatch.

### Old path

Keep the current Python runtime available:

- as fallback when `vision_native` fails to load
- as a benchmark comparison baseline
- as a behavior oracle during parity testing

### New path

Add a runtime selector, for example:

- `VISION_BACKEND=python`
- `VISION_BACKEND=native`

The exact env name can be chosen later, but the design requires an explicit switch during rollout.

## Phased Implementation

### Phase 1: Native hot path with Python targeting retained

Goal:
- prove native capture plus TensorRT integration
- establish the extension-module build and packaging path

Scope:
- native DXGI capture
- native preprocess
- native TensorRT inference
- native decode
- Python continues to own targeting, occlusion compensation, enhancement, and autofire

Why this phase exists:
- it validates the toolchain and engine loading path first
- it gives a lower-risk checkpoint before the full vision migration

### Phase 2: Full native vision logic

Goal:
- move the rest of production vision state out of Python

Scope:
- native color classification
- native `TargetSelector` parity
- native occlusion compensation
- native enhancement pipeline
- native autofire gate
- native perf accounting

At the end of Phase 2, Python should only receive `VisionResult`.

### Phase 3: Default-path switch

Goal:
- make native the default runtime

Scope:
- native runtime becomes the default under the normal gamepad startup path
- Python vision path remains fallback only
- benchmark and real-play verification determine whether the old path can eventually be retired

## Testing Strategy

The migration must be parity-first, not benchmark-first.

### Python-level integration tests

Add tests that validate:

1. Python can create and destroy `VisionEngine`
2. aiming transitions do not leak stale results
3. `poll_latest()` returns the expected result contract
4. controller handoff still receives valid `ControllerTarget` values

### Native parity tests

Build a deterministic parity harness comparing Python and C++ outputs for the same synthetic detections or recorded intermediate cases.

Required parity targets:

- selected target identity
- target point
- `slow_zone`
- `fire_zone`
- target source
- `dx / dy`
- auto-fire decision

### Benchmark validation

After parity is acceptable, benchmark:

- idle CPU usage
- ADS loop `age_ms`
- ADS loop `infer_ms`
- game FPS impact

Success should be evaluated on:

- lower `age_ms`
- reduced Python CPU pressure
- stable controller behavior

not merely on raw loop FPS.

## Risks

### Risk: behavior drift versus the tuned Python targeting path

This is the biggest migration risk.

Mitigation:

- treat current Python tests as behavior specs
- build parity fixtures before switching the default path
- migrate logic in the same conceptual order as the current Python pipeline

### Risk: toolchain friction on Windows

Mitigation:

- phase the work
- validate CMake, TensorRT, CUDA, and pybind11 early
- keep the Python fallback path live until the native build is routine

### Risk: engine-only deployment is less flexible than the current Python fallback

Mitigation:

- explicitly keep the Python `best.pt` fallback path for rollout
- make engine loading failures fall back cleanly rather than block startup

### Risk: over-scoping the first milestone

Mitigation:

- use Phase 1 as a real checkpoint
- do not bundle debug features into the first native engine
- do not bundle controller changes into the native migration

## Validation Criteria

This design is successful only if all are true:

1. the Python controller API remains unchanged
2. the native engine can run the current `best.engine`
3. the native runtime does not require frame transfer back into Python
4. targeting and controller behavior remain within parity tolerance
5. ADS transitions remain warm and do not cold-start inference
6. the native path improves or stabilizes latency metrics relative to the Python baseline

## Deferred Work

Explicitly deferred from this design:

- debug overlay parity
- debug frame-save parity
- ONNX or LibTorch runtime alternatives
- standalone C++ app mode
- controller-side logic migration
- removal of the Python fallback path

## Recommendation

The project should proceed with **Phase 1 first**, not jump straight into a complete rewrite without checkpointing.

That recommendation is not a retreat from the full C++ vision goal. It is the shortest path to a stable final architecture:

1. prove native capture plus TensorRT works cleanly in-process
2. then migrate the tuned vision logic with parity tests in place
3. only then make native the default runtime
