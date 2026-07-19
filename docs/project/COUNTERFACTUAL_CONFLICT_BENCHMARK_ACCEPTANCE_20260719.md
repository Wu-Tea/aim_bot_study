# Counterfactual Conflict Benchmark Acceptance (2026-07-19)

## Scope

This baseline adds measurement capability for sustained-aim conflicts. It replays the same deterministic target and user-input trace with alternative manual/AI mixes, then reports both local correction regret and the burden carried into the following motion. It does not change the shipping controller policy or claim an aiming improvement.

## Reproducibility identity

- Revision: `ebf3b45800e13f76746512c69e7f7b3c80664111`
- Artifact schema: `sustained-aimlab-v1`
- Counterfactual schema/candidate set: `1` / `1`
- Config fingerprint (FNV-1a 64): `16587694727024197693`
- Seeds: `1337`, `20260718`, `424242`
- Profiles: `pure`, `mixed`
- Cohorts: `ads`, `bodylock`
- Duration: 60,000 ms per run, 12 runs total
- Mode: `full`, four replays per conflict kind, 500 ms stable horizon

The checked-in artifact is `runs/native_perf/sustained_aimlab_counterfactual_20260719.json`.

## Command

```powershell
b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config D:\work\AI\yolo-study-001\config.toml `
  --duration-ms 60000 `
  --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort both `
  --counterfactual full `
  --revision ebf3b45800e13f76746512c69e7f7b3c80664111 `
  --output runs/native_perf/sustained_aimlab_counterfactual_20260719.json
```

## Aggregate result

| Measure | Result |
| --- | ---: |
| Fixed anchors detected / analyzed | 635 / 219 |
| Dynamic conflicts detected / analyzed | 11,048 / 236 |
| Total analyzed / skipped episodes | 455 / 11,228 |
| 80 ms local regret | 38,411.991 px·ms |
| Future burden | 191,830.935 px·ms |
| Causal error-area gap | 80,262.935 px·ms |
| Hindsight headroom | 191,830.935 px·ms |
| AI-helpful input suppressed | 10,800 ms |
| Manual-helpful input suppressed | 1,280 ms |
| Both inputs harmful | 7,360 ms |
| Wrong-way commitment | 6,483 ms |
| Destructive stacking | 0 ms |

The largest future-burden cases were the mixed profile: seed `20260718` ADS (44,115.745 px·ms), seed `424242` BodyLock (36,462.998 px·ms), and seed `20260718` BodyLock (35,642.184 px·ms). This gives future controller work concrete episodes to inspect instead of assuming that the user or AI was globally correct from a single frame.

## Determinism check

An identical full rerun completed in 55.081 seconds. `compare_sustained_aimlab.ps1` reported zero delta for all 12 core result rows and zero delta for causal gap, future burden, and future settle delay. The duplicate artifact is intentionally not checked in.

## Verification

- `cod_native_sustained_aimlab_trace_tests`: PASS
- `cod_native_sustained_aimlab_counterfactual_tests`: PASS
- `cod_native_sustained_aimlab_simulator_tests`: PASS
- `cod_native_sustained_aimlab_score_tests`: PASS
- `cod_native_controller_tests`: PASS
- `native_pipeline_contract.ps1 -BuildDir b -SkipBuild -SkipBenchmark`: PASS
- Runtime smoke initialization performed by the pipeline contract: PASS
- Full 60-second, 12-run counterfactual benchmark: PASS
- Same-revision full replay comparison: all reported deltas zero

## Interpretation boundary

The causal oracle uses only information available at the branch time and is the primary decision signal. The hindsight oracle uses realized future motion only to quantify headroom and must not be copied into runtime logic. A high detected count with a smaller replayed count is expected: the per-kind replay budget bounds benchmark cost while retaining deterministic coverage.
