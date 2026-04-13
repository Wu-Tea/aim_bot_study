# Gamepad Adaptive Delta Gain Design

**Goal:** Let `AIAimPlugin.ai_delta_gain` ramp up automatically when a target is clearly not being caught up to, so close-range tracking (≤ ~50px error, sustained movement) becomes stronger without raising the static baseline and over-gripping during normal aim.

**Non-goals:**
- No change to `max_pixels`, `max_ai_force`, or deadzone geometry.
- No change to `HorizontalAimAssist` (x-axis force-bonus path) or `OvershootGuard` contracts — this feature only adjusts the scalar applied to `target_dx/target_dy` *before* the sub-plugins run.
- No new tracking memory in `vision/` — this stays a controller-side state machine.

## Problem

In `controllers/gamepad/ai_aim.py:172`, every frame does:

```
target_dx = frame.target_dx * self.config.ai_delta_gain   # 0.7 baseline
target_dy = frame.target_dy * self.config.ai_delta_gain
```

At a 50px radial error the effective pixel delta fed to `_map_pixel_to_stick` is 35px, which maps to ~23% stick force at `max_pixels=150`. For a target moving consistently across the reticle, that's often not enough for the stick force to outrun target velocity, so the error stalls or slowly grows.

We already have prior art for "if error won't converge, temporarily push harder":

- `HorizontalAimAssist.catchup_bonus` (`controllers/gamepad/horizontal_assist.py:76-83`) raises the x-axis `max_ai_force` cap when error direction persists and isn't shrinking.

The gap: catchup_bonus raises the *cap*, but if the desired stick value is already well below the cap (small-error case), lifting the cap does nothing. What's actually needed at small errors is a larger *input* to the pixel-to-stick mapping — i.e. a higher effective `ai_delta_gain`.

## Approach

Introduce a new sub-plugin `AdaptiveDeltaGainSubPlugin` that:

1. Observes each new target revision (`target_dx/dy`, `is_aiming`, `timestamp`).
2. Tracks per-axis whether the **absolute error is failing to shrink** while the AI is actively engaged.
3. Maintains a bounded multiplicative **gain bonus** `[0.0 .. max_bonus]` per axis.
4. Exposes the bonus through the `AIAimContext` so the outer plugin can apply it to the pre-scaled delta before sub-plugins that depend on `assist_dx/assist_dy` run.

Because the other sub-plugins read `context.assist_dx/assist_dy`, the adaptive gain sub-plugin must run **first** in the chain. That's a small ordering invariant, not an API change.

### State per axis

- `_prev_abs_error: float | None` — last observed `|target_d*|`.
- `_nonconverging_updates: int` — consecutive revisions where error didn't shrink by > ε.
- `bonus: float` — current multiplicative boost, starts at 0.

### Update rule (per observation, per axis)

```
if not is_aiming:                      # full reset
    reset()
    return

cur = abs(target_d_axis)

if cur < min_error_px:                 # close enough — do not accumulate
    decay()
    remember(cur)
    return

if prev is None:
    remember(cur); return

if cur >= prev - convergence_epsilon_px:
    # error is flat or growing
    _nonconverging_updates += 1
    if _nonconverging_updates >= trigger_frames:
        bonus = min(max_bonus, bonus + gain_per_update)
else:
    _nonconverging_updates = 0
    bonus = max(0.0, bonus - decay_per_update)

remember(cur)
```

### Apply rule (per frame)

```python
context.target_dx *= (1.0 + bonus_x)
context.target_dy *= (1.0 + bonus_y)
context.assist_dx  = context.target_dx
context.assist_dy  = context.target_dy
```

(Outer plugin must set `assist_dx/dy = target_dx/dy` **after** this sub-plugin runs, or the sub-plugin writes both in a single step. See "Integration" below.)

### Windup safety

Three gates prevent runaway bonus:

1. **Close-range gate** — below `min_error_px` the bonus decays instead of accumulating. Prevents stacking bonus when we're already on target and just jittering.
2. **Convergence release** — any observation that shrinks error by > ε resets the trigger counter and decays bonus. Single good frame is enough to start cooling.
3. **OvershootGuard coupling** — when `OvershootGuardSubPlugin.apply` returns a `x_desired_scale < 1.0` or `x_carry_scale < 1.0` (i.e. the guard is actively damping), the adaptive plugin forces an immediate bonus decay that frame. This ties the two systems: if the guard thinks we're overshooting/converging, we're not in a "can't catch up" state.
4. **Opposing manual stick** — if `|manual_rx| > opposing_input_threshold` and its sign opposes `target_dx` (user is actively pulling away), freeze accumulation on that axis. Same convention as `HorizontalAimAssist._has_opposing_manual_input`.

