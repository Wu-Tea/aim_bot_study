# COD Recoil Direct Usability Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining runtime gaps so the COD recoil branch can be used end-to-end for signature collection, live weapon recognition, recoil profile collection, and profile-driven gamepad recoil switching.

**Architecture:** Keep the existing recognizer, collector, sidecar, and gamepad profile executor, but close the last-mile usability gaps with four bounded additions: HUD-aware capture, OCR-backed text fusion in the live recognizer, a real signature-authoring path, and a runnable launcher that wires recognizer state into the gamepad host. The implementation stays conservative: full-screen or explicit-region capture for correctness first, small OCR ROI crops, JSON-backed signature/profile assets, and local process orchestration instead of moving CV logic into the controller thread.

**Tech Stack:** Python 3.11, existing DXGI capture backend, OpenCV, `rapidocr_onnxruntime` when available, `unittest`, JSON storage, Windows batch and Python subprocess launch helpers

**Execution Status:** Implemented and branch-verified on `2026-05-06`. Verification passed with:
- `python -m unittest tests.test_vision_capture tests.weapon_identity.test_text tests.weapon_identity.test_resolver tests.recoil_collection.test_tooling tests.runtime.test_recoil_sidecar_service tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host -v`
- `python -m unittest tests.weapon_identity.test_models tests.weapon_identity.test_signatures tests.weapon_identity.test_adapters tests.weapon_identity.test_resolver tests.weapon_identity.test_text tests.recoil_collection.test_models tests.recoil_collection.test_segmentation tests.recoil_collection.test_extraction tests.recoil_collection.test_storage tests.recoil_collection.test_tooling tests.runtime.test_recoil_sidecar_service tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host tests.test_vision_capture -v`
- `python -m py_compile vision\\weapon_identity\\models.py vision\\weapon_identity\\signatures.py vision\\weapon_identity\\adapters.py vision\\weapon_identity\\resolver.py vision\\weapon_identity\\runtime_state.py vision\\weapon_identity\\text.py vision\\recoil_collection\\models.py vision\\recoil_collection\\capture.py vision\\recoil_collection\\segmentation.py vision\\recoil_collection\\extraction.py vision\\recoil_collection\\storage.py vision\\capture.py tools\\weapon_recognizer.py tools\\weapon_signature_capture.py tools\\recoil_collector.py tools\\recoil_runtime_launcher.py runtime\\recoil_sidecar\\models.py runtime\\recoil_sidecar\\service.py controllers\\gamepad\\recoil_compensation.py controllers\\gamepad_controller.py`

---

## File Map

### New files

- `vision/weapon_identity/text.py`
- `tools/weapon_signature_capture.py`
- `tools/recoil_runtime_launcher.py`
- `tests/weapon_identity/test_text.py`

### Existing files to modify

- `vision/capture.py`
- `tools/weapon_recognizer.py`
- `vision/weapon_identity/adapters.py`
- `tests/test_vision_capture.py`
- `tests/recoil_collection/test_tooling.py`
- `gamepad_start.bat`
- `docs/project/GAMEPAD_OVERVIEW.md`
- `docs/superpowers/plans/2026-05-05-cod-recoil-system-master-plan.md`

### Runtime data directories

- `artifacts/weapon_signatures/`
- `artifacts/recoil_profiles/`
- `artifacts/recoil_state/`

## Task 1: HUD-Aware Recognizer Capture

**Files:**
- Modify: `vision/capture.py`
- Modify: `tools/weapon_recognizer.py`
- Modify: `tests/test_vision_capture.py`
- Modify: `tests/recoil_collection/test_tooling.py`

- [ ] Add a failing capture test that proves `ScreenCaptureThread` can be constructed with an explicit absolute region instead of always center-cropping.
- [ ] Run: `python -m unittest tests.test_vision_capture.ScreenCaptureThreadTests.test_capture_thread_accepts_explicit_region_without_center_recompute -v`
- [ ] Expected: FAIL because `ScreenCaptureThread` does not yet accept an explicit region.
- [ ] Implement optional `region=(left, top, right, bottom)` support in `ScreenCaptureThread`, keeping the existing center-crop path unchanged for the collector.
- [ ] Add a failing recognizer tooling test that proves the recognizer defaults to full-screen capture for live HUD work and can still accept an explicit region override.
- [ ] Run: `python -m unittest tests.recoil_collection.test_tooling.WeaponRecognizerToolTests.test_build_capture_thread_defaults_to_fullscreen_region_for_live_hud tests.recoil_collection.test_tooling.WeaponRecognizerToolTests.test_build_capture_thread_uses_explicit_region_override -v`
- [ ] Expected: FAIL because the recognizer still center-crops through `capture-width` and `capture-height`.
- [ ] Extend `tools/weapon_recognizer.py` CLI with a conservative live-capture contract:
  - `--capture-mode fullscreen|center|region`
  - `--capture-left`
  - `--capture-top`
  - keep `--capture-width` and `--capture-height` for `center|region`
  - default to `fullscreen` for recognizer direct usability
