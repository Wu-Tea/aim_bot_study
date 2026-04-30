# DEC-2026-04-30-003 COD22 Yellow-Dot Mixed Cue Acquisition

Status: accepted
Date: 2026-04-30

## Context

The native vision runtime now has a consolidated hot path with:

- ROI capture
- WarmScan / ActiveTrack scheduling
- TensorRT person detection
- shared grayscale derivation
- EgoMotionEstimator
- VisionTargetSelector
- BodyStateTracker
- CenterCueRefiner
- damping-only controller-facing enhancement

The previous center-cue work assumed a more generic yellow UI signal near the crosshair. For COD22 specifically, the user clarified that gameplay can be configured so enemies show a **single yellow dot above the head** instead of the more problematic blood-bar style marker.

Additional user-confirmed constraints:

- the yellow dot appears only on enemies
- the yellow dot is close enough to the head that both `x` and `y` are meaningful once fused with person geometry
- blood-bar compatibility is not required for this slice

At the same time, the yellow dot alone does not provide a full body box or torso geometry. That makes a pure cue-only controller target risky, especially before `person` / `body-state` confirmation is available.

## Decision

Use a **mixed acquisition strategy** for COD22 yellow-dot support:

1. support only the **single yellow dot** cue shape in this slice
2. when only the yellow dot is available, treat it as a **provisional seed**
3. provisional seeds may bias scan/search and aim-entry pickup, but do **not** become standalone confirmed controller targets or direct auto-fire triggers
4. once person detection / body-state attaches, fuse the yellow dot with that target and treat it as a real 2D cue for both `x` and `y`
5. keep controller-facing enhancement on the existing damping-only path

## Reasons

- preserves the user-requested benefit of using the enemy-only yellow dot as an acquisition channel
- avoids creating a second independent controller-facing target authority before person geometry is known
- reduces false horizontal drift compared with trying to support long yellow health-bar UI
- fits the current native runtime architecture better than replacing person/body-state with a cue-only target path

## Rejected Alternatives

- keep the yellow cue only as a late refiner after body-state, with no acquisition role
- use the yellow dot as a fully standalone controller target source before person/body-state confirmation
- spend this slice supporting both the yellow dot and the blood-bar UI at once
- reopen the older global cue interpretation instead of keeping the cue constrained to the COD22 yellow-dot semantics

## Consequences

- the native engine needs an internal provisional-cue state or equivalent scan/search bias path
- center-cue logic should evolve from generic yellow-pixel centroiding toward dot-shape gating
- some live tuning will still be needed to decide how aggressively provisional seeds influence aim entry and search windows
- blood-bar mode remains outside the preferred supported path for COD22

## Review Triggers

Revisit this decision if any of the following change:

- COD22 no longer exposes a stable enemy-only yellow dot configuration
- the yellow dot proves too unstable or too far from head geometry for fused 2D use
- controller behavior needs provisional cue seeds to become stronger than scan/search bias
- the project later decides to reintroduce broader yellow UI compatibility beyond the single-dot mode
