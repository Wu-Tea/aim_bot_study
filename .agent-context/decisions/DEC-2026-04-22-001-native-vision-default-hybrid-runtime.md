# DEC-2026-04-22-001: Native vision default with hybrid runtime

Status: accepted
Date: 2026-04-22
Confirmed by: committed project state plus explicit user requests to use native vision in the real gamepad startup path
Related sessions:
- 2026-04-22T20:03:07+08:00
Related files:
- `gamepad_start.bat`
- `gamepad_debug.bat`
- `gamepad_native_debug.bat`
- `main.py`
- `vision/native_runner.py`
- `vision/runner.py`
- `docs/project/NATIVE_VISION.md`
Supersedes: none
Superseded by: none

## Context

The project started with repeated gameplay perf analysis on the Python vision path. The user prioritized latency and first wanted optimization focused on screenshot + recognition. After Python-side capture and pipeline optimizations stopped yielding decisive gains, the work moved into a staged C++ native vision implementation.

## Decision

Use native C++ vision as the default runtime for the gamepad startup path, while keeping the Python controller and the Python vision path available as the fallback and behavior oracle.

## Reasons

- Native vision produced materially better live throughput and lower end-to-end result freshness than the previous Python path.
- The native path now covers the full hot vision loop instead of only an isolated scaffold.
- Keeping the controller in Python lowers rollout risk while preserving the large performance win from moving the hot vision path native.
- Retaining Python vision as a fallback makes validation and rollback simpler.

## Rejected Alternatives

- Keep Python vision as the production default: rejected because the measured latency improvement from native vision was large enough to justify rollout.
- Rewrite vision and controller together before rollout: rejected because it would delay delivery and increase behavior-regression risk without being necessary to get the main performance gain.

## Evidence

- Recent commit chain from `2fd5ab0` through `2d5ecfe` implements the native ROI capture, inference, selector, enhancement, and Python runner integration.
- `docs/project/NATIVE_VISION.md` documents native vision as the default `gamepad_start.bat` path.
- User-observed live logs showed native behavior with much lower `wait_ms` and `age_ms` than the earlier Python path.

## Consequences

- `gamepad_start.bat` now defaults to `VISION_BACKEND=native`, `VISION_CAPTURE_FPS=140`, and `VISION_QUIT_KEY=0`.
- Python vision must still be maintained as a fallback path until native rollout validation is considered sufficient.
- Future latency analysis should focus on `age_ms` and live controller feel, not only isolated `infer_ms`.

## Review Triggers

- Revisit if native gamepad stability regresses in long-play testing.
- Revisit if the controller becomes the next proven dominant bottleneck.
- Revisit if the Python fallback path becomes unnecessary to maintain.
