# Mouse Telemetry Debugging

This note is for live mouse testing when aim assist feels like it has no effect,
moves in small steps, or gets stuck firing.

## What To Run

Use the debug launcher from the repo root:

```bat
scripts\launch\debug\mouse_native_debug.bat
```

The launcher does three things before and after the live run:

1. It probes the selected mouse injection backend.
2. It writes a run-specific CSV under `artifacts\mouse_telemetry\`.
3. It analyzes that exact CSV with `tools\analyze_mouse_telemetry.py --assert-healthy`.

If the injection probe fails, the launcher stops before starting the app. That
means Windows did not accept the selected backend at the cursor layer. Set
`MOUSE_PROBE_INPUT=0` only when intentionally bypassing that check.

## What The CSV Proves

The CSV is meant to separate the control chain into layers:

- `is_aiming`, `manual_left_pressed`, `physical_right_pressed`: button state reached the controller.
- `target_source`, `target_age_ms`: vision delivered a fresh target.
- `ai_phase`, `snap_rows` in the summary: the fast snap phase engaged.
- `output_dx`, `output_dy`: AI produced a command.
- `move_x`, `move_y`: command became integer mouse movement.
- `mouse_move_gap_p95_ms`: movement cadence is continuous enough for 120Hz-class control.
- `injected_echo_manual_rows`: manual takeover happened immediately after an injected move.
- `reverse_injected_echo_rows`: injected mouse movement was echoed back as opposite-sign manual input.
- `injection_errors`: Windows injection calls failed during the run.

## How To Read Failures

- `No aiming rows`: mouse button state did not reach the controller.
- `No controller target rows`: aiming reached the controller, but vision did not deliver targets.
- `snap phase never engaged`: target existed, but the fast定位 phase did not start.
- `AI output exists but no integer mouse move`: output scale is too small or stuck in fractional remainders.
- `large gaps`: movement is being emitted sparsely and will feel stepped.
- `recent injected move`: injected movement reached manual arbitration before suppression could consume it.
- `Synthetic mouse echo`: injected movement is being mistaken for user input and suppressing AI.
- `Mouse injection failures`: the input backend failed during live injection.

## Manual Analysis

To analyze a specific run:

```bat
py -3.11 tools\analyze_mouse_telemetry.py artifacts\mouse_telemetry\mouse-controller-YYYYMMDD-HHMMSS-debug.csv --assert-healthy
```

The goal is not just a passing test. A useful run should show aiming rows, target
rows, snap rows for far targets, nonzero output, nonzero integer moves, low target
age, and mouse move gaps that stay below the stepped-motion threshold.
