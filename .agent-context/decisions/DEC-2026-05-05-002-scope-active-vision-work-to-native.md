## DEC-2026-05-05-002: Scope Active Vision Work To Native

Status: accepted
Date: 2026-05-05
Confirmed by: user
Related sessions:
- 2026-05-05T16:38:49+08:00
Related files:
- vision/native_runner.py
- vision/runner.py
- tests/test_native_vision_targeting_bridge.py
- .agent-context/handoff.md
- .agent-context/session-log.md
Supersedes: none
Superseded by: none

## Context

The repository still contains both the native runtime (`vision/native_runner.py` plus `native/vision_native/`) and the older Python vision path (`vision/runner.py`), but recent project direction has already concentrated live validation and optimization work on the native route.

The user explicitly asked to record that the current vision module should only consider the native route, and asked for more regression coverage around native upper-body-only targeting behavior instead of reopening broader backend discussion.

## Decision

For current and near-term vision-module work, treat the native route as the only active scope:

1. native targeting, cue integration, live validation, and regression additions are the default and only active vision workstream
2. the Python vision backend remains a repository fallback only, not a parity, tuning, or regression target unless the user explicitly reopens it
3. vision behavior questions should be answered against the native runtime first, including chest-up / upper-body-only exposure handling

## Reasons

- It matches the user’s explicit instruction for the project’s current direction.
- It keeps testing and debugging effort aligned with the runtime that actually matters for current controller-facing behavior.
- It prevents attention from drifting back into Python/native parity work while the native path is still being revalidated and tuned.
- It narrows future regression work to the path where current yellow-cue, ROI-copy, and upper-body matching behavior really lives.

## Rejected Alternatives

- Continue treating Python and native vision as equal active workstreams: rejected because it spreads effort across a backend the user does not want to prioritize right now.
- Remove the Python backend immediately from the repository: rejected because the user asked to scope active work, not to delete the fallback implementation.
- Keep the decision informal in chat only: rejected because this scope boundary is likely to matter in later sessions and test planning.

## Evidence

- User request: “记录一下，现在vision模块只考虑native路线”
- Repository fact: the current controller-facing default path is native, and recent accepted decisions already prioritize native hotpath and cue work over broader parity or rewrite efforts.
- Verification added in the same session focused only on native bridge/runtime tests, including new upper-body-only aim-point regression coverage.

## Consequences

- New vision regression coverage should default to native test surfaces such as `tests/test_native_vision_targeting_bridge.py`.
- Python vision behavior should not be used as the reference implementation for new active-work decisions unless the user explicitly reopens that comparison.
- Handoffs and future sessions should interpret “vision work” as “native vision work” by default in this project state.

## Review Triggers

- Revisit if the user explicitly asks to reopen Python backend tuning, parity, or regression work.
- Revisit if the controller-facing default runtime changes away from native.
- Revisit if the Python backend becomes strategically important again for deployment, fallback quality, or research comparison.
