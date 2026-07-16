# Refactor B Baseline — 2026-07-16

## Purpose

This scorecard freezes the observable behavior and performance at the exact point before the Option B pipeline rewrite. It is an oracle, not an assertion that current behavior is correct. Existing failures must remain visible during the rewrite and must not be silently reclassified.

## Source Identity

- Branch at capture: `dev`
- Git commit: `e6c1f2f61e28517a999981c0fd877a093114b852`
- Worktree before and after capture: clean (`git status --short` empty)
- Native binaries: rebuilt in `Release` from the commit above before capture
- Runtime process: stopped before capture
- Raw artifact root: `runs/native_perf/refactor_b_baseline_20260716_e6c1f2f`

## Seed Manifest

| Benchmark | Seed | Notes |
| --- | ---: | --- |
| Native gamepad, random FOV | `1337` | Explicit `--random-fov-seed` |
| Native gamepad, selector intent | `1337` | Explicit `--selector-intent-seed` |
| Native gamepad, ROI fallback | `1337` | Explicit `--roi-fallback-seed` |
| AimLab | `12345` | Existing canonical selector baseline |
| Left-stick motion | deterministic | No random source exposed |
| Live failure | deterministic | No random source exposed |
| Scheduler | wall-clock measurement | Seed does not apply |
| Telemetry | deterministic payload loop | Seed does not apply |
| Color readback | deterministic buffers | Seed does not apply |
| BGRA inference | deterministic generated frame | Seed does not apply |
| Vision GPU service | deterministic simulation | Seed does not apply |

## Verification and Benchmark Status

| Item | Result | Key evidence |
| --- | --- | --- |
| Benchmark metrics tests | PASS | Exit `0` |
| AimLab benchmark tests | PASS | Exit `0` |
| Left-stick benchmark tests | PASS | Exit `0` |
| Gamepad benchmark self-test | **FAIL (existing)** | Slide-occlusion body-lock close-assist assertion |
| Full gamepad suite | PASS harness | Exit `0`; behavioral defects remain in metrics |
| Left-stick benchmark | PASS | `defect_count=0`, `desired_gate_pass=true` |
| Left-stick `--require-fixed` | PASS | Exit `0` |
| AimLab seed 12345 | PASS harness | Scenario scores recorded in raw log |
| Live-failure benchmark | **FAIL (existing)** | `tracker_continuity_frames=0`, `passed=false` |
| Scheduler precision 60 s | PASS harness | Exit `0` |
| Scheduler legacy 60 s | PASS harness | Exit `0` |
| Telemetry 1,000,000 records | PASS | `dropped=0` |
| Color readback 2,000 iterations | PASS | Exit `0` |
| BGRA inference 200 iterations | PASS | Exit `0` |
| Vision GPU service simulation | PASS | Exit `0` |

## Controller / Tracker Scorecard

All values below come from `gamepad_all_seed1337.json`.

