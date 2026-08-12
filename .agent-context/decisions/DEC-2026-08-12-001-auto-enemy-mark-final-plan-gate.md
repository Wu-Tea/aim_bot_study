# DEC-2026-08-12-001: Auto Enemy Mark Uses Final-Plan Markability

Status: accepted
Date: 2026-08-12
Confirmed by: the user asked to record the reviewed auto-mark design after
observing frequent, unnatural empty world marks in live Black Ops 7 play.
Related sessions:
- 2026-08-12
Related files:
- `native/runtime_app/person_detection_gesture.h`
- `native/runtime_app/runtime_loop.cpp`
- `native/pipeline_contract/target_plan.h`
- `native/vision_native/src/target_selector.cpp`
Supersedes: the deferred auto-mark wording in
`DEC-2026-08-11-002-native-irdt-single-owner-v1.md`
Superseded by: none

## Context

The implemented gesture treats a recently selected person with enemy cue
evidence as permission to synthesize D-pad Up. That is not equivalent to the
game accepting an enemy mark: Black Ops 7 applies the mark along the current
crosshair ray, while the selected person may still be outside that ray during
ADS acquisition. The implementation also lets 250 ms-old evidence authorize a
press and treats L3 and LT as independent opportunities to mark the same
target generation.

## Decision

- L3 and LT express one pending mark request; neither directly authorizes an
  output press merely because a person was detected.
- The request is consumed only after the final controller `TargetPlan` says
  the current target is markable: a direct person observation exists, the
  current frame has enemy cue evidence, the target is not cue-only
  continuation, D and R are valid, and the crosshair is inside R.
- Markability must hold for two consecutive fresh Vision observations of the
  same selector target generation before one 50 ms synthetic D-pad Up press.
- A selector target generation may be auto-marked at most once, regardless of
  whether L3, LT, or both supplied the request. A new confirmed target
  generation may be marked once after satisfying the same gate.
- Historical enemy evidence may preserve aim continuity but may not authorize
  a digital mark. In particular, the current 250 ms evidence hold is removed
  from actuation authority.
- A physical D-pad Up input always passes through unchanged.
- The gesture remains outside aim authority: it consumes the existing final
  I/R/D/T plan and may only merge one digital button into the delivered output.

## Reasons

- The final plan is the existing single source of truth for identity, current
  hittable region, desired point, freshness and cue continuation; a parallel
  person/cue state machine recreates conflicting ownership.
- Requiring the crosshair inside R aligns the software event with the game's
  ray-based mark behavior instead of firing while AI is still acquiring.
- Two fresh observations add roughly one Vision-frame interval at the current
  cadence while rejecting one-frame selector or cue noise.
- Once-per-generation semantics prevents the current L3-then-LT double mark
  without adding an arbitrary global cooldown.

## Rejected Alternatives

- Keep detection-time triggering and tune the 250 ms hold: rejected because
  evidence age cannot prove that the crosshair ray intersects the enemy.
- Add corpse, distance, weapon and map-specific exceptions: rejected because
  these patch symptoms while retaining the wrong actuation owner.
- Add a Body/Pose model only for marking: rejected for V1 because the existing
  D/R contract already provides the required conservative geometry.
- Treat current cue alone as markability: rejected because live empty marks
  demonstrate that cue-confirmed selection can precede crosshair alignment.

## Evidence

- User-confirmed live symptom on 2026-08-12: the current strategy emits many
  empty marks and looks unnatural.
- Repository fact: `PersonDetectionGesture` currently preserves enemy evidence
  for 250 ms and can fire from either L3 or LT before `TargetPlan` is built.
- Repository fact: `TargetPlan` already exposes D, R, current enemy cue,
  observation age, selector target generation and cue continuation.

## Consequences

- The current `PersonDetectionGesture` implementation and its tests are known
  to implement the superseded semantics and require a focused rewrite.
- A RED regression must cover far/off-axis detection, stale cue, cue-only
  continuation, L3-then-LT duplication, friendly/corpse candidates, stable
  inside-R success, target replacement and physical D-pad passthrough.
- Add bounded telemetry for synthetic-mark request, fire/cancel result, target
  generation and block reason so video incidents can be joined to output
  behavior. Do not log personal media paths.

## Review Triggers

- Live evidence shows that crosshair-inside-R still produces frequent empty
  marks because R is not a faithful hittable region.
- A supported COD title uses a mark mechanic that is not crosshair-ray based.
- Measured two-frame confirmation creates a visible or gameplay-relevant delay.

## Implementation checkpoint — 2026-08-12

- Completed in the production runtime. L3/LT now open one 250 ms request; only
  two fresh final plans for the same selector generation may emit one 50 ms
  D-pad Up press.
- The final gate requires a direct current class-0 person, current enemy cue,
  valid D/R, no cue-only continuation and crosshair inside R. Green-friendly
  and selector-rejected corpse candidates fail before actuation.
- L3 can wake Vision without changing controller aim intent. Physical D-pad Up
  remains an unconditional passthrough.
- Mark request, confirmation, generation/scope, fire/cancel and block reason are
  recorded in telemetry schema 17.
- Focused gesture, selector, runtime wiring and telemetry tests pass as part of
  the full `49/49` Release CTest suite. Matched live acceptance remains open.
- Later user-requested refinement: accepted L3 request edges use configurable
  `l3_cooldown_ms` (default 1000); LT requests remain independent.
