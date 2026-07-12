# Native Runtime Performance Acceptance — 2026-07-12

## Decision

Overall release decision: **FAIL**.

The implementation is buildable and preserves the controller/authority/recoil
baseline, but the experimental precision scheduler did not meet its promotion
gate. It remains opt-in and the legacy scheduler remains the default. Several
long-running/live-hardware gates are explicitly `UNVERIFIED`. Pascal/GTX 1060
deployment is outside the supported scope.

## Reference Machine

- OS: Windows 11 Pro 10.0.26100
- CPU/RAM environment: 64 GB system RAM
- GPU: NVIDIA GeForce RTX 4070 SUPER, 12,282 MiB, SM 8.9
- Driver: 591.59
- Development stack: CUDA 13.1, TensorRT 10.15.1.29
- Feature branch: `codex/native-runtime-performance-config`
- Baseline commit: `3f6ad8c`

## Section Results

| Section | Status | Evidence |
| --- | --- | --- |
| A. Configuration | UNVERIFIED | Normal template has 73 assignments total, 52 non-recoil, and at most 8 per module. The legacy fixture parses without unknown keys, and precedence/range/source tests pass. Because the fixture currently spot-checks resolved values rather than comparing every supported value against the old loader, exhaustive legacy equivalence is not claimed. |
| B. Vision cadence/wakeup | UNVERIFIED | Canonical 160/20 resolution, latest-only arithmetic, interruptible false→true wake, post-transition sequence barrier, and wake metrics pass unit tests. A deterministic 100-transition test recorded zero pre-aim authority grants. The required 60-second live cadence and hot-inference wake percentile run were not completed against a reproducible capture workload. |
| C. Telemetry | UNVERIFIED | Disabled mode, bounded non-blocking ring, exact overflow counters, per-frame deduplication, background serialization, rotation, and writer failure pass tests. One million enqueue operations recorded p95/p99 0.0001 ms, max 0.056 ms, zero drops. The required compiled-out comparison and 30-minute profile soak were not completed. |
| D. Scheduler | FAIL | 60-second A/B shows the precision candidate reduces CPU time but misses cadence and p99 non-regression gates. It was not promoted; `mode = "legacy"` remains default. |
| E. Color readback | UNVERIFIED | Buffer/fallback and selector fixture tests pass. Synthetic 10,000-copy A/B improved p95 from 0.0640 ms pageable to 0.0305 ms pinned (52.34%). Recorded-gameplay 10,000-candidate authority parity and mapped-resource lifetime gates were not available, so pageable remains default. |
| F. Controller/authority | UNVERIFIED | Focused controller, ADS, bodylock, auto-fire, output-validation, protocol, selector, benchmark-metrics, and recoil tests pass, and `native_pipeline_contract.ps1 -SkipBuild` passes. The committed comparison summary proves its listed metrics, but does not yet contain every required wrong-target, authority/fire, overshoot, target-error, and bodylock aggregate; full Section F PASS is therefore not claimed. |
| G. GTX 1060/Pascal | EXCLUDED | Removed from product scope. The runtime targets SM 7.5+ and loads existing `.engine` files without a companion manifest. |

## Scheduler Evidence

| Mode | Hz | CPU seconds / 60s | lateness p99 | missed | max consecutive |
| --- | ---: | ---: | ---: | ---: | ---: |
| Legacy default | 999.15 | 58.5625 | 0.0388 ms | 51 | 6 |
| Precision, 700 µs tail | 996.40 | 22.8906 | 0.0589 ms | 216 | 17 |
| Precision, 800 µs tail | 997.40 | 28.9844 | 0.0417 ms | 156 | 14 |
| Precision, 850 µs tail | 996.617 | 27.6094 | 0.0470 ms | 203 | 15 |
| Legacy, AboveNormal | 999.467 | 58.4062 | 0.0541 ms | 32 | 6 |
| Precision 800 µs, AboveNormal | 999.183 | 29.0312 | 0.0629 ms | 49 | 6 |

The precision candidate saves more than 15% CPU, but 800 µs still regresses
p99 lateness by about 7.5%, falls outside 1000 ± 2 Hz, and exceeds the missed
deadline/consecutive limits. The rollback rule therefore keeps legacy default.
Repeating both modes at `AboveNormal` brought precision cadence and missed rate
inside their individual limits, but its p99 remained about 16.3% worse than the
same-priority legacy run and both modes exceeded the maximum-consecutive limit.

## Configuration Surface

- vision: 8 keys
- telemetry: 7 keys
- scheduler: 3 keys
- input: 3 keys
- output: 2 keys
- tracker: 7 keys
- ADS: 8 keys
- bodylock: 8 keys
- auto-fire: 6 keys
- recoil: all 21 documented native overrides retained

ADS uses multiplicative strength controls so `1.0` preserves the distinct
existing sustain (`0.64/0.80`) and acquisition (`1.0/1.0`) ceilings. Acquisition
and sustain smoothing remain separate. This gives coherent add/subtract tuning
without silently changing the default controller behavior.

## Machine-Readable Artifacts

Generated artifacts are under the ignored directory:

`runs/native_perf/runtime_acceptance/local/`

- `scheduler_legacy_60s_run1.json`
- `scheduler_precision_tail700_60s_run1.json`
- `scheduler_precision_tail800_60s_run1.json`
- `scheduler_precision_tail850_60s_run1.json`
- `telemetry_enqueue_ring_1m.json`
- `color_readback_10k.json`
- `gamepad_baseline_3f6ad8c.json`
- `gamepad_candidate.json`
- `effective-config.txt`

The baseline gamepad executable was built from detached worktree commit
`3f6ad8c`, and both baseline/candidate used the same
`config.native.example.toml` and `--random-fov-ticks 0` workload.

The compact, reviewable comparison result is committed as
`docs/project/native_runtime_non_regression_20260712.json`; raw run artifacts
remain ignored because they are large and reproducible from the recorded
commit, workload, seeds, and hashes.

## Remaining Qualification Work

The following are validation work, not hidden implementation claims:

1. Repeat scheduler comparisons three times for five minutes per condition under isolated and representative game load.
2. Run telemetry compiled-out comparison, 10-minute enqueue stress, and 30-minute profile soak.
3. Run 100 aim transitions against hot live inference and capture wake percentiles.
4. Run pinned/pageable recorded-gameplay authority parity and mapped-resource lifetime A/B.

## User/ADS Telemetry Extension

Focused deterministic tests now cover versioned schema and per-file rotation
metadata, conservative anonymous target identity, 250 Hz event windows, input
episodes, control-to-image response pairing, visual ADS settle, ADS
lifecycle/completeness, and enabled-only runtime integration. These tests prove
implementation behavior; they do not upgrade Section C to PASS.

Before profile/model use, a 30-minute representative soak must still prove:

- critical event drops equal zero;
- every model-eligible record has complete sequence ranges;
- ambiguous target crossings remain diagnostic;
- visual settle is never granted from LT/timer evidence alone;
- telemetry-enabled controller outputs exactly match a disabled deterministic
  replay;
- enough `calibration_clean` ADS events exist per compatible
  weapon/optic/FOV context.
