# Current State

**Last reviewed:** 2026-08-03
**Reviewed baseline:** protected commit `5d2f3be`, its accepted rollback, and
the currently installed schema-13 diagnostic runtime described below.
**Scope:** production runtime facts present in the reviewed repository, explicitly marked shadow work, and the next live validation boundary

## Production Path

The default gamepad path is the full native C++ runtime:

```text
scripts/launch/gamepad_start.bat
  -> scripts/launch/gamepad_native_cpp_start.bat
  -> native/vision_native/build/Release/cod_native_runtime.exe
```

Python remains available for fallback, training/export, recoil tooling and debug
utilities. The production Vision contract remains `640x512` capture, isotropic
resize to `480x384`, and `models/best_480x384.engine`.

The protected live-accepted rollback has SHA-256
`872F6FFFD1598C64ABC34E0D551F4C112B40FEAC630844C6CB0B0FEE50558C38`.
Its matching executable/config backup is under `artifacts/runtime-backups/`,
and its Release CTest passed `36/36`. The currently installed diagnostic
runtime is `25CF27F9946FF58404C4AE57795870E97512337EE1A90FC47776AB2826A10155`;
it adds the schema-13 W3/W4 observability contract but is not yet a newly
accepted control baseline.

## Current Control Ownership

```text
Vision observation + physical intent
  -> intent-aware selector
  -> TargetCoordinator (identity, lifecycle and ADS-acquisition owner)
  -> immutable TargetPlan
  -> ADS acquisition OR BodyLock trajectory follow
  -> AimDynamicsShaper
  -> VectorIntentFuser (one final target-relative output)
  -> ADS-only brake
  -> fixed recoil feed-forward
  -> ViGEm delivery
```

Current boundaries:

- Manual and shaped AI are proposals, not additive forces or independently preserved output budgets. Fresh error, stopping demand and the mode force envelope determine one final vector and may reject wrong direction or excessive strength from either proposal.
- A continuous deliberate-exit path remains available, but a single near-full manual tick does not automatically bypass target-relative safety.
- Fresh BodyLock position is distinct from bounded motion feed-forward. Target velocity may help closing/tangential tracking but cannot freely reverse a meaningful fresh radial correction.
- A confirmed selector replacement resets target geometry/control state. It does not rearm ADS while physical LT remains held.
- ADS activation radius is spatial (`135 px`) and independent of timing/output range.
- ADS nominal acquisition is `135 ms` from first eligible target admission. Empty waiting frames do not consume it. A visible, still-unacquired target may continue conditionally, with `220 ms` as a hard target-acquisition ceiling rather than a mandatory duration.
- Settle, center crossing, moving away, target loss/switch, non-helpful output and deliberate manual escape can complete or shorten acquisition; same-target short occlusion keeps the acquisition identity/timer.
- Vision results are latest-only at the control boundary; repeated old results do not become new controller observations.
- SDL event pumping has one owner, restoring both sticks without disturbing the remaining button/trigger signals.
- OCR/native weapon recognition is absent from the hot path. Recoil profile playback is disabled; recoil defaults to fixed downward `feedback_amount` feed-forward.

The governing manual/AI decision is
[Predictive Manual/AI Control Envelope](../../.agent-context/decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md).

### Planned Target-Count-Aware Ownership

The next ownership policy is user-confirmed but not implemented in the protected
runtime:

- With exactly one credible, full-authority hostile candidate, AI owns the
  target-relative solution. Manual remains observable and may help the same
  solution, but sustained opposing input does not by itself pull the controller
  away from the only valid target.
- With multiple credible candidates, stable manual intent may request a handover
  only when it points toward an eligible alternative. Merely detecting two raw
  boxes or seeing manual oppose the current target must not weaken AI globally.
- Candidate eligibility, selected identity and handover belong to the
  intent-aware selector and `TargetCoordinator`. `VectorIntentFuser` continues
  to execute one immutable target-relative plan and must not become a second
  multi-target selector.
- Zero-target, weak/ambiguous evidence, friendly/corpse rejection and physical
  ADS release retain their existing authority boundaries. "One target" means
  one credible candidate after evidence gating, not one detector box.

