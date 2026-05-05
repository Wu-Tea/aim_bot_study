# COD Recoil Recognition And Collection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the shared foundation, standalone weapon recognizer, standalone recoil collector, and forward-compatible runtime handoff for the COD recoil system across `COD20`, `COD21`, and `COD22`.

**Architecture:** The system is split into small independent units: shared identity/profile models, per-title weapon adapters, image-first recognition, standing-only recoil collection, and a future-facing runtime handoff contract. The first delivery block ends with a usable recognizer plus collector; later tasks add the sidecar and gamepad integration without redesigning the earlier data formats.

**Tech Stack:** Python 3.11, existing vision capture utilities, OpenCV or existing image-processing dependencies, `unittest`, JSON storage, lightweight local process boundaries

---

## File Map

### New files

- `vision/weapon_identity/__init__.py`
- `vision/weapon_identity/models.py`
- `vision/weapon_identity/signatures.py`
- `vision/weapon_identity/adapters.py`
- `vision/weapon_identity/resolver.py`
- `vision/weapon_identity/runtime_state.py`
- `vision/recoil_collection/__init__.py`
- `vision/recoil_collection/models.py`
- `vision/recoil_collection/capture.py`
- `vision/recoil_collection/segmentation.py`
- `vision/recoil_collection/extraction.py`
- `vision/recoil_collection/storage.py`
- `tools/weapon_recognizer.py`
- `tools/recoil_collector.py`
- `tests/weapon_identity/test_models.py`
- `tests/weapon_identity/test_signatures.py`
- `tests/weapon_identity/test_adapters.py`
- `tests/weapon_identity/test_resolver.py`
- `tests/recoil_collection/test_models.py`
- `tests/recoil_collection/test_segmentation.py`
- `tests/recoil_collection/test_extraction.py`
- `tests/recoil_collection/test_storage.py`
- `tests/recoil_collection/test_tooling.py`
- `runtime/recoil_sidecar/__init__.py`
- `runtime/recoil_sidecar/models.py`
- `runtime/recoil_sidecar/service.py`
- `tests/runtime/test_recoil_sidecar_service.py`

### Existing files likely to modify later

- `controllers/gamepad/recoil_compensation.py`
- `controllers/gamepad_controller.py`
- `docs/project/GAMEPAD_OVERVIEW.md`
- `config/loader.py`

### Data directories

- `artifacts/recoil_profiles/`
- `artifacts/weapon_signatures/`

## Delivery Blocks

- Block A: shared foundation and storage
- Block B: weapon recognition
- Block C: recoil collection
- Block D: runtime sidecar and main-app handoff

The first shipping target is Blocks `A-C`.

## Task 1: Shared Identity And Profile Models

**Files:**
- Create: `vision/weapon_identity/models.py`
- Create: `vision/recoil_collection/models.py`
- Create: `tests/weapon_identity/test_models.py`
- Create: `tests/recoil_collection/test_models.py`

- [x] Define immutable dataclasses for weapon identity records, visual signatures, recognition events, recoil samples, recoil profiles, and profile summaries.
- [x] Keep identity and profile objects separate so blueprint aliases do not become new canonical weapons.
- [x] Add serialization helpers: `to_dict()` and `from_dict()` for each persisted model.
- [x] Write model tests covering round-trip serialization, required-field validation, and blueprint alias handling.
- [x] Run: `python -m unittest tests.weapon_identity.test_models tests.recoil_collection.test_models -v`
- [x] Expected: all new model tests pass.

## Task 2: JSON Storage Layer

**Files:**
- Create: `vision/recoil_collection/storage.py`
- Modify: `vision/weapon_identity/models.py`
- Create: `tests/recoil_collection/test_storage.py`

- [x] Implement JSON readers and writers for identity records, signature records, and recoil profiles.
- [x] Normalize on UTF-8 JSON with stable key ordering so stored artifacts diff cleanly in git.
- [x] Write storage tests for:
  - identity save/load round trip
  - profile save/load round trip
  - corrupted JSON failure path
  - missing required field rejection
- [x] Run: `python -m unittest tests.recoil_collection.test_storage -v`
- [x] Expected: passing tests for valid files and explicit failures for invalid payloads.

## Task 3: Per-Title Adapter Layer

**Files:**
- Create: `vision/weapon_identity/adapters.py`
- Create: `tests/weapon_identity/test_adapters.py`

- [x] Add adapter classes for `cod20`, `cod21`, and `cod22`.
- [x] Each adapter must define:
  - weapon icon ROI
  - weapon-name text ROI when applicable
  - slot or switch suspicion hints when available
  - a human-readable adapter name and expected title behavior
- [x] Add an adapter registry so the recognizer and collector can request an adapter by game id.
- [x] Write tests that verify adapter lookup and ROI payload shapes for all three titles.
- [x] Run: `python -m unittest tests.weapon_identity.test_adapters -v`
- [x] Expected: registry and ROI contract tests pass.

## Task 4: Visual Signature Extraction And Matching

**Files:**
- Create: `vision/weapon_identity/signatures.py`
- Create: `tests/weapon_identity/test_signatures.py`

