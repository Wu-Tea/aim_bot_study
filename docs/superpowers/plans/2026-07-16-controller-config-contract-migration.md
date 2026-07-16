# Controller Configuration Contract Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the live controller and profile-faithful benchmarks consume one provable canonical configuration, restore the `0.365` target geometry setting to the new tracker path, and quarantine legacy aliases and stress overrides.

**Architecture:** Resolve configuration once into component-owned values plus source metadata. Resolve upright target geometry once before `TargetCoordinator` stores the observation, so ADS and BodyLock share one aim point. Route all benchmark tuning overrides through a named fixture object and serialize them; normal runs remain immutable and profile-faithful.

**Tech Stack:** C++20, custom TOML-like runtime parser, CMake/CTest-style executable tests, JSON benchmark artifacts, PowerShell verification.

---

## File structure

- Modify `native/controller_native/runtime_config.h`: add canonical tracker geometry and source/compatibility state; remove public fields only after their consumers are classified.
- Modify `native/controller_native/runtime_config.cpp`: parse canonical geometry, translate the legacy alias, validate it, and preserve deterministic precedence.
- Modify `native/runtime_app/main.cpp`: expose canonical effective values and deprecated/ignored alias sources.
- Create `native/controller_native/target_geometry.h` and `target_geometry.cpp`: one pure function for upright/wide/no-box target geometry.
- Create `native/controller_native/target_geometry_tests.cpp`: geometry and no-double-application contract.
- Modify `native/controller_native/native_gamepad_controller.cpp`: apply geometry at the fresh observation boundary before `TargetCoordinator`.
- Modify `native/controller_native/cod_native_gamepad_benchmark.cpp`: remove anonymous tuning mutations and consume explicit benchmark profiles.
- Create `native/controller_native/benchmark_config_profile.h` and `benchmark_config_profile.cpp`: immutable loaded-profile snapshot, named stress overrides, and artifact metadata.
- Create `native/controller_native/benchmark_config_profile_tests.cpp`: profile-faithful immutability and override serialization tests.
- Modify `native/vision_native/CMakeLists.txt`: register the new production sources and test executables.
- Modify `config.native.example.toml`: document only canonical live controls and the `0.365` geometry value.
- Create `docs/project/CONTROLLER_CONFIG_AUDIT_20260716.md`: field-by-field alias/internal/removed inventory with active consumers.

### Task 1: Lock canonical parsing and compatibility precedence

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `native/runtime_app/main.cpp`

- [ ] **Step 1: Write failing parser tests**

Add tests that load three temporary configurations and assert:

```cpp
// Canonical value.
"[gamepad.tracker]\naim_height_ratio = 0.365\n"
// Legacy-only alias.
"[runtime.gamepad]\nbody_lock_upper_body_ratio = 0.365\n"
// Conflict: canonical must win regardless of file order.
"[runtime.gamepad]\nbody_lock_upper_body_ratio = 0.40\n"
"[gamepad.tracker]\naim_height_ratio = 0.365\n"
```

Expected active value for all canonical/conflict cases: `0.365f`. Add invalid-range cases for `-0.01` and `1.01`.

- [ ] **Step 2: Run the parser test and verify RED**

Run:

```powershell
native\vision_native\build\Release\cod_native_runtime_config_tests.exe
```

Expected: failure because `gamepad.tracker.aim_height_ratio` is unknown or absent.

- [ ] **Step 3: Add one canonical field and centralized alias resolution**

Add `float aim_height_ratio = 0.365f;` to the active tracker runtime configuration. Track whether canonical and legacy spellings were observed while parsing; finalize once after the file is read:

```cpp
if (seen_tracker_aim_height_ratio) {
    config.gamepad.tracker.aim_height_ratio = canonical_aim_height_ratio;
    source = ConfigValueSource::User;
} else if (seen_legacy_upper_body_ratio) {
    config.gamepad.tracker.aim_height_ratio = legacy_upper_body_ratio;
    source = ConfigValueSource::DeprecatedAlias;
}
```

