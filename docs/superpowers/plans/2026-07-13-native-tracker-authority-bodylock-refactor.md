# Native Tracker / Authority / Bodylock Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the coupled native tracker/controller orchestration with explicit track-memory, selected-track, assist-authority, bodylock-lifecycle, and assist-dynamics contracts while preserving a runnable pre-refactor release and meeting the benchmark gates in the approved design.

**Architecture:** Vision continues to detect candidates and the intent-aware selector remains the only production target selector. The current CV Kalman implementation becomes estimator-only track memory; a separate authority policy decides observed assist, short continuity, track-only, or reject; bodylock lifecycle and dynamics shape only authorized AI delta before recoil. Existing controller state is adapted during migration, then the duplicated provider selection/hold/authority logic is removed.

**Tech Stack:** C++17, CUDA 13.1, TensorRT 10.15, CMake/Visual Studio 2022, PowerShell release tooling, native deterministic benchmark executables.

---

## File and responsibility map

New focused files:

- `scripts/release/package_native_runtime.ps1`: reproducibly stage the frozen native runtime, runtime DLLs, config, engine, recoil data, launchers, README, and SHA-256 manifest.
- `scripts/verify/verify_native_release.ps1`: verify manifest hashes, relative paths, launcher print-only behavior, runtime `--once`, and a second ZIP extraction.
- `native/pipeline_contract/track_memory.h`: estimator-only observation, track estimate, selected-track reference, source, and lifecycle types.
- `native/pipeline_contract/assist_authority.h`: assist-authority state/reason and bodylock-lifecycle types shared across tracking, controller, telemetry, and benchmarks.
- `native/tracking_native/track_memory_service.h/.cpp`: backend-neutral all-candidate ingestion, selected-observation-to-track binding, and explicit track lookup.
- `native/tracking_native/track_memory_service_tests.cpp`: deterministic association, identity, observation-time, and no-authority tests.
- `native/controller_native/assist_authority_policy.h/.cpp`: the only component allowed to grant controller or fire permission.
- `native/controller_native/assist_authority_policy_tests.cpp`: observed/continuity/track-only/reject and user-yield truth table.
- `native/controller_native/bodylock_lifecycle.h/.cpp`: warm/tracking/coast/yield state transition ownership.
- `native/controller_native/bodylock_lifecycle_tests.cpp`: same-track dropout, switch, timeout, and manual escape transitions.

Existing files with narrowed responsibilities:

- `native/fps_2d_tracker_package/include/fps_tracker/target_tracker.hpp` and `src/target_tracker.cpp`: expose all non-lost estimator snapshots without selecting one or deciding authority.
- `native/tracking_native/tracker_backend.h`, `fps_reference_tracker.h/.cpp`, and `tracker_contract.h`: adapt the existing Kalman tracker to estimator-only contracts while retaining a temporary legacy query adapter.
- `native/vision_native/include/vision_native/types.h`, `target_selector.h`, and `src/target_selector.cpp`: carry the selector-owned backing detection index through `VisionResult`.
- `native/runtime_app/vision_controller_adapter.h/.cpp`: create stable per-frame observation IDs and the selector-owned observation reference.
- `native/controller_native/controller_vision_snapshot.h`: transport the observation batch and selector reference without a second selector.
- `native/controller_native/target_snapshot_provider.h/.cpp`: become a facade over track memory and authority; remove middle-layer selection, projection authority reconstruction, and rewritten timestamps.
- `native/controller_native/controller_tick_context.h`: carry explicit track, authority, and bodylock-lifecycle state during compatibility migration.
- `native/controller_native/output_validation_policy.h/.cpp`: validate authorized AI delta only; never replace manual input without authority.
- `native/controller_native/native_gamepad_controller.h/.cpp`: own bodylock lifecycle, apply dynamics to AI delta, and preserve recoil as final feed-forward.
- `native/controller_native/aim_assist_dynamics.h/.cpp`: retain state on ordinary bodylock ticks and apply bounded attack/release/jerk/sign transitions to assist only.
- `native/controller_native/output_mixer.h`: expose selected track, authority, lifecycle, raw assist, shaped assist, manual, before-recoil, recoil, and final components.
- `native/runtime_app/runtime_telemetry.*`, `telemetry_target_identity.*`, and `native/telemetry_native/aim_perf_file_logger.*`: record ownership and transition reasons.
- `native/controller_native/cod_native_gamepad_benchmark.cpp`: fix the stale baseline self-test, keep the two RED defect scenarios, and score every design gate.
- `native/vision_native/CMakeLists.txt`: register new sources and focused tests.

## Task 1: Preserve the current workspace, freeze rollback, and ship the pre-refactor release

**Files:**

- Create on refactor branch: `scripts/release/package_native_runtime.ps1`
- Create on refactor branch: `scripts/verify/verify_native_release.ps1`
- Create locally: `artifacts/releases/native-runtime-pre-tracker-refactor-20260713/`
- Create locally: `artifacts/releases/native-runtime-pre-tracker-refactor-20260713.zip`
- Preserve on safety branch: all six design-time dirty paths

