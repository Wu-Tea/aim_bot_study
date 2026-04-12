# Gamepad Controller Plugin Refactor Design

**Goal:** Refactor `controllers.gamepad_controller.GamepadController` so the main loop keeps only base gamepad passthrough responsibilities while enhancement behavior is moved into composable plugins.

**Primary outcome:** the main loop should only read physical input, build a frame snapshot, invoke plugins, and write the final virtual gamepad output.

**Non-goals:**
- No tuning changes to aim feel, recoil strength, fire timing, or deadzone parameters
- No runtime plugin discovery or dynamic loading
- No behavior changes to the physical-to-virtual passthrough layer
- No event bus or generalized hook system

## Architecture

The refactor introduces a two-level plugin structure:

1. `GamepadController` remains the host and owns device I/O, thread lifetime, shared cross-thread signals, and default plugin assembly.
2. Controller-level plugins implement discrete enhancement features such as AI aiming, automatic fire, and recoil compensation.
3. `AIAimPlugin` owns its own internal sub-plugin chain for right-stick AI algorithms such as horizontal assist, overshoot limiting, and future motion prediction.

This keeps the host small without flattening all feature logic into one plugin namespace. Controller-level plugins operate on controller output. AI sub-plugins operate only inside the AI aim pipeline.

## Host Responsibilities

`GamepadController` keeps only these responsibilities:

- initialize and own the physical joystick and virtual gamepad
- read the current physical state each loop iteration
- expose cross-thread signal setters such as `update(dx, dy)` and `set_auto_fire(pressed)`
- build a per-frame input snapshot
- create the base output from raw passthrough mapping
- invoke controller-level plugins in a fixed order
- write the final output to the virtual gamepad

The host must not contain feature-specific enhancement logic after the refactor.

## Controller-Level Plugins

The first controller-level plugins are:

- `AIAimPlugin`
- `AutoFirePlugin`
- `RecoilCompensationPlugin`

Each plugin owns its own state and exposes a minimal interface:

- `reset()`
- `apply(frame, output)`

`frame` is read-only input for the current iteration. `output` is the mutable virtual-controller result being built for the current iteration.

## AI Aim Internal Plugins

`AIAimPlugin` owns a second plugin chain for AI right-stick logic. The initial sub-plugins are:

- horizontal aim assist
- overshoot guard

Future features such as motion prediction, catch-up boost, or additional overshoot constraints are added here instead of becoming controller-level plugins.

This boundary is intentional:

- controller-level plugins decide how controller output is modified
- AI sub-plugins decide how AI right-stick correction is generated and constrained

## Frame and Output Objects

The host builds two structured objects each loop:

### `GamepadFrame`

Read-only snapshot of the current state, including:

- timestamp
- physical left/right stick values
- physical trigger values
- physical button states
- current aiming state
- latest AI target delta
- latest automatic-fire request signal

### `GamepadOutput`

Mutable output state sent to the virtual gamepad, including:

- left/right stick outputs
- trigger outputs
- button outputs
- any derived per-frame flags needed by later plugins

The host initializes `GamepadOutput` from base passthrough mapping before plugins run. Plugins then modify the output in place.

## Call Order

Controller-level plugins run in this fixed order:

1. `AIAimPlugin`
2. `AutoFirePlugin`
3. `RecoilCompensationPlugin`

Rationale:

- AI aim must update right-stick output before any fire-dependent behavior runs.
- Automatic fire must determine whether this frame is actually firing.
- Recoil compensation must run after automatic fire so it can respond to effective fire state, not only to the raw request signal.

The first stage does not support dynamic reordering. Keeping a fixed order reduces ambiguity and makes regression checking simpler.

## Automatic Fire

The existing `auto RB` behavior is generalized into `AutoFirePlugin`.

`AutoFirePlugin` receives a configuration value:

- `fire_output="RB"`
- `fire_output="RT"`

Behavior:

- If `fire_output="RB"`, automatic fire is OR-combined with the physical RB button state.
- If `fire_output="RT"`, automatic fire drives the virtual RT output to its firing value while preserving physical passthrough when inactive.

For compatibility, `GamepadController.set_auto_rb()` remains as a thin alias to `set_auto_fire()` during the refactor.

## Reset and State Ownership

State ownership moves fully into plugins.

- `GamepadController.reset()` broadcasts `reset()` to all controller-level plugins.
- `AIAimPlugin.reset()` broadcasts `reset()` to its internal AI sub-plugins.
- The host does not directly clear plugin-specific state fields.

This avoids hidden coupling where the host must know how each enhancement feature stores its internal memory.

## Threading Model

`GamepadController` remains a threaded host.

Cross-thread methods such as `update(dx, dy)` and `set_auto_fire(pressed)` only update shared input signals under lock. They do not invoke plugin logic.

All plugin state transitions occur on the main loop thread during `apply(frame, output)`.

This keeps plugin logic single-threaded and prevents partial state updates from spreading across helper methods.

## Configuration Extension Point

The first stage does not add a full user-facing configuration system, but the construction path should allow one later.

Required extension points:

- `GamepadController` can accept an explicit controller-level plugin list, with the current stack as the default
- `AutoFirePlugin` accepts `fire_output`
- `AIAimPlugin` can accept an explicit internal sub-plugin list, with the current AI stack as the default

This allows future enable/disable and ordering configuration without another interface rewrite.

## Error Handling

- If no plugins are supplied, the host still functions as a pure passthrough controller
- Plugin reset failures should not silently corrupt host state; failures should surface clearly during development
- Missing optional behavior should degrade to passthrough, not to malformed controller output

## Testing

The refactor should preserve current behavior through targeted tests:

- controller host tests for plugin ordering and signal propagation
- `AutoFirePlugin` tests for RB and RT output modes
- `AIAimPlugin` integration tests to confirm existing horizontal assist and overshoot guard behavior is preserved
- regression checks that passthrough mapping still works when enhancements are disabled or inactive

## Migration Notes

The refactor is structural. Existing tuning constants and feature semantics remain unchanged unless a separate follow-up task explicitly changes them.
