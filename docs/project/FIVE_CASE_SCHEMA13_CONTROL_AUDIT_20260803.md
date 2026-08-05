# Five-Case and Schema-13 Control Audit - 2026-08-03

## Purpose and Current Boundary

This record combines two related investigations for the native gamepad runtime:

1. five user-marked gameplay cases captured by the protected schema-12 runtime;
2. a later schema-13 capture from the currently installed diagnostic runtime.

The controller uses Vision and physical right-stick input to select a target,
shape ADS/BodyLock assistance and emit one final ViGEm right-stick vector. The
investigation asks whether the reported jumps, alternating strong/lazy feel and
slight latency come from stale/queued data, target policy, or the final-output
controller itself.

No production source, configuration or installed executable was changed while
creating this record. Findings are separated into **user-confirmed**,
**repository/reviewer evidence**, and **inferred/open** claims.

## Runtime and Evidence Identity

| Capture | Runtime SHA-256 | Schema | Role |
|---|---|---:|---|
| `20260803T131652Z_65236_1` | `872F6FFF...50558C38` | 12 | Five-case behavioral audit; invalid for W3/W4 promotion |
| `20260803T135819Z_61220_1` | `25CF27F9...A10155` | 13 | Current timing, ego-motion and control-continuity diagnosis |

Both sessions retained `state=active` after their process ended and each has one
malformed/truncated last JSONL line. Valid preceding records remain usable, but
neither manifest should be treated as a clean shutdown-duration record.

## Slight-Latency Report

**User-confirmed:** the schema-13 test felt slightly more delayed. The user
asked whether Luna parsing logs in the background may have contributed.

A comparison was made only on fields shared by schema 12 and 13. Latencies use
the earliest active-acquisition consumption of each unique source frame;
cadence intervals are restricted to the same physical ADS epoch and target
acquisition ID. The latency cohorts contain 3,093 old and 1,717 new unique
active-acquisition source frames; within-acquisition cadence has 2,890 and 1,602
intervals, respectively.

| Active-acquisition metric | Old P50 / P95 | New P50 / P95 | Reading |
|---|---:|---:|---|
| Capture copy complete -> result ready | 7.396 / 12.002 ms | 7.287 / 11.919 ms | Inference/copy tail did not slow |
| Result ready -> Vision publish | 0.002 / 0.003 ms | 0.003 / 0.003 ms | No meaningful change |
| Vision publish -> controller consume | 0.478 / 0.948 ms | 0.498 / 0.974 ms | About +0.02 / +0.03 ms |
| Controller consume -> ViGEm submit | 0.094 / 0.161 ms | 0.094 / 0.186 ms | Median unchanged; small tail increase |
| Result ready -> ViGEm submit | 0.580 / 1.047 ms | 0.600 / 1.084 ms | About +0.02 / +0.04 ms |
| Capture-acquire source-frame interval | 7.849 / 15.712 ms | 7.859 / 21.034 ms | Median unchanged; tail widened ~5.32 ms |
| Controller-consume interval | 8.929 / 18.989 ms | 8.038 / 21.003 ms | Median faster; tail widened ~2.01 ms |

Active-acquisition `accumulated_frames > 1` was nearly unchanged: `35.66%`
versus `35.93%`. The schema-13 full-chain audit also measured Vision publish to
controller consume at `0.50/0.98 ms` P50/P95, result ready to ViGEm at
`0.59/1.09 ms`, and source present to ViGEm at `11.31/17.28 ms`.

**Conclusion:** there is no evidence that Vision results waited in a controller
queue or that an internal result-to-output stage materially regressed. The
slight latency feeling is compatible with the wider source/controller cadence
tail. Background Python parsing, disk reads, memory pressure, Defender activity
or video decoding could plausibly add shared-system jitter, but this capture is
not a controlled A/B and cannot assign causality to Luna.

The acceptance check is one same-map, same-weapon, same-settings run with Luna,
log parsers and video decoding stopped, followed only if needed by a deliberate
background-load run. Compare game frame time/FPS, source interval P95/P99,
capture-to-result, publish-to-consume, accumulated frames and result-to-ViGEm.

## Proven Command-Continuity Defect

The schema-13 capture rejects the earlier stale-vector explanation:

- 21,370 acquisition source frames are unique and increasing;
- observer counters end with zero duplicate/out-of-order consumption;
- result-ready to ViGEm normally completes in about one millisecond.

The repeatable defect is instead inside final-output continuity:

```text
fresh Vision tick
  -> normal stateful slew
  -> post-slew target-relative clamp
  -> reduced result stored as previous_output_

intervening non-fresh controller tick
  -> larger legacy proposal becomes active again
  -> output slews back in the opposite direction
```

In schema 13, 44 same-target/manual-stable fresh clamps changed the final vector
by at least `0.25`; 31/44 (`70.45%`) rebounded oppositely within 25 ms. Mean clamp
was `0.363`, maximum `0.788`, and mean rebound delay `9.45 ms`.

