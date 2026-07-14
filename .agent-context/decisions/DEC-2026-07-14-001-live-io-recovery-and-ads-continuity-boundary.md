# DEC-2026-07-14-001: Live I/O Recovery and ADS Continuity Boundary

Status: proposed
Date: 2026-07-14
Confirmed by: user requested that the reproduced findings be recorded and repaired
Related sessions: 2026-07-14 live native runtime telemetry investigation
Related files:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `native/controller_native/sdl_gamepad_reader.cpp`
- `native/controller_native/virtual_gamepad.cpp`
- `native/controller_native/assist_authority_policy.cpp`
- `native/controller_native/target_snapshot_provider.cpp`
- `native/runtime_app/runtime_loop.cpp`
Supersedes: none
Superseded by: none

## Context

Four live native-runtime sessions on 2026-07-14 reproduced complete loss of physical control until restart. Each log contains a 0.53-0.77 second runtime/sample stall followed by a physical input tuple that never changes again before process termination. The current SDL reader treats a non-null joystick handle as permanently available, reports the device connected unconditionally, and has no detach or reopen path.

The same logs also reproduce large bidirectional ADS vertical jumps while production vision reports no target. Every material jump is attributed to tracker `continuity` with reason `short_evidence_gap`. Tracker age reaches the 96ms continuity boundary while ADS retains full `assist_scale=1.0` and a vertical maximum force of 1.0.

## Decision

- Native physical input and virtual output become explicit health-managed boundaries rather than fire-and-forget calls.
- SDL attachment loss must be observable and recoverable without restarting the process.
- ViGEm update failures must be checked, logged, and recovered with bounded retry/reinitialization.
- ADS and bodylock must not share identical tracker-only control authority.
- Bodylock may retain benchmarked short-occlusion tracker continuity.
- ADS without current observed target evidence must not retain full-strength snap. Tracker continuity may preserve identity/position for reacquisition, but its ADS control output must fail closed or remain mechanically bounded so it cannot create a visible vertical jump.

## Reasons

- Restart-only recovery is caused by stale external-resource handles, not controller tuning.
- Existing telemetry cannot currently distinguish SDL detach, ViGEm rejection, and successful report delivery.
- Full-strength ADS output on tracker-only evidence violates the accepted rule that tracker memory is continuity rather than truth.
- Globally shortening tracker lifetime would damage bodylock slide, jump, and occlusion behavior that previous benchmarks intentionally preserve.

## Rejected Alternatives

- Ask the user to restart after every detach: it leaves a reproduced runtime defect in place.
- Only reduce global tracker projection age: this couples ADS safety to bodylock continuity and risks returning bodylock dropout/chatter.
- Globally reduce ADS Y force: it hides tracker-only jumps by weakening legitimate observed-target acquisition.
- Treat `output_sent_ns` as proof of ViGEm delivery: current code records the timestamp even when the update result is ignored.

## Evidence

- Same-day input freezes after stalls of `575.6ms`, `531.3ms`, `774.8ms`, and `750.9ms`.
- Zero physical-input changes after those stalls for `6.5s`, `50.8s`, `15.2s`, and `17.3s` respectively.
- Tracker-only ADS Y peak output of `-1.06677` and `+1.03637` while production vision has no target.
- All material no-production-target ADS Y frames use `assist_authority=continuity` and `assist_authority_reason=short_evidence_gap`.
- Peak frames have no material dynamic, ADS brake, carry brake, or recoil contribution.

## Consequences

- Add an injectable/testable SDL device-health boundary or equivalent adapter so detach/reopen behavior is covered without requiring physical hardware in tests.
- Add ViGEm update result and reconnect state to runtime health telemetry.
- Add a controller benchmark reproducing observed target loss into tracker coast during ADS and asserting bounded/no assist.
- Re-run bodylock occlusion, continuity, random FOV, selector, ROI, vision, scheduler, telemetry, and runtime pipeline verification before integration.

## Review Triggers

- Device recovery selects the ViGEm output device as physical input.
- Reconnect attempts create controller duplication or button/stick discontinuities.
- ADS target-loss bounding causes visible fresh-frame flicker or slow reacquisition.
- Bodylock occlusion or sustain metrics regress relative to the accepted A0/refactor baseline.
