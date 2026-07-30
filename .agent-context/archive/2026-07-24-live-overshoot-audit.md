# 2026-07-24 Live Strong-Assist Overshoot Audit

## Log coverage

- `20260724T024127Z_29300_1`: local 10:41-13:00, 10.844 GiB, 740,191 controller samples; 48,639 target-present samples. This is the only useful gameplay telemetry today.
- `20260724T055531Z_20196_1`: local 13:55, 0.029 GiB; all 652 target-present controller samples were manual mode with about `0.0124` mean stick magnitude and zero AI, consistent with idle/drift.
- Launcher proves a local 17:45-20:46 run, but stdout says `perf_log=false aim_perf_file_log=false`; therefore there is no detailed 18:00 control evidence.

## Streaming audit

Thresholds: meaningful manual input `>0.03`, strong AI magnitude `>0.15`; target-space Y was converted to stick-space with `error_y=-target_dy`.

- Usable-session mode split: manual 25,330; ADS snap 5,000; BodyLock 18,309 target-present samples.
- Strong-AI wrong-way rate against current visual error:
  - ADS snap: 51 / 3,758 = 1.36%.
  - BodyLock: 951 / 14,498 = 6.56%.
- Center-crossing debt (same track/mode, <=35 ms continuity, crossing within 12 px):
  - ADS snap: 85 crossings; AI old direction 12.94%, final old direction 51.76%, manual old direction 64.71%.
  - BodyLock: 1,371 crossings; AI old direction 10.43%, final old direction 37.56%, manual old direction 50.91%.
  - All modes: 1,681 crossings; 154 AI-old, 626 final-old, 829 manual-old, but only 53 had AI and manual stacking together in the old direction.
- Near-center exits: 70 same-track episodes moved from <=8 px to >=20 px within 150 ms; 53 were BodyLock, 16 manual, 1 ADS. Of all exits, 32 began with final output that became wrong after the target crossed, 34 with manual input that became wrong, and 19 with AI that became wrong.

## Interpretation and limits

- Repository evidence: BodyLock has materially more stale/wrong AI than ADS, while human directional inertia is even more common. The main live failure is not simply “AI and human always add together”; it is delayed reversal attribution and insufficient/late counter-correction, plus a smaller subset where AI itself retains old-direction debt.
- Some <=8 to >=20 px jumps happen in 15-40 ms on the same observed track and may include detector/box-anchor motion, not only physical camera overshoot. Do not tune directly from raw exit count without replay/episode inspection.
- The shadow learner was observational only and often unable to attribute response: 87,757 shadow records, 9,318 accepted by any delay (10.62%); response windows were dominated by `identity_weak` (115,815), so this log does not prove the learner can yet control the defect.

## Relevance to causal POV / ego-motion estimation

- This audit is direct acceptance evidence for the in-progress causal ego-motion estimator. A target-space reversal seen by BodyLock can be caused by at least three different sources: target motion, camera/POV motion produced by the user's movement and aim, or a vision-anchor discontinuity. Treating all three as target acceleration creates stale prediction and unnecessary counter-steer.
- The estimator should provide evidence for decomposition, not become another control owner. `TargetCoordinator` remains responsible for the plan; the estimator supplies a confidence-gated ego-motion component and must abstain when identity/observation quality is weak.
- The live acceptance comparison should retain these metrics:
  - BodyLock strong-AI wrong-way rate: current reference `6.56%`.
  - BodyLock center crossings with AI old-direction debt: current reference `10.43%`.
  - BodyLock center crossings with final old-direction debt: current reference `37.56%`.
  - Same-track `<=8 px` to `>=20 px` exits within 150 ms: current reference `53` BodyLock episodes in this session.
- Improvement is only credible if these debts fall without increasing 10-20 px under-follow/stickiness, user-fight, target-switch error, or anchor-jump chasing. The 70 center-exit episodes should be classified into continuous physical response versus anchor discontinuity before using their raw count as an optimization objective.
- The current shadow attribution weakness (`10.62%` accepted by any delay and many `identity_weak` windows) is also an estimator constraint: do not learn a POV response from weak identity, target switches, cue-only holds, or discontinuous aim anchors.