The old five-case capture independently reproduces 109 fresh-envelope clamp
transitions and 79 paired rebounds after deduplicating by `sample_ns` and not
double-counting consecutive already-fresh rows. This proves a command-level
fresh-clamp/non-fresh-rebound defect. It does not by itself prove physical
camera effect, delivered-stick causality or sole causation of a video symptom.

The narrow implementation direction is to retain the last accepted
target-relative final/envelope as bounded short-lived state across intervening
non-fresh ticks. A newer observation, target identity/lifecycle change,
deliberate escape or expiry must supersede it. This must stay inside the single
final-output owner rather than becoming an additive hold or second controller.

## Five Marked Cases

| Case | Behavioral result | Continuity cross-check | Narrow owner / next evidence |
|---|---|---|---|
| 1 - multiple targets | Target ambiguity is visible, but no preferred-target rule or eligible-alternative set was logged; selector bug is not proven. | 1 clamp / 1 rebound at about -0.828 s; identity/multi-target changes confound attribution. | Selector/coordinator only after the target-count-aware product rule and candidate telemetry exist. |
| 2 - accidental wrong manual input | User-input error is confirmed, with controller-policy interaction. | 3 clamps / 2 rebounds; strong representative pair at -0.128 s (`0.453` clamp, `0.430` rebound, `8.878 ms`, cosine `-0.940`). | Final-output continuity fix; retain the user-error fact rather than relabeling the whole case as AI-only. |
| 3 - wrong rightward ADS localization | ADS lifecycle/BodyLock continuation is plausible, but timing alignment and physical-effect evidence are insufficient. | 1 nominal pair; rebound cosine only `-0.097` after `22.883 ms`, so attribution is weak. | Per-target trace segmentation and a current-runtime effect join. |
| 4 - left/up overshoot then correction | Output reversal and error sign change exist, but physical overshoot cannot be assigned to BodyLock, envelope or camera motion. | No qualifying clamp/rebound within the focus window. | Keep unexplained; require final delivered vector plus background-flow effect timing. |
| 5 - slow LMG at close range | Slow weapon ADS and close-range geometry are a policy limitation candidate; one clip cannot tune global timing/gain. | 3 clamps / 2 rebounds; two strong pairs at +0.641 s and +0.778 s. | Fix continuity first, then compare matched weapon/close-range captures; the defect is present but not proven as the only cause. |

The five-case behavioral audit is complete, while its W3/W4 gate remains
`W3_W4_CAPTURE_INVALID_OLD_RUNTIME` because it used schema 12 and the protected
old executable.

## W3, W4 and ADS Findings

- **W3 blocked:** search-radius clipping is fixed and compute P95 is `2.08 ms`,
  but valid rates are only `64.18%` for ADS and `73.40%` for BodyLock, mostly
  from low-confidence rejection. Lowering the threshold blindly would convert
  unknown motion into false realized motion.
- **W4 provisional/not promotable:** calibrated present time is available for
  `99.95%` of ego rows. Offline peaks are around 5 ms for ADS and 10 ms for
  BodyLock, but 95%-of-peak bands are broad (`0-20/25 ms`) and only W3-valid
  motion rows participate. No stable response curve is established.
- **ADS lifecycle conforms:** 115 acquisition IDs were observed. Requested AI
  begins in about `0.98/1.06 ms` P50/P95; extension beyond 135 ms occurs only
  with an existing visible unacquired target. The approximately 221 ms observed
  ceiling is next-tick reporting around the hard 220 ms maximum.
- **Trace semantics need repair:** `first_fused_output` can record manual output
  before any AI contribution, and a consumed acquisition ID may span a later
  persistent-target change. Use a per-target assist segment and log first
  material AI separately.

W5 remains unimplemented. No scheduled output is currently reconciled through
`scheduled -> in-flight -> realized`, and no 150-200 ms memory affects control.

## Work Order and Acceptance Gates

1. Fix fresh/non-fresh final-output continuity and add deterministic same-target,
   stable-manual, target-loss, target-switch, escape and expiry fixtures.
2. Repair per-target telemetry segmentation and first-material-AI semantics so
   the live proof cannot mix target lifecycles.
3. Build and review a candidate, then run the no-background-load latency/control
   capture. Do not infer success from KD alone.
4. Revisit the still-unexplained Case 4 with delivered-vector and camera-effect
   evidence.
5. Improve W3 quality and repeat event-window W4 identification. Begin W5 only
   after the response curve and realized-motion contract pass shadow gates.

## Evidence Artifacts

- [Five-case behavioral audit](../../artifacts/benchmarks/causal-aiming-state-20260802/live-five-case-audit-20260803/CASE_AUDIT.md)
- [Fresh clamp/rebound evidence](../../artifacts/benchmarks/causal-aiming-state-20260802/live-five-case-audit-20260803/FRESH_ENVELOPE_CLAMP_REBOUND.md)
- [Schema-13 live capture review](../../artifacts/benchmarks/causal-aiming-state-20260802/w3-w4-live-capture-20260803-135819/REVIEW.md)
