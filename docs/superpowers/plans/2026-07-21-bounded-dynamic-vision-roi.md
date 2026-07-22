# Bounded Dynamic Vision ROI Implementation Plan

> Superseded for execution by the staged plans derived from `docs/superpowers/specs/2026-07-22-bounded-dynamic-roi-master-design.md`. Retained as historical planning evidence; do not execute this file directly.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded 480x416 capture-offset planner that protects an already committed edge target, preserves stable aim coordinates, resets on every ADS epoch, and proves its value with independently corrected video replay plus a deterministic closed-loop benchmark.

**Architecture:** `CaptureRoiPlanner` is the only new stateful owner. It consumes the previous committed `TargetPlan`, the latest stable body geometry and user intent, computes a minimal offset inside a finite sniffing envelope, and supplies that offset to `DxgiRoiCapture` for the next frame. Detector/color work remains ROI-local; detections are translated into the existing centered stable coordinate system before selector/tracker/controller use. Raw video replay validates vision coverage, while a separate closed-loop scenario validates tracker/controller interaction.

**Tech Stack:** C++17, DXGI Desktop Duplication, D3D11, CUDA/TensorRT, pybind11, Python 3.11, OpenCV, JSON artifacts, CMake/Visual Studio Release builds.

---

## File and ownership map

**New production units**

- `native/vision_native/include/vision_native/capture_roi_planner.h`: planner input/config/output contract and single stateful owner.
- `native/vision_native/src/capture_roi_planner.cpp`: bounded sniffing, ADS reset, prediction, intent speed scaling and rate limiting.
- `native/vision_native/include/vision_native/roi_coordinates.h`: stateless ROI clamp and local/stable coordinate transforms.
- `native/vision_native/src/roi_coordinates.cpp`: transform implementation shared by runtime and tests.
- `native/pipeline_contract/capture_roi_guidance.h`: trivially copyable controller-to-vision guidance snapshot.

**Existing production files changed**

- `native/vision_native/include/vision_native/dxgi_capture.h`
- `native/vision_native/src/dxgi_capture.cpp`
- `native/vision_native/include/vision_native/types.h`
- `native/vision_native/include/vision_native/vision_engine.h`
- `native/vision_native/src/vision_engine.cpp`
- `native/vision_native/include/vision_native/target_selector.h`
- `native/vision_native/src/target_selector.cpp`
- `native/runtime_app/vision_service.h`
- `native/runtime_app/vision_service.cpp`
- `native/runtime_app/runtime_loop.h`
- `native/runtime_app/runtime_loop.cpp`
- `native/controller_native/runtime_config.h`
- `native/controller_native/runtime_config.cpp`
- `native/runtime_app/telemetry_collectors.h`
- `native/runtime_app/telemetry_collectors.cpp`
- `native/runtime_app/telemetry_schema.h`
- `native/runtime_app/runtime_telemetry.cpp`
- `native/vision_native/src/vision_native_module.cpp`
- `native/vision_native/CMakeLists.txt`
- `config.native.example.toml`

**Benchmark and fixture files**

- `tools/dynamic_roi_fixture.py`: fixture initialization, validation and annotation interpolation.
- `tools/benchmark_dynamic_roi_video.py`: full-frame fixed/dynamic replay using the production engine, selector and planner bindings.
- `tests/test_dynamic_roi_fixture.py`
- `tests/test_dynamic_roi_video_benchmark.py`
- `benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json`
- `native/controller_native/dynamic_roi_closed_loop_benchmark.cpp`
- `native/controller_native/dynamic_roi_closed_loop_benchmark_tests.cpp`
- `artifacts/benchmarks/dynamic_roi/20260721_acceptance.json`: retained final evidence, created only after all gates pass.

## Task 1: Establish the independently corrected video fixture

**Files:**
- Create: `tools/dynamic_roi_fixture.py`
- Create: `tests/test_dynamic_roi_fixture.py`
- Create: `benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json`

- [ ] **Step 1: Write failing fixture-schema tests**

Define tests for source hashing, full-screen coordinate bounds, strictly increasing frame IDs, unique target identity and interpolation that never crosses an explicit occlusion boundary:

```python
def test_fixture_rejects_model_output_without_review():
    fixture = minimal_fixture(reviewed=False)
    with pytest.raises(ValueError, match="independently reviewed"):
        validate_fixture(fixture, require_reviewed=True)

def test_interpolation_stops_at_occlusion_transition():
    frames = interpolate_annotations(
        keyframes=[visible_keyframe(48), occluded_keyframe(54)],
        first_frame=48,
        last_frame=54,
    )
    assert [row["frame_id"] for row in frames] == [48, 54]
```

- [ ] **Step 2: Run the fixture tests and confirm failure**

