# Native Live I/O and ADS Continuity Recovery Design

Date: 2026-07-14
Status: approved for implementation

## Problem

The native runtime has two independently reproduced live failures.

1. After a 531-775 ms controller/runtime stall, SDL physical input becomes a constant stale sample until the process is restarted. The SDL reader does not inspect joystick attachment state or reopen a detached device. ViGEm report failures are also ignored, so the output boundary cannot prove delivery.
2. When a new production vision frame has no target, the tracker can retain `short_evidence_gap` continuity for about 96 ms. ADS currently treats that continuity like observed evidence and can emit approximately full-scale vertical snap even though no live target exists.

## Behavioral Boundaries

### Physical input

- `available` means the SDL backend and handle exist; `attached` means SDL confirms the handle still represents a connected device.
- A detached SDL handle returns a disconnected neutral state and starts bounded reconnect attempts.
- Reconnect prefers the original physical device identity and must not silently select the ViGEm virtual output.
- While reconnecting, the runtime may use a genuinely connected XInput physical source, but never replay the last SDL sample.
- Successful reconnect resets trigger initialization and reports a recovery event.

### Virtual output

- Every ViGEm update result is checked.
- A failed report is not recorded as successfully delivered.
- The backend performs bounded reconnect attempts and retries the current report after recovery.
- Health state exposes delivery success, error code, and reconnect count for logs/telemetry.

### ADS versus bodylock continuity

- Tracker memory continues to preserve target identity and predicted position for reacquisition and bodylock short occlusions.
- ADS strong control additionally requires the latest production vision snapshot to contain an observed target.
- Once a new no-target production snapshot arrives, tracker-only continuity may remain internally available but ADS AI output must stop. Manual input remains untouched.
- The gate is based on current snapshot presence, not `fresh_observation`, because the 1 kHz controller consumes one 160 Hz vision frame across several ticks.
- Bodylock continuity policy and its 96 ms projection window are not globally shortened.

## Test Strategy

- Pure SDL device-selection and reconnect-policy tests cover detach, bounded retry, exact-name recovery, and rejection of a virtual-output-only candidate.
- ViGEm health-policy tests cover success, failed delivery, reconnect scheduling, and successful recovery.
- Controller regression reproduces observed ADS target -> production no-target -> tracker continuity and asserts zero ADS assist while preserving manual stick output.
- Existing bodylock occlusion and tracker continuity benchmarks must remain near the accepted baseline.
- Full native tests, Python tests, controller benchmarks, telemetry/scheduler benchmarks, and native vision tests/benchmarks are rerun before integration.

## Acceptance

- No stale SDL sample is reported as connected after SDL attachment loss.
- The runtime can recover a matching physical SDL device without restart.
- ViGEm failures are observable and do not masquerade as successful delivery.
- The reproduced no-production-target ADS scenario produces no AI Y jump.
- Existing observed-target ADS acquisition/predictive-brake and bodylock continuity metrics remain within their established acceptance limits.
- The full configured test and benchmark suite passes.
