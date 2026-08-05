# DEC-2026-07-14-002: Small/Far Target Assist Authority

Status: proposed
Date: 2026-07-14
Confirmed by: user confirmed the symptom; the proposed solution is AI-inferred and not accepted
Related sessions: 2026-07-14 I/O/ADS repair, user-profile analysis and strong-AI config trial; 2026-08-03 head-peek micro-adjustment clarification
Related files:
- .agent-context/handoff.md
- .agent-context/session-log.md
- .agent-context/decisions/DEC-2026-07-07-001-ai-assist-authority-boundaries.md
- native/controller_native/assist_authority_policy.cpp
- native/controller_native/vector_intent_fuser.cpp
- native/controller_native/cod_native_gamepad_benchmark.cpp
- native/controller_native/recoil_profile.cpp
- native/vision_native/src/target_selector.cpp
- config.toml
Supersedes: none
Superseded by: none

## Context

The user reports that visually small or very distant targets still cause AI stick input. For weapons whose recoil is already mostly neutralized by recoil feedback, a small manual correction is sufficient; added AI can become lateral or upward jitter.

The same session showed AI is substantially better during legitimate mid/far ADS acquisition. The aggressive live trial improved normal acquisition but increased adversarial near-target output and user-fight risk. Detection and selection alone therefore may not justify strong authority.

The August 3 clarification adds a distinct exposed-region case: when cover or a
window leaves only a scalp/head-sized firing opportunity, the user wants an
approximately 3% manual trim to the visible point. Instead, the manual trim can
feel swallowed while AI contributes small interfering movements. This symptom
is user-confirmed. Whether its primary cause is target-point geometry, proposal
selection, input thresholding, localization noise, or a combination remains
unconfirmed.

## Decision

Proposed direction:

- Treat apparent target size/distance and evidence quality as assist-authority inputs.
- Do not grant meaningful ADS output to a small/far candidate solely because it was detected and selected.
- Preserve strong mid/far acquisition when evidence is credible; bound, observe-only, or reject assist when apparent evidence is weak.
- Keep recoil as independent final feed-forward; do not make recoil consume target, tracker, size, or authority state.
- Prove the boundary with a deterministic benchmark before implementation.
- Do not choose between reduced AI authority, an exposed-region aim anchor, or a
  protected micro-trim contract until a fixture separates those mechanisms.
- Defer implementation until the current runtime/260 ms weapon and W0-W6
  mainline is complete.

## Reasons

- A global strength reduction would discard the measured mid/far AI advantage.
- Small/far localization noise can become visible stick motion.
- Recoil is already tuned for weapon behavior; coupling target instability into recoil mixes responsibilities.
- Authority gating can preserve credible acquisition without allowing weak evidence to control the stick.

## Rejected Alternatives

- Globally reduce ADS strength: loses measured acquisition benefit and hides the evidence problem.
- Reduce recoil feedback: reintroduces recoil and does not prevent lateral AI jitter.
- Smooth final combined output: risks delaying manual input and recoil timing.
- Add an immediate hard size cutoff: may reject legitimate distant targets and lacks benchmark evidence.

## Evidence

- User-confirmed symptom: small/far targets cause AI input and lateral/upward jitter when manual plus recoil would otherwise be stable.
- User-confirmed refinement: in a head-peek/scalp-only firing opportunity, the
  application can suppress an approximately 3% manual adjustment while adding
  small AI interference.
- Repository fact: `VisionTargetSelector::target_point` derives one point from
  the body box center X and a pose-dependent fixed Y ratio; it does not identify
  the currently exposed or user-desired scalp point.
- Repository fact: `VectorIntentFuser` uses a `0.02` intent threshold and, for a
  fresh reliable target, selects the higher-demand validated manual or AI
  proposal without reserving a micro-manual ownership floor.
- **AI-inferred, unproven:** anchor mismatch plus proposal selection can explain
  the reported feel, but live telemetry or a deterministic fixture is required
  before assigning causality.
- Profile: far ADS helpful rate was 63.7% manual versus 95.3% AI; AI was stronger-helpful in 77.8%.
- Aggressive trial: standard final error improved 12.14 -> 8.70px, but fight rose 130 -> 138, near-high 380 -> 421, and max final 0.809 -> 1.122.
- AI-inferred and unproven: apparent-size/evidence authority is the direct cause. A focused benchmark is required.

## Consequences

- Next work starts with benchmark reproduction, not recoil changes or another global gain.
- When the mainline is complete, sweep visible fraction/occlusion, desired point
  versus box-derived point, body-box size, error, confidence/tier, freshness,
  ADS phase, `0.02-0.05` manual correction, motion, and recoil feedback.
- Score weak small/far AI output, lateral/upward jitter, conflict, low-recoil stability, legitimate far acquisition, and recovery as evidence strengthens.
- The strong-AI config remains a reversible live A/B trial, not the accepted baseline.
- Until then, this record is informational only and must not change the protected
  August 3 runtime or interrupt the 260 ms/W0-W6 validation sequence.

## Review Triggers

- The benchmark cannot reproduce the jitter.
- Evidence identifies wrong selection, stale projection, or body-center error instead.
- Telemetry shows the approximately 3% physical/manual proposal survives intact
  and the disturbance originates after fusion or in the game response.
- Size/evidence gating harms legitimate far acquisition.
- Recoil-only playback jitters with AI disabled.
- The current mainline is complete and the user asks to prioritize this case.
- The user accepts, rejects, or refines this proposal after live testing.