Run:

```powershell
D:\env\python\python.exe -m pytest tests/test_dynamic_roi_fixture.py -q
```

Expected: collection fails because `tools.dynamic_roi_fixture` does not exist.

- [ ] **Step 3: Implement the fixture schema and validator**

Use this durable top-level shape:

```python
REQUIRED_RANGES = {
    "first_partial_target": (0.70, 1.50),
    "second_edge_crossing": (3.60, 4.60),
}

def validate_fixture(data: dict, require_reviewed: bool = True) -> None:
    if data["schema_version"] != 1:
        raise ValueError("unsupported schema_version")
    if require_reviewed and data["review"]["status"] != "independently_reviewed":
        raise ValueError("fixture must be independently reviewed")
    width, height = data["source"]["width"], data["source"]["height"]
    for episode in data["episodes"]:
        previous = -1
        for row in episode["annotations"]:
            if row["frame_id"] <= previous:
                raise ValueError("frame_id must be strictly increasing")
            previous = row["frame_id"]
            validate_box(row["visible_body_box_screen"], width, height)
            validate_box(row["protected_upper_body_box_screen"], width, height)
            validate_point(row["chest_point_screen"], width, height)
```

The initializer computes the video SHA-256 and writes source metadata, but stores only the source filename rather than the user's personal absolute path. It sets review status to `generated_unreviewed` and must never silently promote a fixture to reviewed.

- [ ] **Step 4: Generate engine-assisted proposals for only the two accepted ranges**

Run:

```powershell
D:\env\python\python.exe tools/dynamic_roi_fixture.py init `
  --video "C:\Users\Administrator\Videos\NVIDIA\Call of Duty Black Ops 7\Call of Duty Black Ops 7 2026.07.21 - 15.47.31.01.mp4" `
  --output benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json `
  --ranges first_partial_target=0.70:1.50 second_edge_crossing=3.60:4.60 `
  --keyframe-stride 4
```

Expected: a schema-valid but `generated_unreviewed` fixture plus a contact sheet under `runs/dynamic_roi_fixture/`.

- [ ] **Step 5: Correct the keyframes and mark the fixture reviewed**

For every boundary, visibility and identity transition, inspect the full-screen frame and correct these full-screen fields: `target_id`, `visible_body_box_screen`, `protected_upper_body_box_screen`, `chest_point_screen`, `visibility`, and `occluded`. Mark `review.status` as `independently_reviewed` only after the contact-sheet review. Do not copy the current engine box unchanged on a clipped frame.

- [ ] **Step 6: Validate the retained fixture**

Run:

```powershell
D:\env\python\python.exe tools/dynamic_roi_fixture.py validate `
  --fixture benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json `
  --require-reviewed
```

Expected: `fixture valid`, source hash present, both ranges present, no invalid boxes or identity gaps.

- [ ] **Step 7: Commit the fixture contract**

```powershell
git add tools/dynamic_roi_fixture.py tests/test_dynamic_roi_fixture.py benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json
git commit -m "test: add reviewed dynamic ROI video fixture"
```

## Task 2: Produce the fixed-ROI video baseline before production changes

**Files:**
- Create: `tools/benchmark_dynamic_roi_video.py`
- Create: `tests/test_dynamic_roi_video_benchmark.py`

- [ ] **Step 1: Write failing metric tests**

Cover visible recall, complete misses, consecutive miss duration, protected-region clipping, chest-point error, identity switches and exact-zero offset in the first episode:

```python
def test_consecutive_miss_uses_video_timestamps():
    rows = [row(0.000, True), row(0.016, False), row(0.033, False), row(0.050, True)]
    metrics = summarize_episode(rows)
    assert metrics["max_consecutive_miss_ms"] == pytest.approx(34.0, abs=1.0)

def test_non_edge_episode_requires_zero_offset():
    rows = [row(0.0, True, offset=(0, 0)), row(0.016, True, offset=(1, 0))]
    metrics = summarize_episode(rows)
    assert metrics["nonzero_offset_frames"] == 1
```

- [ ] **Step 2: Run tests and confirm failure**

```powershell
D:\env\python\python.exe -m pytest tests/test_dynamic_roi_video_benchmark.py -q
```

Expected: import failure for `tools.benchmark_dynamic_roi_video`.

- [ ] **Step 3: Implement fixed replay and JSON provenance**

The tool must:

- verify source SHA-256 against the fixture;
- crop the exact centered 480x416 from every source frame;
- call the current `vision_native_cpp.NativeEngine` at an explicit command-line decode floor;
- pass decoded boxes through `NativeTargetSelector`;
- compare the committed target with full-screen annotations;
- write revision, engine SHA-256, config fingerprint, fixture version and per-frame evidence.

