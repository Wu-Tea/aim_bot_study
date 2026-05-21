# Root Structure Cleanup

Last reviewed: 2026-05-21

## Current Implementation Status

The first cleanup slice has been applied:

- local `.idea/` and `.claude/settings.local.json` files are ignored and removed from git tracking
- launcher implementations live under `scripts/launch/`
- root launcher files remain as compatibility shims
- `ControllerFactory` lives in `controllers/factory.py`
- root `controller.py` remains as a compatibility shim
- `models/README.md` and `artifacts/benchmarks/README.md` document artifact ownership

## Goal

Make the repository root easier to scan without breaking the current daily launch flow. The project is Windows-oriented and several `.bat` files are real user entry points, so root cleanup should happen in phases rather than one large move.

## Current Root Shape

The root currently mixes five kinds of things:

| Kind | Examples | Notes |
| --- | --- | --- |
| User entry points | `gamepad_start.bat`, `mouse_start.bat`, `recoil_app_start.bat` | Useful in root because they are launched manually. |
| Python entry and compatibility files | `main.py`, `controller.py`, `controllers/factory.py` | `main.py` is the unified launcher. `controllers/factory.py` owns the factory. `controller.py` is a compatibility shim. |
| Source directories | `controllers/`, `vision/`, `recoil_app/`, `runtime/`, `config/`, `training/` | These are normal top-level package directories. |
| Runtime and generated state | `config.toml`, `artifacts/`, `debug_captures/`, `native/vision_native/build/`, `.venv/`, `.worktrees/`, `__pycache__/` | Mostly ignored local state. Do not commit these. |
| Local tool/editor state | `.idea/`, `.claude/` | Currently some files are tracked, but they are better treated as local machine state unless explicitly shared. |

## Recommended Target Shape

```text
.
|-- README.md
|-- AGENT.md
|-- requirements.txt
|-- main.py
|-- controller.py                  # temporary compatibility shim
|-- gamepad_start.bat              # optional root shim during transition
|-- mouse_start.bat                # optional root shim during transition
|-- recoil_app_start.bat           # optional root shim during transition
|-- config/
|-- controllers/
|-- docs/
|-- models/
|-- native/
|-- recoil_app/
|-- runtime/
|-- scripts/
|   |-- launch/
|   |   |-- gamepad_start.bat
|   |   |-- mouse_start.bat
|   |   |-- recoil_app_start.bat
|   |   `-- debug/
|   |       |-- gamepad_debug.bat
|   |       |-- gamepad_native_debug.bat
|   |       `-- mouse_native_debug.bat
|   `-- legacy/
|       `-- recoil_toolkit.bat
|-- tests/
|-- tools/
|-- training/
`-- vision/
```

The important choice is whether to keep root launcher shims. Keeping shims means the root is not perfectly minimal, but the user's muscle memory and docs stay stable. Removing shims makes the root cleaner but forces every launch command and test fixture to change at once.

## Cleanup Phases

### Phase 1: No-Behavior Hygiene

These changes are low risk because they do not move runtime entry points:

- Keep `config.toml` ignored and document that it is the actual local runtime file.
- Remove tracked local tool state from git:
  - `.claude/settings.local.json`
  - `.idea/.name`
  - optionally the rest of `.idea/`
- Keep generated local directories ignored:
  - `.venv/`
  - `.worktrees/`
  - `__pycache__/`
  - `debug_captures/`
  - `native/vision_native/build/`
  - runtime `artifacts/`
- Add a short `models/README.md` before deleting or moving any model file.

### Phase 2: Launcher Consolidation

Move the full launcher implementations under `scripts/launch/`, then decide whether root files are kept as small shims.

Recommended transition:

1. Move full scripts into `scripts/launch/`.
2. Keep root `gamepad_start.bat`, `mouse_start.bat`, and `recoil_app_start.bat` as shims that call the moved scripts.
3. Move debug launchers into `scripts/launch/debug/`.
4. Move `recoil_toolkit.bat` into `scripts/legacy/` or remove it after `tools.recoil_toolkit_console` is retired.
5. Update docs and tests to prefer the `scripts/launch/` paths while accepting root shims during the transition.

### Phase 3: Python Entry Cleanup

Move `ControllerFactory` from root `controller.py` to `controllers/factory.py`.

Recommended transition:

1. Create `controllers/factory.py`.
2. Change `main.py` to import `ControllerFactory` from `controllers.factory`.
3. Keep root `controller.py` as a compatibility shim:

```python
from controllers.factory import ControllerFactory
```

4. Update tests and docs to refer to `controllers/factory.py`.
5. Remove the root shim only after no tests, docs, or user scripts import `controller`.

### Phase 4: Historical Artifacts

Tracked benchmark JSON files are useful as historical baselines, but they make `artifacts/` look like both source-controlled history and local runtime output.

Recommended options:

- Move tracked benchmark snapshots to `docs/project/benchmarks/`.
- Or keep them in `artifacts/benchmarks/` and add a README that clearly says this subfolder is source-controlled while other `artifacts/` subfolders are local runtime output.

### Phase 5: Model Registry

The root `models/` folder currently contains several generations of `.pt`, `.onnx`, and `.engine` files. Do not delete them blindly because training/export docs still reference some of them.

Recommended first step:

- Add `models/README.md` with:
  - current runtime model
  - fallback model
  - training base model
  - historical pose/detect models
  - whether each file is required, optional, or archival

## Suggested First Implementation Slice

Start with a small commit that does not change runtime behavior:

1. Stop tracking local tool state:
   - `git rm --cached .claude/settings.local.json`
   - `git rm --cached .idea/.name`
2. Add ignore rules for local tool state if needed.
3. Add `models/README.md`.
4. Add an `artifacts/benchmarks/README.md` or move benchmark JSON to `docs/project/benchmarks/`.
5. Run:

```powershell
py -3 -B -m unittest tests.test_startup_scripts tests.test_main_cli -v
git diff --check
```

This gives the root directory a clearer ownership model before moving user-facing launchers.
