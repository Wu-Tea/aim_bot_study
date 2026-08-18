# Agent Handoff

Last updated: 2026-08-18
Active scope: production native C++ controller; ADS remains user-accepted and
the current candidate repairs sustained horizontal BodyLock demand.
Staleness trigger: refresh after the next live BodyLock candidate session.

## Current Objective

Live-validate the capture-aligned BodyLock target-motion candidate on sustained
lateral targets, direction changes and firing/FOV disturbance.

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
- BodyLock now has one focused `BodylockTargetMotionObserver`. For each direct,
  same-target capture interval it removes the causally aligned pre-recoil
  camera command from observed screen motion, then publishes the target's
  sustaining total motion. Two samples establish the estimate, a three-sample
  median rejects one-frame spikes, and deceleration discharges faster than rise.
- The estimate only drives `Observed` BodyLock. Cue continuation neither trains
  nor consumes target-motion authority, preserving the existing close/full and
  far/reduced cue policy. ADS, final manual arbitration and independent recoil
  composition are unchanged.
- The frozen production-path incident changed from no response within 900 ms,
  `21.040884 px` step error growth and `16.869894 px` final residual to `23 ms`,
  `2.698075 px` and `0.735421 px`; recovery overshoot is `0 px`. No-step,
  slowdown and target-generation replacement counterfactuals pass.
- Full Release build and `70/70` CTest pass. The complete regression contract
  passes with zero issues; architecture contract version is 4.
- Candidate runtime:
  `native/vision_native/build/Release/cod_native_runtime.exe`, SHA-256
  `bbdb1bc5344b094d7553623e7a735c8a37e2f53461d65c0cfff87e9563dfe8a8`.
- The newest complete-shard audit found 27 strict same-direction follow runs.
  Final arbitration fully preserved at least 95% of the stronger of manual and
  BodyLock request on 328/340 samples, yet error still grew materially on
  90/340 samples. All 90 growing-error samples were already fully delivered.
- Representative stable-identity tracks 445, 460 and 626 retained direct
  observations and full authority. Their BodyLock requests were generally below
  both the user's sustaining stick and the configured request cap; Dynamics
  shaping was not the material limiter.
- The prior evidence-supported upstream diagnosis is now implemented and GREEN
  offline. Live feel under weapon view kick, FOV transitions and real detector
  noise remains unverified; do not call those conditions fixed without a new
  session.
- The online response estimate near 1090--1160 px/(stick*s), versus the 500
  fallback, is a credible secondary attenuation risk but is not yet proven
  numerically wrong for the game.

## Next Action

Run the candidate in the same sustained-horizontal gameplay conditions. If it
still falls behind or overshoots, capture a new log and compare observed
screen-rate, BodyLock target-motion demand, requested output and final output on
the same target generation before changing confidence or arbitration.

## Blockers

- Live acceptance is pending.
- `scripts/verify/native_pipeline_contract.bat -SkipBuild -SkipBenchmark`
  currently fails on the pre-existing `pre_recoil_stick` name in
  `native_gamepad_controller.cpp`; the same lines exist at HEAD. Runtime tests,
  the recoil contract and the incident complete gate pass.

## Active Questions

- Whether the current two-sample/median/rise-release envelope is smooth enough
  under live recoil and detector geometry noise without becoming late.
- Whether the response estimator's roughly 1090--1160 px/(stick*s) live values
  are accurate; the current incident does not prove a calibration defect.
- Whether stronger same-direction manual ever creates a separate overshoot
  incident. Do not change the current arbitration contract without that RED.

## Relevant Decisions

- `decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`
- `decisions/DEC-2026-08-07-001-target-first-final-output.md`
- `decisions/DEC-2026-08-11-001-incident-first-gameplay-validation.md`
- `decisions/DEC-2026-08-12-002-ads-full-authority-after-admission.md`
- `decisions/DEC-2026-08-17-001-retire-direct-controller-experiment.md`

## Files To Read First

- newest native session manifest and detailed telemetry
- `artifacts/telemetry-audits/20260817-bodylock-sticky-manual-ai/report.md`
- `artifacts/telemetry-audits/20260817-latest-bodylock-horizontal/report.md`
- `artifacts/regressions/bodylock-position-motion-axis-conflict-20260817/regression-manifest.json`
- `artifacts/regressions/bodylock-target-direction-latency-20260817/regression-manifest.json`
- `artifacts/regressions/bodylock-target-motion-total-20260818/regression-manifest.json`
- `native/controller_native/bodylock_target_motion_observer.cpp`
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
implementation. The BodyLock target-motion observer is a focused, incident-owned
component, not permission to restore a general causal-motion stack.