Use one record per video frame:

```python
record = {
    "frame_id": frame_id,
    "time_seconds": time_seconds,
    "roi_offset": [0, 0],
    "target_visible": truth["visibility"] != "occluded",
    "selected_target_id": selected_target_id,
    "confidence": confidence,
    "protected_clip_ratio": clip_ratio,
    "chest_error_px": chest_error,
}
```

- [ ] **Step 4: Run Python tests**

```powershell
D:\env\python\python.exe -m pytest tests/test_dynamic_roi_fixture.py tests/test_dynamic_roi_video_benchmark.py -q
```

Expected: all pass.

- [ ] **Step 5: Run and retain the pre-change baseline**

```powershell
$env:PATH='D:\env\TensorRT-10.15.1.29\bin;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin;' + $env:PATH
$env:PYTHONPATH='D:\work\AI\yolo-study-001\native\vision_native\build\Release'
D:\env\python\python.exe tools/benchmark_dynamic_roi_video.py `
  --mode fixed `
  --decode-floor 0.40 `
  --video "C:\Users\Administrator\Videos\NVIDIA\Call of Duty Black Ops 7\Call of Duty Black Ops 7 2026.07.21 - 15.47.31.01.mp4" `
  --fixture benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json `
  --engine models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine `
  --output runs/dynamic_roi_acceptance/fixed_decode_040_before.json
```

Expected: both episodes reported separately; first-episode nonzero offset is zero; baseline artifact includes exact revision/config/engine identity. This is the immutable current-production baseline. Later runs add `fixed + 0.20` as a separate ablation rather than overwriting it.

- [ ] **Step 6: Commit the baseline tool**

```powershell
git add tools/benchmark_dynamic_roi_video.py tests/test_dynamic_roi_video_benchmark.py
git commit -m "test: add fixed ROI video replay baseline"
```

## Task 3: Implement the planner as an isolated pure component

**Files:**
- Create: `native/vision_native/include/vision_native/capture_roi_planner.h`
- Create: `native/vision_native/src/capture_roi_planner.cpp`
- Create: `native/vision_native/src/capture_roi_planner_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Define the planner contract and failing tests**

Use this public contract:

```cpp
struct CaptureRoiPlannerConfig {
    bool enabled = true;
    float sniff_scale = 1.50f;
    float prediction_ms = 50.0f;
    float max_speed_px_per_sec = 1200.0f;
    float intent_speed_bonus = 0.25f;
    float edge_margin_x_px = 56.0f;
    float edge_margin_y_px = 48.0f;
};

struct CaptureRoiPlannerInput {
    bool controller_aiming = false;
    std::uint64_t ads_epoch = 0;
    bool has_committed_target = false;
    std::uint64_t target_id = 0;
    common_native::Box2f stable_body_box_px{};
    common_native::Vec2f stable_predicted_aim_px{};
    common_native::Vec2f velocity_px_per_sec{};
    common_native::Vec2f user_intent_direction{};
    float user_intent_strength = 0.0f;
    float guidance_age_ms = 0.0f;
    float dt_seconds = 0.0f;
};

struct CaptureRoiPlannerOutput {
    common_native::Vec2f applied_offset_px{};
    common_native::Vec2f desired_offset_px{};
    common_native::Box2f predicted_protected_box_px{};
    float intent_speed_scale = 1.0f;
    bool edge_clamped = false;
    bool epoch_reset = false;
};
```

Tests must assert:

- new ADS epoch always returns `(0,0)` on its first update;
- no target and ADS off return `(0,0)` and clear state;
- a safe centered target requests zero;
- an edge target requests only the minimum necessary offset;
- `sniff_scale=1.50` clamps to `+/-120,+/-104` for 480x416;
- aligned intent changes speed only, never desired direction;
- opposing intent cannot reverse the ROI;
- a target returned to the centered viewport produces desired zero;
- disabled mode is bit-stable zero.

- [ ] **Step 2: Add the test target and verify RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_capture_roi_planner_tests
```

Expected: build fails because planner sources do not exist.

- [ ] **Step 3: Implement minimal planner math**

Implement one `update()` and one `reset()`; do not add modes or a second hold timer. Derive maximum offsets from sniff scale and calculate the protected box as the middle 70% width and top 55% height, shifted to the tracker-predicted aim point. Compute the minimum displacement against the centered safe rectangle, clamp it, and rate-limit with `move_toward`.

