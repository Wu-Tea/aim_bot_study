# Partial-Occlusion Combat Benchmark Design

Date: 2026-07-17

## Purpose

Add a deterministic native benchmark for diagonal target motion through lower-body
occlusion. The benchmark must report two scores separately:

1. `partial_occlusion_combat`: ordinary combat motion with a plausible, delayed but
   directionally correct user.
2. `partial_occlusion_human_errors`: the same scene distribution with log-derived
   stale-direction, single-axis, crossing-inertia, and delayed-correction mistakes.

The benchmark measures the current controller. It does not change ADS, BodyLock,
tracker, recoil, AutoFire, or live configuration behavior.

## Evidence Envelope

The scenario envelope comes from ten recent native telemetry sessions containing
90,904 ADS samples:

- 143 same-track diagonal observation-gap/reacquisition events;
- gap duration P25/P50/P75/P90/P95 = 27/42/65/110/161 ms;
- reacquisition absolute X movement median 12.2 px and Y movement median 8.8 px;
- manual peak P25/P50/P75/P90 = 0.28/0.39/0.59/0.79;
- 53 events had wrong manual alignment on reacquisition; stable correction delay
  median 122 ms and P75 177 ms;
- 394 fully observed diagonal 100 ms windows included 67 mostly-wrong and 83
  sustained single-axis-conflict windows;
- 71 center-crossing events retained old-direction manual input, with median
  magnitude 0.29 and median stable correction delay 62 ms.

Telemetry does not contain selected body-box geometry or physical left-stick axes.
Consequently, occlusion geometry and left-stick motion are deterministic synthetic
inputs constrained by the observed timing and right-stick distributions; they are
not labeled as direct log replay.

## Architecture

Keep the benchmark outside `cod_native_gamepad_benchmark.cpp`:

- `partial_occlusion_benchmark.h/.cpp` owns scenario directives, log-derived
  constants, metric aggregation, and scoring.
- `partial_occlusion_benchmark_tests.cpp` tests schedules and scores without the
  runtime controller.
- `cod_native_partial_occlusion_benchmark.cpp` runs the real
  `NativeGamepadController`, writes JSON, and prints a two-row score summary.

The runner maintains world target position and reticle position separately. Score
truth is always the configured full-body chest point. A clipped Vision body box may
move the controller's observed aim point, but must never move benchmark truth. This
prevents the geometry/scoring mismatch found in the existing ADS settle case.

## Shared Combat Timeline

Each scenario contains four diagonal cases: left-up, left-down, right-up, and
right-down. Each case runs at 1000 Hz controller rate and 100 Hz Vision rate:

1. 140 ms full-body observed acquisition;
2. 50 ms progressive lower-body clipping;
3. a 30, 60, 110, or 160 ms processed no-target gap;
4. 50 ms biased partial-box reacquisition;
5. 260 ms full-body recovery.

The full body is 84x180 px. Partial visibility clips it to 55-65% height. Target
velocity combines diagonal target motion with deterministic left-stick-relative
screen motion; no weapon profile or live calibration data is required. Track identity
stays stable in this first benchmark so association-jump behavior remains a separate
future adversarial test.

## Manual Profiles

`partial_occlusion_combat` uses a delayed proportional manual model with a 55 ms
reaction delay, magnitude cap 0.45, and drift below 0.02. It does not deliberately
reverse the target direction.

`partial_occlusion_human_errors` assigns one classic mistake to each diagonal case:

- stale full-vector direction for 120 ms after reacquisition;
- X wrong / Y correct for 100 ms;
- X correct / Y wrong for 100 ms;
- center-crossing inertia followed by a 160 ms delayed correction.

Magnitude is capped at 0.60. Values below 0.03 are classified as no intent.

## Metrics and Scores

Both scenarios report raw metrics:

- mean, P95, final, and peak true error;
- X/Y maximum overshoot;
- occlusion peak error;
- reacquisition time to 20 px;
- P95 final-output delta and output spike count;
- ADS/BodyLock/manual frames and mode changes;
- correct-manual AI opposition frames;
- wrong-manual high-force fight frames;
- geometry-bias peak between Vision aim and world-truth aim.

Scores are bounded to 0-100 and remain decomposed:

- tracking 30%;
- overshoot 20%;
- recovery 20%;
- smoothness 15%;
- intent preservation 15%.

The overall score is the weighted component mean. The error-mixed scenario does not
receive credit merely because AI overpowers the user: high AI force against a known
wrong-manual phase is penalized, while correct-manual opposition is penalized more
strongly. JSON records the formula version and every component score.

## Debug Log Collection Plan

A later, separately approved telemetry change should add selected-target-only fields
at the existing debug telemetry rate:

- `physical_left_x`, `physical_left_y`;
- raw selected `body_x1/y1/x2/y2`;
- raw Vision aim X/Y;
- resolved chest aim X/Y;
- tracker-projected X/Y and track velocity X/Y;
- selected observation ID and backing frame ID already available where possible.

The writer should reuse current snapshots, allocate no new hot-path queue, remain off
when debug telemetry is disabled, and retain the existing fresh-session cleanup
policy. A derived offline analyzer will then label actual partial-box shrinkage,
observation gaps, and manual correction delays. None of this telemetry work is part
of the current benchmark implementation.

## Verification

- Schedule tests prove both scenario names, four diagonal cases, all four gap
  durations, partial-body clipping, and all classic error profiles.
- Geometry tests prove world-truth chest aim does not move when the observed box is
  clipped.
- Score tests prove a perfect trace scores 100 and that error, output spikes, correct
  manual opposition, and wrong-manual full-force fight reduce their components.
- Build and run the standalone executable with current `config.toml` and seed 1337.
- Run focused controller tests and the native pipeline contract because the runner
  links the production controller, even though production behavior is unchanged.