### Reset conditions

- `is_aiming` transitions to false.
- No target revision for > `stale_seconds` (detection dropout) — to avoid the bonus persisting across a reacquisition that may land on a different target.
- Sub-plugin `reset()` (target lost, pipeline reset, etc.).

## Integration

Ordering change in `AIAimPlugin.__init__` default `sub_plugins`:

```python
self.sub_plugins = tuple(sub_plugins) if sub_plugins is not None else (
    AdaptiveDeltaGainSubPlugin(),      # new — must run first
    HorizontalAssistSubPlugin(),
    OvershootGuardSubPlugin(),
)
```

Two options for where the multiplication lands:

- **Option A (preferred):** adaptive plugin mutates `context.target_dx/dy` and `context.assist_dx/dy` together. Outer plugin sets `assist_*` equal to `target_*` *before* the chain (unchanged from today), and the adaptive plugin rewrites both. Horizontal assist then adds `feedforward_dx` on top of the boosted `assist_dx`. Clean.
- **Option B:** outer plugin reads `context.delta_gain_bonus_x/y` after the chain and re-multiplies. Leaks the plugin's state into the outer plugin. Rejected.

Go with Option A.

Fixed `config.ai_delta_gain` stays as the **baseline** multiplier applied once in `AIAimPlugin.apply` (line 173-174). The sub-plugin layer adds a dynamic `(1 + bonus)` on top. Effective gain = `ai_delta_gain * (1 + bonus)`, capped at `ai_delta_gain * (1 + max_bonus)`.

## Config

New dataclass alongside the existing configs:

```python
@dataclass(slots=True, frozen=True)
class AdaptiveDeltaGainConfig:
    min_error_px: float = 6.0
    convergence_epsilon_px: float = 0.5
    trigger_frames: int = 3
    gain_per_update: float = 0.08
    decay_per_update: float = 0.12
    max_bonus: float = 0.6          # effective gain ceiling = 0.7 * 1.6 = 1.12
    opposing_input_threshold: int = 4500
    stale_seconds: float = 0.15
```

Decay is intentionally faster than accumulation — it's better to lose the bonus too fast than to overshoot.

Default `max_bonus=0.6` keeps the worst-case effective gain (`0.7 * 1.6 ≈ 1.12`) below the "trust the target delta literally" line, so we never scale errors upward.

## Testing

Pure unit tests in `tests/gamepad/test_gamepad_adaptive_delta_gain.py`:

1. **Baseline behaviour** — first observation, zero bonus; `apply` leaves `target_*` unchanged.
2. **Accumulation** — N consecutive observations with `|target_dx|` flat at 40px ⇒ after `trigger_frames` the bonus starts growing, capped at `max_bonus`.
3. **Convergence decay** — once error starts shrinking, bonus decays each revision toward 0.
4. **Close-range freeze** — error below `min_error_px` does not accumulate, and pre-existing bonus decays.
5. **Opposing input freeze** — opposing manual_rx past threshold holds accumulation on x only.
6. **Reset on aim release** — `is_aiming=False` clears bonus and counters.
7. **Stale target reset** — gap > `stale_seconds` between observations resets accumulator (even if `is_aiming` is still true, because target_revision hasn't advanced).
8. **Integration test** in the existing gamepad plugin-chain test file: a scripted frame sequence with persistent 40px x-error produces a larger `output.right_x` after several frames than it did on frame 1.

No changes to existing tests should be required — the sub-plugin is additive and starts with `bonus=0`.

## Rollout

1. Land the sub-plugin and config behind its defaults, but with `max_bonus=0.0` in `AIAimPlugin`'s default construction. This wires the code path without changing behaviour.
2. Playtest with `max_bonus` bumped via config injection, iterate on `gain_per_update` / `decay_per_update` / `trigger_frames`.
3. Once numbers settle, raise the default `max_bonus` to the tuned value and update `docs/project/GAMEPAD_OVERVIEW.md`.

## Open questions

- Should the bonus be **per-axis independent** or **radial**? Per-axis is cheaper to reason about and matches how `compute_axis_soft_strengths` already splits x/y. Going with per-axis unless playtesting shows diagonal targets under-tracking.
- Should we feed the bonus into `max_ai_force` as well as the delta? Probably not — `HorizontalAimAssist.catchup_bonus` already does that job for x, and lifting the y cap is a separate decision we haven't justified yet.