- [ ] **Step 1: Verify the exact dirty set before preserving it**

Run from `D:\work\AI\yolo-study-001`:

```powershell
$expected = @(
  ' D config.native.example.toml',
  ' M native/controller_native/body_lock_short_plan_policy_tests.cpp',
  ' M native/controller_native/controller_behavior_tests.cpp',
  '?? color_readback_benchmark.json',
  '?? scheduler_benchmark.json',
  '?? telemetry_benchmark.json'
)
$actual = @(git status --short)
if (Compare-Object $expected $actual) { throw 'Dirty set changed; inspect before snapshot.' }
```

Expected: no output and exit `0`.

- [ ] **Step 2: Commit the dirty state on the durable safety branch**

```powershell
git switch -c codex/pre-tracker-refactor-workspace-snapshot-20260713
git add --all
git diff --cached --check
git commit -m "Snapshot workspace before tracker authority refactor"
git status --short
```

Expected: one snapshot commit and empty status on the safety branch. CRLF conversion warnings are allowed; trailing-whitespace errors are not.

- [ ] **Step 3: Return the main worktree to clean `dev` and verify every snapshot path**

```powershell
$snapshot = git rev-parse codex/pre-tracker-refactor-workspace-snapshot-20260713
git diff-tree --no-commit-id --name-status -r $snapshot
git switch dev
if (git status --porcelain) { throw 'dev is not clean after snapshot.' }
git rev-parse HEAD
```

Expected: all six paths appear in the snapshot commit and `dev` is clean at `5d49c452c895bdd89d834aa94467e40edf0a5479`.

- [ ] **Step 4: Freeze the rollback source point**

```powershell
git tag -a pre-tracker-authority-refactor-20260713 -m "Runnable native baseline before tracker authority bodylock refactor" 5d49c452c895bdd89d834aa94467e40edf0a5479
git show --no-patch --decorate pre-tracker-authority-refactor-20260713
```

Expected: annotated tag resolves to `5d49c45`.

- [ ] **Step 5: Rebuild the frozen runtime from clean `dev`**

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build native/vision_native/build --config Release --target cod_native_runtime cod_native_controller_tests cod_native_gamepad_benchmark cod_native_benchmark_metrics_tests cod_native_output_validation_tests cod_native_recoil_contract_tests -- /m
```

Expected: every target builds and `cod_native_runtime.exe` is refreshed from the tagged commit.

- [ ] **Step 6: Write the package script with a closed allow-list**

The script accepts `-SourceRoot`, `-OutputRoot`, and `-SourceCommit`; resolves `model_path`, `profile_directory`, `calibration_directory`, `weapon_directory`, and `recognizer_state_path` from `config.toml`; copies only the two launchers, config, runtime executable, `nvinfer_10.dll`, `nvinfer_plugin_10.dll`, `SDL2.dll`, `ViGEmClient.dll`, the selected engine and existing same-basename companion files, and referenced recoil data. It creates missing empty calibration directories, writes `README.txt`, then writes this manifest shape:

```json
{
  "schema_version": 1,
  "source_commit": "5d49c452c895bdd89d834aa94467e40edf0a5479",
  "created_at_utc": "ISO-8601 UTC timestamp",
  "entrypoint": "scripts/launch/gamepad_start.bat",
  "config": "config.toml",
  "model_path": "models/candidates/body_union_manual_core_x2_neg_e6_640x512.engine",
  "files": [
    {"path": "relative/path", "size": 123, "sha256": "uppercase SHA-256"}
  ]
}
```

The script throws if a required file is absent and recreates only the named output directory, never a computed parent.

- [ ] **Step 7: Write the release verifier**

`verify_native_release.ps1` must:

```powershell
param([Parameter(Mandatory)][string]$ReleaseRoot)
$manifest = Get-Content -LiteralPath (Join-Path $ReleaseRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest.files) {
  $path = Join-Path $ReleaseRoot $file.path
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $($file.path)" }
  if ((Get-Item -LiteralPath $path).Length -ne [int64]$file.size) { throw "Size mismatch $($file.path)" }
  if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw "Hash mismatch $($file.path)" }
}
Push-Location $ReleaseRoot
try {
  $env:GAMEPAD_START_PRINT_ONLY='1'
  $env:GAMEPAD_START_FIRE_CHOICE_OVERRIDE=''
  $env:GAMEPAD_START_RECOIL_CHOICE_OVERRIDE='1'
  cmd /c scripts\launch\gamepad_start.bat
  if ($LASTEXITCODE -ne 0) { throw 'Launcher resolution failed.' }
  & .\native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once
  if ($LASTEXITCODE -ne 0) { throw 'Runtime smoke failed.' }
} finally {
  Pop-Location
}
```

- [ ] **Step 8: Package, ZIP, extract elsewhere, and verify both copies**

```powershell
& .\scripts\release\package_native_runtime.ps1 -SourceRoot 'D:\work\AI\yolo-study-001' -OutputRoot 'D:\work\AI\yolo-study-001\artifacts\releases\native-runtime-pre-tracker-refactor-20260713' -SourceCommit '5d49c452c895bdd89d834aa94467e40edf0a5479'
Compress-Archive -LiteralPath 'D:\work\AI\yolo-study-001\artifacts\releases\native-runtime-pre-tracker-refactor-20260713\*' -DestinationPath 'D:\work\AI\yolo-study-001\artifacts\releases\native-runtime-pre-tracker-refactor-20260713.zip' -Force
& .\scripts\verify\verify_native_release.ps1 -ReleaseRoot 'D:\work\AI\yolo-study-001\artifacts\releases\native-runtime-pre-tracker-refactor-20260713'
$second = 'D:\work\AI\yolo-study-001\artifacts\release-smoke\native-runtime-pre-tracker-refactor-20260713'
Expand-Archive -LiteralPath 'D:\work\AI\yolo-study-001\artifacts\releases\native-runtime-pre-tracker-refactor-20260713.zip' -DestinationPath $second -Force
& .\scripts\verify\verify_native_release.ps1 -ReleaseRoot $second
```

Expected: both verifications exit `0`, the engine loads without `.runtime.json`, and `git -C D:\work\AI\yolo-study-001 status --porcelain` stays empty.

- [ ] **Step 9: Commit release tooling on the refactor branch**

```powershell
git add scripts/release/package_native_runtime.ps1 scripts/verify/verify_native_release.ps1
git commit -m "Add reproducible native runtime release packaging"
```

## Task 2: Repair the stale clean-baseline self-test and record A0

**Files:**

- Modify: `native/controller_native/cod_native_gamepad_benchmark.cpp:1335-1395`
- Create locally: `runs/native_perf/native_gamepad_benchmark_refactor_A0_5d49c45_20260713.json`
- Create locally: `runs/native_perf/native_gamepad_benchmark_bodylock_continuity_A0_5d49c45_20260713.json`

- [ ] **Step 1: Reproduce the pre-existing failure**

```powershell
& native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --self-test
```

Expected: FAIL only with `moving chase benchmark should report body-lock close-assist strength`.

- [ ] **Step 2: Correct the self-test scenario ownership**

Replace the smooth-scenario close-assist assertion with a centered-band assertion:

```cpp
require_benchmark_check(
    moving_chase.ads_manual_stress_body_lock_centered_samples > 0,
    "smooth moving chase should report centered body-lock samples when error stays below close-assist band");