- [ ] **Step 4: Build and run planner tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_capture_roi_planner_tests
native\vision_native\build\Release\cod_native_capture_roi_planner_tests.exe
```

Expected: `[CaptureRoiPlannerTests] PASS`.

- [ ] **Step 5: Commit planner core**

```powershell
git add native/vision_native/include/vision_native/capture_roi_planner.h native/vision_native/src/capture_roi_planner.cpp native/vision_native/src/capture_roi_planner_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "feat: add bounded capture ROI planner"
```

## Task 4: Make capture offsets and coordinate transforms frame-exact

**Files:**
- Create: `native/vision_native/include/vision_native/roi_coordinates.h`
- Create: `native/vision_native/src/roi_coordinates.cpp`
- Create: `native/vision_native/src/roi_coordinates_tests.cpp`
- Modify: `native/vision_native/include/vision_native/dxgi_capture.h`
- Modify: `native/vision_native/src/dxgi_capture.cpp`
- Modify: `native/vision_native/include/vision_native/types.h`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing transform and clamp tests**

```cpp
const Detection local{200, 80, 300, 280, 0.8f, 0};
const Detection stable = to_stable_detection(local, {-100.0f, 40.0f});
require_near(stable.x1, 100.0f, 0.001f);
require_near(stable.y1, 120.0f, 0.001f);

const RoiPlacement placed = clamp_roi_offset(1920, 1080, 480, 416, {900, -500});
require(placed.roi_left == 1440);
require(placed.roi_top == 0);
require(placed.edge_clamped);
```

Also simulate a static target across offsets `(0,0),(-40,0),(-80,-30),(60,40),(0,0)` and require stable-coordinate spread below 0.5 px.

- [ ] **Step 2: Verify transform tests fail**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_roi_coordinates_tests
```

Expected: missing source/header failure.

- [ ] **Step 3: Implement stateless geometry helpers**

Provide:

```cpp
RoiPlacement clamp_roi_offset(
    int output_width, int output_height,
    int crop_width, int crop_height,
    common_native::Vec2f requested_offset) noexcept;
Detection to_stable_detection(
    const Detection& local,
    common_native::Vec2f applied_offset) noexcept;
Detection to_local_detection(
    const Detection& stable,
    common_native::Vec2f applied_offset) noexcept;
```

- [ ] **Step 4: Extend DXGI capture with next-frame offset**

Add `set_roi_offset(int offset_x, int offset_y)` and keep `roi_left/roi_top` as actual output coordinates. Add `roi_offset_x/roi_offset_y` to `DxgiCaptureMetadata` and `FramePacket`. `grab()` must snapshot the applied offset into the same metadata object as the texture and frame ID.

- [ ] **Step 5: Run coordinate tests and the existing capture smoke**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_roi_coordinates_tests vision_native_cpp
native\vision_native\build\Release\cod_native_roi_coordinates_tests.exe
powershell -ExecutionPolicy Bypass -File tools/run_native_vision_capture_smoke.ps1
```

Expected: coordinate tests pass; capture smoke still returns 480x416 and reports zero offset by default.

- [ ] **Step 6: Commit capture geometry**

```powershell
git add native/vision_native/include/vision_native/roi_coordinates.h native/vision_native/src/roi_coordinates.cpp native/vision_native/src/roi_coordinates_tests.cpp native/vision_native/include/vision_native/dxgi_capture.h native/vision_native/src/dxgi_capture.cpp native/vision_native/include/vision_native/types.h native/vision_native/CMakeLists.txt
git commit -m "feat: add frame-exact ROI capture offsets"
```

## Task 5: Make selector coordinates viewport-aware and restore weak association

**Files:**
- Modify: `native/vision_native/include/vision_native/target_selector.h`
- Modify: `native/vision_native/src/target_selector.cpp`
- Modify: `native/vision_native/src/target_selector_tests.cpp`
- Modify: `native/vision_native/src/vision_engine.cpp`

- [ ] **Step 1: Add failing selector tests**

Tests must cover:

```cpp
const auto baseline_region = centered.required_color_region(local_batch);
selector.set_stable_viewport({-100.0f, 40.0f, 480.0f, 416.0f});
const auto shifted_region = selector.required_color_region(
    translate_batch_to_stable(local_batch, {-100.0f, 40.0f}));
require(shifted_region == baseline_region);

// 0.385 can continue the active target but cannot create one.
require_false(fresh_selector.select(batch_with_conf(0.385f)).has_target);
prime_active_target(selector);
require_true(selector.select(batch_with_conf(0.385f)).has_target);
require_false(selector.select(batch_with_conf(0.385f)).fire_authority);
```

Also assert a 0.385 challenger cannot switch identity and a friendly detection remains rejected.

- [ ] **Step 2: Run target selector tests and confirm RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_target_selector_tests
native\vision_native\build\Release\cod_native_target_selector_tests.exe
```

Expected: new viewport/weak-continuity tests fail.

- [ ] **Step 3: Separate pixel-local and stable selector geometry**

