# Causal Response Shadow Acceptance — 2026-07-22

## Outcome

**Formal status: `G3_REJECTED`.** The implementation, deterministic synthetic gates, mutations, CPU gates, full Release build, and registered native regression suite pass. Formal G3 shadow acceptance is withheld only because no fresh real G0 session with matching revision/config/engine/schema/crop identity was available for causal replay. The runtime is suitable for user shadow logging and does not change delivered control.

The feature learns a local response/delay model in process memory and scores short-horizon control scales after output delivery. It solves delayed-observation debt and provides evidence for later control changes; it is not itself a new controller owner.

## Identity

- Branch revision used for retained acceptance: `54fa45e`.
- Build family: MSVC 17.14 / CMake Release, Windows x64.
- Configuration: `config.native.example.toml`, SHA-256 `bf190db0482194a687d459c0358621981441cc77b5173ee75bc3fc42f3a1350a`.
- Engine hash: unavailable in the isolated worktree; therefore no claim about engine-matched real replay.
- Capture contract: `480x416`.
- Telemetry schema: 6; causal journal schema `causal_response_journal_v1` and shadow schema `causal_response_shadow_v1`.
- Fixed seeds: `1337`, `7331`, `20260722`.

## Retained results

Two full acceptance runs were retained:

- `artifacts/benchmarks/causal-response/20260722-171035-54fa45e/`
- `artifacts/benchmarks/causal-response/20260722-171048-54fa45e/`

All non-timing results were identical. Each seed evaluated 556 causal decisions:

| Seed | Top-1 agreement | Causal gain (px·ms) | Remaining regret (px·ms) | Harmful release | Single-target 1.15 gain | Multi-target 1.15 regret |
|---:|---:|---:|---:|---:|---:|---:|
| 1337 | 95.8633% | 205,508.88 | 11,285.71 | 0 | 16,276.58 | 21,065.83 |
| 7331 | 95.8633% | 205,516.67 | 11,292.40 | 0 | 16,263.87 | 21,046.05 |
| 20260722 | 95.8633% | 205,522.59 | 11,371.03 | 0 | 16,255.82 | 21,058.41 |

These are closed-loop synthetic shadow-ranking results, not delivered-controller score changes. Because G0-G3 do not feed the result back into output, current ADS/BodyLock benchmark output is intentionally unchanged.

## CPU

Across the two retained acceptance runs:

- control-history push p95/p99: 0.1/0.1 µs;
- fresh vision observe+estimate p95: 10.9–18.9 µs;
- fresh vision observe+estimate p99: below 25 µs in retained runs.

This passes the controller p95 `<10 µs`, p99 `<25 µs` history gate and the fresh-vision p95 `<150 µs` learner gate. Computation occurs only on a new committed capture, not every controller tick.

## Verification

- G1 mutations M1–M10: all detected for all seeds.
- Determinism: duplicate reports are byte-identical for every seed.
- Full serial Release build: passed. A preceding parallel retry produced lock-file errors after a timed-out MSBuild; serial reproduction passed after adding the missing `target_geometry.cpp` dependency to `cod_native_target_snapshot_tests`.
- Registered native CTest: 22/22 passed.
- Default config: control learning disabled.
- Disabled: no learner allocation/calls.
- Shadow and rollout-shadow: estimate/result has no data path into selector, TargetCoordinator, ADS, BodyLock, fuser, AutoFire, recoil, or final output.
- Learner state is an owned runtime `unique_ptr` and is empty after process restart.
- No weapon/FOV/sensitivity input, persistent learner state, injected stick, extra vision pass, CUDA allocation, or TensorRT allocation was added.

## Policy evidence

The synthetic plant supports the user's proposed asymmetry:

- one strong eligible target may benefit from evaluating a bounded 1.15 AI candidate;
- target ambiguity makes the same unconditional increase costly, so multi-target snapshots retain the four conservative candidates `0/0.70/0.85/1.0`;
- deliberate opposing manual escape remains a hard invariant and labels AI scale 0;
- reduced-scale candidates are rejected when their own predicted error-area burden is worse than retaining scale 1.0, yielding zero harmful-release episodes in the retained plant.

## Remaining gate

Enable `runtime.control_learning` in `rollout_shadow` with telemetry for a fresh gameplay session, then replay that session with its generated manifest. Required evidence is response/delay calibration, unidentifiable-window ratio, pending error, change-detection behavior, output/TargetPlan/AutoFire/recoil hash equality, and worst episodes. Until that evidence exists, G4 live adjustment remains unauthorized.
