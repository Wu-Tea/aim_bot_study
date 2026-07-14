# DEC-2026-07-14-002: Small/Far Target Assist Authority

Status: proposed
Date: 2026-07-14
Confirmed by: user confirmed the symptom; the proposed solution is AI-inferred and not accepted
Related sessions: 2026-07-14 I/O/ADS repair, user-profile analysis, and strong-AI config trial
Related files:
- .agent-context/handoff.md
- .agent-context/session-log.md
- .agent-context/decisions/DEC-2026-07-07-001-ai-assist-authority-boundaries.md
- native/controller_native/assist_authority_policy.cpp
- native/controller_native/cod_native_gamepad_benchmark.cpp
- native/controller_native/recoil_profile.cpp
- config.toml
Supersedes: none
Superseded by: none

## Context

The user reports that visually small or very distant targets still cause AI stick input. For weapons whose recoil is already mostly neutralized by recoil feedback, a small manual correction is sufficient; added AI can become lateral or upward jitter.

The same session showed AI is substantially better during legitimate mid/far ADS acquisition. The aggressive live trial improved normal acquisition but increased adversarial near-target output and user-fight risk. Detection and selection alone therefore may not justify strong authority.

## Decision

Proposed direction:

- Treat apparent target size/distance and evidence quality as assist-authority inputs.
- Do not grant meaningful ADS output to a small/far candidate solely because it was detected and selected.
- Preserve strong mid/far acquisition when evidence is credible; bound, observe-only, or reject assist when apparent evidence is weak.
- Keep recoil as independent final feed-forward; do not make recoil consume target, tracker, size, or authority state.
- Prove the boundary with a deterministic benchmark before implementation.

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
- Profile: far ADS helpful rate was 63.7% manual versus 95.3% AI; AI was stronger-helpful in 77.8%.
- Aggressive trial: standard final error improved 12.14 -> 8.70px, but fight rose 130 -> 138, near-high 380 -> 421, and max final 0.809 -> 1.122.
- AI-inferred and unproven: apparent-size/evidence authority is the direct cause. A focused benchmark is required.

## Consequences

- Next work starts with benchmark reproduction, not recoil changes or another global gain.
- Sweep body-box size, error, confidence/tier, freshness, ADS phase, manual correction, motion, and recoil feedback.
- Score weak small/far AI output, lateral/upward jitter, conflict, low-recoil stability, legitimate far acquisition, and recovery as evidence strengthens.
- The strong-AI config remains a reversible live A/B trial, not the accepted baseline.

## Review Triggers

- The benchmark cannot reproduce the jitter.
- Evidence identifies wrong selection, stale projection, or body-center error instead.
- Size/evidence gating harms legitimate far acquisition.
- Recoil-only playback jitters with AI disabled.
- The user accepts, rejects, or refines this proposal after live testing.
