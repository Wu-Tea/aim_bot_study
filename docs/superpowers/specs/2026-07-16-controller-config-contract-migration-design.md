# Controller Configuration Contract Migration

Date: 2026-07-16
Status: approved design, pending implementation review

## Purpose

The controller rewrite introduced `TargetCoordinator`, `AdsAcquisitionController`, and
`BodylockFollowController`, but the configuration contract was not migrated completely.
Several legacy `GamepadAiAimConfig` fields are still parsed and exercised by legacy tests
while the live controller no longer consumes them. In the other direction, benchmark
scenarios silently replace values loaded from `config.toml`, so some reported results do
not describe the user's live profile.

This migration establishes one canonical configuration path for the live controller,
keeps legacy names as temporary compatibility aliases, and makes benchmark overrides
explicit and auditable.

## User-visible outcome

The normal profile will have four clear owners:

```toml
[gamepad.tracker]
backend = "fps_reference"
projection_age_ms = 96
max_velocity_px_per_sec = 1200
lead_seconds = 0.026
lead_max_px = 18
aim_height_ratio = 0.365

[gamepad.ads]
strength_scale = 1.54
vertical_strength_scale = 1.26
range_px = 150
snap_duration_ms = 160

[gamepad.bodylock]
strength = 0.45
vertical_strength = 0.50
activation_range_px = 80
tolerance_px = 8
manual_escape_threshold = 0.45
manual_escape_preservation = 0.55
```

Tracker owns target identity, target geometry, prediction, and short-term continuity.
ADS owns acquisition response. BodyLock owns follow response and manual escape. Dynamics
remains the single output shaper after either controller.

The values above are the user-selected candidate profile for comparison. The migration
must not silently rewrite them or any later user-selected values.

## Canonical configuration model

Each public configuration field must map directly to one active production consumer.
The canonical runtime structures are component-oriented rather than retaining one large
legacy aim structure:

- `TrackerRuntimeConfig`: target association, projection, lead, and geometry.
- `AdsAcquisitionControllerConfig`: acquisition force, response range, and duration.
- `BodylockFollowControllerConfig`: follow force, activation/settle ranges, and manual
  escape policy.
- `GamepadAimAssistDynamicsConfig`: the one post-controller shaping stage.

Parsing may initially populate an aggregate runtime configuration, but construction of
the live components must use only these canonical component contracts. A field that has
no active consumer must not remain in the documented canonical schema.

## Compatibility policy

Legacy keys remain readable during the migration window. They are aliases, not a second
source of truth.

Precedence is deterministic:

1. Canonical `[gamepad.*]` key.
2. Legacy `[runtime.gamepad]` alias.
3. Profile/default value.

If a canonical key and its legacy alias both appear, the canonical value wins and the
effective-config dump reports the legacy key as ignored/deprecated. A legacy-only key is
translated once into the canonical field and reported as a deprecated source.

Compatibility aliases must be represented in a centralized alias table or equivalent
single mapping layer. Controllers and benchmarks must never branch on whether a value
came from a canonical or legacy spelling.

The first implementation will classify every legacy field as one of:

- `alias`: still meaningful and mapped to a canonical live field;
- `internal`: required only by a retained non-public helper and not documented as a live
  tuning control;
- `removed`: obsolete after the single-controller rewrite and rejected with a clear
  configuration error or deprecation diagnostic according to existing compatibility
  requirements.

No new compatibility gate is added to the per-tick controller path.

## Target geometry

`gamepad.tracker.aim_height_ratio` is the canonical geometric lock position. Its valid
range is `0.0..1.0`; the candidate baseline is `0.365`.

For an upright target with a valid body box:

```text
aim_y = body_top + body_height * aim_height_ratio
```

The adjustment occurs once when a fresh observation becomes a tracker candidate, before
`TargetCoordinator` stores or predicts the point. ADS and BodyLock therefore consume the
same `TargetPlan.aim_px`; neither controller applies an additional vertical offset.

Geometry rules:

- Upright and crouched targets use the configured ratio.
- Wide/low targets (prone, downed, or otherwise non-upright) preserve the Vision-selected
  aim point, clamped to the valid body box.
- Candidates without a valid body box preserve the Vision-selected aim point.
- Coasting and occlusion prediction advance the already-resolved geometric point and
  never apply the ratio again.
