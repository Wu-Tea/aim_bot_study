# Refactor B Acceptance — 2026-07-16

## Outcome

The production controller now has one ownership path:

`VisionObservationBatch + IntentState -> TargetCoordinator -> TargetPlan -> ADS/BodyLock -> AimDynamicsShaper -> AutoFire -> Recoil`

The runtime executable no longer links the former ADS completion/carry-brake gates,
legacy controller tracker, legacy AI aim, short-plan policy, authority policy,
BodyLock lifecycle, old dynamics, or output-validation stages. Those files remain
only for historical benchmark/test comparison.

## Fixed acceptance cases

- Left-stick drift (`abs(left_x)=0.0118`) is learned as neutral noise and does not
  weaken ADS.
- Actual left-stick motion is interpreted together with observed target/reticle
  response; no weapon/ADS-speed table is required.
- 100 Hz observations remain finite and stable under a 1000 Hz controller.
- A brief selected-target occlusion enters a bounded continuity hold. Assist may
  hold or decay but cannot increase without a new observation.
- Strong opposing right-stick intent takes ownership in under 16 ms without a
  separate brake/lifecycle gate.
- ADS acquisition hands off to BodyLock by one hysteretic/timed rule. ADS brake
  semantics cannot be applied inside BodyLock.
- AutoFire readiness counts unique vision frames rather than repeated controller
  ticks, preserves readiness between 100 Hz observations, and requires an
  actually observed strong target.
- Debug telemetry is opt-in. Fresh sessions are grouped and rotated; the log tool
  lists/prunes complete old sessions with dry-run as the default.

## Baseline comparison (seed 1337)

| Scenario | Baseline BodyLock frames | New frames | Baseline max overshoot | New max overshoot | New BodyLock smoothness | Output spikes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| moving chase | 0 | 1997 | 28.031 px | 5.615 px | 97.62 | 0 |
| slide visible | 0 | 1997 | 45.836 px | 38.593 px | 94.26 | 0 |
| slide + occlusion | 0 | 1997 | 61.656 px | 48.845 px | 94.76 | 0 |
| jump chase | 0 | 1997 | 57.313 px | 48.057 px | 94.06 | 0 |

All four moving cases meet the 180 ms BodyLock sustain requirement in all four
segments (`sustain_score=100`). The adversarial manual-fight count fell from 132
to 2. The dedicated continuity case reports `PASS`, 3 ms maximum opposing assist,
zero final-output jerk events, and no short BodyLock runs.

This rewrite deliberately trades some tracking error for ownership and smoothness.
For the four cases above, mean error is respectively 31.44, 39.01, 41.31, and
43.13 px (baseline 27.09, 39.22, 41.40, and 43.44 px); P95 is 95.36 px versus
83.52 px baseline. Moving-case mean error regresses, while the other three are
approximately neutral or slightly improved. This is retained because the primary
acceptance target was eliminating missing BodyLock, user fight, output spikes,
and large overshoot—not optimizing a second layer of gains before real-play
validation.

## Verification

- Production `cod_native_runtime.exe`: Release build PASS; real model startup and
  20 controller ticks PASS at 1000 Hz.
- New pipeline unit/integration set: PASS.
- AutoFire gate tests: PASS.
- Native gamepad self-test: PASS.
- Full native gamepad suite: PASS, seeds `1337/1337/1337`.
- Left-stick benchmark `--require-fixed`: PASS, 5 scenarios, 0 defects.
- Live occlusion benchmark: PASS; observed force `0.555556`, 50 continuity ticks,
  last force approximately zero, max tick delta `0.024`.
- AimLab benchmark/tests: PASS harness, seed `12345`. Historical low-scoring
  selector scenarios remain visible and were not relabeled as fixed.
- Log manager C++ tests and Python prune tests: PASS (`3 passed`).

Artifacts:

- `runs/native_perf/refactor_b_rewrite_gamepad_final_seed1337.json`
- `runs/native_perf/refactor_b_rewrite_lstick_final.json`
- `runs/native_perf/refactor_b_rewrite_live_final.json`

## Log operations

```powershell
python tools/manage_native_logs.py list --root runs/native_perf
python tools/manage_native_logs.py prune --root runs/native_perf --keep 5
python tools/manage_native_logs.py prune --root runs/native_perf --keep 5 --apply
```

The first two commands are read-only. `--apply` removes only whole sessions that
the dry run selected; it is never performed automatically at startup.
