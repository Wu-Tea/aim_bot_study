# Native Controller Benchmarks

## Baseline Definition

- Baseline name: `native-controller-authority-20260707`
- Date recorded: `2026-07-07`
- Branch: `dev`
- Git commit at record time: `0789224f71eb39918f30a3988da8ce6b79f49db7`
- Dirty worktree: `true`
- Accepted artifact: `runs/native_perf/native_gamepad_benchmark_ads_authority_final_20260707.json`
- Previous comparison artifact: `runs/native_perf/native_gamepad_benchmark_metrics_expanded_20260707.json`
- Rejected experiment artifact: `runs/native_perf/native_gamepad_benchmark_fresh_wrong_body_evidence_gate_20260707.json`
- AimLab command output baseline: `native/vision_native/build/Release/cod_native_aimlab_benchmark.exe`, seed `12345`

This baseline freezes the current native controller and tracker behavior before larger tracker or pipeline refactors. It is not a claim that the current behavior is optimal. It is the scorecard that later changes must beat or preserve.

## Verification Commands

Run these before treating a tracker/controller refactor as validated:

```powershell
native\vision_native\build\Release\cod_native_controller_tests.exe
native\vision_native\build\Release\cod_native_benchmark_metrics_tests.exe
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --self-test
native\vision_native\build\Release\cod_native_aimlab_benchmark.exe
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --random-fov-ticks 0 --output runs\native_perf\native_gamepad_benchmark_<new-label>.json
scripts\verify\native_pipeline_contract.bat
git diff --check
```

## Current Accepted Gamepad Scorecard

The accepted artifact is `native_gamepad_benchmark_ads_authority_final_20260707.json`.

| Scenario | Mean Error | P95 Error | Final Error | Max Overshoot | Large 50px | Unreliable High | No-Fresh High | Err-Target High |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ads_diagonal_late_vision_fov_occlusion_50hz` | 17.440 | 63.816 | 9.345 | 43.680 | 0 | 320 | 0 | 0 |
| `ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire` | 16.970 | 63.816 | 11.438 | 52.375 | 1 | 320 | 0 | 0 |
| `ads_diagonal_err_target_recovery_100hz_dynamic_fire` | 42.777 | 94.481 | 18.314 | 66.692 | 3 | 447 | 372 | 286 |
| `ads_diagonal_err_target_late_position_fov_occlusion_50hz_dynamic_fire` | 74.763 | 151.899 | 75.922 | 107.280 | 4 | 925 | 572 | 368 |

Interpretation:

- The no-fresh ADS acquisition cap is useful: late vision no-fresh high output is now `0`.
- Err-target and fresh-but-wrong target cases remain bad. The next tracker or authority refactor should target these without hurting bodylock.
- `ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire` still has one 50px overshoot and max overshoot above 50px, so it is not solved.

## Bodylock Regression Guard

Bodylock should not be judged by ADS overshoot alone. Moving targets, slides, jumps, crouch cycles, and arc jumps need continuity and enough assist strength.

| Scenario | Mean Error | P95 Error | Final Error | Max Overshoot | Large 50px | Bodylock Frames | Low Close | Dropout | Sustain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ads_bodylock_moving_chase_100hz_dynamic` | 12.840 | 56.245 | 3.160 | 12.516 | 0 | 2622 | 0 | 0 | 100.0 |
| `ads_bodylock_slide_visible_chase_100hz_dynamic` | 26.474 | 79.651 | 23.838 | 88.546 | 3 | 2406 | 251 | 191 | 50.0 |
| `ads_bodylock_slide_occlusion_chase_100hz_dynamic_fire` | 24.563 | 82.580 | 8.784 | 95.590 | 1 | 2529 | 54 | 38 | 75.0 |
| `ads_bodylock_crouch_cycle_chase_100hz_dynamic` | 43.025 | 110.900 | 62.879 | 133.110 | 9 | 1598 | 633 | 606 | 0.0 |
| `ads_bodylock_jump_chase_100hz_dynamic` | 33.040 | 111.249 | 23.265 | 165.395 | 4 | 1918 | 443 | 352 | 0.0 |
| `ads_bodylock_arc_jump_chase_100hz_dynamic` | 32.328 | 78.970 | 12.026 | 83.790 | 6 | 2079 | 453 | 398 | 0.0 |

Regression rule:

- A tracker refactor must not improve ADS by making bodylock stall.
- Watch `body_lock_frames`, `low_close`, and `dropout` first.
- A change that reduces `body_lock_frames` by roughly 5% or increases `low_close`/`dropout` materially should be treated as suspicious unless live testing confirms better feel.

