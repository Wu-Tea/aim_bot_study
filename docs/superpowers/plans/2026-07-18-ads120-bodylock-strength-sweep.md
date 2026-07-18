# ADS 120 ms and BodyLock Strength Sweep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compare ADS 120 ms and several BodyLock force limits against the current 160 ms baseline under fixed ordinary/small target scripts and two slowdown environments.

**Architecture:** Use the existing native sustained-Aimlab executable with the same three seeds, pure/mixed manual profiles, and isolated ADS/BodyLock cohorts. Change only `config.toml` in the isolated worktree between runs, restore it afterward, and write ignored raw artifacts under `runs/native_perf/aim_tuning_20260718/`.

**Tech Stack:** C++ native sustained-Aimlab benchmark, TOML runtime configuration, PowerShell JSON aggregation.

---

### Task 1: Establish the immutable sweep matrix

**Files:**
- Temporarily modify: `config.toml`
- Create benchmark output: `runs/native_perf/aim_tuning_20260718/*.json`

- [ ] **Step 1: Verify the isolated worktree starts at the accepted baseline**

Run:

```powershell
Select-String -Path config.toml -Pattern 'strength_scale = 1.54','snap_duration_ms = 160','strength = 0.45','vertical_strength = 0.50'
git status --short
```

Expected: all four baseline values are present and only this plan is tracked as a new change.

- [ ] **Step 2: Fix the controller matrix**

Run these four configurations without changing any other controller value:

```text
baseline: ads=160 ms, bodylock=0.45/0.50
ads120:   ads=120 ms, bodylock=0.45/0.50
balanced: ads=120 ms, bodylock=0.50/0.56
strong:   ads=120 ms, bodylock=0.52/0.58
```

Expected: the matrix separates ADS timing from BodyLock force so their effects can be attributed independently.

### Task 2: Run fixed-seed ordinary and small-target cohorts

**Files:**
- Temporarily modify: `config.toml`
- Create benchmark output: `runs/native_perf/aim_tuning_20260718/*.json`

- [ ] **Step 1: Run the default slowdown environment**

For every controller configuration and target profile, run:

```powershell
b\Release\cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml `
  --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort both --target-profile <ordinary|small> `
  --slowdown-edge 0.50 --slowdown-center 0.40 `
  --output runs\native_perf\aim_tuning_20260718\<configuration>-<profile>-default.json
```

Expected: each artifact contains 12 runs and ends with `cod_native_sustained_aimlab_benchmark PASS`.

- [ ] **Step 2: Run the stronger COD19-like slowdown environment**

Repeat the same matrix with:

```text
--slowdown-edge 0.40 --slowdown-center 0.30
```

Expected: the same script hashes are retained for a given target profile and seed; only the simulated camera slowdown changes.

### Task 3: Aggregate decision metrics and restore the worktree

**Files:**
- Restore: `config.toml`
- Read: `runs/native_perf/aim_tuning_20260718/*.json`

- [ ] **Step 1: Aggregate by configuration, slowdown, target profile, manual profile, and cohort**

Report sums for `acquire_points`, `tracking_points`, `targets_acquired`, `over_events`, `undertrack_events`, `bodylock_entry_failures`, and `unexpected_mode_ms`, plus weighted means for error diagnostics.

Expected: ADS conclusions use the ADS cohort; BodyLock conclusions use the post-entry isolated BodyLock cohort.

- [ ] **Step 2: Restore the accepted configuration**

Restore exactly:

```toml
snap_duration_ms = 160
strength = 0.45
vertical_strength = 0.50
```

Expected: `git diff -- config.toml` is empty.

- [ ] **Step 3: Verify no runtime source or user configuration was changed**

Run:

```powershell
git status --short
git diff --check
```

Expected: only the committed plan is present; all benchmark artifacts remain in the ignored `runs/` tree.
