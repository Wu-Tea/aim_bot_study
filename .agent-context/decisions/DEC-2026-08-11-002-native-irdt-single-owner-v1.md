# DEC-2026-08-11-002: Native I/R/D/T Single-Owner V1

Status: implemented; live acceptance pending
Date: 2026-08-11
Confirmed by: the user defined D as a hittable upper-chest/head or
posture-valid point, R as the currently recognisable hittable region, and every
directional correction while I is owned as an attempt to move D on that same
person. The user authorized the algorithm refactor while keeping the broad
benchmark implementation out of scope.

Related:

- `DEC-2026-08-11-001-incident-first-gameplay-validation.md`
- `docs/project/AIM_CONTROL_PRODUCT_CONTRACT_V1_20260811.md`
- `docs/benchmarks/CLOSED_LOOP_GAMEPLAY_ACCEPTANCE_V1_20260811.md`
- `artifacts/regressions/vertical-correction-cue-release-20260811/`

## Decision

- VisionTargetSelector is the only owner of target identity I. A valid active
  identity is not replaced because another candidate has a higher score.
- The same right-stick sample has one identity purpose. It is
  `AcquireTarget`, `CorrectCurrentTarget`, or `HandoverTarget`; Vision ignores
  correction-only input when ranking identities.
- Vision publishes one selected source point and R. TargetCoordinator owns D,
  preserves it as normalized coordinates inside a moving R, and resets it only
  on a confirmed identity replacement.
- Any filtered directional input while ADS owns I updates D, independent of
  firing state or candidate count. IntentFilter is the single noise/deadzone
  owner; D adds no second deadzone.
- D is clamped inside R. Continued outward pressure at the boundary for the
  configured dwell becomes an explicit handover request. Only then may the
  selector consume direction for another identity.
- AssistControlStateMachine remains the sole final T owner. On an axis that is
  correcting D, the physical axis is the immediate output; AI is not added as
  a competing force. On release, AI may resume toward the retained D.
- Cue may translate the last direct R and carry D only for the same generation,
  with fresh cue evidence, for at most 180 ms. Cue remains aim-only and cannot
  train its own geometry.
- Production telemetry schema 14 records source point, R, D, region/point
  source, correction axes, boundary contact and exit intent.

## Why

The prior chain allowed one stick sample to be interpreted concurrently as
manual output, target handover, firing-only vertical offset and AI opposition.
It also allowed multiple layers to recompute an anatomical point. That made a
small correction disappear until an escape threshold released nearly the full
held input. The single-owner contract removes the conflicting meanings instead
of adding another smoother or compensation threshold.

## Current approximation and limits

- The current R is a lightweight pose-aware region derived from the selected
  detector box. It is explicit and replaceable, but it is not Body/Pose or
  occlusion truth and does not yet prove every point is hittable.
- `CueTranslated` is a bounded continuity estimate, not direct proof that the
  translated region is visible through cover.
- D traversal (180 ms), boundary dwell (250 ms), cue ceiling (180 ms), and the
  R geometry constants are provisional until matched COD video/input/telemetry
  calibration.
- The broad benchmark runtime, plant and scoring were not changed. One legacy
  unit oracle was aligned with the already-fixed wrong-identity hard gate: a
  partial average improvement cannot erase any wrong strong target lock.
  Passing the incident, unit tests and runtime smoke is implementation evidence,
  not gameplay acceptance.

## Verification checkpoint

- Fixed known-bad incident: direct acknowledgement `0.546220`, cue
  acknowledgement `0.142001`, release step `0.734222`, overall RED.
- Candidate on unchanged oracles: `1.116667`, `0.876222`, `0.000000`, overall
  GREEN.
- Release CTest: 43/43 pass.
- Native pipeline contract, including a real DXGI/model/runtime one-shot: PASS.
- Project skill contracts: 4/4 pass.

## Deferred feature

Superseded on 2026-08-12 by
`DEC-2026-08-12-001-auto-enemy-mark-final-plan-gate.md`. The feature has since
been implemented with detection-time semantics, but live evidence rejected
that behavior. L3/LT now mean a pending request; only final-plan markability may
authorize one D-pad Up press, and the gesture remains outside I/R/D/T aim
authority.

## Next acceptance step

Run one matched Black Ops 7 session with synchronized physical input, schema-14
telemetry and video. Verify the source point, R, D and final T at the reported
downward-correction/cue transition. If R is wrong while owner semantics remain
correct, improve the single geometry producer rather than adding a controller
patch or immediately training a Body/Pose model.
