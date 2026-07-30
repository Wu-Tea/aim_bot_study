# Remaining-work production acceptance

Date: 2026-07-30

## Purpose

Vision reports a target error at capture time, while the controller continues
to send camera input before the next Vision frame arrives. Controlling from the
old error on every controller tick repeats work that has already been delivered.
This change makes the tracker own one controller-rate state:

`remaining work = captured target error - confirmed pre-recoil camera work`

Target motion prediction remains in the tracker. ADS and BodyLock continue to
own their existing rate and strength laws. Recoil remains outside this
calculation.

## Production contract

- Only successfully delivered, output-enabled samples enter the pending-motion
  ledger.
- The accounted sample is the final fused right-stick command immediately
  before recoil, so physical and AI camera work are counted once.
- Fresh Vision is authoritative. Work sent after its capture timestamp is
  subtracted once when the observation is consumed.
- Target changes, a new ADS epoch, output failure/disable, and controller reset
  clear the accounting state.
- Low response confidence reduces accounting authority; it does not create a
  second brake or post-controller gate.
- The ledger is fixed-capacity and allocation-free in the control loop.

Runtime controls under `[gamepad.tracker]`:

```toml
remaining_work_enabled = true
remaining_work_scale = 0.60
```

Setting `remaining_work_enabled = false` is the immediate rollback path.

When telemetry is enabled, controller samples expose
`remaining_work_{x,y}`, `delivered_camera_work_{x,y}`,
`remaining_work_confidence`, and `remaining_work_valid`.

## Fixed-seed acceptance

Revision baseline: `d186d1e`

- Seeds: `2026073001`, `2026073002`, `2026073003`
- Duration: 60 seconds per seed/cohort
- Vision/controller: 80 Hz / 1,000 Hz
- Target slot: 1,330 ms
- Intent fusion: `vector-baseline`
- Candidate: integrated delivered-work accounting at scale 0.60
- Baseline: current captured error

The final rerun used the production-built benchmark executable. Negative error,
overshoot, stall, and output-delta percentages are improvements.

| Scene | Cohort | Score | Mean error | Overshoot/acquisition | Stall/acquisition | P95 output delta |
|---|---|---:|---:|---:|---:|---:|
| Ordinary moving | ADS | +26.2% | -11.4% | -64.7% | -36.5% | -2.8% |
| Ordinary moving | BodyLock | +3.9% | -3.4% | +106.6%* | -26.5% | -8.3% |
| Strafe + vertical + 36 ms occlusion | ADS | +31.9% | -3.3% | -4.9% | -22.7% | -13.0% |
| Strafe + vertical + 36 ms occlusion | BodyLock | +3.0% | -1.8% | -7.2% | -19.9% | -13.1% |

`*` Ordinary BodyLock has a small overshoot denominator. Mean error, stall time,
score, and output smoothness improve, but this metric remains a live-test
guardrail rather than proof of universal overshoot reduction.

Recovery and arc profiles were also tested across the same three-seed family.
They improved score and mean error in every retained cohort. The detailed
exploration table remains in `README.md`.

## Verification

- Production runtime builds successfully in Release.
- Focused config, pending-motion, coordinator, ADS, BodyLock, controller
  integration, telemetry writer, and telemetry collector tests pass.
- Full CTest: 32/34 pass. The two failures are the existing
  `NativeLeftStickMotionBenchmarkTests` quality threshold and
  `NativeSustainedAimlabLearningTests` learner-actionability threshold; neither
  is in the remaining-work production path. They remain visible instead of
  weakening their assertions.

## Live acceptance guardrails

The synthetic result supports a testable build, not automatic promotion to the
main baseline. During live validation, watch:

- continued push after crossing during dense compound motion;
- false BodyLock interruption/stop events;
- ordinary BodyLock overshoot;
- target/ADS-epoch resets after a switch;
- `remaining_work_valid` dropping immediately after failed output delivery.

If the camera feels over-consumed or reluctant, disable the feature first.
Do not compensate by adding another brake; use the telemetry fields to determine
whether response scale, delivery accounting, or target identity is wrong.