```

After `slide_occluded` is created, add:

```cpp
require_benchmark_check(
    slide_occluded.ads_manual_stress_body_lock_close_assist_samples > 0 &&
        slide_occluded.ads_manual_stress_body_lock_close_assist_mean_ai_output > 0.0,
    "slide occlusion benchmark should report body-lock close-assist strength");
```

This preserves the metric contract without forcing a well-centered smooth scenario to manufacture `18-72px` error.

- [ ] **Step 3: Build and verify the repaired benchmark harness**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_gamepad_benchmark cod_native_benchmark_metrics_tests -- /m
& native\vision_native\build\Release\cod_native_benchmark_metrics_tests.exe
& native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --self-test
```

Expected: both tests PASS.

- [ ] **Step 4: Record clean A0 artifacts**

```powershell
& native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --random-fov-ticks 0 --output runs\native_perf\native_gamepad_benchmark_refactor_A0_5d49c45_20260713.json
& native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --suite bodylock_continuity --output runs\native_perf\native_gamepad_benchmark_bodylock_continuity_A0_5d49c45_20260713.json
```

Expected: full suite exits `0`; both defect scenarios remain RED with the design-time metrics, proving the benchmark still detects the defect.

- [ ] **Step 5: Commit only the harness correction**

```powershell
git add native/controller_native/cod_native_gamepad_benchmark.cpp
git commit -m "Fix bodylock benchmark baseline self-test"
```

## Task 3: Introduce estimator-only and authority contracts

**Files:**

- Create: `native/pipeline_contract/track_memory.h`
- Create: `native/pipeline_contract/assist_authority.h`
- Modify: `native/controller_native/controller_vision_snapshot.h`
- Modify: `native/controller_native/controller_tick_context.h`
- Modify: `native/vision_native/CMakeLists.txt`
- Test: `native/tracking_native/track_memory_service_tests.cpp`

- [ ] **Step 1: Write compile-time and value-semantics tests for the new contracts**

The test constructs an observed estimate and verifies that authority is absent from it:

```cpp
pipeline_contract::TrackEstimate estimate;
estimate.track_id = 42;
estimate.backing_observation_id = 9001;
estimate.source = pipeline_contract::TrackEstimateSource::Observed;
estimate.lifecycle = pipeline_contract::TrackLifecycle::Confirmed;
estimate.last_observed_at = {12.0};
estimate.query_time = {12.010};
estimate.observation_age_ms = 10.0;
require(estimate.track_id == 42, "track identity must survive transport");
require(estimate.source == pipeline_contract::TrackEstimateSource::Observed,
        "estimate source must remain geometry-only");
```

Also construct `SelectedTrackRef` and `AssistAuthorityDecision` separately and verify no conversion constructor exists between them.

