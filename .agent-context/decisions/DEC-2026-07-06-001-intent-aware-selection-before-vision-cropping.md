# DEC-2026-07-06-001: Intent-Aware Selection Before Vision Cropping

Status: proposed
Date: 2026-07-06
Owner: Codex/user discussion

## Context

The native C++ runtime currently detects targets in a broad crop and selects one target for controller use. Live evidence suggests wrong-target locks can happen in multi-target scenes: the user may intend a close side-running target near the lower-left of the crosshair, while selector/ADS can prefer a farther, smaller, more front-facing target on the right/up.

The selector already has interfaces that can accept `UserAimIntent`, but the live `VisionEngine` path has not been confirmed to pass right-stick intent into target selection. ADS snap consumes the selected strong target and does not independently reselect by user intent.

## Decision

Prefer intent-aware target selection and ADS authority gating before implementing hard userInput-based vision image cropping.

The first implementation direction should be:

- Pass live user right-stick intent into `VisionTargetSelector`.
- Keep the base vision crop broad.
- Use user intent, cue/live evidence, and validity state to gate ADS strong-snap eligibility.
- Treat userInput-guided ROI as a later soft-ROI/performance optimization, not the first behavioral fix.

## Rationale

- Passing user input into selector directly addresses wrong-target choice.
- ADS strong snap should be narrower than the vision detection range.
- Hard cropping risks losing targets during sliding, jumping, occlusion, edge-of-crop motion, or user correction.
- Cue/live/validity evidence should decide authority, not only contribute score.
- Soft ROI can be added later once intent-aware selection behavior is measurable.

## Rejected Alternatives

- Immediate hard crop based on right-stick direction: too much risk of missing edge targets or reinforcing a wrong direction.
- Only shrink ADS detection range: may fail close side-running cases and does not solve intent mismatch.
- Only increase cue weight: helps corpse-lock risk but does not solve user-intended target selection.
- Let controller fix wrong selector output after the fact: controller cannot reliably infer the intended target once vision has collapsed candidates to the wrong strong target.

## Consequences

- Add native tests/benchmarks for multi-target intent selection.
- Update live native runtime wiring so `VisionEngine` receives and uses current `UserAimIntent`.
- Keep weak/cue/predicted targets unable to grant fire authority.
- Defer hard ROI cropping until selection behavior is stable and a soft-ROI fallback model is designed.

## Review Triggers

- Live testing still shows wrong-target ADS snaps after intent-aware selector changes.
- Benchmarks show intent gating causes unacceptable target acquisition slowdown.
- Vision performance becomes the limiting factor and soft ROI is needed for frame time.
- New detector/model behavior changes cue reliability or corpse-lock patterns.
