# Subagent Brief: P1 Fresh-Observation Control Authority

Date: 2026-08-02
Owner: luna-max task `019fbaf1-359a-7041-b782-333c7f2ac698`
Status: ready for dispatch

## Objective

Implement and verify the current P1 control-envelope fixes without deploying:

1. manual and AI must not remain additive forces; strong physical input may be
   limited in direction and magnitude by one predicted-result envelope so the
   user can push decisively without excess combined force;
2. fresh ADS/assist evidence must cancel obsolete-direction manual demand through
   the existing single `VectorIntentFuser` owner;
3. fresh Observed BodyLock position must bound stale radial velocity feed-forward.

## Read First

- `.agent-context/handoff.md`
- `.agent-context/decisions/DEC-2026-08-02-001-fresh-observation-bounds-radial-authority.md`
- `.agent-context/decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`
- `.agent-context/decisions/DEC-2026-08-01-002-contextual-manual-ai-dual-proposal-arbitration.md`
- `.agent-context/decisions/DEC-2026-07-31-001-separate-target-motion-and-firing-disturbance.md`
- `.agent-context/decisions/DEC-2026-07-29-002-tracker-remaining-work-contract.md`

## Proven Starting Facts

- `RuntimeConfig.gamepad.intent.fresh_vision_wrong_way_manual_floor` is parsed,
  but `vector_intent_fusion_config()` does not pass it to the fuser.
- The live ADS crossing had fresh error reverse and shaped AI brake correctly,
  while manual preservation kept final output in the obsolete direction.
- `BodylockFollowController` sends `error_px + error_rate * feedforward_gain`
  through `ResponseModelAimSolver`; its sign guard excludes Observed lifecycle.
- In wrong BodyLock intervals, requested assist was already wrong while
  `remaining_work_valid=false`, Pending/rollout shadow was zero, and dynamic
  adjustment was zero.
- W1/W3 current isolated build passes CTest `36/36`; do not remove or give the
  ego-motion observer control authority.
- The user explicitly removed any requirement to preserve a fixed amount of
  wrong or excessive manual input. Large raw input must be safe to use; a clear
  exit channel remains required but one near-full tick is not exact ownership.
- Far-target/head-peek 3% micro adjustment is recorded and deferred. Do not
  implement IntentFilter/micro-trim changes in this P1 or let them distract from
  excessive combined force.

## Required Work

### A. Make live-derived RED fixtures first

- ADS fixture: after fresh horizontal error crosses center, use representative
  `AI=-0.54`, `manual=+0.32`; assert fused output cannot remain positive solely
  because the general 0.75 escape-preservation path bounded the AI brake.
- Verify the same rule rotationally (vertical and diagonal), not as X/Y special
  cases.
- BodyLock fixtures: fresh Observed position changes sign while retained motion
  remains old-direction. Cover at least 24 ms, 83 ms and roughly 90 ms stale-tail
  shapes; assert requested assist never deepens nontrivial fresh radial error.
- Add controls for true target reversal, tangent following, ordinary moving
  target, near-center noise, occlusion/coasting, continuous deliberate exit,
  pure AI, far BodyLock, and recoil/firing disturbance.
- Add strong manual sequences at `0.70` and `1.00` with same-direction and
  opposing AI. Prove the fused target-relative demand does not become raw
  `manual + AI`, does not deepen fresh error, and stays within a continuous
  stopping/predicted-travel envelope.

### B. Wire the dedicated fresh policy into the one fuser

- Add an explicit fuser config field sourced from
  `fresh_vision_wrong_way_manual_floor`; do not silently reuse
  `body_lock_manual_escape_preservation`.
- Activate only for a fresh, reliable, single-target observation with clear
  radial conflict. Tangential and near-full input are proposals, not guaranteed
  exact passthrough; limit them only when predicted outcome evidence warrants it
  and preserve a continuous deliberate-exit path.
- Keep the existing transition continuous. No unconditioned global manual scale,
  new post-fuser brake, or duplicated intent classifier.
- Telemetry must make the policy activation and retained manual contribution
  distinguishable from the ordinary dual-proposal path.

### C. Bound Observed BodyLock radial feed-forward at the solver source

- Retain separate position and motion proposals from `ResponseModelAimSolver`.
- Project motion onto the fresh position radial direction. It may assist closing
  or tangent motion, but cannot reverse a nontrivial fresh radial position
  proposal. Use a continuous near-center envelope; no one-frame hard threshold.
- If resetting state on confirmed center crossing is useful, reset/cap radial
  velocity only after multi-frame evidence and preserve tangent velocity.
- Keep non-Observed safety behavior, single lifecycle owner, and existing
  maximum force. Do not disable feed-forward or lower BodyLock gain globally.

### D. Add causal telemetry

Expose at minimum target `error_rate`, solver `position_stick`, `motion_stick`,
whether the radial bound applied, and the bound reason in the existing bounded
telemetry path. Do not increase persisted rate beyond the existing cap.

## Constraints

- Shared worktree contains extensive user/agent changes. No reset, checkout,
  cleanup, broad formatting, unrelated edits, or commit.
- Do not implement selector/box-anchor changes, ADS 135-to-220 lifecycle, W3
  actuation, Remaining promotion, OCR/profile restoration, or runtime install.
- Do not implement the deferred 3% IntentFilter/micro-trim feature in this P1.
- Work in the existing isolated build family. Preserve W1/W3 and all unrelated
  changes.
- Do not use a final-output clamp to hide wrong requested assist.

## Acceptance

- New RED fixtures fail for the intended old behavior and pass for the new one.
- Focused fuser, BodyLock/solver, coordinator/pipeline, telemetry and config tests
  pass.
- Full isolated CTest passes with no existing test removed or weakened.
- `git diff --check` passes.
- Matched benchmark controls show no material regression in pure AI, bounded and
  timely deliberate exit, tangent following, ordinary moving/reversal, far
  BodyLock and firing/occlusion; strong manual+AI overshoot and continued push
  must improve rather than merely shift to another stage.
- Return exact changed files, test commands/results, benchmark deltas, remaining
  risks, isolated candidate path/SHA, and an explicit statement that runtime was
  not overwritten.

## Stop Conditions

Stop and report instead of widening scope if the fix requires another control
owner, global gain reduction, W3 actuation, or conflicts with existing unowned
dirty changes.