- [ ] **Step 2: Run the new test target and confirm it fails to compile**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_track_memory_tests -- /m
```

Expected: FAIL because the two headers and target do not exist.

- [ ] **Step 3: Add the exact data contracts**

`track_memory.h` defines:

```cpp
enum class TrackEstimateSource : std::uint8_t { Observed, Projected };
enum class TrackLifecycle : std::uint8_t { Tentative, Confirmed, Coasting, Lost };

struct TrackObservationDetection {
    std::uint64_t observation_id = 0;
    common_native::Box2f body_box_px;
    common_native::Vec2f aim_point_px;
    bool has_aim_point = false;
    float confidence = 0.0f;
    int class_id = 0;
    std::string evidence_tier = "observed_strong";
    bool is_friendly = false;
};

struct TrackObservationBatch {
    std::uint64_t frame_id = 0;
    common_native::TimeSeconds capture_time;
    common_native::TimeSeconds ready_time;
    common_native::Vec2f screen_center_px;
    std::vector<TrackObservationDetection> detections;
};

struct TrackEstimate {
    std::uint64_t track_id = 0;
    std::uint64_t backing_observation_id = 0;
    std::uint64_t backing_frame_id = 0;
    TrackEstimateSource source = TrackEstimateSource::Projected;
    TrackLifecycle lifecycle = TrackLifecycle::Lost;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    common_native::Vec2f velocity_px_per_sec;
    double position_sigma = 0.0;
    double association_ambiguity = 1.0;
    double association_quality = 0.0;
    common_native::TimeSeconds last_observed_at;
    common_native::TimeSeconds query_time;
    double observation_age_ms = 0.0;
};

struct SelectedTrackRef {
    bool has_selection = false;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t track_id = 0;
    std::uint64_t backing_frame_id = 0;
    float confidence = 0.0f;
    const char* reason = "none";
};
```

`TrackObservationDetection` is declared in the same header with observation ID, body box, optional aim point, confidence, class, evidence tier, and friendly flag. `tracking_native::TrackerDetection` becomes a temporary alias so the pipeline contract does not depend back on a tracker implementation namespace.

`assist_authority.h` defines `AssistAuthorityState { Reject, TrackOnly, Continuity, ObservedStrong }`, `AssistAuthorityReason`, `BodylockLifecycleState { Inactive, Warm, Tracking, Coast, Yield }`, and an `AssistAuthorityDecision` containing selected track ID, assist state, reason, `AssistAuthority`, `FireAuthority`, and `assist_scale`.

- [ ] **Step 4: Build and run the contract test**

Expected: `cod_native_track_memory_tests.exe` exits `0`.

- [ ] **Step 5: Commit contracts only**

```powershell
git add native/pipeline_contract native/controller_native/controller_vision_snapshot.h native/controller_native/controller_tick_context.h native/tracking_native/track_memory_service_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Add explicit tracker and assist authority contracts"
```

## Task 4: Convert the FPS tracker to all-candidate Track Memory

**Files:**

- Modify: `native/fps_2d_tracker_package/include/fps_tracker/target_tracker.hpp`
- Modify: `native/fps_2d_tracker_package/src/target_tracker.cpp`
- Modify: `native/tracking_native/tracker_backend.h`
- Modify: `native/tracking_native/fps_reference_tracker.h`
- Modify: `native/tracking_native/fps_reference_tracker.cpp`
- Create: `native/tracking_native/track_memory_service.h`
- Create: `native/tracking_native/track_memory_service.cpp`
- Test: `native/tracking_native/track_memory_service_tests.cpp`

- [ ] **Step 1: Add failing identity and observation-time tests**

Feed two detections for three frames, query all estimates, and assert:

```cpp
require(estimates.size() == 2, "track memory must expose both eligible candidates");
const auto selected = memory.resolve_selected_observation(second_detection_id, query_time);
require(selected.has_selection, "selector observation must bind to a track");
require(selected.track_id == estimate_for(second_detection_id).track_id,
        "tracker may not substitute its center/preferred target");
require_near(estimate.last_observed_at.value, capture_time, 1e-9,
             "query must not rewrite real observation time");
```

Add a frame with no selected target but two detections and assert both tracks still update.

- [ ] **Step 2: Run and confirm the tests fail against the selected-target backend API**

Expected: FAIL because only `TrackerSnapshot query()` exists.

- [ ] **Step 3: Expose all non-lost snapshots from `fps::TargetTracker`**

Add:

```cpp
[[nodiscard]] std::vector<TrackSnapshot> snapshots(TimeSec queryTime) const;
```

Implementation returns `makeSnapshot` for each non-lost track without the `minAssistConfidence` filter. Existing `query()` calls `snapshots()` and keeps its legacy scoring only for compatibility tests.

- [ ] **Step 4: Add estimator-only backend methods**

`TrackerBackend` gains:

```cpp
virtual void ingest_batch(const pipeline_contract::TrackObservationBatch& batch) = 0;
virtual std::vector<pipeline_contract::TrackEstimate> estimates(
    const TrackerQuery& query) const = 0;