Add an explicit stable viewport to the selector. Color readback regions convert stable detections back to ROI-local pixels; target identity, distances, smoothing and output remain stable-coordinate. External cue points receive the same frame offset before target selection.

- [ ] **Step 4: Lower only the engine decode floor**

Change `kSelectorDecodeConfidenceFloor` from `0.40f` to `0.20f`. Keep pickup at 0.65, enemy-cue pickup at 0.42, tracking at 0.40 and weak association at 0.20. Do not relax fire authority.

- [ ] **Step 5: Run selector and AutoFire regressions**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_target_selector_tests cod_native_controller_tests
native\vision_native\build\Release\cod_native_target_selector_tests.exe
native\vision_native\build\Release\cod_native_controller_tests.exe
```

Expected: all pass, including weak-target no-fire tests.

- [ ] **Step 6: Commit viewport-aware selection**

```powershell
git add native/vision_native/include/vision_native/target_selector.h native/vision_native/src/target_selector.cpp native/vision_native/src/target_selector_tests.cpp native/vision_native/src/vision_engine.cpp
git commit -m "fix: preserve weak targets across shifted vision viewports"
```

## Task 6: Wire committed tracker guidance and ADS epoch reset to vision

**Files:**
- Create: `native/pipeline_contract/capture_roi_guidance.h`
- Modify: `native/vision_native/include/vision_native/vision_engine.h`
- Modify: `native/vision_native/src/vision_engine.cpp`
- Modify: `native/runtime_app/vision_service.h`
- Modify: `native/runtime_app/vision_service.cpp`
- Modify: `native/runtime_app/vision_service_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`

- [ ] **Step 1: Define a trivially copyable guidance snapshot**

```cpp
struct CaptureRoiGuidance {
    std::uint64_t ads_epoch = 0;
    std::uint64_t target_id = 0;
    std::uint64_t source_frame_id = 0;
    bool controller_aiming = false;
    bool has_committed_target = false;
    bool has_body_box = false;
    common_native::Box2f stable_body_box_px{};
    common_native::Vec2f stable_predicted_aim_px{};
    common_native::Vec2f velocity_px_per_sec{};
    common_native::Vec2f user_intent_direction{};
    float user_intent_strength = 0.0f;
    float observation_age_ms = 0.0f;
};
static_assert(std::is_trivially_copyable_v<CaptureRoiGuidance>);
```

- [ ] **Step 2: Write failing VisionService epoch tests**

Extend the fake poller to record guidance and epoch resets. Assert:

- keepwarm idle polling never preserves an offset into a new controller ADS epoch;
- a false-to-true physical ADS transition increments epoch exactly once;
- the first poll in the epoch receives reset guidance and centered offset;
- repeated `set_aiming(true)` does not reset again;
- ADS release clears guidance.

- [ ] **Step 3: Verify service tests fail**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_vision_service_tests
native\vision_native\build\Release\cod_native_vision_service_tests.exe
```

Expected: fake poller lacks guidance/epoch methods.

- [ ] **Step 4: Add explicit service APIs**

Add these methods to `IVisionServicePoller`, `VisionService`, and `VisionEngine`:

```cpp
virtual void begin_ads_epoch(std::uint64_t epoch) = 0;
virtual void set_capture_roi_guidance(
    const pipeline_contract::CaptureRoiGuidance& guidance) = 0;
```

`VisionService::set_aiming` owns the controller false-to-true sequence and calls `begin_ads_epoch` before the immediate first poll. This is separate from engine keepwarm aiming.

- [ ] **Step 5: Build guidance from the previous committed plan**

At the start of each runtime tick, combine `controller_.last_target_plan()` with the latest stable selected body box. Guidance is valid only when the plan lifecycle is not `None`, target ID is nonzero, the controller is physically aiming, and observation age is within the existing plan budget. A challenger or raw detection never supplies guidance before coordinator commitment.

- [ ] **Step 6: Apply the planner in the frame-exact VisionEngine sequence**

`VisionEngine::poll_once()` must perform this order:

```text
snapshot guidance and ADS epoch
-> planner.update
-> capture.set_roi_offset
-> capture.grab (frame metadata freezes applied offset)
-> TensorRT inference in ROI-local coordinates
-> color/cue classification in ROI-local coordinates
-> translate detections and capture-local cue points with that frame's offset
-> selector.set_stable_viewport and select in stable coordinates
-> publish VisionResult with applied/desired offset and stable body/aim geometry
```

Add `roi_offset_x`, `roi_offset_y`, `roi_desired_x`, `roi_desired_y`, `roi_edge_clamped`, `roi_epoch_reset`, and `roi_intent_speed_scale` to `VisionResult`. A failed transform or stale epoch returns centered capture state rather than retaining an unverified offset.