- [x] Implement small classical feature extractors for:
  - normalized grayscale template
  - edge map
  - perceptual hash
- [x] Implement a scorer that can compare a live ROI against stored signatures and return ranked candidates.
- [x] Keep the first version deterministic and explainable; do not introduce a trained model here.
- [x] Write tests covering exact match, near match, low-confidence mismatch, and tie ordering.
- [x] Run: `python -m unittest tests.weapon_identity.test_signatures -v`
- [x] Expected: candidate ranking is stable and mismatches degrade cleanly.

## Task 5: Recognition Resolver And Stateful Cache

**Files:**
- Create: `vision/weapon_identity/resolver.py`
- Create: `vision/weapon_identity/runtime_state.py`
- Create: `tests/weapon_identity/test_resolver.py`

- [x] Implement a resolver that fuses:
  - ranked image matches
  - optional OCR or text-window name matches
  - previous confirmed weapon
  - switch suspicion state
- [x] Add conservative confidence logic:
  - emit a new weapon only when confidence passes threshold
  - otherwise keep the last confirmed weapon and mark status degraded
- [x] Represent recognizer output as a `RecognitionEvent`.
- [x] Write tests for:
  - `COD22` blueprint alias resolving to canonical weapon id
  - `COD21` text-window confirmation with cached carry-forward
  - ambiguous image-only case that remains degraded
  - conflicting image/text signals where previous confirmed weapon is retained
- [x] Run: `python -m unittest tests.weapon_identity.test_resolver -v`
- [x] Expected: all resolver state and confidence rules pass.

## Task 6: Standalone Weapon Recognizer Tool

**Files:**
- Create: `tools/weapon_recognizer.py`
- Modify: `vision/weapon_identity/adapters.py`
- Modify: `vision/weapon_identity/resolver.py`
- Create: `tests/recoil_collection/test_tooling.py`

- [x] Build a CLI tool that accepts:
  - `--game`
  - `--profile-dir`
  - `--signature-dir`
  - `--capture-width`
  - `--capture-height`
  - `--fps`
- [x] Reuse existing capture infrastructure where possible instead of inventing a second full-screen pipeline.
- [x] Print structured current-weapon events to stdout and optionally write the latest state to a JSON file for future sidecar consumption.
- [x] Add a tooling test that validates argument parsing and latest-state file writing with stubbed recognizer dependencies.
- [x] Run: `python -m unittest tests.recoil_collection.test_tooling -v`
- [x] Expected: recognizer CLI smoke tests pass with stubbed data.

## Task 7: Standing-Only Recoil Collection Models And Segmentation

**Files:**
- Modify: `vision/recoil_collection/models.py`
- Create: `vision/recoil_collection/segmentation.py`
- Create: `tests/recoil_collection/test_segmentation.py`

- [ ] Define collection-session models for:
  - capture session metadata
  - burst window
  - burst sample series
- [ ] Implement standing-fire burst segmentation using a mix of:
  - center-region motion onset
  - ammo change when visible
  - optional manual start/stop fallback
- [ ] Write tests for:
  - single burst detection
  - multiple separated bursts
  - no-burst case
  - noisy motion that should not start a firing window
- [ ] Run: `python -m unittest tests.recoil_collection.test_segmentation -v`
- [ ] Expected: segmentation behaves deterministically on synthetic input.

## Task 8: Recoil Curve Extraction And Averaging

**Files:**
- Create: `vision/recoil_collection/extraction.py`
- Create: `tests/recoil_collection/test_extraction.py`

- [ ] Implement burst alignment, zero-point normalization, resampling, outlier rejection, and averaged-curve generation.
- [ ] Store both `samples_x` and `samples_y`, even if the first runtime consumer mostly uses vertical compensation.
- [ ] Compute variance and profile confidence from repeated bursts.
- [ ] Write tests for:
  - aligned curve generation from repeated synthetic bursts
  - outlier rejection
  - variance increase when bursts disagree
  - low-confidence result when too few clean bursts exist
- [ ] Run: `python -m unittest tests.recoil_collection.test_extraction -v`
- [ ] Expected: curve extraction and confidence logic pass on synthetic fixtures.

## Task 9: Standalone Recoil Collector Tool

**Files:**
- Create: `vision/recoil_collection/capture.py`
- Create: `tools/recoil_collector.py`
- Modify: `vision/recoil_collection/storage.py`
- Modify: `vision/weapon_identity/resolver.py`

- [ ] Build a collector CLI that supports:
  - `--game`
  - `--mode hipfire|ads`
  - `--standing-only`
  - `--profile-dir`
  - `--signature-dir`
  - `--output`
- [ ] The collector must:
  - recognize or confirm the current weapon
  - capture repeated bursts
  - summarize curve quality before save
  - persist a structured recoil profile JSON
- [ ] Add a JSON summary output that a future session can inspect without replaying the whole capture.
- [ ] Run:
  - `python -m unittest tests.recoil_collection.test_models tests.recoil_collection.test_segmentation tests.recoil_collection.test_extraction tests.recoil_collection.test_storage tests.recoil_collection.test_tooling -v`
