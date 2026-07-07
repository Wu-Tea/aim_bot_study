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

Scenario shape:

- Controller loop: 100 Hz.
- Active windows: 1000-2200 ms and 3200-4400 ms.
- Synthetic DXGI no-update gap: 1700-2000 ms.
- Metrics focus: active snapshot FPS, fresh source FPS, long no-update gaps, activation GPU spike, GPU p95, result age p95, reused snapshots, estimated GPU occupancy, and occupancy bucket stability.
- `estimated_gpu_occupancy_pct` is calculated from synthetic GPU work time, not from whole-card `nvidia-smi` utilization. Repeat-last reused snapshots are counted as controller-facing snapshot updates but not GPU work.

| Strategy | Active Snapshot FPS | Active Fresh FPS | Max Long Gap | Activation GPU Max | GPU p95 | Age p95 | Active Reused Rows |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| current_sync_poll | 43.75 | 43.75 | 1020.0 ms | 55.0 ms | 6.5 ms | 13.5 ms | 0 |
| warmup_only | 43.75 | 43.75 | 1020.0 ms | 25.0 ms | 6.5 ms | 12.5 ms | 0 |
| idle_low_rate_keepwarm | 43.75 | 43.75 | 320.0 ms | 6.5 ms | 6.5 ms | 10.5 ms | 0 |
| repeat_last_keepwarm | 50.00 | 43.75 | 0.0 ms | 6.5 ms | 6.5 ms | 10.0 ms | 15 |
| independent_worker | 87.50 | 87.50 | 1010.0 ms | 45.0 ms | 6.5 ms | 7.7 ms | 0 |
| worker_keepwarm | 100.00 | 87.50 | 0.0 ms | 6.5 ms | 6.5 ms | 7.5 ms | 30 |
| always_full_rate | 100.00 | 87.50 | 0.0 ms | 6.5 ms | 6.5 ms | 7.5 ms | 30 |

## Estimated GPU Occupancy

This table compares the old baseline, candidate strategies, and the current default GPU service policy. Bucket stability uses 500 ms occupancy buckets; lower active bucket stdev/CV means the strategy's scheduled GPU work is steadier during aim-active windows.

| Strategy | Overall GPU Occupancy | Active GPU Occupancy | Idle GPU Occupancy | Active Bucket Stdev | Active Bucket CV | Active Snapshot FPS / Overall GPU % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_sync_poll | 16.6% | 34.5% | 0.0% | 13.3 pp | 0.36 | 2.64 |
| warmup_only | 14.4% | 30.0% | 0.0% | 8.9 pp | 0.28 | 3.04 |
| idle_low_rate_keepwarm | 18.9% | 28.9% | 9.7% | 8.0 pp | 0.26 | 2.31 |
| repeat_last_keepwarm | 18.6% | 28.4% | 9.5% | 7.3 pp | 0.25 | 2.69 |
| independent_worker | 29.6% | 61.7% | 0.0% | 18.3 pp | 0.28 | 2.96 |
| worker_keepwarm | 34.2% | 56.9% | 13.3% | 14.5 pp | 0.25 | 2.92 |
| always_full_rate | 61.2% | 56.9% | 65.1% | 14.5 pp | 0.25 | 1.63 |

Cost/stability interpretation:

- Compared with `current_sync_poll`, `worker_keepwarm` roughly doubles overall expected GPU work (`16.6% -> 34.2%`) because it keeps the model warm and supplies 100 Hz controller-facing snapshots.
- That extra cost buys three things in this scenario: no long controller-facing vision gap, no activation GPU spike, and lower active occupancy CV (`0.36 -> 0.25`).
- `always_full_rate` does not improve active snapshot FPS, fresh source FPS, activation spike, long-gap behavior, or active bucket stability over `worker_keepwarm`; it mainly raises idle expected GPU occupancy from `13.3%` to `65.1%`.
- `idle_low_rate_keepwarm` and `repeat_last_keepwarm` are much cheaper, but they do not reach the current default's 100 Hz controller-facing active snapshot stream.
- The current default is therefore a middle position: it deliberately spends more GPU than the old baseline, but avoids the much higher idle cost of full-rate inference.

Input/output comparison:

| Comparison | Extra Overall GPU Occupancy | Snapshot FPS Change | Fresh FPS Change | Long Gap Change | Activation GPU Max Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| `current_sync_poll -> worker_keepwarm` | +17.6 pp | +56.25 FPS | +43.75 FPS | -1020.0 ms | -48.5 ms |
| `worker_keepwarm -> always_full_rate` | +27.0 pp | +0.00 FPS | +0.00 FPS | +0.0 ms | +0.0 ms |

This makes the cost boundary explicit: moving from the old baseline to `worker_keepwarm` buys stability and 100 Hz controller-facing snapshots; moving from `worker_keepwarm` to `always_full_rate` only spends more idle GPU in this benchmark.

Interpretation:

- The old synchronous poll baseline is not compute-bound in steady state, but it has a cold activation spike and a long gap when vision is idle or capture has no updated frame.
- Warmup alone reduces the first activation spike but does not remove long gaps.
- Low-rate idle keepwarm removes the activation GPU spike and reduces the idle-to-active gap, but it still cannot provide a 100 Hz controller-facing snapshot stream.
- Worker keepwarm gives the controller a stable 100 Hz snapshot stream while keeping fresh source updates near the configured active vision rate.
- Always-full-rate does not improve this synthetic result over worker keepwarm, so the current default should prefer worker keepwarm over wasting full-rate GPU work while idle.

Current default runtime entry:

- `gpu_service_enabled = true`
- `gpu_service_active_fps = 100`
- `gpu_service_idle_fps = 20`
- `gpu_service_keepwarm_when_idle = true`
- `gpu_service_repeat_last_on_no_update = true`

Operational fallback:

- Set `VISION_GPU_SERVICE_ENABLED=0` or `vision.gpu_service_enabled = false` to return to the old synchronous poll path.