```

`FpsReferenceTracker` maps `TrackSnapshot` geometry, velocity, sigma, ambiguity, backing detection/frame, real observation age, lifecycle, and source. It does not copy tracker assist/fire authority into `TrackEstimate`.

- [ ] **Step 5: Implement `TrackMemoryService` lookups**

The service stores only the latest query result and provides:

```cpp
void ingest(const pipeline_contract::TrackObservationBatch& batch);
std::vector<pipeline_contract::TrackEstimate> estimates(double query_time) const;
std::optional<pipeline_contract::TrackEstimate> find_track(
    std::uint64_t track_id, double query_time) const;
pipeline_contract::SelectedTrackRef resolve_selected_observation(
    std::uint64_t observation_id, double query_time) const;
```

Resolution matches `backing_observation_id`; no confidence/center fallback is allowed.

- [ ] **Step 6: Run tracker invariants and service tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_track_memory_tests cod_native_target_snapshot_tests -- /m
& native\vision_native\build\Release\cod_native_track_memory_tests.exe
& native\vision_native\build\Release\cod_native_target_snapshot_tests.exe
```

Expected: both PASS.

- [ ] **Step 7: Commit the estimator-only memory layer**

```powershell
git add native/fps_2d_tracker_package native/tracking_native native/vision_native/CMakeLists.txt
git commit -m "Expose all-candidate estimator-only track memory"
```

## Task 5: Make the intent-aware selector the single selected-target owner

**Files:**

- Modify: `native/vision_native/include/vision_native/types.h`
- Modify: `native/vision_native/include/vision_native/target_selector.h`
- Modify: `native/vision_native/src/target_selector.cpp`
- Modify: `native/vision_native/src/target_selector_tests.cpp`
- Modify: `native/runtime_app/vision_controller_adapter.cpp`
- Modify: `native/controller_native/controller_vision_snapshot.h`
- Modify: `native/controller_native/target_snapshot_provider.h`
- Modify: `native/controller_native/target_snapshot_provider.cpp`
- Test: `native/controller_native/target_snapshot_provider_tests.cpp`

- [ ] **Step 1: Write a failing two-target ownership test**

Use two candidates where the tracker center/confidence preference differs from the selector result. Assert that the controller-facing state uses the selector's observation ID and bound track ID, never the tracker's legacy selected snapshot.

- [ ] **Step 2: Carry selector backing identity through vision**

Add `source_detection_index` to selector candidates and these fields to `VisionResult`:

```cpp
bool has_selected_detection = false;
std::uint32_t selected_detection_index = 0;
```

`result_from_target()` copies the chosen candidate index. Hold/cue-only results set `has_selected_detection=false` unless backed by a current detection.

- [ ] **Step 3: Build the selector-owned observation reference in the runtime adapter**

Use the existing stable ID formula `(frame_id << 32) | (index + 1)` and populate `ControllerVisionSnapshot::selected_observation_id`. Candidate and tracker detection IDs must use the same function.

- [ ] **Step 4: Remove controller-side re-selection**

Delete `select_middle_layer_target`, `middle_layer_candidate_score`, and candidate intent re-ranking from `TargetSnapshotProvider`. On submission, ingest all non-friendly eligible detections, then resolve only `snapshot.selected_observation_id` to a track.