## Focused Controller Diagnostics

| Scenario | Current Score |
| --- | --- |
| `adversarial_controller_authority_100hz` | wrong target `140`, user fight `433`, invalid strong `84`, stale high `27`, err target `99`, recovery `100`, near high `224`, projected `350`, p95 error `74.739`, mean manual/AI alignment `-0.336` |
| `ads_manual_carry_through_100hz` | same-direction accel `38`, near-high `47`, brake active `205`, brake inactive near-high `22`, sign flips `2`, max overshoot `2.069`, final error `0.316` |
| `ads_bodylock_near_high_output_100hz` | near-high `720`, brake inactive `720`, acquisition expired high `420`, chatter `4`, output spikes `0`, low close `0`, p95 output delta `0.035`, smoothness `98.943` |

Interpretation:

- The adversarial controller case is the strongest evidence that tracker/authority state needs a real design pass.
- Bodylock near-target high output is not automatically a failure. In live play, close bodylock may need strong continuous output, so do not globally cap it as if it were ADS snap.

## Rejected Experiment

The experiment `native_gamepad_benchmark_fresh_wrong_body_evidence_gate_20260707.json` disabled projected suspicious candidate aim authority based on crude body-box evidence.

It is rejected as a baseline because it regressed bodylock:

| Scenario | Accepted | Rejected |
| --- | ---: | ---: |
| `ads_bodylock_slide_visible_chase_100hz_dynamic` bodylock frames | 2406 | 2226 |
| `ads_bodylock_slide_visible_chase_100hz_dynamic` low close | 251 | 346 |
| `ads_bodylock_slide_visible_chase_100hz_dynamic` dropout | 191 | 286 |
| `ads_bodylock_slide_visible_chase_100hz_dynamic` max overshoot | 88.546 | 99.824 |
| `ads_bodylock_jump_chase_100hz_dynamic` bodylock frames | 1918 | 1826 |
| `ads_bodylock_jump_chase_100hz_dynamic` low close | 443 | 485 |
| `ads_bodylock_jump_chase_100hz_dynamic` dropout | 352 | 394 |

Do not reintroduce this target-provider gate without a cleaner signal that separates ADS point-target authority from bodylock continuity.

## AimLab Selector Baseline

Command:

```powershell
native\vision_native\build\Release\cod_native_aimlab_benchmark.exe
```

Seed: `12345`

| Scenario | Final | Selection | Control | Cooperation | Safety | Wrong ADS | Fight | Helpful |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `multi_target_flick` | 0 | 0 | 100 | 0 | 100 | 52 | 52 | 0.458 |
| `near_side_vs_far_front_no_intent` | 0 | 0 | 100 | 0 | 100 | 119 | 119 | 0.000 |
| `near_side_vs_far_front_perfect_intent` | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 1.000 |
| `near_side_vs_far_front_intent` | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 1.000 |
| `near_side_vs_far_front_manual_clean` | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 1.000 |
| `near_side_vs_far_front_manual_slow` | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 1.000 |
| `near_side_vs_far_front_manual_noisy_recover` | 100 | 100 | 100 | 100 | 100 | 0 | 0 | 1.000 |
| `near_side_vs_far_front_manual_slow_late` | 0 | 0 | 100 | 0 | 100 | 67 | 59 | 0.000 |
| `ads_diagonal_pull` | 50 | 100 | 0 | 0 | 100 | 0 | 22 | 0.511 |
| `moving_track` | 50 | 100 | 0 | 0 | 100 | 0 | 34 | 0.533 |
| `slide_occlusion_delay` | 0 | 0 | 0 | 0 | 100 | 16 | 16 | 0.417 |
| `corpse_cue_loss` | 0 | 0 | 0 | 0 | 0 | 48 | 48 | 0.500 |
| `err_target_recovery` | 0 | 0 | 0 | 0 | 100 | 56 | 56 | 0.481 |

Interpretation:

- Established manual intent can steer selector pickup in the near-side-vs-far-front case.
- Late slow manual intent still fails and remains a sticky wrong-target risk.
- Corpse cue loss, err-target recovery, slide occlusion delay, and moving-track control are intentionally still weak. They should guide the tracker/authority refactor rather than be papered over by local controller caps.

## Refactor Validation Rules

For tracker or vision-controller protocol refactors:

1. Re-run the accepted gamepad benchmark and this AimLab benchmark.
2. Compare against this document before tuning values.
3. Separate evaluation into ADS authority, bodylock continuity, adversarial authority, and selector intent.
4. Treat "ADS better but bodylock worse" as a failed tradeoff until live testing proves otherwise.
5. Treat "benchmark better because tracker overpowers live vision/user correction" as suspicious, not as an automatic win.
6. Do not accept a refactor that reduces target/component logging quality; logs are now part of the debugging surface.

## Next Expected Work

The next useful work is not another small ADS cap. It is a tracker/authority refactor with a measurable interface:

- explicit target authority state instead of overloading raw `aim_authority`;
- tracker memory decay when live evidence or user correction disagrees;
- separate ADS point-target authority from bodylock continuity authority;
- candidate verification that can reject fresh-but-wrong detections without stalling moving bodylock;
- benchmark comparison against this baseline after the structural change.

## Left-Stick Relative-Motion RED Baseline (2026-07-15)

The focused left-stick benchmark isolates the reported player-strafe defect
without adding optical flow, background processing, weapon mobility data, or a
controller fix. It drives the real native controller at 100 Hz, captures vision
at 50 Hz, delivers observations with 30 ms delay, and starts scoring after the
initial ADS acquisition phase.

Build and run it with:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build --preset modern-release --target cod_native_lstick_benchmark -- /m

& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build --preset modern-release --target cod_native_lstick_benchmark_tests -- /m

native\vision_native\build-modern\Release\cod_native_lstick_benchmark_tests.exe

& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir native\vision_native\build-modern -C Release `
  -R NativeLeftStickMotionBenchmarkTests --output-on-failure

native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe `
  --output artifacts\benchmarks\native_gamepad\left-stick-motion-defect-20260715.json

native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe `
  --require-fixed
```

Normal execution validates the harness and exits zero even when defects are
reproduced. `--require-fixed` is the optimization gate and intentionally returns
non-zero for this RED baseline.

Accepted RED artifact:
`artifacts/benchmarks/native_gamepad/left-stick-motion-defect-20260715.json`.
Two consecutive runs produced the same SHA-256:
`6D408FD4CFAFD6CE53504076D109F64591196CC4AA7381A637DAC6B9522FA3BF`.

The open-loop invariance probe produced:

| Metric | Result |
| --- | ---: |
| Maximum AI-right trace delta when only left intent changes | 0.000 |
| Maximum final-right trace delta when only left intent changes | 0.000 |
| Maximum left-axis passthrough error | 0.000 |
| Left intent ignored | true |

### Production-chain strafe/dropout probe

This probe supplements the closed-loop kinematic fixtures with the real native
selector/tracker/controller path. It is based on the 2026-07-15 live evidence:
the detector can still expose candidates while the selected production target
becomes unavailable, bodylock falls back to manual, and reacquisition can bind a
new track. It does not replay pixels, add a vision pass, or depend on weapon ADS
mobility data.

Adding the `production_chain` object advances the artifact contract from schema
version 1 to version 2.

Right-stick input with absolute value at or below `0.02` is classified as
deadzone-sized drift, not intentional correction. The fixture uses
`manual_right_x = -0.00393677`; therefore the previously observed maximum
`0.0118` would also be drift under this contract.

| Metric | RED result |
| --- | ---: |
| Total controller frames | 430 |
| `body_lock` / `ads_snap` / `manual` frames | 294 / 45 / 91 |
| Detector-candidate-present selected-target gap | 70 frames / 700 ms |
| Production target missing inside that gap | 62 frames / 620 ms |
| Target present but bodylock unavailable | 20 frames / 200 ms |
| Drift classified as manual correction | 0 frames |
| Final output effectively drift-only | 113 frames |
| Maximum continuous drift-only interval | 900 ms |
| Selected-track changes | 7 |
| Reacquisition latency after scheduled return | 0 ms |
| Requested assist suppressed before final output | 0 frames |
| Failure reason | `production_chain_drift_only_gap` |

The 80 ms difference between the 700 ms selected-target gap and the 620 ms
production-target-missing duration is tracker coast, not detector recovery. The
probe records candidate presence, selected track id, lifecycle, requested AI,
final right-stick output, mode, and final-output limit reason at phase/state
events so a later fix can identify which layer improved.

`requested_suppressed_frames` is part of the scorecard because the live log
contained a final-output suppression example. This deterministic fixture does
not force that branch today, so its RED result is `0`; the reproduced failure is
the longer selector/tracker/bodylock dropout chain.

