# ADS Epoch And Color Cue Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ADS snap consumable only once per physical ADS press, preserve hard green-friendly rejection, and preserve yellow cue as bounded enemy-candidate assistance.

**Architecture:** `TargetCoordinator` owns the ADS epoch lifecycle and may never re-enter `AdsAcquire` after that epoch's snap window is consumed. `VisionTargetSelector` remains the sole owner of color classification: green removes a candidate before ranking, while yellow may lower pickup confidence, improve ranking, and bridge a short occlusion but may not create aim/fire authority without a person detection. Production-path C++ tests make these behaviors refactor-resistant.

**Tech Stack:** C++20, CMake/MSBuild Release native tests, Python unittest bridge tests, native benchmark executables.

---

### Task 1: Lock ADS Snap To One Physical ADS Epoch

**Files:**
- Modify: `native/controller_native/target_coordinator.h`
- Modify: `native/controller_native/target_coordinator.cpp`
- Test: `native/controller_native/target_pipeline_integration_tests.cpp`

- [ ] **Step 1: Write the failing integration test**

Add a scenario that presses ADS, reaches BodyLock, keeps LT held past `ads_snap_window_ms`, changes to a far/new target, and requires BodyLock rather than ADS. Then release and press LT again and require ADS to re-arm.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build native/vision_native/build --config Release --target cod_native_target_pipeline_integration_tests && native/vision_native/build/Release/cod_native_target_pipeline_integration_tests.exe`

Expected: FAIL because the held-ADS target change currently re-enters `AdsAcquire`.

- [ ] **Step 3: Implement one epoch latch in TargetCoordinator**

Pass ADS epoch start time into `begin_ads_epoch`, store the snap deadline/consumed state, and allow `BodyLockFollow -> AdsAcquire` only while the current physical ADS epoch is still inside its initial snap window. Do not add a controller output override.

- [ ] **Step 4: Run focused tests**

Run the integration test plus `cod_native_target_coordinator_tests.exe` and `cod_native_controller_tests.exe`.

- [ ] **Step 5: Commit**

Commit controller lifecycle and its regression tests as one atomic change.

### Task 2: Make Friendly And Yellow Cue Production Contracts Explicit

**Files:**
- Modify: `native/vision_native/src/target_selector_tests.cpp`
- Modify only if a failing production-path test exposes a defect: `native/vision_native/src/target_selector.cpp`

- [ ] **Step 1: Add production C++ contract tests**

Add BGRA ROI fixtures proving: a green-marked high-confidence person is rejected; green friendly plus yellow enemy selects only the enemy; yellow lowers pickup confidence to the cue-assisted threshold; yellow-only pixels with no person detection produce neither target nor fire authority; and yellow cue hold never grants fire authority.

- [ ] **Step 2: Run the new tests**

Run: `cmake --build native/vision_native/build --config Release --target cod_native_target_selector_tests && native/vision_native/build/Release/cod_native_target_selector_tests.exe`

Expected: existing behavior should pass where intact. Any failure identifies a production-path gap before code changes.

- [ ] **Step 3: Apply only evidence-required selector fixes**

Keep green as an early hard rejection and yellow as bounded auxiliary evidence. Do not restore standalone yellow aim authority, duplicate color scanners, or a second selector.

- [ ] **Step 4: Run native and Python bridge selector suites**

Run `cod_native_target_selector_tests.exe` and the focused `tests.test_native_vision_targeting_bridge` green/yellow cases.

- [ ] **Step 5: Commit**

Commit selector contract tests and any evidence-required fix separately from ADS lifecycle.

### Task 3: Correct Benchmarks And Verify Runtime

**Files:**
- Modify only if required by failing semantics: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

- [ ] **Step 1: Verify benchmark input semantics**

ADS acquisition cohorts must create a physical ADS rising edge per acquisition episode. BodyLock cohorts must hold ADS continuously and must not obtain fresh snap on spawned targets.

- [ ] **Step 2: Run fixed-seed comparisons**

Run the controller self-test, fixed seed `1337` gamepad benchmark, sustained AimLab seed `12345`, selector tests, and native pipeline contract.

- [ ] **Step 3: Build the production runtime**

Build `cod_native_runtime.exe` in the canonical `native/vision_native/build/Release` directory and record its timestamp and SHA-256.

- [ ] **Step 4: Document behavior and scores**

Record that ADS scores now measure real re-scope episodes, while continuously held target changes are BodyLock-only. Record selector color-contract results and any benchmark deltas without comparing against configuration-unproven artifacts.

- [ ] **Step 5: Final verification and commit**

Run `git diff --check`, inspect `git status`, and commit the benchmark/documentation/runtime-source changes.
