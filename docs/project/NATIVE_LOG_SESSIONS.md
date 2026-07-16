# Native Debug Log Sessions

High-rate native telemetry is disabled by default. Enable it only while reproducing a problem by setting either `runtime.telemetry.enabled = true` or the compatibility switch `runtime.vision.aim_perf_file_log = true`.

Each enabled runtime creates one directory under:

```text
runs/native_perf/sessions/<UTC timestamp>_<pid>_<nonce>/
```

`runs/native_perf/fresh_session.json` is atomically updated at startup and is the canonical way to locate the newest run. Do not infer the fresh run from individual JSONL modification times.

An active writer owns `.active`. Clean shutdown replaces it with `.closed`. Telemetry rotation uses monotonically numbered files inside the session and never overwrites earlier parts of the same reproduction.

## Inspect

```powershell
python tools/manage_native_logs.py list --root runs/native_perf
```

## Preview cleanup

```powershell
python tools/manage_native_logs.py prune --root runs/native_perf --dry-run --max-age-days 7 --keep-latest 2 --max-total-gb 20
```

## Apply cleanup

Run the same command without `--dry-run`. Cleanup removes whole closed sessions only. It protects the fresh session, active sessions, pinned sessions, and the configured number of newest closed sessions. Invalid manifests and paths outside `runs/native_perf/sessions` are reported but never deleted.

Large debug sessions are expected. Rotation protects writer stability; retention happens between sessions instead of truncating the active evidence stream.