- [ ] Update `_build_capture_thread(...)` so:
  - `fullscreen` captures `(0, 0, screen_width, screen_height)`
  - `region` uses the explicit absolute rectangle
  - `center` preserves the legacy center-crop behavior
- [ ] Run:
  - `python -m unittest tests.test_vision_capture tests.recoil_collection.test_tooling -v`
- [ ] Expected: capture and recognizer tooling tests pass with the new full-screen default.

## Task 2: OCR-Backed Live Text Fusion

**Files:**
- Create: `vision/weapon_identity/text.py`
- Modify: `tools/weapon_recognizer.py`
- Modify: `vision/weapon_identity/adapters.py`
- Create: `tests/weapon_identity/test_text.py`
- Modify: `tests/recoil_collection/test_tooling.py`

- [ ] Add a failing OCR normalization test for a small helper that turns one OCR pass into ordered non-empty text candidates with duplicate and whitespace cleanup.
- [ ] Run: `python -m unittest tests.weapon_identity.test_text.WeaponTextExtractionTests.test_normalize_ocr_lines_returns_unique_text_candidates -v`
- [ ] Expected: FAIL because the helper module does not exist.
- [ ] Implement `vision/weapon_identity/text.py` with:
  - lazy `RapidOCR` loading
  - `extract_text_candidates(frame, roi, ocr_reader=None) -> tuple[str, ...]`
  - normalization that trims, deduplicates, and preserves order
  - graceful fallback to `()` when OCR backend is unavailable
- [ ] Add a failing recognizer test that proves a live `cod21` or `cod20` frame can pass OCR-derived names into `resolve_weapon(...)`.
- [ ] Run: `python -m unittest tests.recoil_collection.test_tooling.WeaponRecognizerToolTests.test_process_frame_passes_ocr_text_candidates_into_resolver -v`
- [ ] Expected: FAIL because `text_candidates=()` is still hardcoded.
- [ ] Update `SignatureWeaponRecognizer` so it:
  - optionally accepts an injected OCR reader
  - crops `adapter.weapon_name_text_roi`
  - builds text candidates from the ROI
  - passes those text candidates into `resolve_weapon(...)`
- [ ] Keep the recognizer conservative:
  - if OCR is missing, continue with image-only behavior
  - if OCR returns nothing, pass `()`
  - do not crash when the text ROI is empty
- [ ] Run:
  - `python -m unittest tests.weapon_identity.test_text tests.recoil_collection.test_tooling tests.weapon_identity.test_resolver -v`
- [ ] Expected: live recognizer coverage now includes text-assisted resolution.

## Task 3: Signature Authoring Toolchain

**Files:**
- Create: `tools/weapon_signature_capture.py`
- Modify: `tests/recoil_collection/test_tooling.py`
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`

- [ ] Add a failing tooling test that proves a dedicated signature-capture CLI can:
  - capture or load a frame
  - crop the title adapter weapon ROI
  - write a `VisualSignatureRecord`
  - optionally write a matching `WeaponIdentityRecord`
- [ ] Run: `python -m unittest tests.recoil_collection.test_tooling.WeaponSignatureCaptureToolTests -v`
- [ ] Expected: FAIL because the tool does not exist.
- [ ] Implement `tools/weapon_signature_capture.py` with a minimal direct-use flow:
  - required: `--game`, `--canonical-weapon-id`, `--display-name`, `--weapon-family`, `--signature-dir`
  - optional: `--image`, `--notes`, `--resolution-bucket`, `--ui-scale-bucket`, `--identity-only`
  - if `--image` is absent, grab one full-screen frame live
  - crop adapter icon ROI
  - extract classical signature payload
  - save `signature-<game>-<weapon>.json`
  - save or merge `identity-<game>-<weapon>.json` in the same directory
- [ ] Include a small capture artifact alongside the JSON:
  - write the cropped icon image to `artifacts/weapon_signatures/crops/`
  - name it with the same signature id for debugging
- [ ] Run:
  - `python -m unittest tests.recoil_collection.test_tooling.WeaponSignatureCaptureToolTests tests.weapon_identity.test_signatures -v`
- [ ] Expected: signature authoring works with image-backed fixtures and feeds the existing matcher format.

## Task 4: Runtime Launch Orchestration

**Files:**
- Create: `tools/recoil_runtime_launcher.py`
- Modify: `gamepad_start.bat`
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`
- Modify: `tests/recoil_collection/test_tooling.py`

