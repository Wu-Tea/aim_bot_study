# Recoil App Stabilization Handoff

Date: 2026-05-11
Scope: `recoil_app` standalone record/recoil runtime and in-process gamepad bridge

## Current User-Facing Entry

Use:

```powershell
.\recoil_app_start.bat
```

or directly:

```powershell
python -m recoil_app --game cod22 --mode record
python -m recoil_app --game cod22 --mode recoil
```

The batch file asks only for:

- game: `COD20`, `COD21`, or `COD22`
- mode: `record` or `recoil`

## Mode Semantics

`record` mode:

- listens for `Y` weapon switch events
- captures the weapon-name OCR region after the switch delay
- auto-creates a minimal weapon identity
- listens for `RT/RB` firing
- learns recoil profiles for weapons without a ready profile
- writes profile JSON, summary JSON, and recoil plot PNG
- never exposes compensation to the controller

`recoil` mode:

- listens for `Y` weapon switch events
- resolves the current weapon by text OCR
- looks up cached profiles in memory
- exposes the active profile to the gamepad recoil plugin
- falls back to fixed `20%` pull when no ready profile exists

## Files And Ownership

Primary runtime:

- `recoil_app/runtime.py`
- `recoil_app/console.py`
- `recoil_app/__main__.py`
- `recoil_app_start.bat`

Thin main-project bridge:

- `controllers/gamepad_controller.py`
- `controllers/gamepad/physical_input.py`

Shared helpers reused by `recoil_app`:

- `vision/weapon_identity/adapters.py`
- `vision/weapon_identity/text.py`
- `vision/recoil_collection/capture.py`
- `vision/recoil_collection/extraction.py`
- `vision/recoil_collection/storage.py`

Do not grow OCR/learning logic inside `controllers/` or the normal vision hot path. Keep new recoil-specific behavior in `recoil_app` unless the change is a small shared helper.

## Runtime Storage

Committed code should not include live assets.

Persistent local assets:

- `artifacts/recoil_app/weapons/identity-*.json`
- `artifacts/recoil_profiles/*.json`
- `artifacts/recoil_profiles/*.summary.json`
- `artifacts/recoil_plots/*.png`
- `artifacts/recoil_app/current_weapon.json`

Do not commit:

- `artifacts/weapon_examples/`
- runtime state JSON
- generated profiles or plots unless explicitly requested

## Latest Fixes Before Handoff

The latest user feedback exposed three practical issues:

- COD21 OCR could absorb lower ammo/numeric rows into the weapon name.
- COD20 OCR could read ammo labels instead of the weapon name because the name crop was too low/right.
- CPU usage was unacceptable when local OCR/sample sweeps were run repeatedly.

The current code includes these mitigations:

- COD20/COD21 name ROIs were moved upward and tightened.
- `recoil_app` rejects obvious ammo labels such as `9毫米鲁格手枪弹` and `7.62BLK`.
- `recoil_app` rejects overlong cross-line OCR garbage.
- RapidOCR defaults to CUDA, with `RECOIL_OCR_PROVIDER=dml|cpu` overrides.
- Switch/motion capture prefers DXGI and falls back to `PIL.ImageGrab` only if DXGI setup fails.
- Standalone console polling is 60 Hz instead of 200 Hz.
- Record-mode capture defaults to 60 FPS instead of 100 FPS.

## Known Risks

- The latest ROI and GPU/DXGI changes were not re-run through heavy OCR replay because that workload was saturating the user's machine.
- Old bad weapon identities may remain under `artifacts/recoil_app/weapons` from earlier misreads. If `switch_cache` prints a bad name, delete the bad identity and restart.
- DXGI setup can fail on some display configurations. If it does, runtime falls back to `PIL.ImageGrab`, which may be CPU-heavier.
- CUDA OCR depends on the installed ONNX Runtime provider actually being usable. If CUDA is unstable, try `RECOIL_OCR_PROVIDER=dml`.

## Next Session Checklist

1. Do not run broad OCR sweeps over `artifacts/weapon_examples` without explicit user approval.
2. Ensure no stale Python process is running before testing.
3. Start `recoil_app_start.bat` fresh so slot cache is clean.
4. Test COD21 Y-switch OCR:
   - expected: weapon name only
   - bad: weapon name plus ammo/numeric rows
5. Test COD20 Y-switch OCR:
   - expected: weapon name line
   - bad: ammo label such as `9毫米鲁格手枪弹` or `7.62BLK`
6. Watch CPU during idle and after Y-switch.
7. If OCR is wrong, add a cheap debug crop dump on Y press and tune from that actual crop.
8. If CPU is still high, disable multi-pass OCR in live paths and reduce record-mode capture size before adding new features.

## Suggested Lightweight Debug Patch If Needed

If live OCR is still wrong, add a temporary opt-in environment variable:

```powershell
$env:RECOIL_DEBUG_DUMP_SWITCH_CROP="1"
```

When set, save the exact post-delay switch crop to:

```text
artifacts/recoil_app/debug_crops/
```

This is safer than running local grid-search OCR sweeps and gives the next agent the exact frame the runtime saw.
