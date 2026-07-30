# Remaining-work controller-rate experiment — 2026-07-29

## Question

Can the tracker/controller use work already delivered between Vision frames to
compute `remaining_work_px`, instead of repeatedly controlling from the last
captured screen error?

The first sections preserve the benchmark-only exploration history. The
accepted scale-0.6 policy is now wired into the production controller behind
`[gamepad.tracker].remaining_work_enabled`; see
`PRODUCTION_ACCEPTANCE.md` for the production contract and final verification.

## Reproducibility

- Revision baseline: `d186d1e`
- Config: local runtime `config.toml`
- Config fingerprint (FNV-1a 64): `10334765943132927346`
- Seeds: `2026072901`, `2026072902`, `2026072903`
- Duration: 60,000 ms per run
- Vision: 80 Hz requested (13 ms simulation interval)
- Controller: 1,000 Hz
- Target motion: moving
- Intent fusion: `vector-baseline`
- Profiles: pure and mixed
- Cohorts: ADS and BodyLock
- Candidate:
  `--remaining-work integrated`, which consumes the final pre-recoil output
  once per controller tick.
- Baseline:
  `--remaining-work current`.

Raw JSON is outside the repository build tree:

- `D:\b\rw20260729\live-ordinary-{current,integrated}.json`
- `D:\b\rw20260729\live-strafe-{current,integrated}.json`
- `D:\b\rw20260729\live-combined-{current,integrated}.json`
- `D:\b\rw20260729\live-combined-occ36-{current,integrated}.json`

## Mixed-input aggregate

The score column is acquisition points plus tracking points. Percentages compare
the integrated candidate with the current-error baseline.

| Scene | Cohort | Score | Mean error | Overshoot area / acquisition | Stall-ring / acquisition |
|---|---:|---:|---:|---:|---:|
| Ordinary | ADS | +28.5% | -23.3% | +120.0%* | -56.1% |
| Ordinary | BodyLock | +5.8% | -25.8% | +5.0% | -37.2% |
| Full reversal strafe | ADS | +27.1% | -14.6% | +28.8% | -25.8% |
| Full reversal strafe | BodyLock | +3.0% | -9.6% | +19.4% | -17.0% |
| Strafe + vertical actions | ADS | +24.0% | -9.8% | +9.0% | -21.2% |
| Strafe + vertical actions | BodyLock | +2.3% | -4.3% | +4.4% | -12.0% |
| Previous + 36 ms occlusion | ADS | +24.8% | -9.8% | +1.8% | -20.9% |
| Previous + 36 ms occlusion | BodyLock | +2.3% | -4.2% | +4.4% | -11.7% |

`*` The ordinary mixed ADS baseline has a small normalized overshoot
denominator: 99 px·ms per acquisition. The candidate is 217 px·ms per
acquisition; despite the large percentage, circle exits per acquisition improve
from 0.270 to 0.237.

## Paired-run consistency

For mixed ADS, score improved in every paired run:

- Ordinary (3 pairs): +20.9% to +35.6%
- Full-reversal strafe (3 pairs): +19.8% to +44.4%
- Strafe + vertical actions (12 pairs): +13.4% to +44.4%
- Previous + 36 ms occlusion (12 pairs): +15.2% to +43.0%

Mean error also improved in every mixed ADS pair. BodyLock improvements were
smaller but consistently positive for score and mean error.

## Decision

The experiment validates `remaining_work_px` as the correct state variable for
reducing under-response between Vision frames. It is not yet a production-ready
controller policy:

- ADS score and mean error improve consistently.
- Stall-ring time falls, matching the reported reduction in lazy tracking.
- Normalized overshoot is unstable and often increases during player motion.
- ADS handoff defect rate rises slightly in the combined scenes.

The next experiment should keep the same remaining-work estimator and add one
bounded terminal policy around center crossing / ADS-to-BodyLock handoff. It
must not reintroduce broad brake gates or change recoil composition.

## Detail exploration — 2026-07-30

No controller or tracker policy was changed during this exploration.

### Vision-rate decomposition

Mixed ADS with the local runtime config:

| Scene | Vision rate | Score change | Mean-error change | Overshoot / acquisition |
|---|---:|---:|---:|---:|
| Ordinary | 40 Hz | +43.1% | -35.7% | -74.1% |
| Ordinary | 80 Hz | +28.5% | -23.3% | +120.0% |
| Ordinary | 160 Hz | +23.2% | -26.5% | +257.2% |
| Ordinary | 200 Hz | +21.0% | -24.1% | +230.0% |
| Full-reversal strafe | 40 Hz | +31.7% | -11.1% | +13.0% |
| Full-reversal strafe | 80 Hz | +27.1% | -14.6% | +28.8% |
| Full-reversal strafe | 160 Hz | +14.7% | -8.6% | +82.5% |
| Full-reversal strafe | 200 Hz | +14.9% | -9.9% | +77.9% |

The score advantage shrinks as Vision becomes more frequent. This supports the
core hypothesis that the candidate is paying down work hidden between captures.
The remaining 15-21% advantage at 200 Hz shows that even a 5 ms capture
interval contains material work at the configured response.

The opposite overshoot trend is also informative. The current tracker absorbs
camera-induced observation motion into relative target velocity, which creates
accidental damping. The candidate removes that contamination and tracks faster,
but exposes the absence of a proper terminal-velocity and handoff policy.

Raw files:

- `D:\b\rw20260729\detail-{ordinary,strafe}-hz{40,160,200}-{current,integrated}.json`
- The 80 Hz row uses the original `live-*` matrix.

### Slowdown decomposition

Changing simulated slowdown did not produce a monotonic overshoot trend.
Therefore, an initially high response-scale fallback is a contributing
uncertainty but cannot explain the result alone.

- With no slowdown, ordinary mixed ADS score improved 18.3% and normalized
  overshoot changed only +6.8%, but handoff defects rose from 25.2% to 38.3%.
- With current 0.5-to-0.4 slowdown, score improved 28.5% and normalized
  overshoot rose 120.0%, while handoff defects were nearly flat.
- With stronger 0.35-to-0.25 slowdown, score improved 29.6% and normalized
  overshoot fell 50.9%.
- Full-reversal strafe remained the unstable interaction: at strong slowdown,
  score improved 23.2% while normalized overshoot rose 44.9%.

Raw files:

- `D:\b\rw20260729\detail-{ordinary,strafe}-slow-{none,strong}-{current,integrated}.json`

### Benchmark interpretation limits

1. The target script hash is identical in every A/B pair, but target dwell is
   outcome-dependent: a successful target remains for the 1,000 ms tracking
   window, while a missed target is replaced after 250-330 ms. One-minute total
   score is a valid closed-loop throughput measure; per-target-type counts are
   not an identical wall-clock replay.
2. `mixed_manual_input()` recomputes a helpful/tangent vector from the current
   error every 1 ms. It is a zero-latency reactive user model, not a recorded
   physical-stick replay. A controller change therefore changes the simulated
   user's later input. Mixed results measure closed-loop interaction, while
   pure results are the cleaner controller-only comparison.
3. In pure ADS, the candidate reduces P95 output delta and jerk across all four
   main scenes. The large mixed direction-discontinuity increase is primarily
   generated by the reactive manual model, not by controller-only output.
4. Settle accounting was re-checked in the native benchmark adapter. The
   adapter submits a candidate snapshot only when `input.fresh_vision` is true;
   non-fresh controller ticks submit an empty batch. Settle therefore already
   counts distinct Vision submissions rather than repeatedly counting a carried
   candidate. A temporary unique-frame toggle produced byte-identical run
   metrics and was removed.

## Isolated replay matrix

The original matrix mixed two useful but controller-dependent effects:
outcome-dependent target replacement and a manual model that recomputed its
direction from the current controller error every millisecond. Two
benchmark-only controls were added:

- `--target-slot-ms 1330` gives every target a fixed wall-clock slot, including
  the acquisition window, tracking window and inter-target gap.
- `--profile scripted` anchors the manual-input segments to target spawn state,
  so the same seed produces the same stick script for both controllers.

The default benchmark path remains unchanged. A regression replay of the 12
historical runs produced identical run objects and script hashes.

At 80 Hz, fixed slots and scripted manual input confirm that the original gain
is not caused by easier target replacement or a user model helping the
candidate:

