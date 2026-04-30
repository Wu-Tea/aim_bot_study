# DEC-2026-04-22-002: Defer full controller and full-project C++ rewrite

Status: accepted
Date: 2026-04-22
Confirmed by: explicit user response declining the rewrite after architecture review ("那算了")
Related sessions:
- 2026-04-22T20:03:07+08:00
Related files:
- `controller.py`
- `controllers/gamepad_controller.py`
- `controllers/gamepad/ai_aim.py`
- `controllers/base_controller.py`
- `docs/project/CONTROLLER_OVERVIEW.md`
- `docs/project/NATIVE_VISION.md`
Supersedes: none
Superseded by: none

## Context

After the native vision migration showed a very large performance improvement, the next question was whether to move the controller or even the whole repo into C++. Inspection of the controller stack showed that the public boundary is small, but the internal gamepad implementation is a high-frequency host loop plus a large plugin/state-machine layer with many behavior-focused tests.

## Decision

Do not start a full controller rewrite or a whole-project C++ rewrite now. Keep the current hybrid architecture, and only reconsider a targeted native controller migration if controller profiling later proves it is the next real bottleneck.

## Reasons

- Native vision already captured the largest available performance win.
- The controller path is more sensitive to hand-feel and behavior regression than the vision hot path was.
- The controller stack contains substantial plugin/state logic and regression coverage, which makes a rewrite more expensive than the public API surface alone suggests.
- A full-project rewrite would add broad maintenance cost without targeting the hottest path first.

## Rejected Alternatives

- Rewrite the full controller immediately in C++: rejected because the ROI is unclear relative to the risk and migration cost.
- Rewrite the whole repo in C++ for language uniformity: rejected because uniformity alone is not enough justification for a high-risk rewrite.

## Evidence

- `controllers/gamepad/ai_aim.py` is a large stateful implementation and `tests/gamepad/` contains extensive controller-focused coverage.
- The user explicitly declined the rewrite after reviewing the tradeoff.
- Native vision measurements already show a step-function improvement without touching the controller runtime.

## Consequences

- Short-term work should focus on validating native vision and finishing nearby controller behavior changes only when they have clear product value.
- The current Python controller remains the production path.
- If further latency work is needed later, it should start from a targeted gamepad-controller migration, not a whole-repo rewrite.

## Review Triggers

- Revisit if controller profiling shows persistent Python-side jitter or latency as the next bottleneck.
- Revisit if future native-controller experiments demonstrate a clear additional gain with acceptable parity risk.
- Revisit if the architecture changes enough that the current plugin/state split is no longer the right abstraction.
