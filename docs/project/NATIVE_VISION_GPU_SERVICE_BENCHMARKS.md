# Native Vision GPU Service Benchmarks

Last updated: 2026-07-07

This page tracks benchmark evidence for making native vision run as a sustained GPU service instead of only polling vision synchronously from the controller loop.

## Synthetic GPU Service Benchmark

Command:

```powershell
python tools\benchmark_vision_gpu_service.py --duration-ms 5000 --active-window 1000:2200 --active-window 3200:4400 --no-update-window 1700:2000 --output-dir runs\native_perf\vision_gpu_service_synthetic_20260707_default_service
```

Scenario shape:

- Controller loop: 100 Hz.
- Active windows: 1000-2200 ms and 3200-4400 ms.
- Synthetic DXGI no-update gap: 1700-2000 ms.
- Metrics focus: active snapshot FPS, fresh source FPS, long no-update gaps, activation GPU spike, GPU p95, result age p95, and reused snapshots.

| Strategy | Active Snapshot FPS | Active Fresh FPS | Max Long Gap | Activation GPU Max | GPU p95 | Age p95 | Active Reused Rows |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| current_sync_poll | 43.75 | 43.75 | 1020.0 ms | 55.0 ms | 6.5 ms | 13.5 ms | 0 |
| warmup_only | 43.75 | 43.75 | 1020.0 ms | 25.0 ms | 6.5 ms | 12.5 ms | 0 |
| idle_low_rate_keepwarm | 43.75 | 43.75 | 320.0 ms | 6.5 ms | 6.5 ms | 10.5 ms | 0 |
| repeat_last_keepwarm | 50.00 | 43.75 | 0.0 ms | 6.5 ms | 6.5 ms | 10.0 ms | 15 |
| independent_worker | 87.50 | 87.50 | 1010.0 ms | 45.0 ms | 6.5 ms | 7.7 ms | 0 |
| worker_keepwarm | 100.00 | 87.50 | 0.0 ms | 6.5 ms | 6.5 ms | 7.5 ms | 30 |
| always_full_rate | 100.00 | 87.50 | 0.0 ms | 6.5 ms | 6.5 ms | 7.5 ms | 30 |

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
