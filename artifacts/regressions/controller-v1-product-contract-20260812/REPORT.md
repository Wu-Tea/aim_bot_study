# Controller V1 and Auto-Mark Regression Report

Date: 2026-08-12

## Outcome

The bounded C++ refactor is complete and the deterministic product contract is
GREEN. The candidate runtime was rebuilt from the current worktree and the
official Release CTest suite passed `49/49`.

This is implementation evidence, not a claim of matched Black Ops 7 gameplay
acceptance. The remaining acceptance step is one live session using the exact
candidate executable and configuration recorded in `manifest.json`.

## Controller behavior fixed

- A selector-owned person receives full configured ADS authority after
  admission. Cue, visibility, reliability and distance no longer reduce ADS.
- Cue arrival does not produce an ADS gain step. Small and large corrections
  are produced by residual error, not separate authority modes.
- A selector-confirmed replacement starts a new identity-scoped ADS acquisition
  while LT remains held; it cannot inherit the prior target's consumed window.
- Final right-stick arbitration is per-axis `T - M`: compatible manual input
  fills the same motion without additive overdrive; stronger compatible manual
  remains native.
- Opposing manual input may be damped but never reversed. The ordinary ceiling
  is 35%, downward input is capped at 10%, and firing-down is never damped.
- Firing-down moves D within R and cannot arm a downward handover merely because
  recoil control reaches the edge of R.
- `TargetPlan.ads_acquisition_active` now publishes post-transition state, so a
  completed ADS plan cannot appear simultaneously as BodyLock and active ADS.

## Auto-mark behavior fixed

- L3 or LT rising creates one 250 ms request, not immediate D-pad actuation.
- Only two consecutive fresh final `TargetPlan`s for the same selector
  generation can fire the mark.
- Both plans must contain a direct current class-0 person, current enemy cue,
  valid D/R, no cue-only continuation, and a crosshair inside R.
- Green-friendly detections and selector-rejected corpse candidates never reach
  the final mark gate. Historical coordinates cannot fire a mark.
- One target generation can be marked once per active Vision scope; L3 and LT
  share the same budget. The synthetic D-pad Up press lasts 50 ms and physical
  D-pad Up always passes through.
- L3 may wake Vision for the request, but it does not grant aim authority.

## Fixed RED to GREEN evidence

| Oracle | Known bad | Candidate |
|---|---:|---:|
| ADS authority without cue | 0.228 | 1.000 |
| ADS authority with cue | 0.950 | 1.000 |
| Cue changes ADS gain | yes | no |
| Mixed compatible X output | 0.364 | 0.450 |
| Firing-down final Y | -0.038 | -0.120 |
| Opposing ADS manual X | 0.180 | 0.117, same sign |
| Firing-down moves D | no, 256 to 256 px | yes, 256 to 312 px |

The legacy Aimlab aggregate was also corrected so any wrong-person strong ADS
event forces release score to zero. A historical scenario assertion that
required an already-fixed path to keep producing wrong locks was replaced with
the actual invariant: if a wrong lock occurs, it cannot be averaged away.

## Verification

- Candidate full Release build: PASS.
- Official Release CTest: `49/49` PASS.
- Candidate replay of all registered tests: `49/49` PASS.
- Product-contract incident: GREEN, all seven hard oracles true.
- Telemetry schema: 17; fixed record size: 1848 bytes, below the 2576-byte
  production-shape ceiling.
- `git diff --check`: PASS.
- Runtime was not launched and the user's running application was not stopped.

## Live acceptance boundary

The code-level defects above are closed. A live run can still expose incorrect
Vision D/R geometry for a posture or title-specific HUD variant; that would be
a new measured selector/geometry incident, not permission to weaken ADS or add
another output owner. Auto-mark is deliberately disabled in the current local
`config.toml`; enable `[gamepad.enemy_mark] enabled = true` only when testing it.
