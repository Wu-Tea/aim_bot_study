# Agent Handoff

Last updated: 2026-05-19T22:20:00+08:00
Updated by: Codex
Active scope: Native/gamepad contract hardening remains in the working tree; recoil_app now has magazine-curve profile recording/playback work in progress-complete state for code/tests.
Staleness: stale after `gamepad_start.bat` live gameplay smoke, a commit/stage decision, live recoil_app recording validation, confirmation/rejection of the scalp-only intervention policy, a native/Python gap-policy change, or any change to the `ControllerVisionState` contract.

## Current Objective

Preserve the now-hardened default `gamepad_start.bat` path while evaluating the user's next idea. The latest work made the native vision to Python controller boundary explicit and measurable:

1. Vision submits target movement, target metadata, auto-fire intent, and timing fields as one controller-facing state.
2. Gamepad and mouse auto-fire now share source freshness discipline with aim targets.
3. The runtime reports end-to-end timing stages beyond native `age_ms`.
4. Native empty detection frames intentionally clear target/fire state; Python fallback prediction is now an explicit policy difference.
5. High-impact runtime knobs are visible in config/docs and benchmark artifacts.

The user's current idea has two parts:

1. In scalp/head-only positions, AI may need to stop intervening and leave control fully manual because the visible target is too small for reliable lock.
2. `recoil_app_start` needs stricter reliability handling because OCR weapon-name recognition and recoil-curve recognition currently feel poor.

Draft plan: `docs/superpowers/plans/2026-05-19-ai-abstention-and-recoil-recognition-draft.md`.

Latest recoil magazine-curve implementation plan: `docs/superpowers/plans/2026-05-19-recoil-magazine-curve-v1-plan.md`.

## Current State

- Current branch/worktree: `dev` in main workspace `D:\work\AI\yolo-study-001`.
- Local `dev` was previously ahead of `origin/dev` by 3 commits: `542c3de`, `9e09a52`, and `c38e129`; no push/commit was performed during the latest work.
- Working tree is dirty with implementation, tests, docs, plan, and context updates. Do not revert unrelated pre-existing `.agent-context/` edits.
- Plan file exists and is updated as executed: `docs/superpowers/plans/2026-05-18-native-gamepad-contract-hardening-plan.md`.
- Major implemented areas:
  - `controllers/base_controller.py`: `ControllerVisionState`, timing snapshot, compatibility submit helper.
  - `controllers/gamepad_controller.py` and `controllers/mouse_controller.py`: atomic state consumption, auto-fire timestamps, timing fields; gamepad stop sends neutral virtual output.
  - `vision/native_runner.py`, `vision/runner.py`, and `vision/perf.py`: single state submission and stage timing metrics.
  - `native/vision_native/src/target_selector.cpp`: no-target / cue-hold fire suppression; empty detection clears active target.
  - `native/vision_native/src/vision_engine.cpp`: native engine path honors `VISION_MODEL_PATH`.
  - `config/loader.py`, `config.toml.example`, and `main.py`: model path, auto-fire, and recoil fallback knobs are explicit.
  - benchmark runners now persist auto-fire and recoil config snapshots.
- Native build passed after C++ changes.
- Focused and broad targeted suites passed in split runs:
  - config/main/perf/native scaffold: 40 tests OK
  - native runner / Python runner / native parity / target bridge: 61 tests OK
  - gamepad + mouse controller/auto-fire: 52 tests OK
  - benchmark runners: 24 tests OK
  - additional vision: 102 tests OK
  - additional gamepad metrics/helpers: 87 tests OK
  - mouse AI/metrics: 50 tests OK
  - recoil / weapon identity: 156 tests OK
  - `py -3 -B -m compileall -q config controllers vision tools tests` passed
  - `git diff --check` passed with LF/CRLF warnings only
