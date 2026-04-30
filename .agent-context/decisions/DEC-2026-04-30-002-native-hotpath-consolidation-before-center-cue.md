# DEC-2026-04-30-002 Native Hot-Path Consolidation Before Center Cue

Status: accepted
Date: 2026-04-30

## Context

The native vision runtime already uses:

- ROI capture
- WarmScan / ActiveTrack scheduling
- TensorRT person detection
- host BGRA frame
- EgoMotionEstimator
- VisionTargetSelector
- BodyStateTracker
- damping-only controller-facing enhancement

A new near-center yellow UI cue is being considered, but the current hot path still has three concrete problems:

- repeated grayscale conversion of the same host frame
- overlapping continuity responsibilities between selector legacy logic and body-state
- risk of thickening the CPU-side image-processing path further if a cue layer is added naively

The yellow cue itself is now clarified as a crosshair-near UI signal that appears only inside an approximately `250x250` center region, not as a world-space marker over the enemy.

## Decision

Before implementing the center yellow UI cue:

1. consolidate the native hot path first
2. make `VisionEngine` own and share grayscale derivation per frame
3. treat `BodyStateTracker` as the only native continuity authority
4. reduce the native path's dependence on selector-side legacy `predicted/hold` logic
5. keep native controller-facing enhancement on `process_damping_only(...)`
6. implement the yellow cue only as a center-zone final-stage refiner after body-state, not as a global selector or WarmScan cue

## Reasons

- avoids compounding duplicate frame-derived work
- prevents a new cue from being layered on top of overlapping continuity systems
- keeps the yellow cue aligned with its actual semantics: center-only UI signal, not global target discovery
- preserves the current controller contract and keeps the cleanup localized to the native mainline path

## Rejected Alternatives

- directly add the yellow cue on top of the current path without cleanup
- use the yellow cue as a global selector / WarmScan signal
- expand the cleanup to Python fallback in the same slice
- introduce a controller-visible `yellow_only_target` source in this slice

## Consequences

- implementation starts with a native-only cleanup pass
- yellow cue integration lands slightly later but on a cleaner pipeline
- Python fallback remains temporarily divergent
- tests for the native runtime should stop assuming selector legacy continuity is still a formal part of the mainline path

## Review Triggers

Revisit this decision if any of the following change:

- grayscale derivation is moved out of `VisionEngine`
- the yellow cue proves too unstable to justify runtime refinement
- the project intentionally reopens Python fallback parity cleanup
- the center cue is later promoted from a final-stage refiner into a broader semantic signal
