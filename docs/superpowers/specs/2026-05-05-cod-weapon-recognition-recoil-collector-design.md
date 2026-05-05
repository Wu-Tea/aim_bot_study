# COD Weapon Recognition And Recoil Collector Design

**Goal:** Design and stage the full two-program recoil system for `COD20`, `COD21`, and `COD22`, while making the first delivery phase concrete and immediately implementable.

The full target system consists of:

- a standalone local `recoil collector` that records structured standing-fire recoil profiles
- a standalone `weapon recognizer` helper that identifies the current weapon from HUD imagery and short-lived weapon-name cues
- a later `runtime recoil sidecar` that selects the active recoil profile and exposes it to the main application
- a shared profile and identity format that all phases consume without redesign

The first delivery phase in this spec will ship:

- a standalone local `recoil collector` that records structured standing-fire recoil profiles
- a standalone `weapon recognizer` helper that identifies the current weapon from HUD imagery and short-lived weapon-name cues
- a shared profile and identity format that later runtime recoil switching can consume without redesign

**Scope:**

- Support `COD20`, `COD21`, and `COD22` through one shared framework with per-game adapters.
- Limit the first collector version to `standing` firing posture only.
- Record recoil profiles for hip-fire and ADS as separate modes when both are sampled.
- Use `image-first, text-assisted, stateful` weapon recognition.
- Persist structured weapon identity and recoil-profile data to disk in a stable schema.
- Keep the collector and recognizer as standalone programs outside the current hot vision/controller loop.
- Define a small future-facing handoff contract for later runtime recoil switching and gamepad integration.

**Non-goals:**

- No first-phase rewrite of `controllers/gamepad/recoil_compensation.py`.
- No first-phase live injection of weapon-specific recoil curves into the gamepad pipeline.
- No support in V1 for crouch, prone, strafing, jump shots, mounts, or movement-modified recoil.
- No attempt to infer full weapon identity from blueprint names alone.
- No dependency on training a large ML model before the first usable version ships.
- No requirement that the collector perfectly generalize across every UI scale or language without per-title calibration data.

## Problem

The current gamepad recoil logic is a single downward pull amount applied while `auto_fire_active` is true.

That behavior is simple, but it is too coarse for real weapon handling:

- different weapons have meaningfully different recoil curves
- some weapons need strong early pull and weaker tail compensation
- some weapons need very little compensation at all
- `COD22` blueprint naming makes text-only recognition unreliable
- `COD21` depends more heavily on transient weapon-name cues
- `COD20` exposes both icon and transient text signals

The current architecture can later consume better recoil data, but it does not yet have:

- a structured way to collect weapon-specific recoil curves
- a shared weapon identity layer across `COD20/21/22`
- a standalone recognizer that can reliably say what weapon is active

## Full System Target

The complete intended product has three runtime actors:

1. `recoil collector`
2. `weapon recognizer`
3. `runtime recoil sidecar`

The relationship is:

- the collector produces trustworthy weapon-specific recoil curves
- the recognizer produces stable current-weapon identity
- the runtime sidecar resolves identity to profile and exposes active recoil data to the controller layer

The existing gamepad runtime is a downstream consumer of this system, not the place where collection or recognition logic should live.

## Phase Decomposition

The overall product is intentionally split into two programs:

1. `recoil collector`
2. `runtime recognizer/helper`

This spec covers the first shippable sub-project:

- build the collector
- build the recognizer
- build the shared storage and identity layer

This first ship block covers:

- collector
- recognizer
- shared storage

The second ship block adds:

- runtime recoil sidecar
- active profile resolution
- controller-facing handoff into the main app

This means the first phase does **not** implement final live gamepad switching yet, but it must leave a clean handoff boundary for that next phase.

## Approaches Considered

### 1. Text-first recognition with OCR as the primary key

Pros:

- easiest initial implementation
- naturally supports transient weapon-name banners

Cons:

- weak fit for `COD22` blueprint names
- fragile across language packs and stylized HUD fonts
- fails when the visible name is absent or delayed

### 2. Image-first recognition with text as confirmation

Pros:

- best fit for `COD22` blueprint naming problem
- works with persistent HUD weapon silhouettes and icon regions
- naturally combines with text windows when they exist
- supports stateful caching to reduce churn

Cons:

- requires per-title ROI handling
- requires curated template or signature data