- `py -3 -B -m unittest discover -v` and one oversized combined command timed out; treat split targeted suites as the usable verification evidence.
- Recoil reliability code has been changed for magazine-curve profiles:
  - `RecoilProfileRecord` now carries optional `profile_type`, `support_counts`, and `fit_summary` metadata while legacy payloads default to `burst_average_v1`.
  - `extract_magazine_recoil_profile()` records a full-magazine curve and preserves hold-to-burst internal pause plateaus.
  - `RecoilCollectorConfig` defaults to `profile_type="magazine_curve_v1"`.
  - `recoil_app.runtime` stores raw magazine episodes under `<profile_dir>/_episodes` and refits profiles across repeated recordings for the same weapon/aim mode.
  - `vision.recoil_collection.readiness.is_profile_ready_for_compensation()` gates active profiles; single-episode magazine profiles stay stored but are not enabled.
  - Sidecar and in-process recoil_app fail closed for unready profiles (`unknown`/`None`), and gamepad recoil playback now follows manual RT/RB firing as well as AI auto-fire, restarting from 0ms on each new fire press.
- Initial read found no first-class `head_only/scalp_only/abstain` target quality in `ControllerTarget`, native result conversion, or `AIAimPlugin`.
- Switch OCR can still be over-trusted in hard scenes, but poor/unready recoil profiles are now gated before activation.

## Next Action

1. Confirm the scalp-only policy: full manual passthrough, partial slowdown-only assist, or adaptive assist after stability.
2. If confirmed, implement target-quality metadata at the `ControllerVisionState` boundary and gate gamepad AI/auto-fire before touching detector internals.
3. For recoil, run live recording validation with two or more full-magazine recordings per weapon/aim mode and inspect the generated profile JSON plus `_episodes` files.
4. If the user wants closure before the new idea, propose either:
   - run `gamepad_start.bat` live smoke, or
   - stage/commit the current working-tree implementation, excluding context-only churn if requested.

## Blockers

- No live gameplay smoke has been recorded after the contract-hardening implementation.
- Full unittest discovery timed out in this environment; split targeted modules passed.
- The current changes are not committed or staged.
- Native/Python behavior intentionally differs on empty detection gaps; future work should not assume full parity without reading the updated tests.

## Active Questions

- Does the user want native empty-gap clearing to remain the production policy, or should a labeled hold/prediction source be reintroduced later?
- Is the current auto-fire freshness threshold (`50ms`) acceptable in live gameplay, or should fire become stricter than aim?
- Should benchmark/live smoke be run before building the next idea, or should the new idea guide the next validation path?
- Should `.agent-context/` updates be committed with implementation work, kept unstaged, or excluded from any future commit?
- For scalp-only targets, should the first behavior be full manual passthrough, weak slowdown-only assist, or adaptive re-enable after several stable frames?
- For `recoil_app_start`, should unknown/poor profile states disable compensation entirely, or keep the current fixed fallback when explicitly enabled?

## Files To Read First

- `docs/superpowers/plans/2026-05-18-native-gamepad-contract-hardening-plan.md`
- `docs/superpowers/plans/2026-05-19-ai-abstention-and-recoil-recognition-draft.md`
- `config.toml.example`
- `gamepad_start.bat`
- `controllers/base_controller.py`
- `controllers/gamepad_controller.py`
- `controllers/mouse_controller.py`
- `controllers/gamepad/auto_fire.py`
- `controllers/mouse/auto_fire.py`
- `vision/native_runner.py`
- `vision/runner.py`
- `vision/perf.py`
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/vision_engine.cpp`
- `tests/test_native_vision_synthetic_parity.py`
- `tests/test_native_vision_targeting_bridge.py`
- `tests/gamepad/test_gamepad_controller_host.py`
- `tests/mouse/test_mouse_controller_host.py`

## Do Not Do

- Do not move controller behavior into C++ until end-to-end timing shows the Python/controller boundary is the bottleneck.
- Do not interpret native `age_ms` as full controller-output latency; use the new timing metrics.
- Do not widen posture heuristics further without recorded material or regression tests.
- Do not silently restore Python prediction parity over the chosen native empty-gap clearing policy.
- Do not commit context-only updates as progress/checkpoint commits unless the user explicitly asks.

## Related Context

- Native vision policy still stands: normal vision work should stay native unless the user explicitly reopens Python vision parity.
- `session-log.md` is the short index; historical details are preserved in `session-log-full.md`.
- No new decision record was created for the 2026-05-19 sync because it records implementation state and verification, not a newly accepted architecture decision.
