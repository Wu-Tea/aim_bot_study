# DEC-2026-08-19-001: Minimal Mouse Controller Adaptation

Status: accepted
Date: 2026-08-19
Confirmed by: user
Related sessions:
- 2026-08-19T15:19:21+08:00
Related files:
- `docs/project/MOUSE_CONTROLLER_REUSE_FIRST_ARCHITECTURE_V2_3_20260814.md` on `codex/mouse-controller-foundation-20260815`
- `native/controller_native/native_gamepad_controller.h`
- `native/controller_native/control_frame.h`
- `native/mouse_native/mouse_relay_contract.h` on `codex/mouse-controller-foundation-20260815`
Supersedes: none
Superseded by: none

## Context

The mouse work had expanded into global frame-motion calibration, a broad
transport safety supervisor, and rollout stages. The user corrected that scope:
the existing Vision path already recognises a person target, the range provides
a stationary dummy for calibration, and the required safety outcome is a
reliable way to release the mouse and close the application. Planning should be
organised by implementation modules and ownership, not by rollout stages.

The current controller already owns target selection/co-ordination, ADS,
BodyLock, shaping and final aim arbitration. Mouse support should adapt device
units around that controller rather than create another controller or response
curve stack.

## Decision

- Mouse control will use thin device adapters around the current production
  controller:

  ```text
  physical mouse counts
  -> MouseRateAdapter
  -> current NativeGamepadController ADS/BodyLock/final-T
  -> MouseActuatorAdapter
  -> final virtual mouse counts
  ```

- The internal normalised controller coordinate is an implementation unit only;
  the product remains physical mouse input plus virtual mouse output and does
  not enumerate or output a virtual gamepad.
- Hipfire and ADS sensitivity are calibrated against one stationary range
  dummy already recognised by Vision. A hotkey records the current same-target
  person position, emits a known small virtual-mouse count, observes the new
  position for the same target generation, calculates pixels per count, and
  emits the reverse count to return near the starting point.
- The right-mouse-button state selects the in-memory calibration slot: released
  calibrates hipfire; held calibrates ADS. Calibration results are process-local
  and are not written to files, the registry, or persistent profiles.
- No global image registration, optical flow, online response learner,
  game-specific mouse response curve, new PID controller, second TargetPlan, or
  second final-arbitration policy belongs in the initial mouse implementation.
- The safety contract is intentionally small. Keyboard input remains outside
  mouse interception. An independent emergency-key watcher must first disable
  mouse interception and restore physical mouse delivery, then close the
  application.
- If an actual kernel mouse filter owns interception, a minimal heartbeat
  timeout that restores physical delivery when the application crashes or
  stops responding is an AI-inferred implementation requirement for the same
  user-requested safe-exit outcome. It is not permission to add a broad VHF,
  PnP, multi-device or rollout supervisor before a concrete need exists.
- Work plans for this feature will be decomposed by responsibility (adapter,
  controller facade, calibration, input/output connection, emergency exit and
  regression tests). Relay, shadow and active modes may be runtime settings or
  validation conditions, but are not the architecture or primary task plan.

## Reasons

- Vision already supplies the person position and target generation needed for
  range calibration, so global scene-motion analysis would duplicate available
  evidence.
- A stationary range dummy makes a known-count before/after measurement
  sufficient for the requested hipfire and ADS calibration.
- Mouse input normally needs a linear count-to-view scale rather than the
  game-specific nonlinear stick curves that complicate gamepad adaptation.
- Reusing the production controller preserves its existing ADS, BodyLock,
  target lifecycle, manual-intent and final-output incident fixes.
- Process-local calibration avoids stale per-game configuration and keeps the
  first implementation small; recalibration is an explicit user action.
- A keyboard escape path plus automatic release on loss of a filter owner
  directly addresses the lockout risk without moving controller logic into the
  driver.

## Rejected Alternatives

- Full-frame block matching or optical flow for calibration: unnecessary while
  a stationary, same-generation person target is available in the range.
- Persistent per-game, per-scope mouse profiles: rejected for the initial
  implementation; the user requested memory-only calibration.
- Reusing or learning gamepad response curves for mouse: rejected because mouse
  adaptation should absorb sensitivity into a linear actuator scale.
- A separate mouse PID, target plan or final-total solver: rejected as duplicate
  controller ownership.
- A comprehensive FailOpen/PnP/multi-device safety architecture before the
  requested escape path exists: rejected as over-design. Only the minimal
  release mechanisms required to prevent lockout are in scope.
- Describing implementation as sequential rollout stages: rejected as the
  primary planning model; module boundaries and contracts are the plan.

## Evidence

- User-confirmed on 2026-08-19: calibration can be performed once against a
  range dummy because Vision already recognises the person position.
- User-confirmed on 2026-08-19: the required safety result is an exit that can
  safely close the application in buggy or exceptional conditions, not an
  upfront exhaustive failure architecture.
- User-confirmed earlier in the mouse discussion: the mouse interception demo
  passed its physical-input blocking and keyboard-close validation.
- Repository evidence: `NativeGamepadController` accepts a
  `PhysicalGamepadState`, and `ControlFrame` exposes the resolved pre-recoil
  stick command before the gamepad-specific ViGEm output boundary.
- Repository evidence: the mouse foundation snapshot and current controller
  branch overlap only in `native/vision_native/CMakeLists.txt`; the latest
  controller can be brought into the mouse branch without rebuilding the mouse
  design from scratch.

## Consequences

- The first implementation remains a small set of device adapters and a range
  calibrator around the existing controller rather than a second control stack.
- Calibration must reject target replacement, missing Vision observations or a
  failed virtual-count probe; an invalid slot must not grant AI output.
- Restarting the process discards hipfire and ADS calibration and requires the
  user to recalibrate before assisted mouse output is enabled.
- Initial calibration may use one X/Y scale if the target game exposes one
  mouse sensitivity. Separate vertical or scope slots are added only after an
  observed need.
- The emergency release path must not depend on the Vision/controller tick.
  When a kernel filter is present, owner/heartbeat loss must restore physical
  mouse delivery even if normal shutdown code cannot run.
- Existing additive `physical + correction` mouse relay semantics must not
  become the controller output contract; the actuator produces the one final
  X/Y report.

## Review Triggers

- Range-dummy movement or detector jitter prevents a stable same-target
  pixels-per-count estimate.
- A target game applies demonstrably nonlinear raw-mouse acceleration or
  distinct vertical, ADS or per-scope sensitivity that one hipfire/ADS pair
  cannot represent.
- The current interception mechanism cannot release the physical mouse after a
  process hang or crash using the minimal emergency-key/heartbeat contract.
- Mouse-adapted normalised inputs fail parity against the current production
  controller's ADS or BodyLock incidents.
- Implementation evidence shows that a proposed omitted module is required for
  correctness or safe shutdown.
