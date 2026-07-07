# Native Vision GPU Service Benchmarks

Last updated: 2026-07-07

This page tracks benchmark evidence for making native vision run as a sustained GPU service instead of only polling vision synchronously from the controller loop.

## Synthetic GPU Service Benchmark

Command:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_default_service
```

GPU occupancy rerun:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_gpu_occupancy
```

Efficiency rerun:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_gpu_occupancy_v2
```

Independent service-cadence rerun:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_fps_budget_v2
```

120/140 Hz default-timing rerun:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --controller-hz 1000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_120_140_default_timing
```

120/140 Hz fast-GPU rerun:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --controller-hz 1000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --strategy worker_keepwarm --strategy worker_keepwarm_120 --strategy worker_keepwarm_140 --steady-gpu-total-ms 2 --cold-gpu-total-ms 2 --output-wait-ms 1 --preprocess-ms 0.08 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_120_140_fast_2ms
```

Timer-resolution fix reruns:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --controller-hz 1000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --strategy worker_keepwarm --strategy worker_keepwarm_120 --strategy worker_keepwarm_140 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_timer_fix_default
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --controller-hz 1000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --strategy worker_keepwarm --strategy worker_keepwarm_120 --strategy worker_keepwarm_140 --steady-gpu-total-ms 2 --cold-gpu-total-ms 2 --output-wait-ms 1 --preprocess-ms 0.08 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_timer_fix_fast_2ms
```

Runtime smoke after the timer-resolution fix:

```powershell
.\native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log --max-ticks 1200
```

Observed startup line:

```text
[NativeRuntime] timer_resolution_ms=1 active=1
```

Scenario shape:

- Synthetic consumer sampling: 1000 Hz to approximate the native runtime's 1 ms controller loop. Older runs used 100 Hz and should not be read as the live controller frequency.
- Active windows: 1000-2200 ms and 3200-4400 ms.
- Synthetic DXGI no-update gap: 1700-2000 ms.
- Metrics focus: active snapshot FPS, fresh source FPS, long no-update gaps, activation GPU spike, GPU p95, result age p95, reused snapshots, estimated GPU occupancy, and occupancy bucket stability.
- `estimated_gpu_occupancy_pct` is calculated from synthetic GPU work time, not from whole-card `nvidia-smi` utilization. Repeat-last reused snapshots are counted as controller-facing snapshot updates but not GPU work.

| Strategy | Active Snapshot FPS | Active Fresh FPS | Max Long Gap | Activation GPU Max | GPU p95 | Age p95 | Active Reused Rows |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| current_sync_poll | 43.75 | 43.75 | 1020.0 ms | 55.0 ms | 6.5 ms | 13.5 ms | 0 |
| warmup_only | 43.75 | 43.75 | 1020.0 ms | 25.0 ms | 6.5 ms | 12.5 ms | 0 |
| idle_low_rate_keepwarm | 60.83 | 60.83 | 320.0 ms | 6.5 ms | 6.5 ms | 19.5 ms | 0 |
| repeat_last_keepwarm | 80.00 | 70.00 | 0.0 ms | 6.5 ms | 6.5 ms | 20.0 ms | 24 |
| independent_worker | 87.50 | 87.50 | 1010.0 ms | 45.0 ms | 6.5 ms | 7.7 ms | 0 |
| worker_keepwarm | 100.00 | 87.50 | 0.0 ms | 6.5 ms | 6.5 ms | 7.5 ms | 30 |
| worker_keepwarm_120 | 119.58 | 104.58 | 0.0 ms | 6.5 ms | 6.5 ms | 8.2 ms | 36 |
| worker_keepwarm_140 | 139.58 | 122.08 | 0.0 ms | 6.5 ms | 6.5 ms | 8.4 ms | 42 |
| always_full_rate | 100.00 | 87.50 | 0.0 ms | 6.5 ms | 6.5 ms | 7.5 ms | 30 |

## Estimated GPU Occupancy

This table compares the old baseline, candidate strategies, and the current default GPU service policy. Bucket stability uses 500 ms occupancy buckets; lower active bucket stdev/CV means the strategy's scheduled GPU work is steadier during aim-active windows.

| Strategy | Overall GPU Occupancy | Active GPU Occupancy | Idle GPU Occupancy | Active Bucket Stdev | Active Bucket CV | Active Snapshot FPS / Overall GPU % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_sync_poll | 16.6% | 34.5% | 0.0% | 13.3 pp | 0.36 | 2.64 |
| warmup_only | 14.4% | 30.0% | 0.0% | 8.9 pp | 0.28 | 3.04 |
| idle_low_rate_keepwarm | 24.8% | 40.0% | 10.7% | 9.8 pp | 0.24 | 2.46 |
| repeat_last_keepwarm | 27.2% | 45.8% | 10.0% | 12.0 pp | 0.25 | 2.94 |
| independent_worker | 29.6% | 61.7% | 0.0% | 18.3 pp | 0.28 | 2.96 |
| worker_keepwarm | 34.4% | 57.2% | 13.3% | 14.9 pp | 0.25 | 2.91 |
| worker_keepwarm_120 | 39.9% | 68.3% | 13.8% | 16.9 pp | 0.24 | 2.99 |
| worker_keepwarm_140 | 45.4% | 79.7% | 13.8% | 19.8 pp | 0.24 | 3.07 |
| always_full_rate | 61.2% | 57.0% | 65.1% | 14.7 pp | 0.25 | 1.63 |

Cost/stability interpretation:

- Compared with `current_sync_poll`, `worker_keepwarm` roughly doubles overall expected GPU work (`16.6% -> 34.4%`) because it keeps the model warm and supplies 100 Hz controller-facing snapshots.
- That extra cost buys three things in this scenario: no long controller-facing vision gap, no activation GPU spike, and lower active occupancy CV (`0.36 -> 0.25`).
- `always_full_rate` does not improve active snapshot FPS, fresh source FPS, activation spike, long-gap behavior, or active bucket stability over `worker_keepwarm`; it mainly raises idle expected GPU occupancy from `13.3%` to `65.1%`.
- `idle_low_rate_keepwarm` and `repeat_last_keepwarm` are much cheaper, but they do not reach the current default's 100 Hz controller-facing active snapshot stream.
- The current default is therefore a middle position: it deliberately spends more GPU than the old baseline, but avoids the much higher idle cost of full-rate inference.
- The 6.5 ms default timing is a conservative benchmark assumption. If live GPU timing is closer to 2-3 ms, 120/140 Hz is much cheaper than the default-timing table suggests.

Input/output comparison:

| Comparison | Extra Overall GPU Occupancy | Snapshot FPS Change | Fresh FPS Change | Long Gap Change | Activation GPU Max Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| `current_sync_poll -> worker_keepwarm` | +17.8 pp | +56.25 FPS | +43.75 FPS | -1020.0 ms | -48.5 ms |
| `worker_keepwarm -> always_full_rate` | +26.8 pp | +0.00 FPS | +0.00 FPS | +0.0 ms | +0.0 ms |

This makes the cost boundary explicit: moving from the old baseline to `worker_keepwarm` buys stability and 100 Hz controller-facing snapshots; moving from `worker_keepwarm` to `always_full_rate` only spends more idle GPU in this benchmark.

## 120/140 Hz Target Sweep

These runs use 1000 Hz synthetic consumer sampling so the benchmark is not capped by an old 100 Hz consumer assumption.

| Steady GPU Time | Strategy | Active Snapshot FPS | Active Fresh FPS | Overall GPU | Active GPU | Idle GPU | Age p95 | Max Long Gap |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2.0 ms | `worker_keepwarm` | 100.00 | 87.50 | 10.5% | 17.5% | 4.0% | 3.0 ms | 0.0 ms |
| 2.0 ms | `worker_keepwarm_120` | 119.58 | 104.58 | 12.2% | 20.9% | 4.2% | 3.7 ms | 0.0 ms |
| 2.0 ms | `worker_keepwarm_140` | 139.58 | 122.08 | 13.9% | 24.4% | 4.2% | 3.9 ms | 0.0 ms |
| 3.0 ms | `worker_keepwarm` | 100.00 | 87.50 | 15.7% | 26.2% | 6.0% | 4.0 ms | 0.0 ms |
| 3.0 ms | `worker_keepwarm_120` | 119.58 | 104.58 | 18.3% | 31.4% | 6.2% | 4.7 ms | 0.0 ms |
| 3.0 ms | `worker_keepwarm_140` | 139.58 | 122.08 | 20.8% | 36.6% | 6.2% | 4.9 ms | 0.0 ms |
| 4.0 ms | `worker_keepwarm` | 100.00 | 87.50 | 21.0% | 35.0% | 8.0% | 5.0 ms | 0.0 ms |
| 4.0 ms | `worker_keepwarm_120` | 119.58 | 104.58 | 24.4% | 41.8% | 8.3% | 5.7 ms | 0.0 ms |
| 4.0 ms | `worker_keepwarm_140` | 139.58 | 122.08 | 27.8% | 48.8% | 8.3% | 5.9 ms | 0.0 ms |

If live `gpu_total_ms` is around 2 ms, both 120 Hz and 140 Hz fit under a 30% active-GPU headroom budget. If it is around 3 ms, 120 Hz is just over 30% active GPU and 140 Hz is aggressive. If it is closer to 4 ms, 100 Hz is already above 30% active GPU.

2026-07-07 timer-resolution fix rerun:

| GPU Assumption | Strategy | Active Snapshot FPS | Active Fresh FPS | Overall GPU | Active GPU | Idle GPU | Age p95 | Active Bucket Stdev | Active Bucket CV | Max Long Gap |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6.5 ms default | `worker_keepwarm` | 100.00 | 87.50 | 34.4% | 57.2% | 13.3% | 7.5 ms | 14.9 pp | 0.25 | 0.0 ms |
| 6.5 ms default | `worker_keepwarm_120` | 119.58 | 104.58 | 39.9% | 68.3% | 13.8% | 8.2 ms | 16.9 pp | 0.24 | 0.0 ms |
| 6.5 ms default | `worker_keepwarm_140` | 139.58 | 122.08 | 45.4% | 79.7% | 13.8% | 8.4 ms | 19.8 pp | 0.24 | 0.0 ms |
| 2.0 ms fast | `worker_keepwarm` | 100.00 | 87.50 | 10.5% | 17.5% | 4.0% | 3.0 ms | 4.5 pp | 0.25 | 0.0 ms |
| 2.0 ms fast | `worker_keepwarm_120` | 119.58 | 104.58 | 12.2% | 20.9% | 4.2% | 3.7 ms | 5.1 pp | 0.24 | 0.0 ms |
| 2.0 ms fast | `worker_keepwarm_140` | 139.58 | 122.08 | 13.9% | 24.4% | 4.2% | 3.9 ms | 6.0 pp | 0.24 | 0.0 ms |

The timer fix changes the runtime scheduling contract rather than the synthetic GPU math: native runtime now requests a 1 ms Windows timer period at startup and the vision worker waits on explicit poll deadlines instead of repeatedly sleeping for 1 ms. This directly targets the live symptom where Windows timer granularity turned intended 120 Hz work into roughly 60-65 Hz service cadence.

## 30 Percent Headroom Budget

If the game already uses roughly 70% of the GPU, there are two useful budget readings:

- Strict active headroom: vision should stay at or below about 30% during aim-active windows.
- Average headroom: vision may average below 30% over the whole run, but can burst above 30% during aim-active windows.

Strict active-headroom candidates:

| Candidate | Active Snapshot FPS | Active Fresh FPS | Overall GPU | Active GPU | Idle GPU | Max Long Gap | Activation GPU Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `active=50 idle=20 repeat=true` | 50.00 | 43.75 | 20.7% | 28.8% | 13.3% | 0.0 ms | 6.5 ms |
| `active=45 idle=20 repeat=true` | 44.58 | 38.75 | 19.4% | 25.5% | 13.8% | 0.0 ms | 6.5 ms |
| `active=40 idle=20 repeat=true` | 40.00 | 35.00 | 18.0% | 23.1% | 13.3% | 0.0 ms | 6.5 ms |

Average-headroom candidates:

| Candidate | Active Snapshot FPS | Active Fresh FPS | Overall GPU | Active GPU | Idle GPU | Max Long Gap | Activation GPU Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `active=80 idle=20 repeat=true` | 80.00 | 70.00 | 28.9% | 45.8% | 13.3% | 0.0 ms | 6.5 ms |
| `active=70 idle=20 repeat=true` | 69.58 | 60.83 | 26.3% | 39.9% | 13.8% | 0.0 ms | 6.5 ms |
| `active=60 idle=20 repeat=true` | 60.00 | 52.50 | 23.4% | 34.4% | 13.3% | 0.0 ms | 6.5 ms |

For a game workload with only about 30% free GPU, the strict reading points to `active=50 idle=20 repeat=true` only under the conservative 6.5 ms timing assumption. Under a measured 2 ms live GPU timing, 120-140 Hz is within budget.

Interpretation:

- The old synchronous poll baseline is not compute-bound in steady state, but it has a cold activation spike and a long gap when vision is idle or capture has no updated frame.
- Warmup alone reduces the first activation spike but does not remove long gaps.
- Low-rate idle keepwarm removes the activation GPU spike and reduces the idle-to-active gap, but it still cannot provide a high-rate controller-facing snapshot stream.
- Worker keepwarm gives the controller a stable active-FPS snapshot stream while keeping fresh source updates near the configured active vision rate.
- Always-full-rate does not improve this synthetic result over worker keepwarm, so the current default should prefer worker keepwarm over wasting full-rate GPU work while idle.

Current default runtime entry:

- `gpu_service_enabled = true`
- `gpu_service_active_fps = 120`
- `gpu_service_idle_fps = 20`
- `gpu_service_keepwarm_when_idle = true`
- `gpu_service_repeat_last_on_no_update = true`

Operational fallback:

- Set `VISION_GPU_SERVICE_ENABLED=0` or `vision.gpu_service_enabled = false` to return to the old synchronous poll path.
