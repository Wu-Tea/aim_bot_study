# Agent Handoff

Last updated: 2026-08-17
Active scope: production native C++ controller; ADS is user-accepted and the
remaining live issue is sustained horizontal BodyLock tracking.
Staleness trigger: refresh after the newest gameplay telemetry audit or the
next BodyLock candidate is tested.

## Current Objective

Audit the newest live telemetry for sustained horizontal target motion. Separate
Vision/identity/velocity-estimation faults from final-arbitration suppression
before changing production behavior again.

## Current State

- Direct/PID production experiment is retired. It was materially faster and
  more accurate on immediate error response, but amplified per-frame geometry
  noise into severe high-frequency oscillation and lacked production lifecycle,
  manual-intent and evidence policies. Keep it only as a diagnostic reference.
- Production BodyLock now has two deterministic repairs: axis-local bounding of
  opposing motion feed-forward, and fresh-position ownership that removes stale
  opposing manual work continuously outside the existing settle envelope.
- The latter fixture changed from six 5 ms ticks of wrong-direction `+0.0975`
  to target-direction `-0.25` on tick zero; four safety counterfactuals pass.
- ADS keeps one snap token per LT. Mid/far uses the nominal 135 ms response;
  close targets transition continuously to 60 ms by visual size. The user has
  confirmed current ADS behavior has no apparent problem.
- Full Release build and `68/68` CTest pass. The BodyLock target-direction
  regression contract passes with zero issues.
- Candidate runtime:
  `native/vision_native/build/Release/cod_native_runtime.exe`, SHA-256
  `7ba259fd89ad8838a79c0c9d79d6eb3b824bcd2586a6b1199e438de7810e7f88`.

## Next Action

Preflight and audit the newest detailed log for sustained horizontal tracking;
freeze a new incident only if a controller-owned signature is reproducible.

## Blockers

None. The user reports that sustained horizontal BodyLock still falls behind.

## Active Questions

- Whether the remaining lag is missing/incorrect target velocity, target
  identity churn, stale/non-fresh evidence, or final-output arbitration.
- Whether the latest log has sufficient detailed telemetry and runtime identity
  to distinguish those mechanisms.

## Relevant Decisions

- `decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`
- `decisions/DEC-2026-08-07-001-target-first-final-output.md`
- `decisions/DEC-2026-08-11-001-incident-first-gameplay-validation.md`
- `decisions/DEC-2026-08-12-002-ads-full-authority-after-admission.md`
- `decisions/DEC-2026-08-17-001-retire-direct-controller-experiment.md`

## Files To Read First

- newest native session manifest and detailed telemetry
- `artifacts/telemetry-audits/20260817-bodylock-sticky-manual-ai/report.md`
- `artifacts/regressions/bodylock-position-motion-axis-conflict-20260817/regression-manifest.json`
- `artifacts/regressions/bodylock-target-direction-latency-20260817/regression-manifest.json`
- `native/controller_native/response_model_aim_solver.cpp`
- `native/controller_native/bodylock_follow_controller.cpp`
- `native/controller_native/assist_control_state_machine.h`

## Do Not Reopen Unless Needed

- restoring Direct as a production path, additive manual-plus-AI owners,
  generic coast/hold/brake, or fixed blend percentages;
- weaker ADS authority, changed recoil ownership, or Vision/model changes;
- treating all cue continuation as a Controller failure or all internally
  stable Vision geometry as visual ground truth.

## Notes

The working tree may contain unrelated user changes; preserve them. Direct's
useful result is its response-speed counterfactual, not its deleted production
implementation. Do not infer a new velocity observer until live joins isolate
missing target-motion demand from downstream suppression.