### 3. End-to-end ML classifier for weapon identity and recoil estimation

Pros:

- elegant long-term path
- one learned model could unify icon variants and noisy HUD states

Cons:

- high data and iteration cost
- harder to debug
- unnecessary complexity for the first useful version

## Chosen Design

Choose **Approach 2**.

The first version will use:

- `image-first` weapon recognition
- `OCR` or text-window parsing as a secondary signal
- a `stateful cache` that carries forward the last confirmed weapon until strong contrary evidence appears
- a standalone recoil collector that measures screen-response curves instead of relying on guessed weapon classes

## Architecture

The first-phase system has four major parts:

1. `weapon adapter layer`
2. `weapon recognizer`
3. `recoil collector`
4. `profile store`

### 1. Weapon adapter layer

Each supported title gets an adapter responsible for:

- HUD region definitions
- cue priorities
- text-window timing rules
- optional calibration defaults

Shared interface responsibilities:

- `detect_game_layout(frame) -> adapter match metadata`
- `extract_weapon_regions(frame) -> ROI bundle`
- `parse_secondary_text(frame, context) -> candidate names`
- `resolve_slot_state(frame) -> primary/secondary hints`

Expected first adapters:

- `cod20`
- `cod21`
- `cod22`

### 2. Weapon recognizer

The recognizer is a standalone helper process that:

- captures small HUD regions at a moderate cadence
- computes image signatures from the active weapon ROI
- optionally OCRs short-lived weapon-name regions
- fuses those signals with recent state
- emits `canonical_weapon_id`, `confidence`, `source`, and `timestamp`

### 3. Recoil collector

The collector is a separate standalone program that:

- captures the game at high frame rate
- watches the center aiming region and selected HUD regions
- segments firing windows
- aligns repeated standing-fire samples
- produces structured recoil curves plus confidence metadata

### 4. Profile store

The shared store persists:

- canonical weapon identities
- per-title aliases and visual signatures
- collected recoil curves
- collection metadata and confidence summaries

The store is designed so later runtime recoil switching can read it directly without schema changes.

## Data Model

The storage layer should treat `weapon identity` and `recoil profile` as related but separate objects.

### Weapon identity record

Each identity record should include:

- `canonical_weapon_id`
- `game`
- `weapon_family`
- `display_name`
- `alias_names`
- `blueprint_names`
- `signature_refs`
- `notes`
- `created_at`
- `updated_at`

Important rule:

- `blueprint_names` are aliases, not canonical ids

That prevents `COD22` skins from fragmenting one base weapon into multiple unrelated profiles.

### Visual signature record

Each signature record should include:

- `signature_id`
- `canonical_weapon_id`
- `game`
- `region_type`
- `resolution_bucket`
- `ui_scale_bucket`
- `feature_type`
- `feature_payload`
- `captured_from`
- `confidence`

The first version should prefer compact classical features over heavier learned embeddings.

Good first candidates:

- normalized grayscale template
- edge map
- perceptual hash
- small ORB or keypoint descriptor set when silhouettes need more robustness

### Recoil profile record

Each recoil profile should include:

- `profile_id`
- `canonical_weapon_id`
- `game`
- `stance = "standing"`
- `aim_mode = "hipfire" | "ads"`
- `sample_interval_ms`
- `duration_ms`
- `initial_delay_ms`
- `samples_x`
- `samples_y`
- `sample_count`
- `burst_count`
- `variance_summary`
- `confidence`
- `capture_resolution`
- `capture_fps`
- `collector_version`
- `created_at`

The profile intentionally stores both `x` and `y` samples even if the first runtime use mostly applies vertical compensation.

## Weapon Recognition Design

The recognizer should be conservative:

> prefer delayed confirmation over confident misclassification

### Core recognition flow

1. Detect or assume the active title adapter.
2. Read the weapon HUD ROI for the adapter.
3. Compute image signatures and compare them against stored signatures for that title.
4. If the adapter supports transient name cues, watch the configured text window after a likely switch event.
5. Fuse image match, text match, slot continuity, and previous confirmed state.
6. Emit a recognized weapon only if the fused confidence passes a threshold.
7. Otherwise, retain the last confirmed weapon and mark current confidence as degraded.

### Title-specific behavior

#### COD22

Primary signal:

- persistent weapon image or silhouette region near the ammo HUD

