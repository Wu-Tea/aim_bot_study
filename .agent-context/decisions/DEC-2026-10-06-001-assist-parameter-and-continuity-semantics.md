# Assist parameter and continuity semantics — 2026-10-06

Status: user-authorized scope; implemented and tested, live Apex feel unverified.

## Authorization and context

The user requested GUI/configuration/native semantics to agree, independent force caps and response times, values without hidden replacement, and reduction of oscillation and intermittent tracking. They explicitly limited missing-observation continuation to 32 ms and authorized autonomous curve tuning after numerical reproduction. Vision ticks mean fresh visual frames, not 1000 Hz controller ticks.

## Current decisions

- Caps are normalized configuration values displayed as percentages; response times use milliseconds. Canonical catalog owns shared numeric defaults, bounds and reload rules. Old aliases migrate explicitly; personal files are not rewritten merely by opening them.
- AI input deadzone is per-axis and inclusive, default 3%, no rescaling above threshold. Manual raw input and recoil are separate. Recoil output zero remains zero.
- Canonical gamepad can continue the same target through a bounded 32 ms fresh-observation gap using original capture age, qualified motion and actual delivered camera work. Invalid identity/evidence, manual exit and expiry stop it. It cannot authorize fire or train estimators from a predicted observation.
- Range-dependent position budget retains a pursuit reference but yields to continuous point braking. `arrival_radius_px` is a smooth approach scale, not a hard BodyLock position deadband; it still determines ADS completion. This supersedes the intermediate October 6 hard-stop behavior recorded in early screenshots/logs.
- Motion estimation accumulates capture-aligned displacement over up to five intervals / 25 ms; existing authority, identity and observer slew boundaries remain. A nominal 25 ms position braking horizon was selected with the retained simulations. These are implementation choices under the user's tuning authorization, not user-specified constants.

## Alternatives and evidence

Three-derivative median filtering can destroy cancellation of adjacent position noise. The previous floor/zero boundary can oscillate even without noise. Globally disabling the pursuit minimum increased some moving-target errors. Adding an arbitrary return-to-zero delay risks retaining stale directional force. No new optical flow or fixed 10-frame/2-frame extrapolator was needed for this iteration.

Final tests: Release, 50 CTest groups, 111 desktop checks. Two-seed plant experiment has 192 scenarios; 90/96 configured-floor comparisons improve mean error. Six stationary cases regress in residual error but remain within their configured radii. See `output/point-curve-tuning-20261006/comparison.json` and CSV for complete limits and cases.

## Review triggers

Revisit when new live Apex telemetry shows persistent overshoot, delayed reversals, cue/source jumps, sensitivity mismatch or deadzone-induced interruption. Do not treat simulated error reduction as live acceptance. Preserve manual authority, acquisition/identity/handover performance and the retired validation-framework boundary.