Implementation is sequenced after W4/W5 evidence: first publish a shadow
ownership mode (`single_target_ai`, `multi_target_hold`,
`multi_target_handover_candidate`) with eligible candidate count, alternative
identity, manual-intent direction/stability and explicit transition reason.
Then validate single-target opposing/overshoot input, two-target intent-aligned
switching, two-target input toward no candidate, candidate-count churn and brief
occlusion before authorizing actuation. Exact persistence and switch curves are
parameters to be selected from those fixtures, not part of the accepted policy.

See [Target-count-aware manual exit authority](../../.agent-context/decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md).

## Telemetry and Runtime Evidence

Normal runtime performance measurement now uses a separate lightweight window
summary. It keeps fixed counters and 0.25 ms histograms on the controller thread;
JSON serialization, file I/O and console output happen on a bounded background
writer. Enable it with detailed telemetry left off:

```toml
[runtime.performance]
enabled = true
interval_ms = 5000
directory = "runs/perf_summary"
stdout_enabled = true

[runtime.telemetry]
enabled = false
```

Each five-second JSONL record reports measured controller/output/Vision rates,
active and idle Vision rates, separately labelled active/idle accumulated-frame
pressure, and count/mean/P50/P95/
P99/max for controller, capture/result, present/result, result/controller and
present/ViGEm latency. It also separates synchronous `ego_stage` from asynchronous
`ego_compute`, so W3's Vision-thread staging cost is no longer hidden inside the
whole pipeline. Files are written to `runs/perf_summary/runtime_perf_summary_*.jsonl`.
The queue is capped at eight windows and reports cumulative writer drops instead
of blocking the controller.

The online W3 ego-motion observer is research-only and defaults to a resource-level
off state:

```toml
[runtime.vision]
ego_motion_enabled = false
```

When off, `VisionEngine` does not allocate its grayscale host/device staging
buffers, create the CPU matcher worker, launch grayscale conversion, perform D2H
readback, or synchronize the TensorRT CUDA stream. W5 short-term memory must use
timestamped final-output history, an identified response model and fresh-Vision
reconciliation; it must not depend on online optical flow. The switch may be
enabled only for explicit calibration/shadow experiments.

Release verification measured about `30 ns` amortized per controller tick while
also recording one Vision sample per six ticks. A summary record is constrained by
test to less than 4 KiB, so the five-second setting has a worst-case upper bound
below 3 MiB/hour. This is the default tool for honest throughput/latency A/B runs.

The structured per-event JSONL telemetry below remains the high-volume diagnostic
tool for causal control investigations and should only be enabled for a targeted
capture:

```toml
[runtime.telemetry]
enabled = true
```

The current trace can join present/capture/result/publish/controller/plan/output stages
using source frame, source observation, persistent target, physical ADS epoch,
target acquisition and controller tick identities. It records explicit target
rejection reasons and the effective activation radius. Schema 13 calibrates the
DXGI present-QPC clock into a steady-clock source-present timestamp; the latest
capture reports that calibration valid for `99.95%` of ego rows. Older schema-12
captures do not have this contract and must leave source-present unavailable.

The protected-baseline live session is `20260802T181639Z_53916_1`. Its manifest
contains executable SHA-256 `872F6FFF...50558C38` and config hash
`da594c01...e751`; it retained `state=active` after shutdown, so it is not a
cleanly closed-session duration record. Runtime/config identity and telemetry
file timestamps remain usable.

The latest diagnostic session is `20260803T135819Z_61220_1`, from executable
`25CF27F9...A10155`, schema 13. Vision publish-to-controller consume is
`0.50/0.98 ms` P50/P95 and result-ready-to-ViGEm is `0.59/1.09 ms`; all 21,370
acquisition source frames are unique and increasing. This rejects controller
mailbox backlog and duplicate Vision consumption as the current explanation.

### Known Final-Output Continuity Defect

The schema-13 audit proves that a fresh post-slew target-relative clamp can
store a reduced `previous_output_`, after which an intervening non-fresh tick
slews back toward a larger legacy proposal. There are 44 qualifying fresh
clamps and 31 opposite rebounds within 25 ms. The old five-case session also
reproduces 109 clamp transitions and 79 rebounds; Case 2 and Case 5 have strong
local matches, while Case 4 has none.

The repair must preserve the accepted target-relative final/envelope across
short non-fresh gaps inside the existing single final-output owner, with newer
Vision, identity/lifecycle changes, deliberate escape and expiry as boundaries.
It must not add another hold, brake or additive controller.