Closed-loop results exclude ticks 0-69 so initial ADS acquisition output is not
misclassified as a strafe defect:

| Scenario | Mean error | P95 error | Max error | Onset / reverse / release response | Manual opposition | Oracle opposition | Failure reasons |
| --- | ---: | ---: | ---: | --- | ---: | ---: | --- |
| `stationary_target_slow_ads` | 11.514 | 19.829 | 20.065 | 270 / n.a. / 310 ms | 0 | 0 | onset lag, release lag |
| `stationary_target_fast_ads` | 22.267 | 37.754 | 38.301 | 170 / 90 / 310 ms | 0 | 0 | onset lag, reverse lag, release lag |
| `same_direction_target_fast_ads` | 9.831 | 16.063 | 16.170 | 270 / 130 / n.a. ms | 0 | 0 | onset lag, reverse lag |
| `opposite_direction_target_fast_ads` | 47.663 | 132.987 | 138.735 | 150 / 310 / 310 ms | 0 | 4 | onset/reverse/release lag, wrong-way output, excessive error |
| `stationary_target_fast_ads_manual_correction` | 7.572 | 12.143 | 12.347 | 70 / 20 / 30 ms | 7 | 0 | onset/release lag, AI opposes manual correction |

Interpretation:

- The current right-stick AI path is exactly invariant to left-stick intent when
  vision and right-stick input are held equal.
- Faster simulated ADS strafe nearly doubles stationary-target mean error
  (`11.514` to `22.267`) even though no weapon-specific data is used.
- Opposite player/target motion is the worst case: P95 error reaches `132.987`
  px and final output points against the oracle correction for four frames.
- When the user also corrects with the right stick, AI opposes that correction
  for seven frames around strafe events.
- The next optimization should beat these event-latency and opposition metrics
  plus the production-chain dropout metrics while preserving the existing
  controller benchmark and bodylock guards.

## Left-Stick Relative-Motion Live-Rate Acceptance (2026-07-15)

The fixed benchmark keeps the historical RED section above for comparison and
advances the artifact contract to schema 3. The controller now runs at 1000 Hz,
the primary vision matrix runs at 80 and 100 Hz, and 50/160 Hz are retained as
stress rates. Every delivered vision sequence must be consumed exactly once;
repeating a fresh flag at controller rate is an acceptance failure.

The implementation remains weapon-independent and vision-neutral. It uses the
existing tracker velocity plus two short-lived controller-side facts:

- an 80 ms left-input event window, so delayed character acceleration can update
  the in-memory strafe gain after the stick-change frame;
- the age of a sequenced observation, capped at 50 ms and applied only to the
  horizontal relative-motion horizon, so a delayed observation is projected to
  the present without increasing vertical lead.

The learned gain is process memory only. It is not keyed by weapon, written to
disk, or backed by an additional detector/optical-flow pass. Long target loss
still releases assist; subsequent ADS acquisition reuses the existing
jerk/step-limited delivery envelope instead of jumping directly to full output.

Run the fixed gate with:

```powershell
native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe `
  --output artifacts\benchmarks\native_gamepad\left-stick-relative-motion-fixed-20260715.json

native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe `
  --require-fixed
```

Accepted artifact:
`artifacts/benchmarks/native_gamepad/left-stick-relative-motion-fixed-20260715.json`.
SHA-256:
`04C0B8DB2A3BDB5CDFC3FAD3B00D1E80ED3895DF4E95F67C8502A5E38B9CDE59`.

| Vision rate | Delivered / consumed | Fast mean error, baseline -> fixed | Mean improvement | Fast P95 error, baseline -> fixed | P95 improvement | Same-direction regression | Max AI tick delta | Large sign flips |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 80 Hz (primary) | 286 / 286 | 21.489 -> 16.272 px | 24.27% | 35.950 -> 26.812 px | 25.42% | 0.00% | 0.035 | 0 |
| 100 Hz (primary) | 357 / 357 | 21.564 -> 16.375 px | 24.06% | 36.000 -> 26.839 px | 25.45% | 0.00% | 0.035 | 0 |
| 50 Hz (stress) | 179 / 179 | 21.550 -> 16.225 px | 24.71% | 35.873 -> 26.784 px | 25.34% | 0.00% | 0.035 | 0 |
| 160 Hz (stress) | 572 / 572 | 21.673 -> 16.351 px | 24.56% | 36.275 -> 27.103 px | 25.28% | 0.00% | 0.035 | 0 |

Additional acceptance results:

- left intent changes the AI trace by `0.0176284`, while left-axis passthrough
  error remains `0`;
- short same-track loss coasts, long identity loss releases, and candidate-only
  gaps do not produce blind assist;
- reacquisition is observed immediately, becomes useful after `1 ms`, and its
  maximum final-output delta is `0.0509829`, below the `0.07` envelope limit;
- stationary and moving ADS-to-BodyLock handoff overshoot are both `0 px`; the
  maximum handoff AI delta is `0.0104769`;
- the schema-3 fixed gate reports `defect_count = 0`.

## Per-Axis Wrong-Way Intervention (2026-07-17)

The partial-occlusion benchmark now separates two kinds of human input:

- `wrong_x` and `wrong_y` occur while the target is stably observed and remain
  below the configured `0.45` explicit-escape threshold;
- stale-direction and crossing-inertia remain coupled to observation gaps and
  retain inputs above `0.45`, so the controller must preserve user takeover.

The fixture also keeps frame sequence and selected-target identity separate.
Previously it used the changing frame sequence as `selected_observation_id`,
which did not represent a persistent tracked target. JSON reports now record
per-axis intervention-frame counts.

The accepted controller path is intervention-only. Normal/helpful/ambiguous
input retains the legacy shared confidence and damping path exactly. On a
stable Observed frame, only an axis that is both wrong-way and worsening may
stop suppressing AI. Strong escape, target change, geometry-size change,
Reacquiring, None, and low reliability clear the decision. A confirmed result
bridges at most 12 ms across ordinary 100 Hz vision / 1000 Hz controller
inter-frame Coasting; it cannot renew without another Observed confirmation.

Seed `1337`, current `config.toml`:

| Scenario / case | Disabled baseline | Accepted | Mean error baseline -> accepted | Intervention X / Y |
| --- | ---: | ---: | ---: | ---: |
| Normal combat aggregate | 71.293475 | 71.293475 | 29.108895 -> 29.108895 px | 0 / 0 |
| Human-error aggregate | 68.860482 | 69.253609 | 32.104730 -> 30.746243 px | 38 / 46 |
| `wrong_x` | 71.204014 | 72.557497 | 41.587511 -> 37.376675 px | 38 / 0 |
| `wrong_y` | 65.276753 | 65.518971 | 38.861727 -> 37.471628 px | 0 / 46 |
| stale-direction | 74.689634 | 74.689634 | 25.796849 -> 25.796849 px | 0 / 0 |
| crossing-inertia | 77.967448 | 77.967448 | 22.879050 -> 22.879050 px | 0 / 0 |

Both aggregates retain zero X/Y overshoot. The accepted artifact is
`runs/benchmarks/axis_intervention_accepted_seed1337.json`; its strict
same-scenario control is
`runs/benchmarks/axis_intervention_disabled_baseline_seed1337.json`.

## Guarded Per-Axis Manual Retention (2026-07-17)

The controller now scales only a confirmed wrong-way physical axis. A new
wrong-axis sequence may start retention only at magnitude `0.25` or above;
after confirmation, the existing 12 ms bridge keeps the decision continuous as
the erroneous input naturally decays. Inputs at or above the `0.45` escape
threshold immediately restore full authority. The configurable floor defaults
to `0.65` under `[gamepad.intent]`.

Seed `1337` with the same `config.toml`:

| Scenario / case | Intervention-only | Guarded retention | Mean error before -> after | P95 output delta before -> after |
| --- | ---: | ---: | ---: | ---: |
| Normal combat aggregate | 71.293475 | 71.293475 | 29.108895 -> 29.108895 px | 0.062601 -> 0.062601 |
| Human-error aggregate | 69.253609 | 69.306753 | 30.746243 -> 30.592179 px | 0.061873 -> 0.061840 |
| `wrong_x` | 72.557497 | 72.731028 | 37.376675 -> 36.836800 px | 0.035939 -> 0.035794 |
| `wrong_y` | 65.518971 | 65.543378 | 37.471628 -> 37.371197 px | 0.066844 -> 0.066917 |

The asynchronous left-stick fixture moves onset, reversal, and release away
from the 80/100 Hz observation cadence and reports three inter-frame transitions
per run. An extra pixel-error projection was rejected: the existing learned
left response already feeds `error_rate` and the short horizon, while the
duplicate projection worsened 100 Hz fast P95 by 1.33%. The accepted run keeps
maximum AI tick delta at `0.064`, has no large sign flips, and the correct
manual-prediction case records zero interventions with full retention (`1.0`).
