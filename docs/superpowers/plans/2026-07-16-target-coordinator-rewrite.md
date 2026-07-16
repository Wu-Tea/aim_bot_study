# Target Coordinator Pipeline Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the duplicated native vision/tracker/controller authority chain with one target coordinator, one intent interpretation, one target plan, separate ADS/BodyLock controllers, one dynamics shaper, one autofire owner, and safe debug-log session lifecycle management.

**Architecture:** Vision emits stateless observations. `IntentFilter` interprets raw sticks once. `TargetCoordinator` is the sole owner of identity, hold, prediction, mode recommendation, response learning, and fire authority, publishing an immutable `TargetPlan`. ADS and BodyLock generate commands from that plan; one `AimDynamicsShaper` and one transparent mixer produce output. The new implementation is benchmarked beside the legacy chain only in development builds, then production wiring switches once and legacy stateful policies are deleted.

**Tech Stack:** C++17, CMake/MSBuild, Catch-style existing native test executables, JSONL telemetry, Python benchmark/report tools, Windows XInput/SDL runtime, existing TensorRT vision service.

**Design and baseline:**

- `docs/superpowers/specs/2026-07-16-target-coordinator-rewrite-design.md`
- `docs/project/REFACTOR_B_BASELINE_20260716.md`
- Raw baseline: `D:/work/AI/yolo-study-001/runs/native_perf/refactor_b_baseline_20260716_e6c1f2f`

---

## Phase 1 — Freeze Contracts and RED Fixtures

### Task 1: Add immutable observation, intent, and plan contracts

**Files:**

- Create: `native/pipeline_contract/vision_observation.h`
- Create: `native/pipeline_contract/intent_state.h`
- Create: `native/pipeline_contract/target_plan.h`
- Create: `native/pipeline_contract/target_plan_contract_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing contract tests**

Cover default-safe construction, finite/range invariants, explicit lifecycle and suppression enums, fixed-capacity horizon storage, trivially copyable publication payload, and absence of owning heap containers in `TargetPlan`.

**Step 2: Run the contract test target and confirm RED**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_target_plan_contract_tests -- /m
native\vision_native\build\Release\cod_native_target_plan_contract_tests.exe
```

Expected: build fails because the contracts do not exist.

**Step 3: Implement minimal contracts**

Use enum classes rather than overlapping booleans. Keep all published arrays fixed-size. `TargetPlan` must contain target identity/lifecycle, current/predicted error, motion label, reliability, authority limits, response estimate/confidence, mode recommendation, fire authority/reason, and a short fixed horizon.

**Step 4: Re-run and confirm GREEN**

**Step 5: Commit**

```powershell
git add native/pipeline_contract native/vision_native/CMakeLists.txt
git commit -m "refactor: define target pipeline contracts"
```

### Task 2: Extend deterministic benchmarks before changing behavior

**Files:**

- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark_tests.cpp`
- Modify: `native/controller_native/cod_native_gamepad_benchmark.cpp`
- Modify: `native/controller_native/aimlab_benchmark.cpp`
- Create: `native/controller_native/target_coordinator_acceptance_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Add RED scenarios**

Add drift-vs-intent, left-strafe onset/reversal/release, same/opposite target movement, scope-border occlusion, overlapping ID churn, jump-apex/fall, and smooth reacquisition fixtures. Preserve seeds `1337` and `12345`.

**Step 2: Add explicit failure metrics**

Record ownership gaps, mode transitions, response latency, wrong-way frames, output jerk/reversal, BodyLock continuity, 50 px overshoot, and manual/AI conflict. Do not accept a single aggregate score.

**Step 3: Run and capture expected RED**

```powershell
native\vision_native\build\Release\cod_native_left_stick_motion_benchmark.exe --require-fixed
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --self-test
native\vision_native\build\Release\cod_native_live_failure_benchmark.exe
native\vision_native\build\Release\cod_native_target_coordinator_acceptance_tests.exe
```