- [ ] **Step 7: Run service and controller lifecycle tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_vision_service_tests cod_native_controller_tests
native\vision_native\build\Release\cod_native_vision_service_tests.exe
native\vision_native\build\Release\cod_native_controller_tests.exe
```

Expected: all pass; first ADS frame centered with keepwarm enabled.

- [ ] **Step 8: Commit lifecycle wiring**

```powershell
git add native/pipeline_contract/capture_roi_guidance.h native/vision_native/include/vision_native/vision_engine.h native/vision_native/src/vision_engine.cpp native/runtime_app/vision_service.h native/runtime_app/vision_service.cpp native/runtime_app/vision_service_tests.cpp native/runtime_app/runtime_loop.h native/runtime_app/runtime_loop.cpp
git commit -m "feat: guide dynamic ROI from committed target plans"
```

## Task 7: Add compact config and opt-in telemetry

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `native/runtime_app/telemetry_collectors.h`
- Modify: `native/runtime_app/telemetry_collectors.cpp`
- Modify: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`
- Modify: `config.native.example.toml`

- [ ] **Step 1: Write failing config tests**

Assert exact defaults and parsing for:

```toml
[vision.dynamic_roi]
enabled = true
sniff_scale = 1.50
prediction_ms = 50
max_speed_px_per_sec = 1200
intent_speed_bonus = 0.25
```

Reject non-finite values and clamp scale to `1.0..1.75`, prediction to `0..100 ms`, speed to `100..3000 px/s`, and bonus to `0..0.5`.

- [ ] **Step 2: Implement config parsing without exposing micro-gates**

Add `DynamicRoiConfig` under `VisionRuntimeConfig`. Keep edge margins and protected-body ratios internal, as required by the design.

- [ ] **Step 3: Add telemetry fields and schema bump**

Increment `kTelemetrySchemaVersion`. Add applied/desired offset, stable viewport, epoch reset, clamp, guidance age, committed target ID, intent speed scale and weak-association status to vision-frame telemetry. Keep logging opt-in under the existing telemetry switch.

- [ ] **Step 4: Run config and telemetry tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime_config_tests cod_native_runtime_telemetry_tests cod_native_telemetry_collectors_tests
native\vision_native\build\Release\cod_native_runtime_config_tests.exe
native\vision_native\build\Release\cod_native_runtime_telemetry_tests.exe
native\vision_native\build\Release\cod_native_telemetry_collectors_tests.exe
```

Expected: all pass and serialized JSON contains the new schema fields only on vision records.

- [ ] **Step 5: Commit config and telemetry**

```powershell
git add native/controller_native/runtime_config.h native/controller_native/runtime_config.cpp native/controller_native/runtime_config_tests.cpp native/runtime_app/telemetry_collectors.h native/runtime_app/telemetry_collectors.cpp native/runtime_app/telemetry_schema.h native/runtime_app/runtime_telemetry.cpp native/runtime_app/runtime_telemetry_tests.cpp config.native.example.toml
git commit -m "feat: configure and observe bounded dynamic ROI"
```

## Task 8: Bind the production planner into video replay

**Files:**
- Modify: `native/vision_native/src/vision_native_module.cpp`
- Modify: `tools/benchmark_dynamic_roi_video.py`
- Modify: `tests/test_dynamic_roi_video_benchmark.py`

- [ ] **Step 1: Add failing Python binding tests**

Require the binding to expose the same production planner, not a Python rewrite:

```python
planner = vision_native_cpp.NativeCaptureRoiPlanner(480, 416, config)
first = planner.update(epoch_input(1, edge_target=True))
second = planner.update(epoch_input(1, edge_target=True))
assert first["applied_offset"] == [0.0, 0.0]
assert second["applied_offset"][0] < 0.0
```

- [ ] **Step 2: Bind planner config/input/output**

Expose `NativeCaptureRoiPlanner.reset()` and `update(dict)` through pybind11. Reject missing/non-finite fields at the binding boundary.

- [ ] **Step 3: Add dynamic replay mode**

For each full-screen frame:

1. apply the production planner's previous output to choose the next 480x416 crop;
2. run the engine at the explicitly selected decode floor;
3. classify/select using exact local-to-stable offset metadata;
4. compare stable output with the reviewed full-screen truth;
5. feed only committed selector/tracker guidance into the next planner update.

- [ ] **Step 4: Run Python and native binding tests**

```powershell
cmake --build native/vision_native/build --config Release --target vision_native_cpp
D:\env\python\python.exe -m pytest tests/test_dynamic_roi_video_benchmark.py tests/test_native_vision_targeting_bridge.py -q
```

Expected: all pass; test proves first ADS frame is centered and later edge offset is nonzero.

- [ ] **Step 5: Commit replay integration**

```powershell
git add native/vision_native/src/vision_native_module.cpp tools/benchmark_dynamic_roi_video.py tests/test_dynamic_roi_video_benchmark.py
git commit -m "test: replay dynamic ROI with production planner"
```

## Task 9: Add the deterministic closed-loop controller benchmark

**Files:**
- Create: `native/controller_native/dynamic_roi_closed_loop_benchmark.cpp`
- Create: `native/controller_native/dynamic_roi_closed_loop_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing scenario tests**