| Scene | Cohort | Score change | Mean-error change | Overshoot / acquisition | Stall-ring change |
|---|---:|---:|---:|---:|---:|
| Ordinary | ADS | +60.2% | -11.6% | +34.6% | -51.3% |
| Full-reversal strafe | ADS | +65.0% | -2.6% | +96.8% | -22.3% |
| Strafe + vertical actions | ADS | +49.7% | -2.7% | +24.8% | -17.2% |
| Previous + 36 ms occlusion | ADS | +50.8% | -2.6% | +18.4% | -18.0% |

Direct BodyLock score improvements remain much smaller, roughly 2.8-6.2%.
This strengthens the earlier conclusion that the main benefit is acquisition
and transition accounting, while full subtraction of estimated delivered work
is too aggressive near terminal events.

Raw files:

- `D:\b\rw20260729\isolated-{ordinary,strafe,combined,combined-occ36}-{pure,scripted}-{current,integrated}.json`

## Estimator detail experiments

Changing tracker velocity alpha between 0 and 0.8 did not produce a general
solution. The configured value was usually best for absolute score and error;
the values that reduced one strafe overshoot case materially regressed
ordinary acquisition.

Two acceleration-feedforward variants were also rejected and removed. They
produced small local improvements but regressed scripted and occluded scenes.
The failure is useful: terminal overshoot is not fixed by appending another
target-acceleration term to the request.

Finally, the fraction of estimated delivered camera work subtracted from the
captured error was swept at 0.4, 0.6, 0.8 and 1.0. A scale of 0.6 is the best
bounded candidate in this matrix:

| Scene / profile | ADS score vs current | Mean error | Overshoot / acquisition | Stall-ring |
|---|---:|---:|---:|---:|
| Ordinary / pure | +40.1% | -6.6% | -91.1% | -26.7% |
| Ordinary / scripted | +43.3% | -5.3% | -49.4% | -29.8% |
| Strafe / pure | +29.2% | -6.0% | -40.6% | -30.7% |
| Strafe / scripted | +47.9% | +0.4% | -23.4% | +10.3% |
| Combined / pure | +21.5% | -4.2% | -11.0% | -20.7% |
| Combined / scripted | +34.2% | -3.2% | +4.0% | +7.0% |
| Combined + 36 ms occlusion / pure | +20.7% | -3.9% | -8.4% | -22.1% |
| Combined + 36 ms occlusion / scripted | +34.6% | -3.6% | -8.5% | +0.6% |

The scale-1.0 candidate still wins raw score, but its fixed-script strafe ADS
overshoot is +96.8% versus current, compared with -23.4% at scale 0.6. In the
occluded combined scripted case, scale 1.0 raises normalized overshoot by
18.4% and stall time by 10.8%; scale 0.6 lowers overshoot by 8.5% and leaves
stall essentially flat.

This is evidence for confidence-weighted delivered-work accounting, not for a
magic constant. The response estimate is uncertain because game sensitivity,
slowdown and camera response vary. Production integration should derive or
learn this confidence from observed response and keep a bounded fallback; it
should not expose a weapon table or add a collection of terminal gates.

Raw files:

- `D:\b\rw20260729\scale-{ordinary,strafe}-{pure,scripted}-{0p4,0p6,0p8}.json`
- `D:\b\rw20260729\scale-{combined,combined-occ36}-{pure,scripted}-0p6.json`

## Full-strength obsolete-manual guardrail

The existing `obsolete` profile was used as an adversarial human-input
guardrail. Every target receives a fixed initial-direction stick magnitude
between 0.60 and 1.00. After the reticle crosses the target plane, that input
continues in the now-wrong direction for 80-140 ms. This is intentionally much
more severe than the normal mixed or scripted profiles.

Scale 0.6 versus the current controller:

| Scene | Cohort | Score | Mean error | Overshoot / acquisition | Continued obsolete push | Stall-ring |
|---|---:|---:|---:|---:|---:|---:|
| Ordinary | ADS | +0.5% | -7.6% | both zero | 0 -> 0 ms | unchanged |
| Ordinary | BodyLock | +0.2% | -9.5% | both zero | 0 -> 0 ms | unchanged |
| Full-reversal strafe | ADS | +3.1% | -6.0% | -69.8% | 3 -> 0 ms | -5.7% |
| Full-reversal strafe | BodyLock | +1.5% | -9.8% | +198.7% | 3 -> 52 ms | -12.9% |
| Combined motion | ADS | +2.3% | -4.2% | -29.6% | 41 -> 16 ms | -3.2% |
| Combined motion | BodyLock | +1.7% | -5.5% | -9.8% | 423 -> 314 ms | -5.5% |
| Combined + 36 ms occlusion | ADS | +2.3% | -4.0% | -24.1% | 36 -> 14 ms | -4.3% |
| Combined + 36 ms occlusion | BodyLock | +1.5% | -4.6% | -9.4% | 394 -> 237 ms | -5.8% |