Secondary signal:

- transient or contextual text when available

Important handling:

- blueprint name changes must not override canonical image-based identity

#### COD21

Primary signal:

- transient weapon-name cue after a switch-like event

Secondary signal:

- limited icon or slot-state support when present

Important handling:

- once recognized, cache the weapon until the next strong switch signal

#### COD20

Primary signal:

- persistent weapon icon or silhouette region

Secondary signal:

- switch-time weapon-name cue

Important handling:

- use icon and text as cross-checks when both exist

### Switch-event detection

The first version does not need deep controller hooks.

Switch suspicion can be triggered by:

- visible HUD slot changes
- rapid changes in the weapon ROI image signature
- transient weapon-name banner appearance

This keeps the recognizer self-contained for the first phase.

### Failure behavior

When confidence is low:

- do not emit a newly confirmed weapon id
- keep the last confirmed weapon if one exists
- surface `degraded` status in logs or UI

The safe failure direction is:

> unknown is better than wrong

## Recoil Collector Design

The collector should measure `actual screen-response recoil`, not attempt to reverse engineer weapon stats.

### User workflow

1. Launch the collector.
2. Select `COD20`, `COD21`, or `COD22`.
3. Select or confirm the recognized weapon.
4. Choose `standing hipfire` or `standing ADS`.
5. Start a guided capture sequence.
6. Fire repeated bursts without manual correction.
7. Review the generated curve summary.
8. Save the profile if confidence is acceptable.

### Capture assumptions

The first version assumes:

- standing posture only
- minimal player movement
- no intentional manual anti-recoil input during collection
- one weapon active for the capture session

These assumptions intentionally narrow the first problem so the collector can become reliable earlier.

### Frame capture strategy

The collector should run at a higher capture rate than the recognizer.

Capture focus:

- center reticle region
- a small contextual ring around the crosshair
- weapon HUD region for metadata and verification

The collector does not need full-frame heavyweight inference.
It needs stable, fast sampling of small ROIs.

### Recoil-window segmentation

The collector should infer a firing window from a combination of cues:

- muzzle or reticle motion onset near the center
- ammo count changes when visible
- optional user-triggered start/stop control in the collector UI

The first version may include a manual-assisted capture mode if it materially improves data quality.

### Curve extraction

For each captured burst:

1. detect recoil onset
2. track relative crosshair or screen-feature displacement over time
3. align the burst to a common zero point
4. resample to a stable interval
5. store `dx(t)` and `dy(t)`

After multiple bursts:

6. reject outliers
7. compute an averaged standing-fire curve
8. compute variance and confidence

### Why multiple bursts are required

One burst is too noisy because:

- capture jitter
- small hand movement contamination
- animation variance
- frame-timing irregularity

Averaging multiple aligned bursts is required for a profile that is trustworthy enough to drive later actuation.

## File And Process Layout

The implementation should keep collector and recognizer independent from the current controller hot path.

Recommended project additions:

- `tools/recoil_collector.py`
- `tools/weapon_recognizer.py`
- `vision/weapon_identity/`
- `vision/recoil_collection/`
- `artifacts/recoil_profiles/`
- `artifacts/weapon_signatures/`

Suggested package split:

- `vision/weapon_identity/adapters.py`
- `vision/weapon_identity/models.py`
- `vision/weapon_identity/signatures.py`
- `vision/weapon_identity/resolver.py`
- `vision/weapon_identity/runtime_state.py`
- `vision/recoil_collection/capture.py`
- `vision/recoil_collection/segmentation.py`
- `vision/recoil_collection/extraction.py`
- `vision/recoil_collection/storage.py`
- `runtime/recoil_sidecar/`

## Runtime Recoil Sidecar Design

The runtime recoil sidecar is not implemented in the first phase, but it is part of the complete design and must be anticipated now.

Its responsibilities are:

- subscribe to recognizer output
- resolve the best matching recoil profile for the current weapon, game, stance, and aim mode
- keep a stable `active_profile` state with confidence and freshness metadata
- expose a tiny local API the main application can read cheaply

### Sidecar input contract

The sidecar should consume:

- recognizer identity events
- profile store contents
- optional runtime context such as `aim_mode`

### Sidecar output contract

The sidecar should publish:

- `canonical_weapon_id`
- `profile_id`
- `game`
- `stance`
- `aim_mode`
- `profile_confidence`
- `identity_confidence`
- `updated_at`
- `status = ready | degraded | unknown`

### Why the sidecar stays separate

Keeping runtime profile selection outside the controller thread preserves:

- a small gamepad hot path
- simpler debugging and restart behavior
- easier replay testing
- safer failure handling when weapon recognition is uncertain

## Main App Integration Design

The later controller integration should change as little of the existing gamepad host as possible.

Expected later integration steps:

- load sidecar output through a small local client
- replace fixed recoil amount selection with active profile lookup
- advance recoil curve execution only while `auto_fire_active` is true
- reset curve execution when firing stops, weapon changes, or confidence drops below threshold

The controller layer should **not** perform:

- OCR
- template matching
- per-title HUD logic
- collector-style frame analysis

That work belongs in the recognizer and sidecar.

## Future Runtime Handoff

Even though live gamepad switching is out of scope for this first phase, the collector and recognizer must expose a small contract that later runtime integration can read.

Minimum future handoff payload:

- `canonical_weapon_id`
- `game`
- `stance`
- `aim_mode`
- `profile_id`
- `confidence`
- `updated_at`

This can later be served by:

- localhost HTTP
- local socket
- or file-backed state

The first phase only needs to define the contract clearly and produce compatible data.

## Error Handling

### Recognizer errors

- missing ROI calibration
- unsupported UI scale
- low OCR confidence
- conflicting image and text signals
- ambiguous blueprint aliasing

Handling:

- retain previous confirmed weapon when possible
- surface `unknown` or `degraded` status instead of guessing

### Collector errors

- capture FPS too unstable
- insufficient repeated bursts
- manual correction detected
- conflicting weapon identity during one session
- variance too high across bursts

Handling:

- fail the capture session or mark the result low-confidence
- do not silently promote poor data into the default profile set

## Testing Strategy

### Unit tests

Add tests for:

- signature matching and tie-breaking
- canonical id resolution from aliases and blueprint names
- per-title adapter ROI selection
- burst alignment and resampling
- variance and confidence calculations

### Fixture-based recognition tests

Store representative HUD crops for:

- `COD20`
- `COD21`
- `COD22`

Test cases should cover:

- normal icon recognition
- transient text-window recognition
- blueprint-name alias resolution
- ambiguous low-confidence cases

### Collector replay tests

Use short recorded capture clips to validate:

- firing-window segmentation
- recoil onset detection
- aligned curve generation
- rejection of noisy bursts

### Integration tests

Validate that:

- the recognizer can write a confirmed `canonical_weapon_id`
- the collector can attach that id to stored profiles
- the profile store can be read back without schema loss

## Migration Plan

### Phase 1A

- add shared weapon identity and recoil profile models
- add storage and schema validation

### Phase 1B

- add `COD20/21/22` adapter scaffolding
- implement first image-signature pipeline
- implement secondary text extraction where appropriate

### Phase 1C

- ship the standalone recognizer helper
- emit recognized current weapon with confidence and degraded status

### Phase 1D

- ship the standalone recoil collector
- support standing-only collection for hipfire and ADS
- persist reviewed profile data

### Phase 1E

- add fixtures, replay tests, and profile-quality validation

### Phase 2A

- implement runtime recoil sidecar state management
- connect recognizer identity output to active profile resolution

### Phase 2B

- integrate sidecar output into the gamepad recoil plugin path
- replace fixed downward pull with profile-driven curve execution

### Phase 2C

- add runtime safety rules for confidence loss, weapon changes, and stale profile state

## Recommendation

Proceed with this first-phase design.

It keeps the hard parts isolated:

- weapon recognition lives outside the hot controller loop
- recoil collection becomes a repeatable data pipeline
- the resulting schema is immediately useful and does not have to be redesigned when live runtime switching is implemented later

This design also keeps the full end state coherent:

- collector owns data acquisition
- recognizer owns weapon identity
- runtime sidecar owns active profile selection
- the controller runtime remains a consumer instead of becoming a dumping ground for CV logic

The first version is intentionally narrow:

- `COD20`
- `COD21`
- `COD22`
- standing posture only
- image-first recognition
- structured profile output

That is the right size for a first real build:

- useful enough to validate the product direction
- constrained enough to implement and test without hidden scope collapse
