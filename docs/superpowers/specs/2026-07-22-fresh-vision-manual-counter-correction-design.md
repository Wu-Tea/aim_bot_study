# Fresh-Vision Manual Counter-Correction Design

## Purpose

Allow the existing vector intent fuser to suppress a sub-escape, clearly
wrong-way manual component more strongly when a fresh, reliable, single-target
vision observation establishes a trustworthy error direction. Tracker-only,
coasting, occluded, reacquiring and multi-target states retain the current
fusion behavior.

## Architecture

`NativeGamepadController` emits one extra boolean input to the existing
`VectorIntentFuser`: a fresh observation pulse that is true only when the
current controller batch contains exactly one valid candidate and the committed
plan is observed and reliable. The fuser owns a short evidence envelope covering
one normal 80–100 Hz vision interval; it expires without another pulse.

Inside the current radial/tangential candidate system, fresh evidence may select
one new counter-correction candidate. It preserves tangential manual input,
retains a configurable floor of the wrong radial input, and gives the already
shaped AI vector full weight. It does not create another brake, controller,
lifecycle, tracker or output stage.

## Eligibility

Strong counter-correction requires all of:

- a fresh single-candidate observation pulse;
- plan lifecycle `Observed` at the pulse;
- reliability at least `0.85`;
- response confidence sufficient for the existing fuser (`>= 0.35`);
- target error magnitude at least `6 px`;
- manual radial projection is wrong-way while shaped AI is closing;
- manual magnitude remains below the existing escape threshold.

The envelope lasts `16 ms`. Without another pulse it expires and candidate
selection becomes byte-for-byte the existing path. New target, reacquisition,
no target and manual escape clear it immediately.

## Configuration

Add one setting under `[gamepad.intent]`:

```toml
fresh_vision_wrong_way_manual_floor = 0.20
```

`1.0` disables the stronger correction. The value affects only the radial
wrong-way component; tangential manual input remains at weight `1.0`.

## Acceptance

- Fresh single-target wrong-way fixtures reach the configured radial floor with
  full shaped-AI weight and preserved tangent.
- Identical coasting/tracker-only and multi-target fixtures retain the previous
  candidate and weights.
- Deliberate diagonal manual escape remains exact physical input.
- Weight transition is continuous and does not add output discontinuities.
- Three-seed 60-second ADS/BodyLock mixed-input A/B improves closing/tracking
  burden without worsening false interruptions, direction discontinuities or
  escape behavior.
- Existing registered native tests remain green.
