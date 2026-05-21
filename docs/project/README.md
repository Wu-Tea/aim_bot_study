# Project Docs Index

This folder mixes current architecture notes, benchmark records, work snapshots, and older planning context. Use this page as the navigation layer instead of guessing which file is still current.

## Read first

1. `PROJECT_OVERVIEW.md`
   - project-level map, architecture diagrams, main flows, data contracts, and review notes
2. `README.md`
   - top-level project entry, startup scripts, dependencies, code structure
3. `WORKLOG.md`
   - latest project snapshot and recent changes
4. `NATIVE_VISION.md`
   - native vision build, runtime, bridge, and validation notes
5. `CONTROLLER_OVERVIEW.md`
   - controller boundaries and supported runtime modes
6. `VISION_OVERVIEW.md`
   - Python backend, native backend, and the shared vision-to-controller contract

## Runtime and architecture docs

- `PROJECT_OVERVIEW.md`
  - project-level map, architecture diagrams, runtime flow, controller flow, recoil flow, and review findings
- `CONTROLLER_OVERVIEW.md`
  - controller factory, shared controller contract, startup paths, and recommendations
- `VISION_OVERVIEW.md`
  - vision entry points, Python vs native backends, timing, debug behavior
- `GAMEPAD_OVERVIEW.md`
  - gamepad host architecture and the default plugin chain
- `MOUSE_OVERVIEW.md`
  - native mouse-output host, continuity rules, and debug entry points
- `MOUSE_TELEMETRY_DEBUGGING.md`
  - live mouse telemetry capture, health checks, and failure interpretation
- `NATIVE_VISION.md`
  - detailed native migration notes, build process, smoke tests, and payload contracts
- `ROOT_STRUCTURE_CLEANUP.md`
  - phased root directory cleanup plan and migration order

## Validation and benchmark docs

- `GAMEPAD_BENCHMARKS.md`
  - representative gamepad benchmark results
- `GAMEPAD_ADS_BENCHMARKS.md`
  - ADS-focused benchmark results
- `GAMEPAD_MANUAL_MIX_BENCHMARKS.md`
  - manual input mixing benchmark results
## Model and training docs

- `PERSON_DETECTOR_TRAINING.md`
  - dataset prep, training, and TensorRT export flow
- `PERF_PLAN.md`
  - older performance optimization plan and execution guidance

## Project state and historical context

- `WORKLOG.md`
  - most useful status timeline in this folder
- `TRACKING.md`
  - older project tracking / roadmap context

## Historical design and implementation records

Outside this folder, the repository also keeps:

- `docs/superpowers/specs/`
  - design docs written before implementation
- `docs/superpowers/plans/`
  - implementation plans and execution breakdowns

## Freshness rule

If two documents disagree:

1. trust the current startup scripts and code
2. then trust `WORKLOG.md`
3. then trust the latest overview doc
4. treat older plans, benchmarks, and tracking notes as historical context