- [ ] **Step 5: Run selector, provider, and AimLab tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_target_selector_tests cod_native_target_snapshot_tests cod_native_aimlab_benchmark_tests cod_native_aimlab_benchmark -- /m
& native\vision_native\build\Release\cod_native_target_selector_tests.exe
& native\vision_native\build\Release\cod_native_target_snapshot_tests.exe
& native\vision_native\build\Release\cod_native_aimlab_benchmark_tests.exe
& native\vision_native\build\Release\cod_native_aimlab_benchmark.exe
```

Expected: all PASS and established user-intent cases retain intended target selection.

- [ ] **Step 6: Record A1 and commit**

Run the full benchmark to `native_gamepad_benchmark_refactor_A1_selected_track_20260713.json`; compare identity switches and bodylock metrics to A0; then commit:

```powershell
git add native/vision_native native/runtime_app/vision_controller_adapter.cpp native/controller_native/controller_vision_snapshot.h native/controller_native/target_snapshot_provider.*
git commit -m "Bind controller tracking to selector-owned target identity"
```

## Task 6: Add the exclusive assist-authority policy and real observation age

**Files:**

- Create: `native/controller_native/assist_authority_policy.h`
- Create: `native/controller_native/assist_authority_policy.cpp`
- Create: `native/controller_native/assist_authority_policy_tests.cpp`
- Modify: `native/controller_native/target_snapshot_provider.h`
- Modify: `native/controller_native/target_snapshot_provider.cpp`
- Modify: `native/controller_native/controller_tick_context.h`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write the authority truth-table tests**

Cover these exact rows:

| Evidence | Same track | Age/uncertainty | User intent | Expected | Fire |
| --- | --- | --- | --- | --- | --- |
| current strong observed | yes | fresh/bounded | neutral/aligned | observed strong | observed-only when requested |
| no current detection | yes | short/bounded | neutral/aligned | continuity | none |
| weak or cue only | yes | bounded | neutral | track-only | none |
| projected only without prior observed grant | yes | bounded | neutral | track-only | none |
| stale or high uncertainty | yes | expired | any | reject | none |
| selected track changed | no | any | any | reject for old track | none |
| strong opposing manual intent | yes | any | opposing | reject/yield | none |

- [ ] **Step 2: Confirm the tests fail before implementation**

Expected: missing policy target.

- [ ] **Step 3: Implement a pure authority decision**

Use an input containing selected ref, optional estimate, current evidence tier, current observed aim/fire flags, prior accepted track ID/time, manual stick, and query time. The policy returns no geometry. Continuity requires the same prior observed track, age within `target_projection_max_age_ms`, bounded estimator sigma, and no opposing manual escape. It never upgrades weak/cue/projected-only evidence to strong or fire.

- [ ] **Step 4: Replace projection authority reconstruction**

Delete assignments that turn projection into `aim_authority=true`, `target_tier="projected"`, or `observed_at_seconds=now_seconds`. Compatibility state maps:

```cpp
state.aim_authority = decision.assist_authority != common_native::AssistAuthority::None;
state.fire_authority = decision.fire_authority == common_native::FireAuthority::ObservedOnly;
state.observed_at_seconds = estimate.last_observed_at.value;
state.has_tracker_projection = estimate.source == pipeline_contract::TrackEstimateSource::Projected;
```

- [ ] **Step 5: Run authority/provider/fire tests**

Expected: policy, target snapshot, auto-fire, controller, and recoil contract tests PASS; tracker-only fire violations equal zero.

- [ ] **Step 6: Record A2 and commit**

Write `native_gamepad_benchmark_refactor_A2_authority_20260713.json`, then commit:

```powershell
git add native/controller_native native/vision_native/CMakeLists.txt
git commit -m "Centralize native assist and fire authority"
```

## Task 7: Enforce exact manual preservation when authority is absent

**Files:**

- Modify: `native/controller_native/output_validation_policy.h`
- Modify: `native/controller_native/output_validation_policy.cpp`
- Modify: `native/controller_native/output_validation_policy_tests.cpp`
- Modify: `native/controller_native/cod_native_gamepad_benchmark.cpp`

- [ ] **Step 1: Add the production-path regression test**

Reproduce projection with no assist authority and manual `(0.80f, 0.66f)`; assert:

```cpp
require_near(output.right_x, 0.80f, 0.0001f,
             "track-only projection must not replace manual X");
require_near(output.right_y, 0.66f, 0.0001f,
             "track-only projection must not replace manual Y");
```

- [ ] **Step 2: Verify the test fails with approximately `(-0.24,-0.24)`**

Expected: FAIL, reproducing the measured authority-loss override.

- [ ] **Step 3: Make validation operate on AI delta only**

At `Reject` or `TrackOnly`, reset validation state and return the exact manual axes. At observed/continuity authority, derive `assist = output - manual`, constrain only assist, then return `manual + constrained_assist`. Candidate holds cannot reverse or attenuate manual.

- [ ] **Step 4: Run focused tests and continuity benchmark**

Expected: output-validation tests PASS; `authority_loss_override_events=0`; `max_excess_final_delta<=0.15`; strong manual axes never reverse.

- [ ] **Step 5: Commit the invariant**

```powershell
git add native/controller_native/output_validation_policy.* native/controller_native/output_validation_policy_tests.cpp native/controller_native/cod_native_gamepad_benchmark.cpp
git commit -m "Preserve manual input outside assist authority"
```

## Task 8: Add explicit bodylock lifecycle and eliminate one-tick destructive resets

**Files:**

- Create: `native/controller_native/bodylock_lifecycle.h`
- Create: `native/controller_native/bodylock_lifecycle.cpp`
- Create: `native/controller_native/bodylock_lifecycle_tests.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing lifecycle transition tests**

Verify `Inactive -> Warm -> Tracking -> Coast -> Tracking`, and immediate `Tracking/Coast -> Yield` for track switch, expired coast, reject, or strong opposing manual input. A one-tick same-track continuity transition must not enter `Inactive` or reset assist history.

- [ ] **Step 2: Implement deterministic transitions**

`BodylockLifecycle::update` consumes authority state, selected track ID, manual axes, and time. `Coast` is limited by the authority decision; it does not extend its own deadline. `Yield` emits one transition event and becomes `Inactive` on the next no-authority tick.

- [ ] **Step 3: Integrate lifecycle into controller state ownership**

Pass lifecycle to AI aim and dynamics. Preserve bodylock controller/dynamics history across `Tracking <-> Coast`; reset on selected-track change, `Yield`, ADS release, or controller reset. Remove mode-string-driven resets that fire merely because one tick is not `body_lock`.

- [ ] **Step 4: Run lifecycle, controller, and mode-chatter tests**

Expected: lifecycle tests PASS; short same-track bodylock runs disappear; authority-loss overrides remain zero.

- [ ] **Step 5: Record A3 and commit**

