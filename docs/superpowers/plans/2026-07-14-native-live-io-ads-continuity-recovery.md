# Native Live I/O and ADS Continuity Recovery Plan

Date: 2026-07-14

## 1. Freeze the failures as tests

- Add an SDL recovery policy/device-selection test that fails because attachment and reconnect behavior do not exist.
- Add a virtual-output health test that fails because report success is currently discarded.
- Add an ADS controller regression using a valid observed target followed by a no-target production snapshot while tracker continuity remains alive.

## 2. Implement physical input health

- Load and call `SDL_JoystickGetAttached`.
- Distinguish handle availability from attachment.
- Add bounded reconnect timing and exact physical-device selection.
- Integrate recovery into `RuntimeLoop::read_physical_gamepad` without replaying stale values.
- Add connection/recovery diagnostics.

## 3. Implement virtual output health

- Return a structured update result from `VirtualGamepad::update`.
- Check ViGEm return codes, reconnect with bounded cadence, and retry the current report once after recovery.
- Record delivery only when the update succeeds and expose health in diagnostics/telemetry.

## 4. Split ADS control evidence from tracker continuity

- Carry latest production-observation presence through `NativeControllerVisionState`.
- Require that presence for ADS strong control while leaving tracker/bodylock continuity intact.
- Keep the field stable across repeated controller ticks for one vision frame and clear it on the next no-target production frame.

## 5. Verify and integrate

- Run focused red/green tests after each change.
- Run all native CTest targets and applicable Python tests.
- Run controller, bodylock, tracker, random-FOV, telemetry, scheduler, pipeline, and native vision benchmarks.
- Compare with the accepted A0 and ADS predictive-brake artifacts; tune only if a regression is demonstrated.
- Update `.agent-context`, commit the verified branch, and merge to local `dev` only after acceptance passes.