| Scenario | Mean px | P95 px | Final px | Max overshoot px | 50 px events | BodyLock frames | Low-close | Dropout |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ads_diagonal_late_vision_fov_occlusion_50hz` | 27.338 | 70.535 | 11.959 | 50.698 | 4 | 0 | 0 | 0 |
| `ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire` | 43.413 | 99.029 | 2.344 | 102.278 | 4 | 0 | 0 | 0 |
| `ads_diagonal_err_target_recovery_100hz_dynamic_fire` | 48.160 | 129.873 | 7.334 | 129.941 | 8 | 0 | 0 | 0 |
| `ads_diagonal_err_target_late_position_fov_occlusion_50hz_dynamic_fire` | 68.250 | 113.456 | 59.766 | 102.079 | 8 | 0 | 0 | 0 |
| `ads_bodylock_moving_chase_100hz_dynamic` | 27.092 | 83.524 | 18.453 | 28.031 | 0 | **0** | 772 | 772 |
| `ads_bodylock_slide_visible_chase_100hz_dynamic` | 39.217 | 83.524 | 47.443 | 45.836 | 0 | **0** | 1892 | 1892 |
| `ads_bodylock_slide_occlusion_chase_100hz_dynamic_fire` | 41.399 | 83.524 | 42.055 | 61.656 | 1 | **0** | 1923 | 1923 |
| `ads_bodylock_jump_chase_100hz_dynamic` | 43.438 | 83.524 | 27.272 | 57.313 | 5 | **0** | 2155 | 2155 |

Additional authority indicators:

- `adversarial_controller_authority_100hz`: wrong-target `140`, user-fight `132`, invalid-strong `120`, stale-high `60`, near-high `403`, projected `321`, P95 error `74.739 px`.
- `ads_manual_carry_through_100hz`: ADS/body/manual frames `221/139/0`, brake-active `0`, near-high `58`, max overshoot `45.783 px`, final error `11.416 px`.
- `ads_bodylock_near_high_output_100hz`: ADS/body/manual frames `221/439/60`, low-close `286`, chatter `4`, output spikes `1`, final error `75.482 px`, smoothness `96.878`.

These results show that BodyLock can exist in a focused handoff fixture while disappearing entirely in the moving-target production-style chase fixtures. That discrepancy is a primary architecture acceptance target.

## Performance Scorecard

| Benchmark | Result |
| --- | --- |
| BGRA preprocess | p50 `0.033 ms`, p95 `0.127 ms`, p99 `0.370 ms` |
| TensorRT infer | p50 `1.370 ms`, p95 `2.145 ms`, p99 `2.667 ms` |
| BGRA GPU total | p50 `1.432 ms`, p95 `2.216 ms`, p99 `2.726 ms` |
| Color readback | pageable p95 `0.0539 ms`, pinned p95 `0.0366 ms`, improvement `32.10%` |
| Telemetry enqueue | p95 `0.0002 ms`, p99 `0.0003 ms`, dropped `0/1,000,000` |
| Scheduler precision | `999.082 Hz`, interval p95 `1298.7 us`, lateness p95 `583.5 us`, missed `55` |
| Scheduler legacy | `999.900 Hz`, interval p95 `1000.2 us`, lateness p95 `0.3 us`, missed `6` |
| GPU service 100 Hz | snapshot `100.00 Hz`, fresh `87.50 Hz`, age p95 `7.5 ms`, estimated active GPU `57.2%` |
| GPU service 120 Hz | snapshot `119.58 Hz`, fresh `104.58 Hz`, age p95 `8.2 ms`, estimated active GPU `68.3%` |
| GPU service 140 Hz | snapshot `139.58 Hz`, fresh `122.08 Hz`, age p95 `8.4 ms`, estimated active GPU `79.7%` |

The GPU-service percentages use the benchmark's conservative synthetic `6.5 ms` work assumption. The measured BGRA p95 on this machine is substantially lower and must be reported separately rather than mixed into that simulation.

## Canonical Re-run Commands

```powershell
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --suite all --random-fov-seed 1337 --selector-intent-seed 1337 --roi-fallback-seed 1337 --output <artifact>.json
native\vision_native\build\Release\cod_native_left_stick_motion_benchmark.exe --require-fixed --output <artifact>.json
native\vision_native\build\Release\cod_native_aimlab_benchmark.exe 12345
native\vision_native\build\Release\cod_native_live_failure_benchmark.exe --output <artifact>.json
native\vision_native\build\Release\cod_native_scheduler_benchmark.exe --seconds 60 --mode precision --output <artifact>.json
native\vision_native\build\Release\cod_native_scheduler_benchmark.exe --seconds 60 --mode legacy --output <artifact>.json
native\vision_native\build\Release\cod_native_telemetry_benchmark.exe --records 1000000 --output <artifact>.json
native\vision_native\build\Release\vision_native_color_readback_benchmark.exe --iterations 2000 --output <artifact>.json
native\vision_native\build\Release\vision_native_bgra_benchmark.exe --model models/candidates/body_union_manual_core_x2_neg_e6_640x512.engine --warmup 20 --iterations 200 --output-json <artifact>.json
python tools/benchmark_vision_gpu_service.py --duration-ms 5000 --controller-hz 1000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --strategy worker_keepwarm --strategy worker_keepwarm_120 --strategy worker_keepwarm_140 --output-dir <directory>
```