Expected: new coordinator tests fail; existing gamepad/live-failure defects remain visible.

**Step 4: Commit only harness changes**

```powershell
git add native/controller_native native/vision_native/CMakeLists.txt
git commit -m "test: freeze target coordinator failure scenarios"
```

---

## Phase 2 — One Intent Interpretation and Online Response Estimate

### Task 3: Implement adaptive gamepad intent filtering

**Files:**

- Create: `native/controller_native/intent_filter.h`
- Create: `native/controller_native/intent_filter.cpp`
- Create: `native/controller_native/intent_filter_tests.cpp`
- Modify: `native/controller_native/controller_tick_context.h`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing tests**

Test neutral bias convergence, noise envelope, no learning during sustained input, onset/reversal/release phases, `0.0118` drift classification, genuine correction classification, and unchanged raw-stick values.

**Step 2: Run RED**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release --target cod_native_intent_filter_tests -- /m
native\vision_native\build\Release\cod_native_intent_filter_tests.exe
```

**Step 3: Implement minimal filter**

Use bounded per-axis bias/noise estimates, hysteresis, and timestamped phases. The filter returns `IntentState`; it never rewrites raw controller input.

**Step 4: Run GREEN and the existing left-stick benchmark**

**Step 5: Commit**

```powershell
git add native/controller_native/intent_filter* native/controller_native/controller_tick_context.h native/vision_native/CMakeLists.txt
git commit -m "refactor: centralize gamepad intent filtering"
```

### Task 4: Implement bounded online ADS response estimation

**Files:**

- Create: `native/controller_native/control_response_estimator.h`
- Create: `native/controller_native/control_response_estimator.cpp`
- Create: `native/controller_native/control_response_estimator_tests.cpp`
- Modify: `native/pipeline_contract/target_plan.h`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing tests**

Test convergence under clean excitation, no update under target acceleration/occlusion/ID ambiguity/right-stick correction, bounded coefficient, confidence decay, ADS epoch reset, warm start inside one process, and safe zero-confidence fallback.

**Step 2: Run RED**

**Step 3: Implement confidence-weighted recursive/exponential estimator**

Estimate reticle-relative response rather than weapon identity. Require clean-window evidence supplied by the future coordinator; do not infer cleanliness internally from duplicate thresholds.

**Step 4: Run GREEN and add one benchmark convergence metric**

**Step 5: Commit**

```powershell
git add native/controller_native/control_response_estimator* native/pipeline_contract/target_plan.h native/vision_native/CMakeLists.txt
git commit -m "feat: learn ADS response without weapon profiles"
```

---

## Phase 3 — Unique Target Owner

### Task 5: Make vision selection publish stateless observation evidence

**Files:**

- Modify: `native/vision_native/include/vision_native/target_selector.h`
- Modify: `native/vision_native/src/target_selector.cpp`
- Modify: `native/vision_native/src/target_selector_tests.cpp`
- Modify: `native/runtime_app/vision_controller_adapter.h`
- Modify: `native/runtime_app/vision_controller_adapter.cpp`

**Step 1: Write tests for observation-only output**

Require all candidates, measured cue/size/reliability/freshness, and no active/pending/owned/fire-latch behavior in the new API. Keep the old API available only under a development benchmark compile definition until cutover.

**Step 2: Run RED**

**Step 3: Extract observation construction**

Do not change inference, CUDA preprocessing, ROI capture, or cue calculations. Continuous size/reliability weighting belongs in the observation contract.

**Step 4: Run target-selector and vision-service tests**

**Step 5: Commit**

```powershell
git add native/vision_native native/runtime_app/vision_controller_adapter.*
git commit -m "refactor: publish stateless vision observations"
```

### Task 6: Implement TargetCoordinator association and lifecycle

**Files:**

- Create: `native/controller_native/target_coordinator.h`
- Create: `native/controller_native/target_coordinator.cpp`
- Create: `native/controller_native/target_coordinator_tests.cpp`
- Modify: `native/controller_native/control_response_estimator.h`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing lifecycle tests**

Cover none/observed/coasting/reacquiring transitions, candidate dwell, evidence-margin switching, intent rejection, hold-budget exhaustion, stale observation rejection, border occlusion, and atomic single-owner invariants.

**Step 2: Run RED**

**Step 3: Implement one committed target and bounded association candidates**

Use predicted position, overlap/scale, cue compatibility, and intent consistency. Centralize every lifecycle timer here.

**Step 4: Run GREEN and coordinator acceptance fixtures**

**Step 5: Commit**

```powershell
git add native/controller_native/target_coordinator* native/controller_native/control_response_estimator.h native/vision_native/CMakeLists.txt
git commit -m "feat: add unique target coordinator lifecycle"
```

### Task 7: Add motion prediction, left-strafe compensation, and short plan

**Files:**

- Modify: `native/controller_native/target_coordinator.h`
- Modify: `native/controller_native/target_coordinator.cpp`
- Modify: `native/controller_native/target_coordinator_tests.cpp`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`

