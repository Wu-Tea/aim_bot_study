# Fusion MW4 Replay Audit - 2026-08-30

## Outcome

The supplied recording reproduces two separate problems:

1. **Display continuity defect, fixed in this candidate.** The old Canvas
   consumed the auto-reset IPC event, applied its 30 FPS throttle, and could
   `continue` before reading shared memory. A correct 8-25 ms observation could
   therefore disappear without ever reaching a render. The old marker contract
   also hid same-generation, selector-confirmed `cue_hold` samples.
2. **Detector/selector acquisition defect, not fixed here.** In the early scene,
   the model first selects a large false person box over the optic/background.
   That false target owns the selector generation before the correct enemy box.
   Drawing raw detections sooner would display the wrong marker, not solve the
   cause.

The native evidence audit is `INSUFFICIENT_EVIDENCE` for causal attribution to
the original live session. The recording contains no Fusion pixels by design,
and executable identity, hardware/refresh covariates, controller activation and
live publish-to-render timestamps are unavailable.

A later user-confirmed live check exposed a separate presentation-window defect:
the full-screen transparent Canvas blocked mouse clicks. This is not visible in
the recording. A focused Windows integration probe reproduced it when the target
window ran on another thread.

## Provenance

- Video: `Replay 2026-08-30 20-57-51.mp4`
- Video SHA-256:
  `52dad97b51b0fc366ade76c18479a6da529f2bf2d2bb441e8ff11a13723cdbf6`
- Source: 1920x1080, 120 FPS, 1616 frames, 13.466667 seconds
- Replay ROI: `(640,284)`, 640x512
- TensorRT engine: `models/best_480x384.engine`
- Engine SHA-256:
  `45fc56274ff3bbc659e534c3b7833065b0483ef8022ac5d7657cd6da7dbdeb21`
- Replay path: current TensorRT engine plus current native selector with recorded
  source timestamps. This is not a live 200 FPS DXGI replay.

## Measured Evidence

### Display-layer incident

The frozen fixture covers frames 1333-1342, generation 12. Frames 1337 and 1341
are one-frame detector gaps where the selector retains a confirmed target as
`cue_hold`.

| Policy | Visible fixed 30 FPS render ticks | Confirmed gaps hidden | Visible runs |
|---|---:|---:|---:|
| Legacy read-on-render/direct-only | 0/2 | 2 | 3 |
| Event ingest plus confirmed 120 ms latch | 2/2 | 0 | 1 |

The render phase is a frozen synthetic worst case because live Canvas timestamps
were not recorded. Generation change and a 5 ms expired-latch counterfactual
both correctly suppress continuity.

### Acquisition latency

- At approximately 0.500 s, the real enemy is already visually discernible.
- Correct person detections begin around 0.583 s.
- Frames 68-69 first commit a large false box over the optic/background.
- The correct enemy becomes the selected generation at frame 75, 0.625 s.
- Conservative visible-enemy-to-correct-marker latency is therefore at least
  125 ms in this segment.
- At 11.542 s, another large scope/weapon-shaped false person box is selected.

The early 0.5-2.5 s window remains 19 marker-positive frames under both marker
policies. This confirms that Canvas continuity does not fix the early identity
problem. In the late 10.5-13.466 s window, direct-only visibility is 19/356
source frames; bounded confirmed continuation raises it to 57/356 without
bridging gaps over 120 ms.

## Candidate Change

- `MsgWaitForMultipleObjectsEx` waits on the Fusion event, a waitable deadline
  timer and the Windows message queue.
- IPC state is read and latched before applying the 30 FPS DWM commit cap.
- At least two consecutive direct samples can guarantee one pending render.
- Same-generation, selector-confirmed target geometry may bridge a detector gap
  for at most 120 ms from the last direct publisher QPC timestamp.
- The Canvas marker center now uses the selector-published `target_x/target_y`
  directly instead of recomputing a point above the body box.
- Non-aim keep-warm Vision polling is 60 Hz instead of 20 Hz. This changes the
  scheduler-only idle interval from 50 ms to about 16.7 ms; the supplied replay
  does not establish a live end-to-end latency improvement.
- Generation changes, unconfirmed loss, malformed geometry and expired samples
  fail closed.
- `Present(0)` avoids blocking IPC ingestion on vertical sync.
- Idle Canvas wakes only for IPC/messages, marker expiry or the one-second
  capture-isolation inspection.
- The visual latch is not fed back into ADS, BodyLock, AutoFire or target
  selection.
- The Canvas window now uses `WS_EX_LAYERED | WS_EX_TRANSPARENT`, excludes
  `WS_EX_NOREDIRECTIONBITMAP`, and is created with `WS_DISABLED`. The old style
  returned `HTTRANSPARENT`, but that cannot route a hit to a different game
  thread. F10/F11 are now thread hotkeys rather than disabled-window input.

## Verification

- Release builds: `fusion_canvas`, `cod_native_runtime`, `vision_native_cpp`
- `FusionOverlayContracts`: pass
- Targeted Python, bridge, replay, and startup lifecycle tests: 28/28 pass
- Native Release CTest suite: 26/26 pass
- Background launch RED/GREEN: the old CRT log handle blocked a live readiness
  read; the rebuilt Canvas remains readable while its process is running.
- Fusion VBS now waits for the hidden PowerShell launcher and reports non-zero
  exits with the launcher-log path. Native startup waits only for the launcher
  process, not the long-lived runtime descendant.
- Frozen replay: legacy exits 1 (RED), candidate exits 0 (GREEN)
- Mouse passthrough: legacy production style and a disabled no-redirection
  counterfactual both block the worker-thread target; layered plus disabled
  production style passes with `0x080800a8`
- Actual two-phase Desktop Duplication gate: `visible_probe_pixels=5184`,
  `excluded_probe_pixels=0`, `control_pixels=5184`
- Final candidate Canvas SHA-256:
  `805ba5ea972f3f04bf047a3b3fe3a66d78507870b7bb689727acc7cbeddae0df`
- Final native runtime SHA-256:
  `1ab0db1ee042c379bff12f226109969043f1026b477bf0538f3f855856f3e2af`

Artifacts:

- `artifacts/analysis/fusion-visibility-20260830-205751/replay-current/replay-current-vs-candidate.mp4`
- `artifacts/analysis/fusion-visibility-20260830-205751/replay-current/frame-1337-current-vs-candidate.png`
- `artifacts/analysis/fusion-visibility-20260830-205751/regression-red.json`
- `artifacts/analysis/fusion-visibility-20260830-205751/regression-green.json`
- `artifacts/analysis/fusion-visibility-20260830-205751/audit-report.json`
- `artifacts/analysis/fusion-visibility-20260830-205751/input-passthrough-legacy-final.log`
- `artifacts/analysis/fusion-visibility-20260830-205751/input-passthrough-disabled-no-redirection-final.log`
- `artifacts/analysis/fusion-visibility-20260830-205751/input-passthrough-disabled-layered-final.log`
- `artifacts/analysis/fusion-visibility-20260830-205751/isolation-disabled-layered-final.log`

## Remaining Gates

This candidate is complete for the frozen visual-continuity regression, not for
live gameplay acceptance. A matched native/live Fusion-off versus Fusion-on run
must still record executable/config/engine identities, hardware and refresh
covariates, publish-to-render timing, frame cadence, Canvas render cost and user
feel. The early false-box acquisition needs its own detector/selector RED fixture
before changing confidence, pickup radius or identity policy. The contained
mouse probe is GREEN; normal MW4 menu, desktop and alt-tab clicks still need a
brief live confirmation in the user's exact fullscreen mode.