The candidate remains beneficial in the broad combined matrices, but its raw
score gain is much smaller because a near-full manual stick owns most of the
plant. The isolated horizontal BodyLock result is a required warning:
remaining-work accounting can improve speed/error while amplifying a
persistently wrong manual command. This estimator solves stale camera-work
accounting; it does not by itself decide whether the user or AI is directionally
correct. A production rollout therefore needs an independently measured
vision-fresh intent-conflict guardrail, especially for BodyLock, rather than
treating scale 0.6 as a complete fusion policy.

Raw files:

- `D:\b\rw20260729\harsh-{ordinary,strafe,combined,combined-occ36}-obsolete-{current,scale0p6,scale1p0}.json`

## Manual contribution ablation

Inspection showed that `--remaining-work integrated` already uses
`before_recoil_stick`: the actual fused and clamped right-stick output after
manual/AI arbitration, with recoil intentionally excluded. Manual work must
not be added again from raw physical input because that would double count
manual contribution retained by fusion and count input that was suppressed or
clamped before delivery.

A benchmark-only `integrated-ai` ablation was added. It uses the same
controller-rate accounting but accumulates only `shaped_assist_stick`. The
difference from normal `integrated` therefore measures the net effect of using
the actual fused output, including delivered manual contribution.

At scale 0.6 under the full-strength obsolete-manual profile:

| Scene | Cohort | Fused score vs AI-only | Mean error | Overshoot / acquisition | Continued obsolete push |
|---|---:|---:|---:|---:|---:|
| Ordinary | ADS | -0.37% | +3.33% | both zero | 0 -> 0 ms |
| Ordinary | BodyLock | -0.17% | +4.27% | both zero | 0 -> 0 ms |
| Full-reversal strafe | ADS | -0.14% | +0.26% | -8.8% | 0 -> 0 ms |
| Full-reversal strafe | BodyLock | -0.03% | +0.34% | +5.0% | 65 -> 52 ms |
| Combined motion | ADS | +0.32% | -0.29% | -6.8% | 18 -> 16 ms |
| Combined motion | BodyLock | -0.05% | +0.13% | +0.7% | 356 -> 314 ms |
| Combined + 36 ms occlusion | ADS | +0.35% | -0.08% | -3.5% | 18 -> 14 ms |
| Combined + 36 ms occlusion | BodyLock | -0.05% | +0.28% | -0.3% | 302 -> 237 ms |

Including delivered manual work has little effect on aggregate score or mean
error, but it reduces obsolete post-cross push in the broad combined cases,
especially BodyLock. This isolates two responsibilities:

- final fused output is the correct physical signal for remaining-work
  accounting; and
- deciding whether a strong manual command is currently correct remains an
  intent-fusion problem, not a remaining-distance problem.

Raw files:

- `D:\b\rw20260729\harsh-{ordinary,strafe,combined,combined-occ36}-obsolete-ai-only-{0p6,1p0}.json`

## Human recovery trajectories

Two controller-independent, benchmark-only manual profiles were added to test
a user who notices an error and tries to recover:

- `recover`: every target begins with a strong wrong-way command of magnitude
  0.65-0.95. After 70-130 ms it reverses directly into an equally strong
  helpful command for 160-240 ms, then releases.
- `arc`: every target begins with a strong tangential error and rotates along a
  quarter-circle into the helpful direction over 140-220 ms, holds briefly,
  then releases. Odd/even targets alternate turn direction.

Both profiles are anchored to target spawn state. Focused tests verify that
zero-output and active controllers receive identical manual samples tick by
tick. The first rejected arc fixture used a 220-300 ms half-circle from fully
wrong to fully correct; it consumed almost the entire ADS deadline and yielded
zero ordinary ADS acquisitions, so it was replaced rather than used as
optimization evidence.

Scale-0.6 integrated remaining work versus current error:

| Profile / scene | Cohort | Score | Mean error | Overshoot px-ms / acquisition | Continued obsolete push | Stall-ring |
|---|---:|---:|---:|---:|---:|---:|
| Recover / ordinary | ADS | +23.9% | -14.1% | 11 -> 662 | 0 -> 0 ms | -4.1% |
| Recover / strafe | ADS | +14.2% | -7.7% | 1,395 -> 2,271 | 0 -> 0 ms | -21.8% |
| Recover / combined | ADS | +10.4% | -6.1% | 3,253 -> 3,203 | 14 -> 12 ms | -36.5% |
| Recover / combined + occlusion | ADS | +10.8% | -5.6% | 3,259 -> 3,038 | 17 -> 18 ms | -30.6% |
| Recover / combined | BodyLock | +2.4% | -2.2% | 3,420 -> 3,526 | 217 -> 163 ms | -13.2% |
| Recover / combined + occlusion | BodyLock | +2.3% | -1.5% | 3,512 -> 3,641 | 222 -> 148 ms | -11.7% |
| Arc / ordinary | ADS | +11.8% | -5.8% | 555 -> 534 | 0 -> 0 ms | +5.2% |
| Arc / strafe | ADS | +6.3% | -4.0% | 3,832 -> 4,191 | 1 -> 0 ms | -18.3% |
| Arc / combined | ADS | +5.8% | -2.6% | 6,311 -> 6,959 | 42 -> 24 ms | -15.6% |
| Arc / combined + occlusion | ADS | +6.1% | -2.8% | 5,965 -> 5,176 | 47 -> 19 ms | -10.1% |
| Arc / combined | BodyLock | +2.0% | -0.8% | 2,003 -> 1,925 | 99 -> 86 ms | -14.0% |
| Arc / combined + occlusion | BodyLock | +1.9% | -0.7% | 2,079 -> 1,966 | 103 -> 84 ms | -12.9% |

Remaining-work accounting is useful when the user recovers: it improves score
and mean error in every retained profile/scene/cohort and generally reduces
stall and obsolete tail push. It is not yet phase-aware. Abrupt wrong-to-right
manual reversal can increase ADS overshoot in ordinary/strafe scenes, while a
continuous arc has smaller but still non-monotonic overshoot changes. The next
estimator detail should distinguish control delivered before and after a
manual-direction change instead of treating all work since capture as one
homogeneous displacement. This is an estimator boundary, not justification
for another post-controller brake.

Raw files:

- `D:\b\rw20260729\recovery-{ordinary,strafe,combined,combined-occ36}-recover-{current,remaining0p6}.json`
- `D:\b\rw20260729\recovery-{ordinary,strafe,combined,combined-occ36}-arc-v2-{current,remaining0p6}.json`
- `D:\b\rw20260729\recovery-comparison.csv`

### Motion and handoff risk map

- The clearest gains are fewer missed ADS acquisitions, less mean error and
  less stall-ring time.
- Direct BodyLock gains are smaller (roughly 2-6%), so most headroom is in
  acquisition and the transition into follow.
- Slide plus strafe is the weakest handoff subgroup: mixed ADS handoff defects
  rise from 46.6% to 52.6%.
- Jump plus strafe improves handoff defects from 32.9% to 28.9%, showing that
  vertical motion is not one uniform failure class.
- Early non-instant handoffs during combined motion are defective in roughly
  76-87% of episodes in both policies. The candidate does not create this
  defect, but it changes how often the path is reached.
- Stale output after target stop and unexpected-mode time generally increase in
  the candidate, while total post-handoff error area falls. This is consistent
  with faster, cleaner velocity tracking that lacks a sufficiently explicit
  stop/reversal terminal model.

### Refined conclusion

`remaining_work_px` should remain the central state variable, but the experiment
currently combines two effects:

1. desired controller-rate accounting of delivered camera work; and
2. removal of accidental camera-motion damping from tracker velocity.

Before production iteration, the next benchmark design should isolate these
effects with:

- unique-Vision-frame settle accounting;
- an open-loop recorded/manual-stick replay in addition to the reactive mixed
  user;
- fixed wall-clock target trajectories for local A/B attribution;
- explicit stop, reversal, and ADS-to-BodyLock terminal-velocity reports.

These are measurement requirements, not a proposal to add more runtime gates.