- [ ] Add a failing launcher test that proves one command can compute a default latest-state path and the env vars the gamepad controller expects.
- [ ] Run: `python -m unittest tests.recoil_collection.test_tooling.RecoilRuntimeLauncherTests -v`
- [ ] Expected: FAIL because the launcher does not exist.
- [ ] Implement `tools/recoil_runtime_launcher.py` with:
  - required: `--game`, `--profile-dir`, `--signature-dir`
  - optional: `--state-file`, `--recognizer-fps`, `--controller-mode`, `--auto-fire-output`, `--recognizer-only`
  - helper to build recognizer subprocess argv
  - helper to build controller env containing:
    - `RECOIL_PROFILE_DIR`
    - `RECOIL_RECOGNIZER_STATE_PATH`
    - existing `VISION_BACKEND` if already present
  - launcher behavior:
    - start recognizer subprocess writing `latest-state.json`
    - if not `--recognizer-only`, start `main.py --controller-mode gamepad ...`
- [ ] Update `gamepad_start.bat` with an opt-in recoil path:
  - if `ENABLE_RECOIL_RUNTIME=1`, call `tools/recoil_runtime_launcher.py`
  - otherwise keep the existing startup path unchanged
- [ ] Run:
  - `python -m unittest tests.recoil_collection.test_tooling.RecoilRuntimeLauncherTests tests.gamepad.test_gamepad_controller_host tests.runtime.test_recoil_sidecar_service -v`
- [ ] Expected: launcher produces the env contract the current controller host already understands.

## Task 5: Direct-Use Verification And Resume Anchors

**Files:**
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`
- Modify: `docs/superpowers/plans/2026-05-05-cod-recoil-system-master-plan.md`

- [ ] Run targeted verification for the finished closure work:
  - `python -m unittest tests.test_vision_capture tests.weapon_identity.test_text tests.weapon_identity.test_resolver tests.recoil_collection.test_tooling tests.runtime.test_recoil_sidecar_service tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host -v`
- [ ] Expected: all direct-use closure suites pass.
- [ ] Run broad regression verification:
  - `python -m unittest tests.weapon_identity.test_models tests.weapon_identity.test_signatures tests.weapon_identity.test_adapters tests.weapon_identity.test_resolver tests.weapon_identity.test_text tests.recoil_collection.test_models tests.recoil_collection.test_segmentation tests.recoil_collection.test_extraction tests.recoil_collection.test_storage tests.recoil_collection.test_tooling tests.runtime.test_recoil_sidecar_service tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host tests.test_vision_capture -v`
- [ ] Expected: the recoil branch remains green after the closure work.
- [ ] Update `docs/project/GAMEPAD_OVERVIEW.md` with a direct-use workflow:
  - capture signature
  - collect recoil profile
  - launch recognizer and gamepad together
  - verify latest-state path and profile directory
- [ ] Append a new completion section to `docs/superpowers/plans/2026-05-05-cod-recoil-system-master-plan.md` that points future sessions at this closure plan and records the accepted completion commits.

## Direct-Use Definition

This closure work is complete only when the branch supports the following manual flow without extra code edits:

1. Run `tools/weapon_signature_capture.py` to author a signature and matching identity JSON.
2. Run `tools/recoil_collector.py` to produce a recoil profile JSON for that weapon.
3. Run `tools/recoil_runtime_launcher.py` to keep a latest recognizer state file updated.
4. Launch the gamepad host through the launcher so it reads:
   - `RECOIL_PROFILE_DIR`
   - `RECOIL_RECOGNIZER_STATE_PATH`
5. Observe that the recognizer resolves the current weapon from image plus OCR when needed and the controller can consume the selected profile through the existing sidecar contract.
