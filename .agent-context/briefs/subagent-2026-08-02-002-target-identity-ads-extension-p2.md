# Subagent Brief: P2 Target Identity Boundary and ADS Extension

Date: 2026-08-02
Owner: luna-max task `019fbaf1-359a-7041-b782-333c7f2ac698`
Role: worker
Status: ready for dispatch
Expiry: P2 completion, a conflicting identity/timing decision, or new live evidence

## Mission

Fix only the two upstream defects remaining after P1, without deploying:

1. a selector-confirmed replacement must start clean target geometry and
   coordinator motion state while preserving the physical ADS epoch and elapsed
   acquisition clock;
2. 135 ms is the nominal ADS boundary, not an automatic completion point. A
   fresh visible target that is still unacquired may remain in ADS acquisition,
   never beyond the configured 220 ms hard ceiling.

## Read First

- `.agent-context/handoff.md`
- `.agent-context/decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`
- `.agent-context/decisions/DEC-2026-07-29-001-decouple-ads-arrival-and-ownership-window.md`
- `native/vision_native/src/target_selector.cpp`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/vector_intent_fuser.cpp`
- their focused tests and `target_pipeline_integration_tests.cpp`

Do not read broad historical benchmark/artifact trees unless a focused test
cannot reproduce the stated defect.

## Proven Starting Facts

- `VisionTargetSelector::commit_target()` increments
  `selector_target_generation_` and publishes `selector_target_changed` only
  after its existing switch confirmation.
- `select_with_frame()` currently updates the selected motion anchor after
  `select_impl()`, but it does not clear the previous person's patch before
  bootstrapping a confirmed replacement. An overlapping replacement can
  therefore inherit the old target's appearance anchor.
- `TargetCoordinator` currently consumes a generation replacement mainly as
  `ads_target_switch_seen_`. Because `new_target` is only `!has_target_`, the
  replacement keeps the old persistent target id, stable-body tracker,
  velocity/acceleration, firing observer, settled state and remaining state.
- `VectorIntentFuser` already provides a one-tick `TargetChanged` admission
  boundary when `TargetPlan.target_id` changes. P2 should make the coordinator
  publish that real identity change rather than add another fuser gate.
- ADS continuation currently requires instantaneous
  `radial_closing_velocity_px_per_sec > 1`. A visible, unacquired target can
  therefore complete near 135 ms on one flat/noisy/stale velocity sample even
  though the product contract allows work up to 220 ms.
- P1 candidate `1AA216FB...4947D744` passes full isolated CTest `36/36`; preserve
  its single-final-output semantics.

## Required Work

### A. Establish RED identity fixtures first

- Selector: two confirmed people overlap enough that the old motion patch would
  pass the current continuity radius. On replacement, assert the first anchor
  is bootstrapped from the new detection/person, not correlated from the old
  patch. Keep controls for same-person frame-local source-id churn, box-edge
  reconstruction, rigid translation, short occlusion/reacquire and rejected
  one-frame challengers.
- Coordinator/pipeline: with continuous fresh frames, increment selector
  generation and `selector_target_changed=true`. Before the fix, demonstrate
  leakage of persistent target id or velocity/stable geometry/remaining into
  the new person. Assert one new persistent target id and one fuser
  `TargetChanged` admission tick after the fix.

### B. Make confirmed selector replacement an identity boundary

- In the frame-aware selector path, clear the old motion anchor/template before
  updating the newly confirmed replacement. Do not reset for same-generation
  reconstruction, ordinary motion, source-id churn, pending challengers or
  unconfirmed switches.
- In `TargetCoordinator`, recognize a fresh, nonzero, changed generation as a
  target replacement independent of frame-local `source_id`.
- On replacement, publish a new persistent `target_id_` and initialize position
  from the new fresh observation. Reset target-owned stable aim, velocity,
  acceleration, firing observer, settled/observed counters, missing state,
  delivered/remaining state and any target-owned motion learning input that
  would otherwise leak across people.
- Preserve the physical LT/ADS epoch, `ads_target_admitted_`, acquisition id,
  start time and elapsed clock. Do not rearm ADS or restart a 135/220 ms budget.
  Keep the existing target-switch terminal evidence so the old acquisition
  cannot silently extend on the replacement.

### C. Implement the user-confirmed ADS timing contract

- Create a RED case at/just after nominal 135 ms with a fresh, reliable, visible
  target outside the capture set and zero, weak or briefly negative radial
  closing velocity. It must remain `AcquiringExtended`; it must not require one
  instantaneous `>1 px/s` sample to prove helpfulness.
- Continue to complete early when settled. Preserve immediate manual escape,
  confirmed center crossing, target loss/identity switch and the configured
  hard ceiling. A replayed controller tick is not a fresh loss decision.
- Add exact boundary tests around nominal time and ceiling, plus moving target,
  noisy radial velocity, occlusion/reacquire, target switch and held-LT controls.
- Do not force ADS to run for 220 ms: 220 ms remains only the maximum ownership
  time for a target that is present but not yet acquired.

### D. Observability and integration

- Reuse existing acquisition state/reason, selector generation/change,
  persistent target id and timing telemetry. Add a narrow reason/field only if
  the new continuation decision cannot otherwise be reconstructed.
- Do not increase persisted telemetry rate or give W3 ego-motion any actuation.

## Constraints and Non-Goals

- Shared worktree is intentionally dirty. No reset, checkout, cleanup, broad
  formatting, stage, commit, deployment or `.agent-context` edits.
- Do not reopen P1 manual/AI allocation, introduce a second controller owner,
  change global gain/sensitivity, implement far 3% micro-trim, promote Remaining
  or shadow ego-motion, or restore OCR/profile recoil behavior.
- Do not use frame-local detection ids as identity and do not rearm held LT.

## Acceptance and Expected Output

- Show the exact RED failures before production edits.
- Focused selector, coordinator, fuser, controller integration and telemetry
  tests pass; full isolated CTest passes; `git diff --check` passes.
- Report exact changed files, old/new behavior, identity reset matrix, ADS
  boundary matrix, residual risks, isolated candidate path/SHA and confirmation
  that the user runtime was untouched.
- Stop and report before widening scope if a clean fix conflicts with the P1
  single-final-output owner or requires guessing target identity outside the
  selector's confirmed generation protocol.
