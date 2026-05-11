# Recoil App Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a `recoil_app` package with `record` and `recoil` modes, then let the main gamepad runtime import `recoil` mode through a thin in-process bridge.

**Architecture:** `recoil_app` owns OCR-based weapon recognition, identity persistence, profile caching, and optional learning sessions. The main controller keeps the existing recoil plugin and only forwards Y/fire signals into a bridge object while asking that bridge for the currently cached active profile.

**Tech Stack:** Python, `pygame`, existing DXGI capture path, existing OCR helpers, existing recoil profile models/storage, OpenCV for debug plots.

---

### Status

- [x] Create `recoil_app` runtime package with identity store, profile store, and reusable runtime object
- [x] Add `record` vs `recoil` mode split at runtime level
- [x] Add standalone `python -m recoil_app` console entry with mode selection
- [x] Add thin `GamepadRecoilBridge` integration path for the main controller
- [x] Preserve fixed `20%` fallback when no cached ready profile exists
- [x] Add regression tests for identity auto-create, profile caching, mode split, and controller bridge delegation
- [x] Slim `recoil_app` persistence to minimal identity records under `artifacts/recoil_app/weapons`
- [x] Remove persistent runtime `config.json` from the primary path; build runtime config in memory from startup choices and env/CLI overrides
- [x] Keep `record` mode compensation-free and reserve compensation output for `recoil` mode only
- [x] Add GPU-first OCR provider selection with `RECOIL_OCR_PROVIDER=dml|cpu` fallback overrides
- [x] Prefer DXGI capture over `PIL.ImageGrab` for switch and learning captures
- [x] Add COD20/COD21 OCR guardrails for ammo-line / cross-line misreads
- [x] Lower standalone polling and record capture defaults to reduce CPU pressure
- [ ] Extend `record` mode learning verification against real live captures after the latest CPU fixes
- [ ] Validate generated recoil plot output on real weapon captures
- [ ] Decide whether the older `recoil_toolkit` flow should remain as debug-only or be retired

### Latest Live Findings

- `COD21` can merge the weapon name and ammo/numeric HUD rows if the crop is too tall or the name selector is too greedy.
- `COD20` can read ammo labels instead of the weapon name when the crop is too low/right; the weapon name is above the ammo type.
- Broad local Python OCR sweeps are not acceptable for routine validation because they caused severe CPU saturation on the user's machine.
- Future ROI debugging should prefer dumping one switch-capture crop per Y press and inspecting that image, not running full OCR grid searches.

### Handoff Verification Boundary

The latest ROI/GPU/DXGI changes were committed as static fixes but intentionally not re-run through heavy OCR replay. The next session should start with a user live smoke test:

- restart `recoil_app_start.bat`
- select the relevant game and `record` mode
- press `Y` and confirm the printed weapon name
- watch CPU usage
- if the name is still wrong, add a cheap debug crop dump and tune from the actual crop