**Step 1: Write failing prediction tests**

Cover constant velocity, bounded acceleration, strafe reversal, left-stick feed-forward with low/high response confidence, jump/fall classification persistence, occlusion uncertainty decay, and bounded reacquisition innovation.

**Step 2: Implement compact alpha-beta-gamma state and fixed horizon**

Use image-relative coordinates. Apply learned left-stick response only as confidence-weighted feed-forward. Never directly multiply `left_x` by a global pixel constant.

**Step 3: Run GREEN and left-stick benchmark**

Expected: `--require-fixed` remains passing and new opposite-direction/response-learning gates pass.

**Step 4: Commit**

```powershell
git add native/controller_native/target_coordinator* native/controller_native/left_stick_motion_defect_benchmark.cpp
git commit -m "feat: plan relative target motion from intent"
```

### Task 8: Publish TargetPlan to the 1 kHz controller without hot-path locks

**Files:**

- Create: `native/controller_native/target_plan_publisher.h`
- Create: `native/controller_native/target_plan_publisher_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/controller_native/controller_pipeline.h`
- Modify: `native/controller_native/controller_pipeline.cpp`

**Step 1: Write publication tests**

Test coherent snapshots across writer/readers, monotonic generation, no torn plan, and O(1) read behavior. Add an allocation counter around controller reads.

**Step 2: Implement double-buffered publication**

Coordinator updates on observations and intent transitions; controller reads the last immutable plan each tick.

**Step 3: Run concurrency tests under repetition**

```powershell
1..100 | ForEach-Object { native\vision_native\build\Release\cod_native_target_plan_publisher_tests.exe }
```

**Step 4: Commit**

```powershell
git add native/controller_native/target_plan_publisher* native/runtime_app/runtime_loop.* native/controller_native/controller_pipeline.*
git commit -m "refactor: publish immutable target plans"
```

---

## Phase 4 — Controllers and Single Output Shaper

### Task 9: Implement ADS acquisition controller with one terminal brake

**Files:**

- Create: `native/controller_native/ads_acquisition_controller.h`
- Create: `native/controller_native/ads_acquisition_controller.cpp`
- Create: `native/controller_native/ads_acquisition_controller_tests.cpp`
- Modify: `native/pipeline_contract/target_plan.h`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing tests**

Cover large-error strength, drift not weakening ADS, genuine manual correction, learned-response stopping error, no-confidence fallback, target reliability scaling, handoff completion, and overshoot limits.

**Step 2: Implement point-acquisition/time-to-go control**

One controller owns terminal settling. It may not call `AdsCarryBrakePolicy`, `AdsCompletionGate`, or BodyLock policies.

**Step 3: Run tests and ADS-focused gamepad/AimLab suites**

**Step 4: Commit**

```powershell
git add native/controller_native/ads_acquisition_controller* native/pipeline_contract/target_plan.h native/vision_native/CMakeLists.txt
git commit -m "feat: control ADS acquisition from target plan"
```