- [ ] Expected: all collector and storage tests pass together.

## Task 10: Fixture And Replay Coverage

**Files:**
- Modify: `tests/weapon_identity/test_signatures.py`
- Modify: `tests/weapon_identity/test_resolver.py`
- Modify: `tests/recoil_collection/test_extraction.py`
- Create fixture directories under `tests/fixtures/` as needed

- [ ] Add representative fixture data for `COD20`, `COD21`, and `COD22` HUD crops.
- [ ] Add replay-oriented samples for collector extraction and segmentation.
- [ ] Ensure fixtures include:
  - a `COD22` blueprint-name mismatch case
  - a `COD21` switch-name confirmation case
  - a `COD20` icon-plus-text confirmation case
- [ ] Run:
  - `python -m unittest tests.weapon_identity.test_signatures tests.weapon_identity.test_resolver tests.recoil_collection.test_segmentation tests.recoil_collection.test_extraction -v`
- [ ] Expected: fixture-backed tests pass and document the intended behavior.

## Task 11: Runtime Recoil Sidecar Scaffolding

**Files:**
- Create: `runtime/recoil_sidecar/models.py`
- Create: `runtime/recoil_sidecar/service.py`
- Create: `tests/runtime/test_recoil_sidecar_service.py`

- [ ] Implement the future-facing sidecar model types and a lightweight service class that can:
  - read recognizer state
  - load matching recoil profiles
  - publish an `active_profile` payload
- [ ] Keep this task scoped to scaffolding and contract validation; do not modify the gamepad plugin yet.
- [ ] Write service tests for:
  - recognized weapon resolving to stored profile
  - degraded recognition yielding degraded sidecar status
  - missing profile yielding unknown or degraded output
- [ ] Run: `python -m unittest tests.runtime.test_recoil_sidecar_service -v`
- [ ] Expected: contract tests pass without requiring live controller integration.

## Task 12: Gamepad Integration Follow-Up

**Files:**
- Modify: `controllers/gamepad/recoil_compensation.py`
- Modify: `controllers/gamepad_controller.py`
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`

- [ ] Replace the fixed recoil amount implementation with a profile-driven curve executor.
- [ ] Advance curve playback only while `auto_fire_active` is true.
- [ ] Reset curve playback on weapon change, firing stop, or sidecar confidence drop.
- [ ] Add or update tests near `tests/gamepad/test_gamepad_recoil_compensation.py` to cover:
  - profile-driven playback
  - reset on stop firing
  - reset on weapon change
  - degraded sidecar fallback
- [ ] Run:
  - `python -m unittest tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host -v`
- [ ] Expected: integration tests pass and docs reflect the new data source.

## Cross-Task Verification

- [ ] Run targeted unit suites after each task instead of waiting for the end.
- [ ] At the end of Block A-C, run:
  - `python -m unittest tests.weapon_identity.test_models tests.weapon_identity.test_signatures tests.weapon_identity.test_adapters tests.weapon_identity.test_resolver tests.recoil_collection.test_models tests.recoil_collection.test_segmentation tests.recoil_collection.test_extraction tests.recoil_collection.test_storage tests.recoil_collection.test_tooling -v`
- [ ] At the end of all blocks, run:
  - `python -m py_compile vision\\weapon_identity\\models.py vision\\weapon_identity\\signatures.py vision\\weapon_identity\\adapters.py vision\\weapon_identity\\resolver.py vision\\weapon_identity\\runtime_state.py vision\\recoil_collection\\models.py vision\\recoil_collection\\capture.py vision\\recoil_collection\\segmentation.py vision\\recoil_collection\\extraction.py vision\\recoil_collection\\storage.py tools\\weapon_recognizer.py tools\\recoil_collector.py runtime\\recoil_sidecar\\models.py runtime\\recoil_sidecar\\service.py`

## Session Resume Notes

- The first executable milestone is reached when Tasks `1-10` are complete.
- The first runtime-integration milestone is reached when Tasks `11-12` are complete.
- If a future session resumes midstream, read this file first, then the design spec at `docs/superpowers/specs/2026-05-05-cod-weapon-recognition-recoil-collector-design.md`.
- Prefer updating checkbox state in this file as work completes so later sessions can resume from the last confirmed task boundary.
- Task `1` is complete and review-approved on commit `ecde59d0b0119ac7cbc33b13441515df5c67a672`.
- Task `2` is complete and review-approved on commit `91fe7d3739acdbe7ae84c42b38a1710f08d64170`.
- Task `3` is complete and review-approved on commit `dbd6423abf192d3bf161103d9bf2fa983da84c4b`.
- Task `4` is complete and review-approved on commit `8dd83e4c1825a3a30373413aeb3cde1b35621dd5`.
- Task `5` is complete and review-approved on commit `53735037395c99f63bdf5d69eeb15a0cac9592a0`.
- Task `6` is complete and review-approved on commit `36944926a3977129fa6d8dfed92b35a541ca2680`.
