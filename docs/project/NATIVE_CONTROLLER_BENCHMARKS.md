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
`4794F655DAB331C90E840FBB1CAA0ABE5108409C0D1377942D0CB9009523A08C`.

The open-loop invariance probe produced:

| Metric | Result |
| --- | ---: |
| Maximum AI-right trace delta when only left intent changes | 0.000 |
| Maximum final-right trace delta when only left intent changes | 0.000 |
| Maximum left-axis passthrough error | 0.000 |
| Left intent ignored | true |

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
  while preserving the existing controller benchmark and bodylock guards.