### Task 10: Implement BodyLock trajectory-follow controller

**Files:**

- Create: `native/controller_native/bodylock_follow_controller.h`
- Create: `native/controller_native/bodylock_follow_controller.cpp`
- Create: `native/controller_native/bodylock_follow_controller_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing tests**

Cover moving/slide/occluded/jump/fall plans, confidence decay, continuous strength near center when motion requires it, smooth plan loss, and raw manual availability.

**Step 2: Implement feed-forward plus bounded residual feedback**

Do not add lifecycle, hold, carry, brake, or short-plan state. All of that comes from `TargetPlan`.

**Step 3: Run GREEN plus BodyLock benchmark subset**

Require nonzero BodyLock frames and improved dropout/low-close without worse chatter/spikes.

**Step 4: Commit**

```powershell
git add native/controller_native/bodylock_follow_controller* native/vision_native/CMakeLists.txt
git commit -m "feat: follow coordinator plans in bodylock"
```

### Task 11: Consolidate stateful shaping and transparent mixing

**Files:**

- Create: `native/controller_native/aim_dynamics_shaper.h`
- Create: `native/controller_native/aim_dynamics_shaper.cpp`
- Create: `native/controller_native/aim_dynamics_shaper_tests.cpp`
- Modify: `native/controller_native/output_mixer.h`
- Modify: `native/controller_native/output_mixer.cpp`
- Modify: `native/controller_native/output_validation_policy.h`
- Modify: `native/controller_native/output_validation_policy.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`

**Step 1: Write failing shaper/mixer tests**

Test acceleration, jerk, reversal, innovation shock, confidence decay, manual arbitration, finite/range validation, exact left-stick passthrough, and exact `raw manual + shaped AI + recoil` composition before clamp.

**Step 2: Implement one stateful shaper**

Make `OutputValidationPolicy` stateless finite/range validation only. Recoil stays outside the AI shaper.

**Step 3: Wire development path in NativeGamepadController**

Use a compile-time benchmark/development switch only. Record both requested and shaped AI values.

**Step 4: Run controller/recoil/left-stick tests**

**Step 5: Commit**

```powershell
git add native/controller_native/aim_dynamics_shaper* native/controller_native/output_mixer.* native/controller_native/output_validation_policy.* native/controller_native/native_gamepad_controller.*
git commit -m "refactor: use one AI dynamics shaper and mixer"
```

### Task 12: Make autofire consume TargetPlan as the sole fire owner

**Files:**

- Modify: `native/controller_native/auto_fire_gate.h`
- Modify: `native/controller_native/auto_fire_gate.cpp`
- Modify: `native/controller_native/auto_fire_gate_tests.cpp`
- Modify: `native/vision_native/include/vision_native/target_selector.h`
- Modify: `native/vision_native/src/target_selector.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`

**Step 1: Write failing positive and suppression-reason tests**

Cover ready fire, stale/coasting/ambiguous/large-error/high-velocity/manual-rejection suppression, cadence, and aim-only mode. Require one enumerated reason per suppression.

**Step 2: Refactor AutoFireGate input to TargetPlan**

Remove vision-side fire hold/latch state. Do not change configured physical fire output semantics.

**Step 3: Run autofire, controller, and live-failure tests**

**Step 4: Commit**

```powershell
git add native/controller_native/auto_fire_gate* native/controller_native/native_gamepad_controller.cpp native/vision_native/include/vision_native/target_selector.h native/vision_native/src/target_selector.cpp
git commit -m "refactor: make target plan drive autofire"
```

---

## Phase 5 — Debug Sessions, Fresh Logs, and Safe Cleanup

### Task 13: Add log-session lifecycle and atomic fresh-session discovery

**Files:**

- Create: `native/runtime_app/log_session_manager.h`
- Create: `native/runtime_app/log_session_manager.cpp`
- Create: `native/runtime_app/log_session_manager_tests.cpp`
- Modify: `native/runtime_app/runtime_telemetry.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/aim_perf_file_logger.h`
- Modify: `native/runtime_app/aim_perf_file_logger.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Write failing filesystem tests**

