# Brake-episode benchmark and window-only exit design

## Objective

Replace the sustained benchmark's nearly inert severe-overshoot counter with a
continuous target-relative braking trace that can distinguish fast controlled
capture, late braking, small recoverable crossing, repeated correction, circle
escape, and premature stopping. Use the new diagnostics to screen combinations
of aim strength, ADS arrival horizon, camera response, and game slowdown before
selecting a gameplay candidate.

The native gamepad launcher must not install a keyboard termination shortcut.
Closing the console window is the interactive shutdown mechanism; finite test
runs continue to stop through their explicit tick limit.

## Scope and boundaries

This work changes benchmark observation and native runtime termination. It does
not add another controller gate, brake policy, or vision workload. Parameter
sweeps use isolated configs and do not modify the user's root `config.toml`.

The repository has consolidated root launch shims. The gamepad entry point is
`scripts/launch/gamepad_start.bat`, which calls
`scripts/launch/gamepad_native_cpp_start.bat`. Neither script currently sets a
quit key; the unwanted `0` shortcut comes from the native runtime default and
the empty-key fallback.

## Window-only native termination

The native runtime will stop polling `GetAsyncKeyState` for a configured quit
key. Interactive termination will be requested only by a Windows console close
event. The handler will set a process-local atomic stop flag; the runtime loop
will observe the flag and leave through its normal shutdown path when Windows
allows the close grace period.

`--max-ticks` and `--once` remain supported for tests and contract runs. They
are programmatic limits, not user shortcuts. Legacy `quit_key` configuration
may remain parseable for compatibility, but it no longer activates a keyboard
hook in the native runtime. Startup documentation and tests will state that the
native gamepad launcher has no termination hotkey.

Closing a console window can still be forcibly terminated by Windows if the
process exceeds the operating-system close timeout. The runtime must not delay
shutdown with new synchronous logging in the console handler.

## Brake episode

Each target owns at most one active brake episode at a time. An episode begins
when the target is observed and any of these conditions holds:

- distance is within three visible radii and radial closing speed is at least
  40 px/s;
- predicted terminal error crosses the target-relative approach plane;
- the target first enters its visible radius;
- the controller is about to hand off from ADS to BodyLock.

At episode start, the scorer freezes the normalized target-relative approach
axis. Longitudinal signed error is the current error projected onto that axis;
lateral error is retained separately. A center crossing requires longitudinal
error to move beyond a noise band on the opposite side. The noise band is
`max(1.5 px, 0.08 * visible_radius_px)`.

An episode settles after error remains within
`max(2 px, visible_radius_px / 3)` for 40 ms without outward radial motion. It
ends on settle, target replacement/loss, manual escape, or the end of the
tracking window. Target motion is already represented in target-relative error,
so raw screen-coordinate crossing is never scored.

## Per-target measurements

The scorer will retain the existing severe `over_events` red line and add:

- `center_cross_events`: noise-qualified approach-plane crossings;
- `max_post_cross_error_px`: farthest opposite-side longitudinal excursion;
- `overshoot_area_px_ms`: integral of opposite-side excursion over time;
- `continued_push_after_cross_ms`: time the AI component continues in the
  pre-cross direction after crossing;
- `brake_start_distance_px`: target distance when the episode begins;
- `time_to_zero_radial_speed_ms`: time from episode start until outward/closing
  speed is arrested;
- `first_entry_to_settle_ms`: time from first circle entry to stable settle;
- `correction_reversal_events`: noise-qualified reversals of the AI radial
  component during the episode;
- `circle_exit_events`: existing circle re-exit count;
- `stall_ring_ms`: existing non-closing time in the 10--20 px ring;
- `handoff_residual_px`: radial error at ADS-to-BodyLock handoff;
- `handoff_closing_speed_px_per_sec`: closing speed at handoff;
- `max_error_px`: existing maximum tracking-window error;
- `direction_discontinuities`: existing large output-delta count.

Missing events use explicit sentinel values in per-target JSON rather than
being silently converted to zero. For example, a target that never settles has
`first_entry_to_settle_ms = -1` and increments `unsettled_targets` in the
aggregate.

## Attribution

The benchmark will pass current plan diagnostics into `ScoreFrame`, including
control mode, predicted terminal error, radial closing speed, and ADS-to-
BodyLock transition. It already passes physical/manual, requested AI, shaped
AI, and final stick components.

Crossing geometry is always recorded. AI braking penalties are accumulated only
when the target is observed and reliable, no manual escape is active, and the
shaped AI projection continues the pre-cross motion. User-caused crossing stays
visible as geometry but does not become an AI late-brake penalty. Coasting and
unreliable frames pause penalty accumulation rather than erasing the episode.

## Aggregation and scoring

All new measurements are exported both per target and in `BenchmarkResult`.
Aggregates include sums for event/time/area measurements, maximum and P95
post-cross excursion, median/P95 settle time, and counts of settled/unsettled
targets.

There is no capped 100-point score. Existing acquisition and tracking points
remain positive objectives. Candidate selection uses a lexicographic safety
gate:

1. reject any candidate that materially increases severe overshoot, circle
   exit, continued-push time, or unsettled-target rate;
2. among safe candidates, prefer more acquisitions and acquisition points;
3. then prefer shorter settle time, smaller post-cross excursion and area, less
   stall-ring time, and fewer correction reversals;
4. use output delta, jerk, and direction discontinuities as the smoothness
   tiebreaker.

This prevents a slow controller from winning merely by never crossing center,
and prevents a fast controller from winning by repeatedly overshooting and
recovering.

## Scenario coverage

The existing ordinary/small targets, pure/mixed input, horizontal/vertical/
diagonal acceleration, reversal, stop, jump/fall, and isolated ADS/BodyLock
cohorts remain. The final sweep covers:

- slowdown edge/center: `0.65/0.55`, `0.50/0.40`, `0.40/0.30`;
- camera response: `400`, `500`, `650` px per stick-second;
- ADS arrival horizon: `100`, `120`, `140`, `160` ms;
- ADS strength scale: `1.40`, `1.54`, `1.68`;
- BodyLock X/Y: `0.45/0.50`, `0.50/0.56`, `0.52/0.58`.

The sweep is staged instead of running the full Cartesian product at 60
seconds:

1. a short deterministic screen rejects clearly slow, unstable, or unsafe
   combinations;
2. surviving ADS and BodyLock candidates run the full 60-second, three-seed,
   ordinary/small, pure/mixed matrix;
3. finalists run response-mismatch and strong-slowdown stress comparisons.

Every comparison within an environment must have identical script hashes.

## Verification

Unit tests will cover a small crossing, large sustained crossing, no-cross fast
settle, circle exit, 10--20 px stall, repeated correction, moving target,
unreliable pause, manual-caused crossing, ADS-to-BodyLock handoff, and a target
that never settles. JSON tests will require all new aggregate and per-target
fields.

Runtime tests will prove that no digit or letter key stops a normal native run,
the close-event stop flag is observed, and `--max-ticks` still terminates test
runs. Startup-script tests will prove the native gamepad launcher neither sets
nor advertises a quit shortcut.

The final report will include the frozen baseline, all sweep dimensions and
seeds, rejected candidates with the failing safety metric, finalists, and the
recommended gameplay configuration. No recommended configuration is written to
the user's root config until explicitly requested.
