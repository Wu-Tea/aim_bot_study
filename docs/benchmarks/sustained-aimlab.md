# Sustained AimLab benchmark

This benchmark measures the production `NativeGamepadController` in a deterministic
60-second closed loop. It is a scoring and regression tool, not a replacement
controller policy.

Each target has a 250–330 ms ADS acquisition deadline. A successful first circle
entry opens a 1000 ms BodyLock tracking window. The target then despawns for 50 ms.
The plant runs at 1000 Hz, observations arrive at deterministic 80–100 Hz intervals,
and the virtual game's slowdown transitions from 1.0 outside the target to 0.5 at
the 24 px circle edge and 0.4 at its center.

The official baseline runs seeds `1337`, `20260718`, and `424242` twice: once with
pure target motion and once with deterministic mixed human mistakes. Scores are
additive: faster acquisition, longer center-weighted tracking, and smooth output
increase the result. `over_events`, `undertrack_events`, false BodyLock interruption,
false stop, error percentiles, output delta, and jerk remain explicit diagnostics.

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify/run_sustained_aimlab_baseline.ps1
```

The script refuses to overwrite an existing baseline. Pass `-Output` for a new
comparison artifact. Every JSON records the git revision, dirty state, config path
and FNV-1a config fingerprint, simulator constants, script hashes, aggregate scores,
and per-target metrics.