Use temporary roots. Test unique session ids, manifest fields, atomic `fresh_session.json`, `.active` to `.closed`, both telemetry writers sharing one directory, rotation without cross-session overwrite, and clean shutdown metadata.

**Step 2: Implement LogSessionManager**

Create `sessions/<UTC>_<pid>_<nonce>/session.json`; publish fresh manifest by write-temp/rename; expose child paths to telemetry and aim trace writers. Store byte count, file list, start/close time, commit, config hash, clean/crashed status, and optional pin.

**Step 3: Integrate writers**

Change `RuntimeTelemetry` rotation from modulo slots to monotonic part numbers inside the session. Change `AimPerfFileLogger` to use the same session. The active session is never truncated for retention.

**Step 4: Run telemetry/log tests and 1M benchmark**

```powershell
native\vision_native\build\Release\cod_native_runtime_telemetry_tests.exe
native\vision_native\build\Release\cod_native_log_session_manager_tests.exe
native\vision_native\build\Release\cod_native_telemetry_benchmark.exe --records 1000000
```

Require zero drops and no material enqueue regression.

**Step 5: Commit**

```powershell
git add native/runtime_app/log_session_manager* native/runtime_app/runtime_telemetry.* native/runtime_app/aim_perf_file_logger.* native/runtime_app/runtime_loop.* native/vision_native/CMakeLists.txt
git commit -m "feat: manage fresh debug log sessions"
```

### Task 14: Add fast safe list/prune tooling and configuration

**Files:**

- Create: `tools/manage_native_logs.py`
- Create: `tools/tests/test_manage_native_logs.py`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `config.toml`
- Modify: `native/runtime_app/main.cpp`
- Create: `docs/project/NATIVE_LOG_SESSIONS.md`

**Step 1: Write failing cleanup tests**

Test list/dry-run output, whole-session pruning, fresh/newest/pinned/active protection, stale-active PID/age checks, age/count/byte ordering, path containment, link refusal, corrupt manifest quarantine/reporting, and reclaimed-byte totals.

**Step 2: Implement list and prune commands**

```powershell
python tools/manage_native_logs.py list --root runs/native_perf
python tools/manage_native_logs.py prune --root runs/native_perf --dry-run
python tools/manage_native_logs.py prune --root runs/native_perf --max-age-days 7 --keep-latest 2 --max-total-gb 20
```

Prune reads only manifests and directory metadata; it never scans JSONL contents. Deletion candidates must resolve inside `<root>/sessions`, be closed/eligible, and not be fresh, pinned, or protected.

**Step 3: Add debug defaults and optional startup cleanup**

Set high-rate trace defaults off. Add session root, cleanup-enabled, minimum age, keep-latest, maximum total GB, and abandoned-session safety age. Startup cleanup must call the same tested policy code or stay disabled; do not create a second retention implementation.

**Step 4: Document recovery and fresh-session workflow**

Include how to enable debug, resolve `fresh_session.json`, pin a useful case, dry-run cleanup, prune, and recover a stale active session.

**Step 5: Run tests and commit**

```powershell
python -m pytest tools/tests/test_manage_native_logs.py -q
native\vision_native\build\Release\cod_native_runtime_config_tests.exe
git add tools/manage_native_logs.py tools/tests/test_manage_native_logs.py native/controller_native/runtime_config* config.toml native/runtime_app/main.cpp docs/project/NATIVE_LOG_SESSIONS.md
git commit -m "feat: prune closed native log sessions safely"
```

---

## Phase 6 — Atomic Production Cutover and Deletion

### Task 15: Switch runtime wiring to the new pipeline

**Files:**

- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/controller_native/controller_pipeline.h`
- Modify: `native/controller_native/controller_pipeline.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Run dual-path benchmark comparison**

Use identical observations and seeds but compare scenario metrics rather than tick equality. Save both artifacts.

