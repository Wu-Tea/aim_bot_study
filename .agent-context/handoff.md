# Agent Handoff

Last updated: 2026-05-11T22:55:15+08:00
Updated by: Codex
Active scope: Gamepad controller config, benchmark coverage, and release-tail tuning
Staleness: stale after `gamepad_start.bat` stops being the default launcher, the gamepad benchmark scenarios or baseline format change materially, the controller loop is moved across the C++/Python boundary, or live testing rejects the current release-tail behavior.

## Current Objective

Stabilize the default gamepad runtime launched through `gamepad_start.bat` while keeping controller tuning measurable:

1. keep the most tactile gamepad tuning knobs visible in `config.toml.example`
2. keep AI aim release-window behavior smooth enough to reduce residual error without reintroducing stale carry
3. compare benchmark runs against the recorded baseline and recent runs
4. make benchmark reports show whether recovery/settle metrics were actually observed before interpreting deltas
5. continue watching the Python/C++ interaction boundary for latency, but do not port controller logic without measurement evidence

## Current State

- Current branch/worktree: `codex/native-gamepad-cleanups` under `.worktrees/native-gamepad-cleanups`.
- Latest commit: `9537921 Improve gamepad release tail and benchmark coverage`.
- User-facing entry remains `gamepad_start.bat`; use this path when rechecking startup assumptions.
- `controllers/gamepad/ai_aim.py` now supports `body_lock_release_tail_scale`.
  - Default is `0.20`.
  - Setting it to `0.0` restores hard zeroing inside the release threshold.
  - Zero-cross still clears body-lock carry so old-direction correction does not leak through.
- `config/loader.py` and `config.toml.example` now expose the release-tail knob through normal config loading.
- Benchmark aggregate reporting now records scenario-level coverage ratios for:
  - turn recovery
  - decel settle
  - wrong-input recovery in manual-mix benchmarks
- `tests/gamepad/benchmark_scoreboard.py` surfaces those coverage deltas in history so future comparisons do not confuse missing measurements with real improvement.

## Verification

Fresh verification before commit:

- `py -3 -B -m unittest tests.gamepad.test_gamepad_benchmark_runner tests.gamepad.test_gamepad_benchmark_scoreboard tests.gamepad.test_gamepad_ai_aim_plugin tests.test_config_loader tests.gamepad.test_gamepad_benchmark_metrics tests.gamepad.test_gamepad_ads_benchmark_metrics tests.gamepad.test_gamepad_manual_mix_metrics tests.gamepad.test_gamepad_adaptive_delta_gain_simulation -v`
  - result: `96` tests passed
- `git diff --check`
  - result: passed
  - note: Git printed LF/CRLF normalization warnings only
- Post-commit `git status --short`
  - result: clean before this context sync

## Next Action

1. Run the controller benchmark again from the latest commit and compare against the current baseline plus the last few local results.
2. Inspect coverage deltas before accepting any recovery/settle improvement as real.
3. If live feel shows sticky residual near target center, tune `body_lock_release_tail_scale` first because it is the new narrow control for that behavior.
4. Smoke-test `gamepad_start.bat` after config changes to confirm the default entry still loads the intended config path.
5. If latency remains a concern, instrument the controller loop and native result handoff before considering any C++ controller migration.

## Blockers

- No live gameplay smoke result is recorded after commit `9537921`.
- The recent work improves benchmark observability but does not by itself prove a new live-feel baseline.
- The C++/Python communication latency concern has not yet been isolated with timing instrumentation in this branch.

## Files To Read First

- `gamepad_start.bat`
- `config.toml.example`
- `config/loader.py`
- `controllers/gamepad/ai_aim.py`
- `controllers/gamepad_controller.py`
- `tests/gamepad/benchmark_metrics.py`
- `tests/gamepad/manual_mix_metrics.py`
- `tests/gamepad/benchmark_scoreboard.py`
- `tests/gamepad/test_gamepad_ai_aim_plugin.py`
- `tests/gamepad/test_gamepad_benchmark_metrics.py`
- `tests/gamepad/test_gamepad_manual_mix_metrics.py`

## Do Not Do

- Do not interpret lower recovery/settle deltas without checking coverage ratios.
- Do not move controller behavior into C++ until the latency hypothesis has direct measurement.
- Do not bury high-feel tuning knobs far down in config examples; keep them easy to find.
- Do not rerun unrelated OCR/recoil sweeps while working this gamepad benchmark/config thread.

## Related Context

- Previous recoil_app and native-vision context remains in `.agent-context/session-log-full.md` and decision records. `.agent-context/session-log.md` is now the short index for the full log.
- Native vision policy still stands: normal vision work should stay native unless the user explicitly reopens Python vision parity.
- This handoff is now scoped to gamepad/controller benchmark work, not recoil_app OCR stabilization.