Build a deterministic 1000 Hz controller / 100 Hz vision scenario derived from the second engagement:

- target enters from the left;
- diagonal velocity increases;
- body scale grows to a close target;
- upper body approaches left/top viewport edges;
- a short observation drop occurs;
- target reverses once;
- controller output changes subsequent relative target position.

Assert deterministic equality for the same seed and assert that changing only ROI policy does not change controller output when the stable observation sequence is identical.

- [ ] **Step 2: Verify benchmark tests fail**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_dynamic_roi_benchmark_tests
```

Expected: missing benchmark source failure.

- [ ] **Step 3: Implement fixed/dynamic matched modes**

The benchmark must report:

- time to acquire;
- cumulative chest error in px*ms;
- visible target loss time;
- BodyLock interruption count;
- jerk and user-fight burden;
- ROI nonzero time, saturation count and recenter latency;
- incorrect switch and AutoFire counts;
- coordinate-invariance residual.

Run aligned, neutral, short-opposing and manual-escape input cohorts for fixed seeds `1337`, `7331`, and `20260721`.

- [ ] **Step 4: Run benchmark unit tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_dynamic_roi_benchmark_tests cod_native_dynamic_roi_benchmark
native\vision_native\build\Release\cod_native_dynamic_roi_benchmark_tests.exe
```

Expected: PASS and same-seed JSON is byte-stable apart from explicitly excluded timing metadata.

- [ ] **Step 5: Commit closed-loop benchmark**

```powershell
git add native/controller_native/dynamic_roi_closed_loop_benchmark.cpp native/controller_native/dynamic_roi_closed_loop_benchmark_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "test: add closed-loop dynamic ROI benchmark"
```

## Task 10: Run the sniff-scale matrix and select a production candidate

**Files:**
- Create after successful run: `artifacts/benchmarks/dynamic_roi/20260721_acceptance.json`
- Create after successful run: `docs/project/DYNAMIC_ROI_ACCEPTANCE_20260721.md`

- [ ] **Step 1: Build all affected Release targets**

```powershell
cmake --build native/vision_native/build --config Release --target vision_native_cpp cod_native_runtime cod_native_target_selector_tests cod_native_capture_roi_planner_tests cod_native_roi_coordinates_tests cod_native_vision_service_tests cod_native_runtime_config_tests cod_native_runtime_telemetry_tests cod_native_telemetry_collectors_tests cod_native_controller_tests cod_native_dynamic_roi_benchmark cod_native_dynamic_roi_benchmark_tests
```

Expected: build succeeds with no missing source or ABI error.

- [ ] **Step 2: Run the complete deterministic test gate**

```powershell
native\vision_native\build\Release\cod_native_capture_roi_planner_tests.exe
native\vision_native\build\Release\cod_native_roi_coordinates_tests.exe
native\vision_native\build\Release\cod_native_target_selector_tests.exe
native\vision_native\build\Release\cod_native_vision_service_tests.exe
native\vision_native\build\Release\cod_native_runtime_config_tests.exe
native\vision_native\build\Release\cod_native_runtime_telemetry_tests.exe
native\vision_native\build\Release\cod_native_telemetry_collectors_tests.exe
native\vision_native\build\Release\cod_native_controller_tests.exe
native\vision_native\build\Release\cod_native_dynamic_roi_benchmark_tests.exe
D:\env\python\python.exe -m pytest tests/test_dynamic_roi_fixture.py tests/test_dynamic_roi_video_benchmark.py tests/test_native_vision_targeting_bridge.py -q
```

Expected: every command passes.

- [ ] **Step 3: Run the decode/ROI ablation and sniff-scale matrix**

```powershell
D:\env\python\python.exe tools/benchmark_dynamic_roi_video.py `
  --compare fixed040 fixed020 dynamic020 `
  --sniff-scales 1.25 1.50 1.75 `
  --video "C:\Users\Administrator\Videos\NVIDIA\Call of Duty Black Ops 7\Call of Duty Black Ops 7 2026.07.21 - 15.47.31.01.mp4" `
  --fixture benchmarks/fixtures/dynamic_roi/20260721_black_ops7_first_two_targets.json `
  --engine models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine `
  --output runs/dynamic_roi_acceptance/video_matrix.json
```

The modes mean:

- `fixed040`: current production capture and current 0.40 decode floor;
- `fixed020`: centered capture with only the weak-association decode correction;
- `dynamic020`: weak-association correction plus bounded dynamic ROI.

Expected gates:

- first episode offset is exactly zero;
- `fixed020` separately quantifies weak-association benefit without claiming ROI benefit;
- `dynamic020` reduces second-episode clipped/missed frames by at least 50% against `fixed020`, or records the exact narrower ROI contribution if the combined gate is met only against `fixed040`;
- no new identity switch or fire-authority frame;
- coordinate residual is at most 0.5 px.

- [ ] **Step 4: Run the closed-loop matrix**

```powershell
native\vision_native\build\Release\cod_native_dynamic_roi_benchmark.exe `
  --modes fixed,dynamic `
  --sniff-scales 1.25,1.50,1.75 `
  --seeds 1337,7331,20260721 `
  --output runs/dynamic_roi_acceptance/closed_loop_matrix.json
```

Expected: ordinary/non-edge controller metrics do not regress; edge loss/error/interruption improves; no ROI/controller oscillation or extra AutoFire.

- [ ] **Step 5: Measure runtime overhead**

Run the existing native acceptance tooling once with dynamic ROI disabled and once enabled using the same engine/config. Require added vision/selector/planner CPU P95 at most 0.15 ms and unchanged inference count/input dimensions.

- [ ] **Step 6: Select the smallest passing sniff scale**

Choose the smallest scale that passes every gate. Do not select 1.50 merely because it was the design default. Record rejected scales and exact reasons.

- [ ] **Step 7: Write the retained acceptance artifact**

Merge provenance and results from `fixed_decode_040_before.json`, `video_matrix.json`, `closed_loop_matrix.json`, runtime timing, revision, config fingerprint, engine SHA-256, fixture review status and known raw-video counterfactual limitation into `artifacts/benchmarks/dynamic_roi/20260721_acceptance.json`. Write a concise interpretation to `docs/project/DYNAMIC_ROI_ACCEPTANCE_20260721.md` that reports weak-association and ROI contributions separately.

- [ ] **Step 8: Commit the accepted config and artifact**

```powershell
git add config.native.example.toml artifacts/benchmarks/dynamic_roi/20260721_acceptance.json docs/project/DYNAMIC_ROI_ACCEPTANCE_20260721.md
git commit -m "test: accept bounded dynamic ROI"
```

## Task 11: Production smoke and handoff

**Files:**
- Update through an approved SyncSet: `.agent-context/handoff.md`, `.agent-context/session-log.md`

- [ ] **Step 1: Run native runtime acceptance**

```powershell
powershell -ExecutionPolicy Bypass -File tools/check_native_cpp_gamepad_runtime.ps1
D:\env\python\python.exe tools/native_runtime_acceptance.py --config config.toml
```

Expected: runtime artifact is current, crop/engine remain 480x416, background launch contract passes, and dynamic ROI config fingerprint is reported.

- [ ] **Step 2: Perform a short instrumented live smoke**

Enable debug telemetry, start a new ADS epoch, verify first-frame offset `(0,0)`, track a close lateral target, release ADS, and verify offset reset. Physical gamepad passthrough and AutoFire behavior must remain unchanged.

- [ ] **Step 3: Inspect the fresh log**

Confirm frame-bound offsets, stable/local coordinates, target identity, planner guidance age and reset events. Reject the build if offset metadata lags its frame or if ROI movement appears as tracker velocity.

- [ ] **Step 4: Present the user acceptance package**

Report:

- selected sniff scale and rejected alternatives;
- fixed versus dynamic video metrics;
- closed-loop ADS/BodyLock metrics;
- overhead;
- exact runtime revision/config/engine;
- remaining limitation that final hand feel requires the user's gameplay validation.

- [ ] **Step 5: Update project context only after showing a SyncSet**

Record the accepted architecture, evidence path and next live-validation action without storing the user's personal video path.

- [ ] **Step 6: Commit handoff documentation**

```powershell
git add .agent-context/handoff.md .agent-context/session-log.md
git commit -m "docs: record bounded dynamic ROI acceptance"
```

## Final completion gate

Do not claim completion unless all of the following are true:

- reviewed fixture exists and validates independently of current engine output;
- pre-change fixed baseline is retained;
- planner, coordinate, selector, service, config, telemetry and controller tests pass;
- video replay and closed-loop artifacts carry revision/config/engine/seed identity;
- every ADS epoch begins centered;
- first non-edge episode remains centered and non-regressed;
- second edge episode improves by the accepted threshold;
- weak association cannot pick, switch or fire;
- runtime overhead passes;
- production smoke shows no frame/offset mismatch;
- working tree is clean.