**User-confirmed:** the diagnostic run felt slightly more delayed. Shared-field
old/new comparison finds essentially unchanged result-to-output latency, but
active source cadence P95 widened from `15.71` to `21.03 ms`. Background log or
video analysis is a plausible resource-contention hypothesis, not a proven
cause; use a same-scenario run with analysis stopped before attributing it.

See [Five-Case and Schema-13 Control Audit](FIVE_CASE_SCHEMA13_CONTROL_AUDIT_20260803.md).

See [Native Log Sessions](NATIVE_LOG_SESSIONS.md) and
[Native runtime telemetry](../benchmarks/native-runtime-telemetry.md).

## Live Acceptance

- **User-confirmed:** two complete games felt substantially better than the prior runtime.
- Both games used one LMG with approximately 400 ms weapon ADS time.
- This validates gross stability, controller input continuity and practical usefulness of the combined repair.
- It does not isolate whether any remaining ADS timing mismatch belongs to weapon timing or controller parameters.
- An approximately 260 ms ADS weapon remains the pending cross-weapon fit comparison, but it follows the proven final-output continuity repair rather than preceding it.

The protected identity, restore set and exact acceptance boundary are in
[August 3 Live-Accepted Native Runtime](LIVE_ACCEPTED_RUNTIME_20260803.md).

## W0-W6 Status

- **W0-W2 - implemented/tested:** acquisition/provenance foundation, joinable stage telemetry, explicit rejection reasons, spatial activation radius, and admission-relative ADS lifecycle.
- **W3 - shadow only / blocked:** live compute P95 is `2.08 ms` and displacement clipping is fixed, but ADS and BodyLock valid rates are only `64.18%` and `73.40%`. It has no actuation authority; do not lower confidence thresholds blindly.
- **W4 - provisional / not promotable:** calibrated present time covers `99.95%` of ego rows and offline peaks appear around 5-10 ms, but peak bands are broad and W3-valid selection biases the cohort. No stable response curve or first-effect observation exists.
- **W5 - not implemented:** no CausalMotionLedger/Causal Remaining v2 reconciles `scheduled -> in-flight -> realized` work, and no 150-200 ms short-term memory affects output. PendingMotion timing repair and short-horizon rollout are diagnostic shadow work only.
- **W6 - not entered:** activation/tuning remains gated on W4/W5 shadow evidence and a separate architecture/acceptance review. Its ownership gate must include the target-count-aware single-target/multi-target policy above before production activation.

## Next Validation

Next implementation and validation order:

1. Repair fresh/non-fresh final-output continuity and cover stable target/manual, target loss/switch, escape and expiry deterministically.
2. Correct per-target assist segmentation and first-material-AI telemetry.
3. Build/review a candidate, then run one same-map/weapon/settings capture with Luna, log parsing and video decoding stopped.
4. If latency still feels higher, run a deliberate background-load A/B and compare game frame time, source cadence, inference, publish-to-consume and result-to-ViGEm.
5. Resume W3/W4 quality work only after actuation continuity is stable; keep W5 gated.

## Other Active Directions

Dynamic ROI remains future Vision/control integration work, not production
behavior. Begin with bounded translation and verified ROI-local to crosshair
coordinate conversion; do not combine translation, scaling and extra inference
without separate coverage.

Far-target/head-peek micro adjustment around 3% remains a deferred product
requirement. The continuity repair must not be widened into micro-aim tuning;
return to that requirement only after the mainline is stable and matched
cross-weapon validation is complete.

## Non-Regression Boundaries

- Do not restore additive manual-plus-AI output, fixed manual preservation floors, duplicate hold/brake/authority owners, or held-LT ADS rearming.
- Do not allow fresh-only safety to create a fresh/non-fresh sawtooth; the accepted final envelope must have an explicit continuity/expiry contract.
- Do not globally lower AI authority or game sensitivity to hide a directional/accounting defect.
- Do not let shadow ego-motion, Remaining, PendingMotion, rollout or a learner become a second production controller.
- Do not describe scheduled output as realized camera motion.
- Do not compare artifacts without executable/config/schema/scenario identity.
- Do not overwrite or discard the protected rollback without a reviewed candidate and explicit installation request.
- Do not attribute the current improvement to W5 memory; W5 is absent.

Historical rationale is indexed in [Archive](../archive/README.md). Reusable
methodology begins at
[Evidence-Driven Real-Time Control Optimization](EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md).