Write `native_gamepad_benchmark_refactor_A3_bodylock_lifecycle_20260713.json`, compare all six bodylock scenarios against A0 and the positive continuity artifact, then commit:

```powershell
git add native/controller_native native/vision_native/CMakeLists.txt
git commit -m "Add explicit bodylock continuity lifecycle"
```

## Task 9: Shape only authorized AI-assist detail

**Files:**

- Modify: `native/controller_native/aim_assist_dynamics.h`
- Modify: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/output_mixer.h`

- [ ] **Step 1: Add failing assist-envelope tests**

Tests assert:

- manual output is bitwise unchanged at reject/track-only;
- ordinary non-firing tracking ticks retain assist history;
- assist step is bounded to `0.10` per nominal tick near target;
- assist decelerates through zero before changing sign;
- strong opposing manual input yields in the current tick;
- recoil input and output are not read or modified by dynamics.

- [ ] **Step 2: Extend dynamics input/output explicitly**

```cpp
struct NativeAimAssistDynamicsInput {
    common_native::Vec2f manual;
    common_native::Vec2f requested_assist;
    pipeline_contract::AssistAuthorityState authority;
    pipeline_contract::BodylockLifecycleState lifecycle;
    common_native::Vec2f target_error_px;
    double position_sigma = 0.0;
    double dt_seconds = 0.001;
    double now_seconds = 0.0;
};

struct NativeAimAssistDynamicsOutput {
    common_native::Vec2f assist;
    const char* limit_reason = "none";
};
```

- [ ] **Step 3: Implement the asymmetric bounded envelope**

Use per-axis previous assist and previous delta. Far correctly directed error uses a faster attack cap; near-target and lifecycle-boundary changes use `<=0.10` step and `<=0.10` delta-of-delta; opposite sign requests first release the old sign toward zero; `Reject`, `TrackOnly`, or `Yield` returns zero without residual accumulation. Scale caps by clamped actual `dt`, and never smooth manual or final-after-recoil output.

- [ ] **Step 4: Place dynamics between planned AI assist and recoil**

Compute raw AI delta as `post_ai - manual`, pass only that delta through dynamics, reconstruct `manual + shaped_assist`, then run ADS-specific policies only when their own authority is active. Recoil remains the final feed-forward stage and retains the existing contract test output.

- [ ] **Step 5: Tune only against the full scorecard**

Run the continuity suite after each single constant change. Accept constants only when `assist_delta_p95<=0.10`, `final_jerk_p95<=0.15`, opposing/plateau duration `<=8ms`, manual preservation `>=0.90`, takeover `<=60ms`, and tracking/error gates remain within design allowances.

- [ ] **Step 6: Record A4 and commit**

Write `native_gamepad_benchmark_refactor_A4_assist_dynamics_20260713.json`, then commit:

```powershell
git add native/controller_native
git commit -m "Smooth authorized bodylock assist detail"
```

## Task 10: Add ownership telemetry and remove obsolete provider state

**Files:**

- Modify: `native/controller_native/output_mixer.h`
- Modify: `native/runtime_app/runtime_telemetry.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/telemetry_target_identity.h`
- Modify: `native/runtime_app/telemetry_target_identity.cpp`
- Modify: `native/telemetry_native/aim_perf_file_logger.h`
- Modify: `native/telemetry_native/aim_perf_file_logger.cpp`
- Modify: `native/controller_native/target_snapshot_provider.h`
- Modify: `native/controller_native/target_snapshot_provider.cpp`

- [ ] **Step 1: Add failing serialization tests for ownership fields**

Require selected track ID, backing observation/frame, real observation age, sigma, ambiguity, authority/reason, lifecycle/reason, requested assist, shaped assist, manual, before-recoil, recoil, and final stick.

- [ ] **Step 2: Populate fields from their owning components**

No telemetry layer infers authority or target identity. JSON names are stable snake-case fields such as `selected_track_id`, `track_observation_age_ms`, `assist_authority`, `assist_authority_reason`, `bodylock_lifecycle`, `bodylock_transition_reason`, `requested_assist_x`, and `shaped_assist_x`.

- [ ] **Step 3: Remove replaced duplicate state**

Delete provider candidate/committed target coordinates, projection-hold authority, middle-layer selector state, and query-time timestamp rewrites once equivalent explicit components are active. Retain only ADS-specific state that is still consumed by ADS tests; do not move ADS tuning into this refactor.

- [ ] **Step 4: Run telemetry and pipeline contract tests**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_runtime_telemetry_tests cod_native_telemetry_target_identity_tests cod_native_benchmark_metrics_tests -- /m
& native\vision_native\build\Release\cod_native_runtime_telemetry_tests.exe
& native\vision_native\build\Release\cod_native_telemetry_target_identity_tests.exe
& native\vision_native\build\Release\cod_native_benchmark_metrics_tests.exe
cmd /c scripts\verify\native_pipeline_contract.bat
```

Expected: all PASS.

- [ ] **Step 5: Commit observability and cleanup**

```powershell
git add native/controller_native native/runtime_app native/telemetry_native
git commit -m "Trace tracker authority and bodylock ownership"
```

