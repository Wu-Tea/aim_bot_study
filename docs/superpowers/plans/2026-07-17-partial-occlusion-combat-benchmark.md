# Partial-Occlusion Combat Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and score two deterministic native diagonal partial-occlusion scenarios: ordinary combat and the same combat distribution mixed with log-derived human errors.

**Architecture:** A small pure C++ scenario/scoring module stays independent of the production controller and has focused tests. A standalone executable adapts its frame directives to `NativeGamepadController`, maintains separate world truth and observed box geometry, then emits JSON and a console scorecard.

**Tech Stack:** C++17, CMake, existing native controller runtime, deterministic fixed-step simulation, JSON text output.

---

### Task 1: Scenario and score contracts

**Files:**
- Create: `native/controller_native/partial_occlusion_benchmark.h`
- Create: `native/controller_native/partial_occlusion_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing schedule contract tests**

Define tests that call `build_partial_occlusion_scenario(ScenarioKind::Combat, 1337)`
and `build_partial_occlusion_scenario(ScenarioKind::HumanErrors, 1337)`. Assert four
cases, gap durations `{30, 60, 110, 160}`, four diagonal direction pairs, partial
body height below full height, no deliberate error in Combat, and all four error
kinds in HumanErrors.

- [ ] **Step 2: Write failing score contract tests**

Construct `PartialOcclusionMetrics` with zero error/fight/spikes and assert every
component and overall score is 100. Construct a degraded metric set with 60 px P95
error, 30 px overshoot, 300 ms recovery, 0.20 output delta, correct-manual opposition,
and wrong-manual high-force fight; assert every affected component and overall score
are below the perfect result.

- [ ] **Step 3: Register and run the missing test target**

Add `cod_native_partial_occlusion_benchmark_tests` to CMake using the test and module
source. Configure and build it. Expected RED result: compilation fails because
`partial_occlusion_benchmark.h/.cpp` and their API do not exist.

### Task 2: Pure deterministic scenario module

**Files:**
- Create: `native/controller_native/partial_occlusion_benchmark.cpp`
- Modify: `native/controller_native/partial_occlusion_benchmark.h`

- [ ] **Step 1: Implement minimal public data types**

Add `ScenarioKind`, `HumanErrorKind`, `VisionPhase`, `ScenarioFrameDirective`,
`ScenarioCase`, `ScenarioDefinition`, `PartialOcclusionMetrics`, and
`PartialOcclusionScore`. Expose deterministic scenario construction, percentile
aggregation, and score calculation; do not include runtime controller headers.

- [ ] **Step 2: Implement the shared four-case schedule**

Generate 1000 Hz directives with Vision-frame markers every ten ticks. Keep the full
body at 84x180 px, clip the observed height during partial phases, use the four fixed
gap durations, and assign the four human-error profiles only for HumanErrors.

- [ ] **Step 3: Implement bounded component scores**

Use piecewise-linear normalization with documented acceptable/bad bands for tracking,
overshoot, recovery, smoothness, and intent. Clamp every component and weighted
overall result to `[0, 100]` and expose `formula_version = 1`.

- [ ] **Step 4: Run focused tests**

Build and run `cod_native_partial_occlusion_benchmark_tests.exe`. Expected GREEN:
all schedule, geometry-truth, and score assertions pass.

### Task 3: Production-controller runner and JSON report

**Files:**
- Create: `native/controller_native/cod_native_partial_occlusion_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add a failing executable smoke assertion**

Extend the focused test target with JSON-field formatting tests for scenario name,
formula version, component scores, raw metrics, seed, and effective config metadata.
Run and observe failure because report serialization is absent.

- [ ] **Step 2: Implement the fixed-step runner**

Load `config.toml`, run the real `NativeGamepadController` at 1000 Hz, submit Vision
at 100 Hz, update target and reticle positions independently, and apply directive
left/right sticks. During missing phases submit a processed no-target frame. During
partial/reacquisition phases submit the clipped or biased selected target while
retaining the full-body chest point as scoring truth.

- [ ] **Step 3: Aggregate controller behavior**

Record true error, axis crossings, occlusion peak, time to 20 px, output delta,
mode changes, geometry bias, correct-manual opposition, and wrong-manual high-force
fight. Feed the raw result through `score_partial_occlusion_metrics`.

- [ ] **Step 4: Write deterministic JSON and console output**

Support `--config`, `--seed`, and `--output`. Emit both scenario objects in one JSON
artifact with effective ADS/BodyLock/geometry config, score formula version, raw
metrics, component scores, and overall score. Print one concise line per scenario.

- [ ] **Step 5: Run focused tests again**

Expected GREEN: schedule, scoring, and serialization tests pass.

### Task 4: Build, benchmark, and verify

**Files:**
- Create: `runs/benchmarks/partial_occlusion_current_seed1337.json` (ignored artifact)
- Create: `runs/benchmarks/partial_occlusion_current_seed1337.console.txt` (ignored artifact)

- [ ] **Step 1: Build Release targets**

Build `cod_native_partial_occlusion_benchmark_tests`,
`cod_native_partial_occlusion_benchmark`, and `cod_native_controller_tests` in
Release. Expected: all targets exit 0.

- [ ] **Step 2: Run focused and controller tests**

Run the two test executables. Expected: zero failures.

- [ ] **Step 3: Run the two-scenario benchmark**

Run with current `config.toml`, seed 1337, and the artifact paths above. Capture the
ordinary-combat and human-error scores plus every component and raw metric.

- [ ] **Step 4: Run the pipeline contract**

Execute `scripts/verify/native_pipeline_contract.bat`. Expected: PASS because no
production controller boundary changed.

- [ ] **Step 5: Review the diff and results**

Run `git diff --check`, `git status --short`, and inspect JSON provenance. Confirm no
runtime/config behavior files changed, both scenarios use the same motion/vision
distribution, and only the manual error directives differ.

