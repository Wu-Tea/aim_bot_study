# DEC-2026-08-17-001: Retire Direct Controller Experiment From Production

Status: accepted
Date: 2026-08-17
Confirmed by: user explicitly abandoned the Direct implementation, requested
its code be deleted, and asked that its exploration direction and tradeoffs be
recorded
Related sessions:

- 2026-08-17 pure PID/direct ADS plus BodyLock experiment
- 2026-08-17 return to the production controller and latency repair

Related files:

- `native/controller_native/ads_acquisition_controller.cpp`
- `native/controller_native/bodylock_follow_controller.cpp`
- `native/controller_native/assist_control_state_machine.h`
- `artifacts/regressions/bodylock-target-direction-latency-20260817/`

Supersedes: none
Superseded by: none

## Context

Repeated production patches had accumulated arbitration and lifecycle judgments.
To test whether that complexity caused missed or delayed work, a deliberately
simple high-frequency controller was built around direct proportional/PID-style
response to the current Vision error, with ADS snap and BodyLock combined and
minimal interference with manual input.

The experiment produced a useful counterfactual. Immediate targeting was much
faster and often more accurate than the layered production path, and it made
controller-owned no-output or late-output behavior easier to see. It also
produced severe high-frequency visual oscillation. The jitter did not always
cause misses because it was fast, but it remained pervasive in gameplay. Attempts
to graft production damping and ADS completion behavior onto Direct did not make
it an acceptable production controller; one observed failure mode was continued
ADS correction after effective arrival, causing repeated crossing.

## Decision

- Remove Direct implementation, runtime selection, configuration and tests from
  the production tree. Direct must not remain as a dormant production branch.
- Continue with the production ADS and BodyLock controllers, carrying over only
  behavior proven by isolated incidents or matched tests.
- Preserve Direct's central diagnostic question: when current Vision error is
  fresh and authoritative, does production respond in the correct direction on
  the current controller tick without an unexplained gate or stale arbitration?
- Use a simple direct/PID response only as an offline counterfactual or benchmark
  concept if needed later. Reintroduction into production requires a new explicit
  decision and evidence that noise, lifecycle and manual-intent boundaries are
  solved without recreating a second controller architecture.

## Advantages Observed

- High immediate response speed and materially better first-correction accuracy.
- Few hidden eligibility gates, making Vision-to-output causality easy to inspect.
- Exposed cases where production received usable target geometry but final
  arbitration delayed or suppressed work.
- Useful lower-complexity reference for response latency and direction oracles.

## Disadvantages Observed

- Severe high-frequency oscillation from treating frame-level error and geometry
  changes as commands without sufficient state/noise handling.
- Weak ADS completion and state-exit behavior could keep correcting after arrival
  and cross the target repeatedly.
- No equivalent of the production evidence, cue, target-generation, explicit
  exit, manual-correction and single-LT/single-snap contracts.
- Fixing those omissions incrementally began rebuilding the production strategy
  around a second controller, recreating the layering problem the experiment was
  intended to test.

## Reasons

- Gameplay established that response speed alone is insufficient; stable camera
  output and correct lifecycle ownership are hard product requirements.
- The experiment was most valuable as evidence against unnecessary production
  delay, not as a maintainable parallel controller.
- One production entry avoids divergent state, thresholds, reset rules and
  regressions.

## Rejected Alternatives

### Keep Direct behind a runtime flag

Rejected because dormant production branches still require configuration,
tests, lifecycle parity and future maintenance, and can silently diverge.

### Continue adding production smoothing to Direct

Rejected because this was already converging toward another layered production
controller without preserving the mature ownership policies.

### Discard every result from the experiment

Rejected because its fast first response demonstrated a legitimate benchmark
for detecting production no-output, wrong-direction and late-arbitration defects.

## Evidence

- User-confirmed: Direct had substantially higher speed and accuracy than the
  then-current production behavior.
- User-confirmed: oscillation remained severe and widespread after multiple
  attempted adjustments.
- User-confirmed: the Direct and hybrid variants were unacceptable and should be
  completely abandoned in production.
- Repository evidence: Direct production code and references were removed; the
  restored production runtime passed the full Release suite.
- Regression evidence: production final arbitration's stale opposing-manual case
  changed from `+0.0975` for all six 5 ms ticks to target direction `-0.25` on
  tick zero while preserving four counterfactual boundaries.

## Consequences

- Future controller work must modify the single production path and be justified
  by incident-first RED/GREEN evidence.
- Production latency tests should include first-material-output direction and
  tick, not only eventual acquisition or aggregate error.
- Direct-like responsiveness may be adopted as a measured response target, but
  not by bypassing target identity, freshness, lifecycle or final-output owners.

## Review Triggers

- Production again shows widespread fresh-target no-output or unexplained delay
  that cannot be isolated in its current owners.
- A shadow direct baseline can be run without adding production configuration or
  code-path ownership.
- A future controller architecture demonstrably handles measurement noise,
  completion, target identity and manual intent with fewer owners than production.
