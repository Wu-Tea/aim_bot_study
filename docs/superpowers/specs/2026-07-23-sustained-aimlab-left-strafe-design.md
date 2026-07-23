# Sustained AimLab Full-Speed Left-Strafe Design

Date: 2026-07-23
Status: approved design, pending implementation

## Objective

Extend the sustained AimLab benchmark with deterministic full-speed horizontal
player movement so the existing production controller can be measured under
realistic left-stick strafing. The benchmark must expose the cost of player
movement without changing controller policy, identifying weapons, adding visual
inference, or invalidating the existing no-strafe baseline.

The benchmark is an instrument for controller optimization. The feature is not
intended to become another production control path.

## Current Gap

The sustained AimLab simulator currently generates target motion, visual
observations, slowdown, controller response delay, and optional mixed right-stick
input. Its native adapter maps `manual_stick` only to the physical right stick.
It does not:

- send a physical `left_x` signal to the production controller;
- model player velocity caused by that signal;
- apply player motion to the POV-target relative position.

Consequently, current AimLab scores do not measure the controller behavior that
occurs when the player strafes during acquisition or tracking.

## Selected Approach

Each generated target carries one deterministic player-strafe script. A run can
either ignore that script (`off`) or execute it (`full_reversal`). A paired
benchmark executes both modes against the same target script and the same
right-stick manual profile.

The script has four phases:

1. neutral before onset;
2. full-speed movement in a seeded horizontal direction;
3. one full-speed reversal;
4. release to neutral.

There are no repeated random direction flips and no per-tick white noise.

## Determinism and Pairing

- New acceptance seeds are `2026072301`, `2026072302`, and `2026072303`.
- Target trajectories, observation timing/noise, acquisition deadlines, visible
  radii, and mixed right-stick inputs are generated once per seed.
- The player-strafe schedule is also generated once and stored with each target.
- The `off` and `full_reversal` variants consume the same `ScenarioScript` and
  therefore retain the same script hash.
- Player-strafe mode is a run identity field, not part of target generation.
- A separate deterministic random stream or a seed-derived generator is used for
  player motion so adding strafe fields does not perturb existing target draws.

This pairing allows score deltas to be attributed to player movement rather than
different targets or different right-stick mistakes.

## Player Motion Model

### Physical input

`left_x` is always one of `-1.0`, `0.0`, or `+1.0`. It is passed through
`PhysicalGamepadState.left_x` to the real native controller adapter.

### Hidden plant parameters

The controller does not receive a weapon identity or the hidden movement
parameters. Each target samples:

- full-speed player motion from `125` to `232 px/s`;
- a first-order response time constant from `100` to `180 ms`;
- onset, reversal, and release times that fit the target's acquisition plus
  tracking horizon.

The speed range is derived from the existing left-stick defect fixtures:
`0.78–1.45 body/s` at a `160 px` reference body height.

Player velocity follows the requested full-speed velocity through a first-order
response:

```text
velocity += (1 - exp(-dt / time_constant)) *
            (desired_velocity - velocity)
```

The POV-target horizontal error receives relative player motion:

```text
error_x += (-player_velocity_x) * dt
```

Positive player movement therefore shifts a stationary target left on screen.
Velocity decays through the same response model after release, preserving
inertia instead of stopping instantly.

### Cohort timing

- ADS acquisition: the strafe clock begins when the target spawns.
- isolated BodyLock: the strafe clock begins only after BodyLock entry, matching
  the existing rule that warm-up is stationary and unscored.

## Public Interface

Add:

```text
--left-strafe off|full-reversal|both
```

Default: `off`, preserving existing callers and historical benchmark semantics.

`both` emits paired runs in a stable order for every combination of seed, manual
profile, and cohort:

1. `off`
2. `full_reversal`

The benchmark report schema is incremented. Every run records its strafe mode.
The simulator metadata records the configured speed and time-constant ranges.

## Audit Metrics

Preserve all existing scoring and defect metrics. Add per-run audit fields:

- left-strafe mode;
- active left-stick milliseconds;
- reversal count;
- maximum absolute `left_x`;
- sampled player top-speed minimum/maximum encountered;
- maximum absolute realized player speed.

The paired comparison uses:

- acquisition points and targets acquired/missed;
- tracking points;
- smooth bonus;
- first-entry and settle timing;
- mean and p95 error;
- over and undertrack events;
- false interruption and false stop events;
- stall-ring time;
- handoff residual and handoff closing speed.

No capped or composite “full score” is introduced.

## Code Boundaries

- `sustained_aimlab_types.*`: target strafe schedule and run-mode types.
- `sustained_aimlab_scenario.*`: deterministic schedule generation and hashing.
- `sustained_aimlab_simulator.*`: strafe clock, player-motion plant, trace fields,
  and run audit metrics.
- `native_benchmark_controller_adapter.*`: map observation `left_x` to physical
  gamepad input.
- `cod_native_sustained_aimlab_benchmark.cpp`: CLI parsing, paired execution,
  console summary, and JSON provenance.
- Existing simulator/adapter/CLI tests: deterministic schedule, physical
  delivery, kinematic sign/inertia, baseline invariance, and report identity.

No production controller source or runtime configuration is changed.

## TDD Acceptance

Implementation begins with failing tests that establish:

1. identical seeds generate identical strafe schedules;
2. generated schedules contain at most one direction reversal;
3. all active requested values are full scale;
4. `off` and `full_reversal` preserve the same script hash and right-stick input;
5. the native adapter receives `left_x`;
6. positive player motion shifts target error left;
7. velocity ramps and decays rather than jumping;
8. BodyLock warm-up remains neutral;
9. default `off` reproduces the pre-feature benchmark result;
10. JSON and console output identify the strafe mode and audit metrics.

After unit tests pass, run the production sustained AimLab executable with:

- the three new seeds;
- `--left-strafe both`;
- both manual profiles;
- both ADS and BodyLock cohorts;
- the current runtime config and revision fingerprint.

The result is diagnostic. A score regression under strafe does not authorize a
controller change by itself.

## Failure Handling

- Reject unknown CLI modes.
- Reject invalid speed/time-constant ranges or malformed phase ordering.
- Fail if paired runs lose script-hash equality.
- Keep output creation atomic through the existing `.partial` behavior.
- Preserve the old `off` default when no new argument is supplied.

## Non-Goals

- weapon recognition or a weapon movement database;
- persistent learning;
- injecting test input into the live application;
- additional vision inference;
- vertical left-stick movement;
- frequent direction jitter;
- controller tuning in the same change.
