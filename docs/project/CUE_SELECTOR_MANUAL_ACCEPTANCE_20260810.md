# Cue Selector and Manual-Correction Candidate — 2026-08-10

## Purpose

This candidate addresses three defects seen in iron-sight and cue-continuation
gameplay:

1. a cue-derived point could be reconstructed above or beside the actual person;
2. target-first control could then suppress a valid physical correction, making
   the bad point feel impossible to pull away from;
3. detailed per-event telemetry materially changed the Vision rate being measured.

The change is intentionally bounded. It does not add a second capture pipeline,
optical flow, a new memory owner, or a new controller thread.

## Root Cause and Repair

- Cue geometry is now owned by one confirmed selector target generation. A
  confirmed target replacement clears the prior cue offset before the new target
  can establish its own person-plus-cue pair.
- Only a direct same-generation person-plus-cue observation may establish or
  update marker-to-target geometry. A cue-held output cannot train the next cue
  reconstruction.
- A reconstructed point may move with the cue, but a per-result displacement
  greater than 36 px from the current same-generation target is rejected before
  it receives aim authority. Rejection releases authority instead of steering at
  a known-bad point.
- When the last reliable direct observation contained exactly one eligible ADS
  target, cue continuation retains a bounded physical correction using the
  configured manual-direction weight (currently 45%). This works when solved
  target output is zero and when physical input opposes a stale cue direction.
- Observed single-target frames keep the normal target-first final-output rule;
  multi-target frames continue to use selector/coordinator handover rather than
  the cue correction shortcut.
- Cue exit is slew-limited for three 1 ms controller ticks so losing cue authority
  cannot expose a full manual step on the same tick.

## Verification

- Release full build: PASS.
- Release CTest: 40/40 PASS.
- Focused executables: target selector, full controller integration, frame-driven
  Vision service, and runtime timing all PASS.
- Regression fixtures prove:
  - cue offset resets on confirmed selector generation change;
  - a 40 px wrong reconstruction loses aim and fire authority;
  - cue with target output `T=0` still permits sustained downward correction;
  - physical input can reverse a wrong cue anchor;
  - cue continuation remains aim-only;
  - direct single-target authority and multi-target intent handover are preserved.
- A short real runtime smoke test produced 13 five-second summary windows with
  controller rate 998.383–1000.006 Hz, result-to-ViGEm P99 0.875–1.125 ms,
  and zero performance-writer drops.

The smoke test had no physical LT input, so Vision correctly remained in its
approximately 20 Hz idle mode. It does not prove the requested active 200 Hz or
source-present-to-ViGEm P99 target. That boundary requires one live ADS run with
only the lightweight performance summary enabled.

## Installed Runtime

- Runtime: `native/vision_native/build/Release/cod_native_runtime.exe`
- SHA-256: `c99e237cefca7c9885ca9c4d5d9cc5526b91a57a57bcae6c3dc2bc2d29312082`
- Detailed telemetry: disabled for normal validation.
- Lightweight performance summary: enabled, five-second file windows, console
  output disabled.
- Local binary/config backup:
  `native/vision_native/build/runtime-backups/cue-selector-manual-20260810/`

## Live Acceptance Boundary

The first live pass should check these exact transitions:

1. iron-sight cue above a partly occluded target: sustained downward manual must
   move the final output down instead of remaining locked above the head;
2. target death or marker loss: aim authority must release rather than continue
   at the old point;
3. confirmed switch between two targets: old marker-to-body geometry must not
   transfer to the replacement;
4. active Vision summary: record active Vision Hz and source-present-to-ViGEm
   P50/P95/P99 without enabling detailed telemetry.

Passing automated tests means the known software defects are covered. Final
acceptance still requires the four live checks because game aim-assist slowdown,
weapon occlusion and actual capture cadence are not reproduced by unit fixtures.
