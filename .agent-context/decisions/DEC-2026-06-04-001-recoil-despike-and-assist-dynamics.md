# DEC-2026-06-04-001: Recoil Despike And Assist Dynamics

Status: accepted
Date: 2026-06-04
Confirmed by: user
Related sessions:
- 2026-06-04T23:42:57+08:00
Related files:
- controllers/gamepad/recoil_compensation.py
- controllers/gamepad/ai_aim.py
- controllers/gamepad/plugin.py
- tests/gamepad/test_gamepad_recoil_compensation.py
- tests/gamepad/test_gamepad_ai_aim_plugin.py
- tests/gamepad/test_gamepad_plugin_chain.py
Supersedes: none
Superseded by: none

## Context

The native vision and gamepad aim-assist pipeline became strong enough that the remaining live feel problem shifted from detection reliability to camera smoothness. The user clarified that some weapons with recoil enabled make the camera feel shaky. The recoil parameters have been tuned carefully over time, so the goal is not to change recoil strength, timing, or weapon feel. The goal is to remove obvious curve spikes from recorded recoil playback and make AI aim-assist movement smoother in the plugin-chain architecture.

## Decision

Implement two separate follow-up work items:

- Recoil playback should generate a conservative despiked playback cache when a recoil profile is read or activated. The original profile on disk must remain unchanged. The despike pass should operate on incremental profile deltas and only repair obvious local spikes or recording artifacts, preserving the overall recoil curve and tuned compensation feel.
- AI aim assist should get a separate `AimAssistDynamicsPlugin` in the gamepad plugin list after `AIAimPlugin` and before recoil playback. The plugin should smooth only the AI assist delta, computed as `output.right_stick - frame.manual_right_stick`, then recompose `manual + smoothed_assist`. It must not delay user manual input and must not smooth final output after recoil compensation.

## Reasons

- Recoil curve noise is best handled at profile read/activation time because playback currently maps adjacent sample differences directly into stick output; small profile spikes can become visible camera jitter.
- Despiking only suspicious local deltas avoids changing the tuned weapon feel, unlike broad low-pass or EMA smoothing over the whole curve.
- A separate assist-dynamics plugin matches the existing plugin-list architecture better than expanding `AIAimPlugin` with more mixed responsibilities.
- Smoothing only the assist delta preserves immediate manual right-stick input.
- Placing assist dynamics before recoil preserves recoil compensation timing and strength.

## Rejected Alternatives

- State-heavy recoil-aware aim damping inside `AIAimPlugin`: rejected because it adds complex fire/recoil state coupling when the desired fix is simpler output shaping.
- Smoothing final gamepad output after all plugins: rejected because it would delay manual input and alter recoil output that the user already tuned.
- Broad low-pass smoothing of recoil cumulative samples: rejected because it could soften the whole recoil curve and shift weapon timing.
- Overwriting recoil profile files with smoothed data: rejected because raw recordings should remain available for reprocessing, comparison, and future tuning.
- Embedding all assist inertia inside `AIAimPlugin`: rejected for the first pass because a plugin-level delta smoother is easier to A/B test and keeps boundaries cleaner.

## Evidence

- User statement: some weapons with recoil enabled cause shaky camera feel.
- User statement: current recoil parameters were tuned for a long time and should not have their overall feel changed.
- Repository fact: `RecoilCompensationPlugin` derives per-frame stick output from adjacent recoil profile sample deltas.
- Repository fact: gamepad plugins mutate a shared `GamepadOutput` sequentially, so plugin placement determines whether smoothing affects manual input, AI assist, recoil, or all of them.
- User accepted the split direction: recoil curve despike plus AI assist dynamics plugin.

## Consequences

- First implementation should add tests proving that non-spiky recoil curves are unchanged or nearly unchanged, obvious single-frame spikes are reduced, total recoil remains close to the original, and disabling despike restores old playback exactly.
- `AimAssistDynamicsPlugin` tests should prove that pure manual input is unchanged, sudden AI assist changes are ramped, assist release is smoother, and recoil output remains outside the dynamics layer.
- Tuning should start conservatively; live feel should decide whether despike thresholds or assist ramp/brake rates need adjustment.

## Review Triggers

- Recoil feels weaker, delayed, or different on weapons the user had already tuned.
- Assist feels sluggish during ADS snap or loses short-window acquisition effectiveness.
- Recoil output still visibly jitters after conservative despike.
- Plugin ordering changes so assist dynamics would accidentally smooth recoil or manual input.
