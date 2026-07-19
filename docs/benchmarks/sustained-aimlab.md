# Sustained AimLab benchmark

This benchmark measures the production `NativeGamepadController` in a deterministic
60-second closed loop. It is a scoring and regression tool, not a replacement
controller policy.

Each target has a 250–330 ms ADS acquisition deadline. A successful first circle
entry opens a 1000 ms BodyLock tracking window. The target then despawns for 50 ms.
The plant runs at 1000 Hz, observations arrive at deterministic 80–100 Hz intervals,
and the virtual game's slowdown transitions from 1.0 outside the target to 0.5 at
the 24 px circle edge and 0.4 at its center.

The official baseline runs seeds `1337`, `20260718`, and `424242` twice: once with
pure target motion and once with deterministic mixed human mistakes. Scores are
additive: faster acquisition, longer center-weighted tracking, and smooth output
increase the result. `over_events`, `undertrack_events`, false BodyLock interruption,
false stop, error percentiles, output delta, and jerk remain explicit diagnostics.

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify/run_sustained_aimlab_baseline.ps1
```

The script refuses to overwrite an existing baseline. Pass `-Output` for a new
comparison artifact. Every JSON records the git revision, dirty state, config path
and FNV-1a config fingerprint, simulator constants, script hashes, aggregate scores,
and per-target metrics.

## Counterfactual conflict analysis

Counterfactual analysis is an offline diagnostic. It replays selected fixed
scenario anchors and dynamically detected manual/AI conflicts from the beginning
of the deterministic script, substitutes a bounded mix policy, and measures the
resulting local regret and downstream correction burden. It does not add an oracle,
planner, gate, or per-tick branch to the live runtime binary.

The primary comparison is the causal oracle, which selects among the finite
candidate mixes using only observations delivered by the branch time. The
hindsight oracle sees realized future branch cost and reports theoretical
headroom; it is not a production acceptance requirement.

Quick regression:

```powershell
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --profile both --cohort both `
  --counterfactual quick `
  --output runs/native_perf/counterfactual-quick.json
```

Full baseline or parameter analysis:

```powershell
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort both --counterfactual full `
  --output runs/native_perf/counterfactual-full.json
```

Compare two artifacts with identical seeds, simulator settings, mode, schema,
candidate set, and replay budget:

```powershell
powershell -ExecutionPolicy Bypass `
  -File scripts/verify/compare_sustained_aimlab.ps1 `
  -Baseline runs/native_perf/counterfactual-before.json `
  -Candidate runs/native_perf/counterfactual-after.json
```

The main new diagnostics are:

- `regret_40/80/160_px_ms`: excess integrated error against the best eligible
  branch over each local horizon;
- `future_burden_px_ms`: downstream integrated error above the hindsight lower
  bound;
- `causal_error_area_gap_px_ms`: production error area minus causal-oracle error
  area; a negative value means production beat this deliberately simple oracle;
- `manual_helped_but_suppressed_ms` and `ai_helped_but_suppressed_ms`: attribution
  of useful isolated components lost by the production mix;
- detected, analyzed, and skipped counts: explicit replay coverage under the
  deterministic per-kind budget.

Do not optimize a single aggregate number. Reduce causal regret and future burden
while requiring non-regression in acquisition speed, tracking, braking,
interruption, overshoot, smoothness, and live feel.
