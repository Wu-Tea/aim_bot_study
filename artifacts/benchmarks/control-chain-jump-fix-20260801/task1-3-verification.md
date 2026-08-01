# ADS / BodyLock jump-stability repair — Task 1–3 verification

Date: 2026-08-01

> Historical checkpoint. Task 3, conditional Task 4, runtime installation and
> live acceptance were completed later the same day. Use the
> [final live acceptance](../../../docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md)
> as the current status.

Status: Task 1 and Task 2 focused contracts pass. Task 3 remains open; this
artifact is evidence, not an acceptance record.

## Identity and safety boundary

- Runtime was not replaced, restarted, or deployed.
- Runtime SHA-256: `3D9ED74CBCD7007F9EEF3CACC6B17EB5763F7056047BE35AC37495BF1CEB7402`
- Config SHA-256: `6CA2D349D13F45645BF93ADB7D7793BDC360D649A5553203DDD62BAD9A63E1CB`
- Engine SHA-256: `45FC5624FF3BBC659E534C3B7833065B0483EF8022AC5D7657CD6DA7DBDEB21`
- Tracked dirty-diff fingerprint at verification: `EBF2CF256E2B71E97A60FF595E7E721DC7518EAA85CE2A99CD61C16BB6B7FAE7`
- Existing dirty files and artifacts were retained.

## Task 1 — VectorIntentFuser

Implemented and covered:

- removed the duplicate hard reliability cutoff from the fuser;
- kept exact physical-manual passthrough for non-finite, NoTarget, and Manual states;
- made Reacquiring release bounded by the existing output continuity state;
- kept TargetChanged and NoTarget as re-entry baselines instead of AI cold starts;
- preserved deliberate diagonal manual escape semantics;
- retained finite-input and rotational/Remaining-direction continuity coverage.

Focused executable: `cod_native_vector_intent_fuser_tests.exe` — PASS.

## Task 2 — ADS → BodyLock handoff

Implemented and covered:

- target/mode context in `AimDynamicsShaper`;
- stale saturated ADS force is clamped into the new BodyLock request envelope;
- target changes start from a neutral AI baseline;
- same-mode slew and two-phase reversal behavior remain bounded;
- same-direction, opposite-direction, vector-Y, target-change, initial-acquisition,
  and non-handoff-mode tests are present.

Focused executable: `cod_native_aim_dynamics_shaper_tests.exe` — PASS.
Controller integration, TargetCoordinator, and BodyLock focused executables — PASS.

## Task 3 — current evidence

Focused build and direct tests pass. `git diff --check` passes.

The current left-stick report is
`left-stick-task1-3-current.json`:

- primary lifecycle AI delta: `0.064`;
- large sign flips: `0`;
- ADS handoff max AI delta: `0.064`;
- ADS handoff max overshoot: `1.86941px`;
- production-chain defect count: `1`, only `reacquire_not_bumpless`.

That remaining production-chain failure is not an output jump: after the new
track is selected, the target error is about `215px`, the benchmark activation
box is `180px`, and TargetCoordinator therefore emits zero BodyLock authority.
The measured useful-reacquire latency is `-1ms` and the output delta is `0`.
This is a target-admission/ownership-envelope issue, so Task 4 was not entered
because the Task 1/2 jump gates no longer reproduce the jump.

Full CTest result: `32/34` passed. The two remaining tests are:

1. `NativeLeftStickMotionBenchmarkTests` — the no-useful-request condition above.
2. `NativeSustainedAimlabLearningTests` — the retained learner's warm round has
   accepted causal updates but `valid_rollout_decisions == 0`; this is an
   independent response/delay-confidence guard, not a Task 1/2 output-owner
   change.

Matched three-seed evidence uses seeds `1337`, `7331`, and `2026`, ordinary
moving targets, 36ms short occlusion, full-reversal left strafe, both ADS and
BodyLock, and pure/mixed input. Files:

- candidate: `sustained-task1-3-3seed-ordinary-moving-occlusion36-reversal.json`
- same-source baseline: `sustained-task1-3-baseline-3seed-ordinary-moving-occlusion36-reversal.json`

The candidate reduced direction discontinuities from `8097` to `59` across
the 12 matched runs. It is not acceptance-ready yet: false interruptions are
`9` versus baseline `8`, and some mixed continued-push/overshoot guardrails
regress. No strength reduction was used to close the jump metric.

Task 4 remains pending and no acceptance document was created.
