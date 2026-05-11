# COD Recoil App Auto-Learn Design

## Goal

Build a `recoil_app` package whose primary role is to be imported by the main gamepad runtime, while still optionally supporting a standalone debug startup path. The package should expose two modes:

- `record`
  - recognize weapons
  - auto-create identities
  - auto-learn recoil profiles
  - generate debug plots
- `recoil`
  - recognize weapons
  - load and cache existing profiles
  - drive recoil compensation with fallback behavior

The reusable runtime should:

- listens for `Y` weapon-switch input
- OCRs the current weapon name automatically
- auto-creates or reuses a weapon identity without manual data entry
- listens for `RT/RB` firing bursts
- auto-learns recoil curves for weapons that do not already have a stored profile
- exposes current weapon/profile state to the main project while keeping the existing controller plugin model intact

## Non-Goals

- do not move recoil-learning logic into `controllers/` or `vision/` hot paths
- do not require a live image-signature workflow
- do not require manual identity pre-registration as the normal path
- do not redesign the main controller plugin chain

## Constraints

- reuse existing OCR, recoil profile, and fire-edge logic where practical
- persistent assets remain on disk under `artifacts/`
- profile JSON remains the source of truth
- after a weapon is recognized, the active profile should be cached in memory for fast switching
- if a recognized weapon has no ready profile, runtime behavior falls back to the existing fixed `20%` pull

## Architecture

### 1. Function-first `recoil_app`

Create a dedicated `recoil_app/` package where the main deliverable is a reusable runtime object, not a separate always-on companion process.

Responsibilities:

- own the physical gamepad listener used for recoil-related events
- own OCR-based weapon-name recognition
- in `record` mode, own auto-upsert of identity records
- in `record` mode, own auto-learning sessions for missing recoil profiles
- own runtime state publication for the main project

Optional:

- a standalone CLI/debug startup may exist, but it is not required for normal AI-aim operation

### 2. Disk-backed, memory-cached profile store

Keep identity and recoil profile JSON on disk:

- `artifacts/recoil_app/weapons/identity-*.json`
- `artifacts/recoil_profiles/*.json`
- `artifacts/recoil_profiles/*.summary.json`

At runtime:

- once a weapon is recognized, load its matching profile(s) from disk into an in-memory cache
- later switches between cached main/offhand weapons should reuse the in-memory profile object instead of rereading JSON on every switch
- disk remains authoritative, but weapon switching should be memory-fast

### 3. State and integration boundary

`recoil_app` keeps its hot state in memory inside the current Python process. Debug snapshots may still be written to disk, but the main project should not depend on rereading profile JSON or an `active_profile.json` file on every switch.

`recoil_app` still publishes lightweight state concepts for observability:

- current weapon state
- active slot
- whether a ready profile exists
- whether fallback mode is active

The main project keeps the plugin model and consumes `recoil_app` through a thin adapter object.

### 4. Main project integration

The main project still uses the existing recoil plugin pattern:

- controller plugin asks for the current recoil profile
- if a ready profile is available, use profile-driven playback
- if not, keep the fixed fallback pull

Only minimal integration changes are allowed:

- instantiate a `recoil_app` runtime/bridge object once
- let the controller forward `Y` and fire-edge input into that object
- let the recoil plugin ask that object for the currently active cached profile
- keep the fixed fallback when no ready profile exists

## Recognition Flow

### Weapon switch recognition

When `Y` rises:

1. flip the active slot
2. capture a short OCR burst from the configured weapon-name ROI
3. pick the best OCR result
4. normalize the weapon name into `canonical_weapon_id = "<game>-<display_name>"`
5. auto-create or update the identity record if it does not exist
6. publish current slot + weapon + profile/fallback status

This is slot-driven, not continuous recognition.

### Fast swap safety

Reuse the already-proven dual-slot + `switch_epoch` model:

- each `Y` press increments `switch_epoch`
- stale OCR results may update cache, but may not overwrite a newer active slot
- rapid main/offhand swaps should remain stable

## Learning Flow

### Unknown-profile path

When a weapon is confirmed and no ready profile exists:

- publish fallback status immediately
- in `record` mode, arm an auto-learning session
- wait for firing input

### Fire-triggered capture

On `RT/RB` press:

- begin a learning capture session if the current weapon needs one

On `RT/RB` release:

- end that burst

The collector should reuse existing motion-trace + segmentation logic where possible, but the app owns the session orchestration.

### Session completion

Once enough clean bursts exist:

- build a recoil profile
- save the profile JSON
- save the profile summary JSON
- generate a recoil visualization PNG
- load the new profile into memory cache
- publish ready status so the main project can switch off fallback

## Visualization

Each learned profile should produce a PNG under a dedicated artifacts folder, for example:

- `artifacts/recoil_plots/<profile_id>.png`

The plot should show:

- raw burst traces
- aligned/averaged recoil curve
- final curve actually used by runtime compensation

## Persistence

`recoil_app` intentionally avoids a persistent runtime config in the current version. The user selects `game` and `mode` at startup, and paths are built in memory from defaults or CLI/env overrides.

The lightweight weapon identity records are stored under:

- `artifacts/recoil_app/weapons/identity-*.json`

Each new `recoil_app` identity record should stay minimal:

- `canonical_weapon_id`
- `game`
- `display_name`
- `created_at`
- `updated_at`

Runtime state snapshots for debugging can also be written to:

- `artifacts/recoil_app/current_weapon.json`

These files are for observability, not hot-path repeated profile loading. The hot path should use in-memory cached profile objects once a weapon is recognized.

`artifacts/weapon_examples/` is user-provided local screenshot material for OCR/ROI tuning and should not be committed.

## Performance Notes

The live record/recoil path must stay conservative because earlier CPU-heavy OCR sweeps saturated the user's machine.

Current defaults:

- OCR provider preference: CUDA first, with `RECOIL_OCR_PROVIDER=dml|cpu` overrides
- screenshot path: DXGI first, `PIL.ImageGrab` fallback only
- console polling: 60 Hz
- record-mode capture FPS: 60 FPS

Do not run broad OCR sweeps over sample images without explicit user permission. Prefer cheap one-frame debug crop dumps for ROI diagnosis.

## Reuse Plan

Reuse as much existing code as possible:

- `controllers/gamepad/physical_input.py`
- `vision/weapon_identity/text.py`
- `vision/weapon_identity/adapters.py`
- `vision/recoil_collection/models.py`
- `vision/recoil_collection/segmentation.py`
- `vision/recoil_collection/extraction.py`
- `vision/recoil_collection/storage.py`
- `runtime/recoil_sidecar/service.py`

Avoid further growth of:

- `controllers/gamepad_controller.py`
- `controllers/gamepad/weapon_switch_recognition.py`
- `tools/recoil_toolkit_console.py`

Those may remain as compatibility/debug paths, but `recoil_app` becomes the new primary path.

## Acceptance Criteria

The first usable version is complete when:

1. starting `recoil_app` alone prints current weapon status after `Y` presses
2. a never-before-seen weapon name creates a valid identity record automatically
3. firing `RT/RB` with an unlearned weapon automatically records enough bursts to save a profile
4. `record` mode generates a recoil plot PNG for each learned profile
5. the main project can import and use `recoil_app` in `recoil` mode directly without separately running a recoil sidecar process
6. the main project can keep the plugin path and use the learned profile automatically
7. if no profile exists yet, `recoil` mode still falls back to fixed `20%`