- Reacquisition resolves geometry from the new fresh observation, then uses the existing
  bounded innovation policy to prevent a jump.

The legacy `body_lock_upper_body_ratio` becomes a compatibility alias for
`gamepad.tracker.aim_height_ratio`. Its old name must not imply that BodyLock owns a
different target from ADS.

## ADS and BodyLock transition semantics

The migration preserves one target plan and makes the configuration names describe the
current coordinator behavior:

- `gamepad.ads.snap_duration_ms` supplies the acquisition time used by the coordinator.
- `gamepad.bodylock.tolerance_px` supplies the settle radius. The existing exit behavior
  based on a multiple of this radius remains explicit and covered by tests.
- `gamepad.bodylock.activation_range_px` is the maximum error for timed fallback from ADS
  to BodyLock; it is not a detection radius.
- ADS `range_px` controls horizontal error-to-output slope and is not an activation gate.

The effective-config dump and example configuration must describe these semantics so
that similarly named pixel values cannot be confused.

## Benchmark configuration pipeline

Benchmark execution is split into two explicit modes.

### Profile-faithful mode

Profile-faithful scenarios use the effective configuration loaded from the supplied
`config.toml`. They may set simulated observations, motion, timing, and input sequences,
but must not replace controller tuning values.

This is the default mode used to establish and compare live baselines.

### Stress-fixture mode

Stress scenarios may override controller tuning only through a named fixture. Every
override must be recorded in the JSON artifact with:

- field name;
- loaded value;
- override value;
- fixture/reason;
- scenario names receiving the override.

The console summary must identify the run as stress-fixture rather than profile-faithful.
Anonymous in-function mutations such as `max(config.value, constant)` or direct
assignment are removed from scenario bodies.

Shared scenario setup accepts an immutable effective configuration plus an optional
explicit override object. The benchmark artifact records the effective values relevant
to Tracker, ADS, BodyLock, and Dynamics even when no override is present.

Historical result files remain historical evidence; they are not rewritten. Comparisons
must reject or flag artifacts whose configuration/override metadata is incompatible.

## Configuration audit and removal

Implementation begins with a generated or test-driven inventory of every public gamepad
field:

- parser spelling and section;
- default value;
- canonical destination;
- live production consumer;
- benchmark consumer/override;
- compatibility status.

The audit is a required implementation artifact. Fields used only by the retired
`NativeAiAim` path must not be kept public merely because old unit tests mention them.
Tests for retired behavior are removed or rewritten against the canonical components.

Removal is limited to controller configuration and benchmark setup. Recoil configuration
is out of scope except where benchmark metadata must faithfully report it.

## Validation

The implementation is test-driven and must cover:

1. Canonical keys parse into the active component configuration.
2. Legacy aliases produce the same active configuration.
3. Canonical keys win deterministic conflicts with aliases.
4. Effective-config output reports canonical, alias, default, and ignored sources.
5. Every documented public key has an active production consumer.
6. Upright boxes resolve to `0.365` of body height.
7. Crouched boxes scale geometrically rather than using a fixed pixel offset.
8. Wide/low and missing-box observations preserve the Vision aim point.
9. Occlusion/coasting does not apply the height ratio repeatedly.
10. ADS-to-BodyLock handoff preserves identical `TargetPlan.aim_px`.
11. Profile-faithful benchmark results contain no controller parameter overrides.
12. Stress fixtures serialize every override and its reason.
13. Existing manual-intent, continuity, vertical, left-stick, AutoFire, and runtime config
    tests remain green.

The fixed-seed profile-faithful suite is run before and after migration. The migration is
accepted only if target geometry changes as intended, no new mode chatter or output spike
defect appears, and the artifact proves which effective configuration was tested.

## Non-goals

- Adding another ADS or BodyLock control stage.
- Adding weapon-specific configuration.
- Changing Vision inference or adding visual processing.
- Introducing a second target point for BodyLock.
- Retuning recoil.
- Rewriting historical benchmark artifacts.

## Rollout

The first release keeps compatibility aliases and adds deprecation/source reporting.
The canonical example configuration and benchmark pipeline switch immediately. Legacy
field removal from storage can follow only after the audit shows no required consumer and
the compatibility window is explicitly closed.
