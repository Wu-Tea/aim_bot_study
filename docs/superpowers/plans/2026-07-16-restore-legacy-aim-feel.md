# Restore Legacy ADS and BodyLock Feel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore strong ADS/BodyLock pull and near-target braking while retaining the single TargetPlan control path and removing dead runtime parameters.

**Architecture:** Select force gains with deterministic offline sweeps against the July 14 artifacts, then add one bounded closing-velocity lookahead inside `BodylockFollowController`. Keep the existing coordinator and single output shaper; do not restore legacy gates.

**Tech Stack:** C++17, CMake/MSBuild Release targets, Python/PowerShell artifact analysis, deterministic native gamepad benchmarks.

---

### Task 1: Freeze and reproduce the current baseline

**Files:**
- Read: `config.toml`
- Read: `runs/native_perf/force_sweep_current_20260716_seed1337.json`
- Read: `runs/native_perf/native_gamepad_benchmark_user_profile_strong_ai_20260714.json`

- [ ] **Step 1: Confirm isolation and backup identity**

Run:

```powershell
git status --short
git rev-parse --short HEAD
git -C D:/work/AI/yolo-study-001 rev-parse --short HEAD
```

Expected: worktree starts at the design commit; main `dev` remains `3406889`; both are clean.

- [ ] **Step 2: Configure and build the benchmark in the worktree**

Run:

```powershell
cmake -S native/vision_native -B native/vision_native/build-feel -DCMAKE_BUILD_TYPE=Release
cmake --build native/vision_native/build-feel --config Release --target cod_native_gamepad_benchmark cod_native_bodylock_follow_controller_tests -j 8
```

Expected: both targets build successfully.

- [ ] **Step 3: Run the unchanged current profile**

Run the full suite with seeds `1337/1337/1337` and write
`D:/work/AI/yolo-study-001/runs/native_perf/force_sweep_backup_recheck_seed1337.json`.

Expected: harness exit `0`; metrics remain within deterministic rounding of the fresh baseline.

### Task 2: Sweep active force gains without changing production defaults

**Files:**
- Generate: `runs/native_perf/aim_feel_sweep/config_*.toml` (untracked benchmark inputs)
- Generate: `runs/native_perf/aim_feel_sweep/result_*.json`
- Create: `runs/native_perf/aim_feel_sweep/summary.json`

- [ ] **Step 1: Generate nine benchmark configurations**

Copy the backed-up `config.toml` into the artifact directory and replace only:

```text
ADS relative scale:      1.00, 1.20, 1.40
BodyLock relative scale: 1.00, 1.35, 1.70
```

The current absolute base is ADS `1.10/1.05`, BodyLock `0.30/0.42`.

- [ ] **Step 2: Run all nine full deterministic suites**

For every config run `cod_native_gamepad_benchmark --suite all` with all three
seeds set to `1337` and store one JSON result.

Expected: all nine harness runs exit `0`.

- [ ] **Step 3: Rank candidates**

Extract moving, slide-visible, slide-occlusion, jump, near-high, and adversarial
metrics. Reject candidates with manual-fight above `20` or overshoot above the
July 14 strong-profile value. Rank the remainder by mean normalized tracking
error, then by close-assist output.

- [ ] **Step 4: Record the chosen absolute gains**

Write the matrix and selection to `summary.json`; do not change `config.toml`
until the chosen profile is recorded.

### Task 3: Add one bounded BodyLock stopping term with TDD

**Files:**
- Modify: `native/controller_native/bodylock_follow_controller.h`
- Modify: `native/controller_native/bodylock_follow_controller.cpp`
- Modify: `native/controller_native/bodylock_follow_controller_tests.cpp`

- [ ] **Step 1: Write the failing closing-target test**

Add a test that compares equal positive errors with zero rate and a negative
closing rate. Assert that the closing command is smaller, remains positive, and
never reverses past the target.

- [ ] **Step 2: Verify RED**

Run `cod_native_bodylock_follow_controller_tests.exe`.

Expected: FAIL because current BodyLock adds velocity feedforward but has no
bounded stopping lookahead.

- [ ] **Step 3: Implement minimal bounded lookahead**

Add `stopping_lookahead_seconds` to `BodylockFollowControllerConfig`. In `axis`,
when `error * error_rate < 0`, project the feedback error toward zero and clamp
it at zero if the projection crosses the target. Keep target-motion feedforward,
force authority, and manual arbitration in the same function.

- [ ] **Step 4: Verify GREEN and existing controller cases**

Run BodyLock unit tests, shaper tests, controller integration tests, gamepad
self-test, and the focused continuity suite.

Expected: all exit `0`; continuity remains PASS.

### Task 4: Apply selected gains and remove dead runtime knobs

**Files:**
- Modify: `config.toml`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `native/runtime_app/main.cpp`

- [ ] **Step 1: Write failing configuration tests**

Update the compact-config fixture so removed keys are reported as unknown and
active strength/range/handoff keys still map into `GamepadAiAimConfig`.

- [ ] **Step 2: Verify RED**

Run `cod_native_runtime_config_tests.exe`.

Expected: FAIL while the parser still accepts the dead keys.

- [ ] **Step 3: Remove dead keys**

Remove the six dead keys listed in the design from accepted-key sets, parser
branches, compact config storage, startup dump, and `config.toml`. Preserve
legacy struct members used by historical benchmark fixtures but stop advertising
them as live runtime settings.

- [ ] **Step 4: Apply the selected gains**

Change only ADS horizontal/vertical scale and BodyLock horizontal/vertical
strength to the values selected in Task 2.

- [ ] **Step 5: Verify GREEN**

Build and run runtime-config tests plus `--dump-effective-config`.

Expected: tests pass; removed settings are absent; chosen active gains are shown.

### Task 5: Full acceptance and delivery

**Files:**
- Create: `docs/project/LEGACY_AIM_FEEL_RESTORE_20260716.md`
- Generate: `runs/native_perf/legacy_feel_selected_seed1337.json`
- Generate: `runs/native_perf/legacy_feel_lstick.json`
- Generate: `runs/native_perf/legacy_feel_live.json`

- [ ] **Step 1: Build all affected Release targets**

Build runtime, controller integration, BodyLock, shaper, config, AutoFire,
gamepad, left-stick, and live-failure targets.

- [ ] **Step 2: Run complete verification**

Run all affected unit targets, gamepad self-test, full seed-1337 suite,
left-stick `--require-fixed`, live occlusion, AimLab seed `12345`, and a 20-tick
real-model runtime smoke.

Expected: all harnesses exit `0`; left-stick reports 0 defects; live occlusion
reports PASS; AutoFire remains PASS.

- [ ] **Step 3: Compare against both baselines**

Document current rewrite, July 14 strong profile, every sweep candidate, and the
selected profile. Report tracking improvement, force recovery, overshoot,
smoothness, and user-fight without hiding regressions.

- [ ] **Step 4: Commit implementation**

Commit source/config/docs on `codex/restore-legacy-aim-feel`. Keep generated
benchmark artifacts outside git unless already tracked by project convention.

- [ ] **Step 5: Finish the branch**

Use `superpowers:finishing-a-development-branch`; do not merge or delete the
backup worktree without the user's selected integration option.
