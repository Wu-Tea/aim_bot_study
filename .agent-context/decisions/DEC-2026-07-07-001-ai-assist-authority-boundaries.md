# DEC-2026-07-07-001: AI Assist Authority Boundaries Before Further Tuning

Status: accepted
Date: 2026-07-07
Confirmed by: user in Codex conversation on 2026-07-07
Related sessions: Current Codex session on native ADS/bodylock, userInput->vision, benchmark, and live overshoot/jitter investigation
Related files:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-07-06-001-intent-aware-selection-before-vision-cropping.md`
- `docs/superpowers/specs/2026-07-06-native-aimlab-userinput-vision-benchmark-design.md`
- `native/controller_native/cod_native_gamepad_benchmark.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/vision_native`
Supersedes: none
Superseded by: none

## Context

Recent live testing and benchmark work show that the native assist problem is no longer only about making ADS/bodylock faster or stronger. The user has observed wrong-target locks, ADS overshoot during manual input, target-near high output, bodylock jitter, corpse/dead-target sticking, and cases where the assist feels like it fights user control.

The current runtime already moves toward userInput-aware selection, but much of the system still behaves like a single selected target is enough for downstream control. Once vision or selector collapses multiple candidates into the wrong target, tracker and controller can only pursue that target correctly.

The user explicitly accepted that this direction should be treated as a durable decision and recorded so future sessions can report the full rationale and implementation direction.

## Decision

Before further tuning ADS/bodylock strength, treat the core architecture problem as AI assist authority management:

- Keep target candidates and evidence visible long enough for selector/authority logic; do not collapse the world into one target too early.
- Treat user input as a first-class intent signal, not as noise and not only as an additive stick component.
- Introduce or formalize a target authority layer that decides whether AI may strongly assist, weakly track, observe only, yield to the user, or release.
- Keep tracker memory as short-term continuity support, not as independent authority to strongly control the stick.
- Preserve ADS and bodylock as separate policies with different success criteria.
- Add benchmark and logging coverage for both positive assistance and anti-intervention behavior before relying on further controller tuning.

The intended pipeline direction is:

```text
Vision candidate targets + evidence
  -> UserIntent-aware selector
  -> TargetAuthority / assist-permission decision
  -> Tracker memory with evidence and user-intent decay
  -> Separate ADS and bodylock control policies
  -> Manual/AI arbitration
  -> Component and decision logging
  -> Benchmark scoring for hit, overshoot, smoothness, and anti-intervention
```

## Reasons

- Wrong target selection cannot be reliably fixed inside the controller after vision has reduced candidates to one target.
- User input is strong evidence about intended target, release intent, correction intent, and target switching.
- ADS and bodylock have different acceptable behavior: ADS should avoid visible overshoot and wrong strong snaps, while bodylock should prioritize smooth continuous tracking and may tolerate small overshoot.
- Tracker memory is needed for sliding, jumping, arc movement, crouch cycles, and brief occlusion, but it becomes harmful if it overpowers live evidence or user correction.
- Benchmark scores are incomplete unless they include situations where AI should not intervene.
- Logging output values alone is insufficient; future debugging needs decision reasons such as selected target id, rejected candidate reason, target authority, user intent stability, and why AI output was limited.

## Rejected Alternatives

- Only tune ADS speed, brake, or detection range: this may reduce one symptom while leaving wrong target locks and user-fight cases unresolved.
- Add more special-case `if` logic inside the controller: this risks coupling ADS, bodylock, corpse handling, tracker memory, and manual correction into an unmaintainable chain.
- Let tracker memory decide strong authority: this can look good in controlled benchmarks while causing sticky wrong-target or stale-target behavior in live play.
- Hard-crop vision from user input as the first fix: this can miss sliding, jumping, edge, occluded, or corrected targets. Soft ROI may be considered later after selection and authority behavior are measurable.
- Use one universal brake for ADS and bodylock: this can reduce ADS overshoot while damaging bodylock tracking of moving targets.

## Evidence

- User live reports include wrong-target ADS snap, ADS overshoot with manual input, bodylock jitter, corpse/dead-target sticking, and assist feeling too strong or visible.
- Recent benchmark work already exposed near-target high output, bodylock chatter/spikes, and sustained tracking weaknesses in slide/crouch/jump/arc movement scenarios.
- The existing decision `DEC-2026-07-06-001-intent-aware-selection-before-vision-cropping.md` established intent-aware selection before hard vision cropping.
- The user explicitly stated this authority-boundary direction is correct and should be recorded as a decision.

## Consequences

- Future work should start with benchmark and decision logging before major controller tuning.
- Benchmark coverage should include multi-target intent, anti-intervention, target validity/corpse/cue loss, err-target recovery, sustained movement tracking, ADS overshoot, and bodylock smoothness.
- Native logging should include candidate count, selected target id, prior target id, user intent direction/strength/stability, target authority, ADS/bodylock permission, tracker memory confidence, near-target budget, manual-opposing state, and AI output limiting reason.
- Controller implementation should move toward bounded policies such as `TargetAuthorityPolicy`, `ManualIntentState`, `ManualIntentArbitrationPolicy`, `AdsAcquisitionPolicy`, `AdsNearTargetPolicy`, and `BodylockTrackingPolicy`.
- Strong authority must remain evidence-gated: cue-only, weak-only, predicted-only, stale, or suspected corpse targets should not silently receive strong ADS/fire authority.
- Any continuation should be able to summarize this decision and point back to this record before implementation resumes.

## Review Triggers

- Live testing still shows AI fighting user correction after authority logging is added.
- Benchmarks improve while live feel regresses, indicating the benchmark is not representative enough.
- ADS changes reduce overshoot but damage bodylock tracking or smoothness.
- Selector intent gating increases acquisition latency beyond acceptable live feel.
- Vision/model changes alter cue reliability or corpse-lock behavior.
- Soft ROI/cropping becomes necessary for performance after authority and selection behavior are benchmarked.
