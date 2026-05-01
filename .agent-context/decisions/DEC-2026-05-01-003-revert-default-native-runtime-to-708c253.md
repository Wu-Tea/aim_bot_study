# DEC-2026-05-01-003: Revert the Default Native Runtime Baseline to Commit 708c253

Status: accepted
Date: 2026-05-01
Confirmed by: user
Related sessions:
- 2026-05-01T16:48:00+08:00
Related files:
- main.py
- gamepad_start.bat
- mouse_start.bat
- gamepad_native_debug.bat
- mouse_native_debug.bat
- vision/native_runner.py
- native/vision_native/src/vision_engine.cpp
- native/vision_native/src/target_selector.cpp
- native/vision_native/src/vision_native_module.cpp
- tests/test_native_vision_runner.py
- tests/test_native_vision_targeting_bridge.py
Supersedes: none
Superseded by: none

## Context

The user reported that the current ROI/native experiment branch was performing significantly worse on real gameplay material than the earlier native detector-led path. The newer branch had accumulated the 2026-04-30 evening hotpath/cadence/yellow-cue changes (`ac224b1` and `2b8a35b`) on top of the earlier native baseline.

An intermediate assistant action incorrectly shifted the startup defaults toward the Python backend. The user corrected that the requested rollback target was still the native YOLO path, not the Python fallback.

Git inspection identified commit `708c253` (`Prune mouse benchmark artifacts`, 2026-04-30 13:22:09 +0800) as the practical pre-hotpath native baseline immediately before the 2026-04-30 evening experiment branch.

## Decision

Restore the default controller-facing native runtime to the `708c253` state and treat that as the active baseline for further live validation and future experiments.

This includes:

1. keeping native as the default startup backend
2. removing the later hotpath/yellow-cue experiment branch from the default runtime
3. deferring further optimization work until the reverted baseline is re-validated live

## Reasons

- It matches the user's explicit correction that the rollback target should remain native.
- It provides a concrete, versioned baseline instead of an ambiguous “older better state”.
- It avoids conflating dissatisfaction with the 2026-04-30 evening experiment branch with a broader rejection of the native runtime itself.
- It creates a stable base for comparing any future yellow-cue or continuity experiment against known-good native behavior.

## Rejected Alternatives

- Revert the default runtime to Python: rejected because the user explicitly corrected this as the wrong rollback target.
- Keep the `ac224b1` / `2b8a35b` experiment branch and continue tuning it live: rejected because the reported regression was large enough that a clean baseline reset was preferred.
- Rewind all the way back to pre-native integration: rejected because the user still wanted the native YOLO path rather than abandoning native altogether.

## Evidence

- User instruction: roll back to the native YOLO path rather than the Python backend.
- Git history:
  - `708c253` on 2026-04-30 13:22:09 +0800
  - `ac224b1` on 2026-04-30 20:58:56 +0800
  - `2b8a35b` on 2026-04-30 23:47:02 +0800
- Verification after rollback:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - `py -3 -m unittest tests.test_startup_scripts tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_targeting_bridge -v`

## Consequences

- The default runtime becomes a simpler native baseline than the later experiment branch.
- The 2026-05-01 proposed birth-path and lateral-tracking optimizations are not abandoned, but they are no longer the current runtime roadmap until baseline validation resumes.
- Any future attempt to reintroduce yellow-cue or continuity-heavy logic should be measured against `708c253`, not assumed safe by default.

## Review Triggers

- Revisit if live validation shows that `708c253` is not actually the correct stable baseline.
- Revisit if a narrower post-`708c253` native commit proves to be a better recovery point.
- Revisit when the user explicitly asks to re-open yellow-cue or continuity experiments on top of the restored native baseline.
