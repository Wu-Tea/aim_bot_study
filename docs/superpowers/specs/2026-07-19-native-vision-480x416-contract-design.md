# Native Vision 480x416 Runtime Contract

## Problem

The live native runtime currently reads `640x512` from `config.toml`, and its
tracked example configuration plus compiled fallback select the matching
`640x512` TensorRT engine. The intended live capture and the engine produced for
it are `480x416`. Because `config.toml` is local and untracked, copying or
regenerating it from the tracked example can silently restore the wrong shape.

## Contract

The default live-native vision path uses one consistent shape and engine:

- capture width: `480`
- capture height: `416`
- model: `models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine`

This contract applies to the local runtime configuration, the tracked native
example, the C++ runtime configuration fallback, and the VisionEngine fallback.
An explicit user configuration may still select another compatible shape and
engine.

## Scope

Change these runtime sources of truth:

1. `config.toml` for the user's immediately launched runtime.
2. `config.native.example.toml` so future config synchronization preserves the
   intended shape.
3. `native/controller_native/runtime_config.h` for missing-config fallbacks.
4. `native/vision_native/src/vision_engine.cpp` for direct VisionEngine fallback.

Do not mechanically replace `640x512` inside selector, telemetry, or benchmark
fixtures. Those values define independent synthetic coordinate systems and do
not control the live capture dimensions.

## Validation

Add or update focused tests that fail while any default runtime boundary still
selects `640x512`. Verify:

- an empty/default runtime configuration resolves to `480x416` and the
  `480x416` engine;
- the tracked example parses to the same values;
- an explicit alternate configuration remains honored;
- the selected engine file exists;
- native runtime config tests and the runtime build pass.

## Follow-up

After this contract is restored, implement the separately requested hidden
start/stop scripts. They must launch the canonical runtime using `config.toml`,
record a PID, and only stop the process when both PID and executable path match.