**Step 2: Confirm all cutover gates**

Gamepad self-test and live-failure must pass; new coordinator cases pass; BodyLock chase has nonzero continuity; ADS overshoot/adversarial metrics meet the design criteria; hot-path allocations are zero.

**Step 3: Replace production construction/wiring**

Remove the development switch from normal runtime configuration. Runtime creates `IntentFilter`, `TargetCoordinator`, publisher, ADS/BodyLock controllers, dynamics shaper, one autofire gate, mixer, and recoil feed-forward.

**Step 4: Run focused tests and runtime smoke**

```powershell
scripts\verify\native_runtime_performance_acceptance.ps1 -BuildDirectory native/vision_native/build -OutputDirectory runs/native_perf/refactor_b_cutover_acceptance
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --max-ticks 1200
```

**Step 5: Commit**

```powershell
git add native/runtime_app native/controller_native native/vision_native/CMakeLists.txt
git commit -m "refactor: cut runtime over to target coordinator"
```

### Task 16: Delete obsolete stateful policies and legacy wiring

**Files:**

- Delete: `native/controller_native/ads_carry_brake_policy.h`
- Delete: `native/controller_native/ads_carry_brake_policy.cpp`
- Delete: `native/controller_native/body_lock_short_plan_policy.h`
- Delete: `native/controller_native/body_lock_short_plan_policy.cpp`
- Delete: `native/controller_native/assist_authority_policy.h`
- Delete: `native/controller_native/assist_authority_policy.cpp`
- Delete: `native/controller_native/bodylock_lifecycle.h`
- Delete: `native/controller_native/bodylock_lifecycle.cpp`
- Delete or reduce to stateless helpers: `native/controller_native/target_snapshot_provider.h`
- Delete or reduce to stateless helpers: `native/controller_native/target_snapshot_provider.cpp`
- Delete superseded tests for the files above
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/aim_assist_dynamics.h`
- Modify: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

**Step 1: Prove no production references remain**

```powershell
Get-ChildItem native -Recurse -File -Include *.cpp,*.h | Select-String -Pattern 'AdsCarryBrakePolicy|BodyLockShortPlanPolicy|AssistAuthorityPolicy|BodylockLifecycle'
```

Expected: only deletion notes/tests before removal.

**Step 2: Delete obsolete implementations and CMake entries**

Do not leave compatibility aliases or dead feature flags.

**Step 3: Reduce `AiAim` and dynamics wrappers**

Keep only genuinely shared stateless math, or delete them if the new controllers fully replace them.

**Step 4: Build every target**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native/vision_native/build --config Release -- /m
```

**Step 5: Commit**

```powershell
git add -A native
git commit -m "refactor: remove duplicate aim authority policies"
```

### Task 17: Full same-seed acceptance and scorecard

**Files:**

- Create: `docs/project/REFACTOR_B_ACCEPTANCE_20260716.md`
- Modify if thresholds are formally promoted: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

**Step 1: Use verification-before-completion**

Rebuild Release from the final commit. Run every command in `REFACTOR_B_BASELINE_20260716.md` with the same seeds and new artifact directory. Also run the complete CTest suite and pipeline contract.

**Step 2: Produce side-by-side scorecard**

Report every baseline scenario, new fixture, exit code, seed, commit, command, artifact hash, controller timing, allocation count, and log cleanup test. Call out improvements and any accepted tradeoff explicitly.

**Step 3: Verify worktree hygiene**

```powershell
git diff --check
git status --short
```

Expected: no diff-check errors and clean worktree after committing the scorecard.

**Step 4: Commit final evidence**

```powershell
git add docs/project/REFACTOR_B_ACCEPTANCE_20260716.md docs/project/NATIVE_CONTROLLER_BENCHMARKS.md
git commit -m "docs: record target coordinator acceptance"
```

**Step 5: Use superpowers:requesting-code-review, then superpowers:finishing-a-development-branch**

Do not merge to `dev` or replace the user's runtime executable until review and explicit integration choice.
