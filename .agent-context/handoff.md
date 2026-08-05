# Agent Handoff

Last updated: 2026-08-03
Active scope: native C++ FPS gamepad runtime; final-output continuity, target identity, ADS lifecycle, live timing evidence, W3/W4 shadow validation and future causal short-term memory.
Staleness trigger: refresh after the fresh/non-fresh continuity fix is reviewed or installed, a controlled no-background-load A/B is captured, or W3-W6 status changes.

## Current Objective

Repair the proven fresh-clamp/non-fresh-rebound command discontinuity without
adding another control owner. Prove the repair with deterministic controller
fixtures and a clean live capture before continuing W3/W4 or W5 work.

## Current State

- Production chain: Vision/selector -> TargetCoordinator -> TargetPlan -> ADS or BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS brake -> fixed recoil feed-forward -> ViGEm.
- Protected rollback source is commit `5d2f3be`; its accepted executable/config SHA begins `872F6FFF` and is retained under `artifacts/runtime-backups/`.
- The currently installed diagnostic runtime SHA-256 is `25CF27F9946FF58404C4AE57795870E97512337EE1A90FC47776AB2826A10155`.
- Latest diagnostic capture is `20260803T135819Z_61220_1`: schema 13, `ads_acquisition_trace_v3`, `ego_motion_shadow_v2`. Its process ended but `session.json` and `.active` were not finalized; one last JSONL line is truncated.
- Vision/controller transport is healthy: publish->consume `0.50/0.98 ms` P50/P95, result-ready->ViGEm `0.59/1.09 ms`, source-present->ViGEm `11.31/17.28 ms`; source frames are unique and increasing with no observer duplicate/out-of-order consumption.
- **Proven defect:** `post_slew_fresh_target_relative_envelope` clamps fresh ticks and writes the reduced result to `previous_output_`; non-fresh ticks may immediately slew back toward a larger legacy proposal. Schema 13 has 44 qualifying clamps and 31 rebounds within 25 ms.
- The old five-case session reproduces 109 command-level clamp transitions and 79 rebounds. Case 2 and Case 5 strongly contain this mechanism; Case 1 is confounded by multi-target identity, Case 3 is weak, and Case 4 has no matching event.
- **User-confirmed:** the latest run felt slightly more delayed. Common-field old/new comparison shows essentially unchanged result-to-output transport but wider cadence tails: active source P95 `15.71 -> 21.03 ms`, consume P95 `18.99 -> 21.00 ms`; medians did not slow materially.
- **Open hypothesis:** Luna/log/video analysis may have caused shared-resource contention. It is plausible but unproven because no controlled same-scenario foreground/background A/B exists.
- W3 is shadow-only and blocked: ADS valid `64.18%`, BodyLock valid `73.40%`, compute P95 `2.08 ms`; do not lower confidence thresholds blindly.
- W4 clock calibration is present, but provisional response peaks have broad bands and are selected through W3-valid rows. `first_effect_observed` is empty; W4 is not promotable.
- W5 is not implemented: no ledger reconciles `scheduled -> in-flight -> realized` work, and no 150-200 ms short-term memory affects output.
- Planned single-target AI ownership / multi-target intent-aligned handover remains a selector/coordinator policy and is not implemented.
- OCR/profile recognition is absent from the hot path. Recoil profile playback is disabled; recoil is fixed downward `feedback_amount` feed-forward.
- Both sticks and other gamepad signals are present after SDL event pumping ownership was repaired.

## Next Action

1. Have Luna implement a bounded continuity state: keep the last accepted target-relative final/envelope across intervening non-fresh ticks, superseded by a newer observation, identity/lifecycle boundary, deliberate escape or short expiry.
2. Add a deterministic test proving that a fresh clamp cannot rebound toward the rejected proposal while target and physical input remain stable; retain target-loss, switch, escape and stale-expiry tests.
3. Repair telemetry segmentation on `(acquisition, persistent target, selector generation)` and distinguish first material AI contribution from arbitrary fused/manual output.
4. Build/review a candidate, then capture one same-map/weapon/settings run with analysis/video decoding stopped. Only afterward run a deliberate background-load A/B if the latency feeling persists.
5. Resume W3 quality and W4 event-window work only after current actuation continuity is stable; W5 remains gated.

## Blockers

- Case 4 cannot be physically attributed without a valid delivered-output/effect join.
- Current W3 signal quality and broad W4 peaks cannot support realized-motion actuation.
- The two latency sessions differ in runtime/schema/scenario and background load, so they establish a tail-jitter observation, not causality.
- The worktree contains historical/untracked benchmark and context artifacts; do not reset, clean or broadly stage them.

## Active Questions

- Does the continuity repair remove alternating strong/lazy behavior, micro-input swallowing and close-range elastic swing without weakening legitimate AI authority?
- Does a no-analysis live run retain the wider cadence tail, and is it visible in game frame time, inference timing or only capture scheduling?
- What mechanism explains Case 4 once final delivered right-stick and physical camera effect can be joined?
- Can W3 reach a validated active-mode confidence rate without accepting ambiguous background flow?

## Relevant Decisions

- [Protect live baseline and defer W5](decisions/DEC-2026-08-03-001-protect-live-baseline-defer-w5.md)
- [Target-count-aware manual exit authority](decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md)
- [Predictive manual/AI control envelope](decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md)
- [Fresh observation bounds radial authority (proposed)](decisions/DEC-2026-08-02-001-fresh-observation-bounds-radial-authority.md)
- [Tracker publishes remaining work](decisions/DEC-2026-07-29-002-tracker-remaining-work-contract.md)

## Files To Read First

1. [Five-case and schema-13 control audit](../docs/project/FIVE_CASE_SCHEMA13_CONTROL_AUDIT_20260803.md)
2. [Current project state](../docs/project/CURRENT_STATE.md)
3. [August 3 protected live baseline](../docs/project/LIVE_ACCEPTED_RUNTIME_20260803.md)
4. [Compact session log](session-log.md)

## Do Not Reopen Unless Needed

- Do not diagnose queued old vectors, duplicate Vision consumption or a slow result->controller path without new contradictory evidence.
- Do not claim background Luna work caused latency; first run the controlled A/B.
- Do not attribute Case 4 to the fresh-envelope defect or treat Case 1 as a selector bug without a preferred-target rule.
- Do not restore additive manual-plus-AI forces, fixed manual preservation floors, held-LT rearming or a second Remaining/learner/controller owner.
- Do not call W3/PendingMotion/rollout diagnostics W5 memory or scheduled output realized camera motion.
- Do not overwrite the protected rollback without a reviewed candidate and explicit installation request.
- Keep raw telemetry, private performance details and personal video paths out of project context.