Do not let parser order determine precedence. Validate `0.0f <= value && value <= 1.0f`.

- [ ] **Step 4: Expose effective source and remove the old live implication**

Print:

```text
gamepad.tracker.aim_height_ratio=0.365 source=user
```

For a legacy-only file, print `source=deprecated_alias`. For a conflict, print the canonical value and a diagnostic that the alias was ignored. Do not print credentials or unrelated environment contents.

- [ ] **Step 5: Run parser/config tests and verify GREEN**

Run the config tests plus:

```powershell
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --dump-effective-config
```

Expected: tests pass and exactly one canonical geometry value is shown.

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native/runtime_config.h native/controller_native/runtime_config.cpp native/controller_native/runtime_config_tests.cpp native/runtime_app/main.cpp
git commit -m "feat: canonicalize tracker aim geometry config"
```

### Task 2: Resolve target geometry once before tracking

**Files:**
- Create: `native/controller_native/target_geometry.h`
- Create: `native/controller_native/target_geometry.cpp`
- Create: `native/controller_native/target_geometry_tests.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing pure geometry tests**

Define the desired API in tests:

```cpp
TargetGeometryInput input{
    .vision_aim = {320.0f, 250.0f},
    .body_box = {280.0f, 100.0f, 80.0f, 200.0f},
    .has_body_box = true,
};
const auto result = resolve_target_geometry(input, {.aim_height_ratio = 0.365f});
REQUIRE_NEAR(result.aim.y, 173.0f, 0.001f);
```

Add cases for a crouched box (same ratio), a wide/low box (preserve Vision Y and clamp), no box (preserve Vision point), and repeat invocation on a projected point (caller contract must not apply it during coasting).

- [ ] **Step 2: Build the new test and verify RED**

Run the CMake build for `cod_native_target_geometry_tests`; expect compilation failure because the API does not exist.

- [ ] **Step 3: Implement the pure resolver**

Use one aspect-ratio classification compatible with the previous behavior:

```cpp
const bool wide_low = box.w > 0.0f && box.h / box.w < 0.65f;
if (!input.has_body_box || box.w <= 0.0f || box.h <= 0.0f) return vision;
if (wide_low) return clamp_to_box(vision, box);
return {vision.x, box.y + box.h * std::clamp(config.aim_height_ratio, 0.0f, 1.0f)};
```

Clamp X/Y only where the design requires preserving a wide/low Vision point.

- [ ] **Step 4: Apply only at fresh observation conversion**

In `observation_batch_from`, resolve `source.aim_point_px` using `source.body_box_px` before assigning `destination.aim_px`. Apply the same rule in the fallback candidate branch. Do not modify `TargetCoordinator` coasting or prediction paths.

- [ ] **Step 5: Add ADS/BodyLock handoff integration assertion**

Extend `target_pipeline_integration_tests.cpp` so a target transitions from ADS to BodyLock and both plans retain the same resolved Y. Assert there is no second `body_height * ratio` subtraction/addition.

- [ ] **Step 6: Run geometry and controller suites**

Run:

```powershell
native\vision_native\build\Release\cod_native_target_geometry_tests.exe
native\vision_native\build\Release\cod_native_controller_tests.exe
native\vision_native\build\Release\cod_native_target_pipeline_integration_tests.exe
```

Expected: all pass.

- [ ] **Step 7: Commit**

```powershell
git add native/controller_native/target_geometry.* native/controller_native/target_geometry_tests.cpp native/controller_native/native_gamepad_controller.cpp native/controller_native/target_pipeline_integration_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "feat: resolve shared tracker aim geometry"
```

### Task 3: Make benchmark configuration provenance explicit