## Task 11: Run the algorithm gate and wire or retire inactive tracker configuration

**Files:**

- Modify if evidence requires: `native/tracking_native/fps_reference_tracker.cpp`
- Modify if evidence requires: `native/pipeline_contract/tracker_config.h`
- Modify if evidence requires: `native/controller_native/runtime_config.cpp`
- Modify if evidence requires: `config.native.example.toml`
- Create locally: `runs/native_perf/native_gamepad_benchmark_refactor_A5_tracker_ablation_20260713.json`

- [ ] **Step 1: Compare A4 estimator evidence before changing the filter**

Inspect prediction error, innovation, sigma, ambiguity, ID switches, observation age, and ego residual. If A1-A4 pass and the remaining estimator error is within gates, record `A5 not required` in the scorecard and do not replace CV Kalman.

- [ ] **Step 2: Prove whether configured backend parameters are active**

Add focused tests that vary `velocity_lowpass_alpha` and `weak_observation_velocity_decay`. A parameter that produces identical estimates is either wired to the FPS backend with a documented mapping or removed from the active compact config and reported as inactive.

- [ ] **Step 3: Run one-variable estimator ablations only when Step 1 proves need**

Each run changes one of process noise, measurement noise, coast decay, or association threshold and writes a separate artifact. A different estimator is tested only after these runs and must beat CV Kalman on the complete scorecard.

- [ ] **Step 4: Commit only demonstrated configuration behavior**

```powershell
git add native/tracking_native native/pipeline_contract native/controller_native/runtime_config.cpp config.native.example.toml
git commit -m "Align active tracker configuration with measured behavior"
```

Skip the commit when no source/config change is justified.

## Task 12: Full acceptance, release fallback recheck, and `dev` integration

**Files:**

- Create: `docs/project/NATIVE_TRACKER_AUTHORITY_BODYLOCK_REFACTOR_SCORECARD.md`
- Create locally: final benchmark and performance artifacts under `runs/native_perf/`
- Update after confirmation through Agent Context Sync: `.agent-context/handoff.md`, `.agent-context/session-log.md`, and a new accepted decision record

- [ ] **Step 1: Build every affected target from the refactor worktree**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_runtime cod_native_controller_tests cod_native_target_selector_tests cod_native_target_snapshot_tests cod_native_output_validation_tests cod_native_body_lock_plan_tests cod_native_track_memory_tests cod_native_assist_authority_tests cod_native_bodylock_lifecycle_tests cod_native_benchmark_metrics_tests cod_native_gamepad_benchmark cod_native_vlock_defect_tests cod_native_recoil_contract_tests cod_native_aimlab_benchmark cod_native_aimlab_benchmark_tests -- /m
```

Expected: build exits `0`.

- [ ] **Step 2: Run focused tests and contracts**

Run every built test executable plus `scripts\verify\native_pipeline_contract.bat`. Expected: all exit `0`, including gamepad `--self-test`.

- [ ] **Step 3: Run final full benchmark suites**

Write final full, bodylock-continuity, selector-intent, AimLab, random-FOV, scheduler, telemetry, and vision performance artifacts. Compare A0-A5 in the scorecard with exact deltas.

- [ ] **Step 4: Enforce the design acceptance gates mechanically**

The acceptance script fails unless:

- authority-loss manual overrides are `0`;
- max excess final delta is `<=0.15`;
- assist delta p95 and final jerk p95 are `<=0.10` and `<=0.15`;
- opposing assist and gain plateau are `<=8ms`;
- manual preservation is `>=0.90`, reversal `<=20ms`, takeover `<=60ms`;
- every positive bodylock scenario retains `>=95%` of its positive continuity target frames;
- dropout/sustain, error, overshoot, wrong-target, user-fight, fire, vision, recoil, and performance gates match Section 12 of the design.

- [ ] **Step 5: Reverify the frozen release and clean worktrees**

```powershell
& scripts\verify\verify_native_release.ps1 -ReleaseRoot 'D:\work\AI\yolo-study-001\artifacts\releases\native-runtime-pre-tracker-refactor-20260713'
git status --short
git -C D:\work\AI\yolo-study-001 status --short
```

Expected: release verification exits `0`; refactor worktree is clean after final commit; main `dev` is clean.

- [ ] **Step 6: Commit scorecard and final verification evidence**

```powershell
git add docs/project/NATIVE_TRACKER_AUTHORITY_BODYLOCK_REFACTOR_SCORECARD.md
git commit -m "Document tracker authority refactor acceptance"
```

- [ ] **Step 7: Update project context through a reviewed SyncSet**

Propose a SyncSet containing the new objective/state, final benchmark evidence, accepted architecture decision, release fallback, commit range, and remaining live-validation risk. Exclude secrets and bulky artifact contents. Apply only after user authorization already given for project-context maintenance or a fresh explicit confirmation.

- [ ] **Step 8: Synchronize to `dev` only after every gate passes**

From clean main `dev`, merge the isolated branch with a non-destructive merge commit, rerun the focused smoke set, and confirm the rollback tag and release remain unchanged. Do not force-push or rewrite history.