**Files:**
- Create: `native/controller_native/benchmark_config_profile.h`
- Create: `native/controller_native/benchmark_config_profile.cpp`
- Create: `native/controller_native/benchmark_config_profile_tests.cpp`
- Modify: `native/controller_native/cod_native_gamepad_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing profile/override tests**

Define an immutable profile wrapper and named override records:

```cpp
BenchmarkConfigProfile profile(runtime_config);
const auto faithful = profile.for_scenario("moving_chase", std::nullopt);
REQUIRE(faithful.config.gamepad.ai_aim.body_lock_box_tolerance_px == loaded_value);
REQUIRE(faithful.overrides.empty());

StressFixture fixture{"legacy-high-force", "boundary stress only"};
fixture.set("gamepad.bodylock.strength", loaded_strength, 0.72f);
const auto stressed = profile.for_scenario("moving_chase", fixture);
REQUIRE(stressed.overrides.size() == 1);
```

Test deterministic JSON serialization of field, loaded value, override value, fixture,
reason, and scenario.

- [ ] **Step 2: Build and verify RED**

Build/run `cod_native_benchmark_config_profile_tests`; expect failure because the profile API is absent.

- [ ] **Step 3: Implement profile-faithful and stress-fixture modes**

The loaded runtime config remains immutable inside `BenchmarkConfigProfile`. A scenario receives a copy only after an optional named fixture is applied. Provide typed setters for the finite set of active Tracker/ADS/BodyLock fields; reject unknown override names.

- [ ] **Step 4: Remove anonymous scenario tuning mutations**

Replace direct assignments and `std::max/std::min` mutations in scenario setup, including the blocks around the current lines 2645, 3211, 3867, 4044, 4249, 4359, 4593, and 4830. Normal scenarios receive no fixture. Boundary-only scenarios request a named fixture declared centrally.

- [ ] **Step 5: Add artifact provenance**

At the artifact root serialize:

```json
"configuration": {
  "mode": "profile-faithful",
  "effective": { "gamepad.tracker.aim_height_ratio": 0.365 },
  "overrides": []
}
```

Stress runs use `"mode":"stress-fixture"` and list every override. Add `--benchmark-mode profile-faithful|stress-fixture`; default to profile-faithful.

- [ ] **Step 6: Verify no silent mutation remains**

Add a source-level/structural test or helper-enforced compilation boundary so scenario bodies cannot mutate a shared loaded configuration. Run a text audit for remaining direct mutations and classify every result as construction of an explicit fixture or a defect.

- [ ] **Step 7: Run benchmark tests and a fixed-seed artifact smoke test**

Run:

```powershell
native\vision_native\build\Release\cod_native_benchmark_config_profile_tests.exe
native\vision_native\build\Release\cod_native_benchmark_metrics_tests.exe
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --suite all --config config.toml --benchmark-mode profile-faithful --random-fov-seed 1337 --selector-intent-seed 1337 --roi-fallback-seed 1337 --output runs\benchmarks\profile_faithful_seed1337.json
```

Expected: exit 0; artifact says profile-faithful and contains no overrides.

- [ ] **Step 8: Commit**

```powershell
git add native/controller_native/benchmark_config_profile.* native/controller_native/benchmark_config_profile_tests.cpp native/controller_native/cod_native_gamepad_benchmark.cpp native/vision_native/CMakeLists.txt
git commit -m "fix: make benchmark config provenance explicit"
```

### Task 4: Audit and prune the public controller configuration

**Files:**
- Create: `docs/project/CONTROLLER_CONFIG_AUDIT_20260716.md`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: legacy controller tests only where a retired field is removed

- [ ] **Step 1: Generate the inventory from declarations and references**

List every `GamepadAiAimConfig` field with parser key, active new-pipeline consumer,
legacy-only consumer, benchmark override, and classification. Treat references exclusively
inside tests/retired `NativeAiAim` as legacy-only, not proof of a live consumer.

- [ ] **Step 2: Write failing public-contract tests**

Create a table of documented canonical keys and assert each maps to a component construction
field. Add rejection/deprecation expectations for removed legacy spellings. The test fails
while unclassified public fields remain.

- [ ] **Step 3: Migrate meaningful aliases and internalize or remove dead controls**

Keep only fields consumed by `TargetCoordinator`, `AdsAcquisitionController`,
`BodylockFollowController`, `IntentFilter`, `AimAssistDynamicsShaper`, AutoFire, and recoil.
Move helper-only values out of the documented live schema. Remove tests whose only purpose
was to preserve a retired multi-stage controller behavior; replace them with canonical
component tests when the behavior remains required.

- [ ] **Step 4: Complete the audit document**

For every old key record one of `alias`, `internal`, or `removed`, its canonical destination
where applicable, and the exact active consumer file. Do not leave unclassified rows.

- [ ] **Step 5: Run full controller/config tests**

Run all Release executables matching controller, runtime_config, ADS, BodyLock, target,
AutoFire, left-stick, vertical-lock, telemetry, and recoil contracts. Expected: all pass.

- [ ] **Step 6: Commit**

```powershell
git add docs/project/CONTROLLER_CONFIG_AUDIT_20260716.md native/controller_native/runtime_config.* native/controller_native/*tests.cpp
git commit -m "refactor: prune retired controller configuration"
```

### Task 5: Update the canonical profile and establish a trustworthy baseline

**Files:**
- Modify: `config.native.example.toml`
- Modify: `config.toml` locally (ignored user profile; back up before editing)
- Create: `runs/config_backups/config.before-contract-migration-20260716.toml`
- Create: `runs/benchmarks/profile_faithful_seed1337.json`

- [ ] **Step 1: Back up and verify the local profile**

Copy the current local config to the named backup and verify hashes before changing it.
Use the approved candidate values including `aim_height_ratio=0.365`, ADS `1.54/1.26/150/160`, and BodyLock `0.45/0.50/80/8/0.45/0.55`.

- [ ] **Step 2: Update the tracked example and effective-config expectations**

Document parameter semantics, especially that ADS `range_px`, BodyLock
`activation_range_px`, and `tolerance_px` are distinct. Remove retired documented keys.

- [ ] **Step 3: Build Release and run the complete verification matrix**

Build runtime and all modified tests. Run profile-faithful benchmark seed 1337, left-stick
`--require-fixed`, vertical BodyLock defects, scheduler, telemetry, and live failure
benchmarks. Record command, seed, commit, effective config, and artifact path.

- [ ] **Step 4: Compare only compatible artifacts**

Compare the new result only against artifacts that prove equivalent configuration mode
and metadata. Mark older silent-override artifacts `configuration-unproven`; do not delete
or relabel their raw numbers.

- [ ] **Step 5: Commit tracked profile/docs changes**

```powershell
git add config.native.example.toml docs/project/CONTROLLER_CONFIG_AUDIT_20260716.md
git commit -m "docs: publish canonical controller profile"
```

### Task 6: Final verification and handoff

**Files:**
- Update: `.agent-context/handoff.md` if present
- Update: relevant `.agent-context` session/decision record

- [ ] **Step 1: Run `git diff --check` and inspect status**

Expected: no whitespace errors and no unintended tracked files.

- [ ] **Step 2: Run all affected tests from fresh Release binaries**

Do not reuse only pre-change test output. Capture exit codes and artifact paths.

- [ ] **Step 3: Confirm runtime effective configuration**

Verify the dumped live values match the approved profile and that no legacy parameter is
reported as the winning source.

- [ ] **Step 4: Update project context**

Record the canonical schema, the `0.365` geometry decision, the benchmark provenance
rule, commits, seeds, results, and remaining compatibility aliases.

- [ ] **Step 5: Final commit if context changed**

```powershell
git add .agent-context
git commit -m "docs: record controller config migration baseline"
```
