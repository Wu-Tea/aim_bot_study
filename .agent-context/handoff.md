# Agent Handoff

Last updated: 2026-04-30T21:08:00+08:00
Updated by: Codex
Staleness: stale after COD22 cue semantics change away from the single yellow dot, native continuity ownership changes again, grayscale derivation moves out of `VisionEngine`, or the cadence / perf-log contract changes materially

## Current Objective

Live-validate and tune the COD22 yellow-dot mixed acquisition v1 on top of the native baseline (`ac224b1`):

1. validate compact yellow-dot gating against the configured COD22 UI
2. tune how strongly provisional cue seeds bias acquisition versus plain crosshair distance
3. verify fused `cue + person/body-state` behavior on ADS entry, head-near lock, and partial occlusion
4. keep controller-facing output conservative: provisional cue seeds still do not directly auto-fire or become standalone confirmed targets

## Current State

Native vision remains the accepted default runtime through `vision/native_runner.py`, with Python vision still available as the fallback. Do not reopen the full-controller or whole-project C++ rewrite unless the user explicitly asks.

The live native controller-facing path is now:

- `ROI capture -> WarmScan / ActiveTrack scheduling -> TensorRT person detection -> shared grayscale + host BGRA frame -> EgoMotionEstimator -> VisionTargetSelector -> BodyStateTracker -> CenterCueRefiner -> AimEnhancement(damping-only) -> controller`

Implementation status aligned with the accepted decisions:

- `DEC-2026-04-30-001` dual-rate `WarmScan / ActiveTrack` is implemented in the current workspace
- `DEC-2026-04-30-002` native hot-path consolidation before center cue is now effectively complete
- `VisionEngine` owns per-frame grayscale derivation and shares it across ego-motion and body-state work
- `BodyStateTracker` is the native mainline continuity authority
- selector-side legacy hold still exists for standalone selector behavior, but it is now explicit `target_source="selector_hold"`
- `VisionEngine` only treats selector outputs with `target_source in {"observed", "reconstructed"}` as confirmed scan observations
- the center yellow cue remains a crosshair-near final-stage refiner only; it is not a global selector or WarmScan signal
- controller-facing native output still goes through `process_damping_only(...)`

Native perf / cadence state:

- perf logging now splits `capture / acquire / copy / pre / infer / decode / post` instead of the older mixed `roi / yolo`
- current timing interpretation:
  - `capture = acquire + copy`
  - visible live jitter is usually `AcquireNextFrame(...)` phase jitter, not TensorRT instability
- runtime cadence is now separately configurable via:
  - `VISION_CAPTURE_FPS`
  - `VISION_TRACK_FPS`
  - `VISION_WARMSCAN_FPS`
  - `VISION_SCAN_FPS`
  - `VISION_RECOVERY_SCAN_FPS`
- current startup-script defaults are:
  - `capture=240`
  - `track=160`
  - `warm_scan=20`
  - `scan=80`
  - `recovery_scan=125`

COD22-specific cue assumptions now confirmed by the user:

- the relevant UI marker is a **single yellow dot** above enemy heads
- this yellow dot is enemy-only; it does not appear on friendlies, items, or unrelated UI
- the earlier COD-style diamond/name marker can be ignored for this slice
- the COD22 blood-bar style marker does not need mainline support if the game can be configured to show the yellow dot instead
- the dot is close enough to the head to be treated as a meaningful 2D cue (`x/y` both useful) once fused with person geometry

Accepted acquisition strategy:

- use a **mixed** scheme rather than choosing between cue-only and person-only
- when only the yellow dot is visible, treat it as a `provisional seed`
- provisional seeds may steer scan/search priority and help aim-entry pickup, but they do **not** directly become controller-facing confirmed targets
- once a person detection / body-state track attaches, the yellow dot becomes a real fused 2D cue and may refine both `x` and `y`

Implementation status for the COD22 slice:

- `CenterCueRefiner` now has a distinct `detect(...)` stage
- cue detection is shape-gated for compact dot-like yellow blobs and rejects wide blood-bar geometry
- native pybind bridge exposes `NativeCenterCueRefiner.detect_rgb(...)` for regression testing
- `VisionTargetSelector` now accepts an optional cue seed and can prefer cue-aligned candidates over a crosshair-closer neighbor
- native pybind bridge exposes `NativeTargetSelector.select_xyxy_with_cue(...)` for regression testing
- `VisionEngine` now detects the yellow cue on active frames, surfaces cue debug fields even before a target is confirmed, and passes provisional cue seeds into selector scans when no active target or only weak/reacquire state is present
- once a confirmed target exists, the existing fused cue refinement path still runs after body-state and before damping-only enhancement

Environment constraint is unchanged: the native runtime still assumes `CUDA 13.1 + TensorRT 10.15.1.29`, and the earlier `failed to create TensorRT runtime` issue was an environment mismatch rather than a model-export regression.

## Verification

Most recent verified native slices:

- `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - result: native build succeeded after the COD22 yellow-dot mixed-acquisition changes
- `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_body_state_bridge tests.test_native_vision_runner -v`
  - result: `49` passed

Earlier in this session, the broader cadence / perf / startup alignments were also verified:

- `py -3 -m unittest tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_body_state_bridge tests.test_native_vision_targeting_bridge -v`
  - result: `64` passed
- `py -3 -m unittest tests.test_startup_scripts -v`
  - result: `5` passed

## Next Action

Run live validation for the COD22 yellow-dot mixed acquisition v1:

1. verify the configured yellow dot is consistently detected in real matches
2. test aim entry where the yellow dot appears before a stable person box
3. test neighboring enemies to make sure cue bias chooses the right person
4. test partial occlusion / head-glitch cases where the dot is visible before clean body geometry

Then re-evaluate whether the current defaults stay:

- `capture=240`
- `track=160`
- `scan=80`
- `recovery=125`

## Blockers

- No code blocker is currently known.
- The main dependency is correctly inserting provisional cue seeds without accidentally creating a second controller-facing target authority.
- If constructor/signature mismatches recur, first check whether an older running Python process is still holding the previous `.pyd`.

## Active Questions

- Should provisional cue seeds merely bias scan/search, or also permit stronger aim-entry attraction before body-state confirmation in the next round?
- What is the safest way to project the yellow dot into body/head geometry as distance changes, once person geometry becomes available?
- After the yellow-dot path lands, are `capture=240`, `track=160`, `scan=80`, and `recovery=125` still the best default tradeoff in live use?

## Relevant Decisions

- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-04-30-001-native-vision-dual-rate-warmscan-active-track.md`
- `.agent-context/decisions/DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md`
- `.agent-context/decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`

## Files To Read First

- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-30-001-native-vision-dual-rate-warmscan-active-track.md`
- `.agent-context/decisions/DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md`
- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `native/vision_native/src/vision_engine.cpp`
- `native/vision_native/src/center_cue_refiner.cpp`
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/body_state_tracker.cpp`
- `native/vision_native/src/ego_motion.cpp`
- `native/vision_native/src/aim_enhancement.cpp`
- `vision/native_runner.py`
- `vision/perf.py`
- `gamepad_start.bat`
- `gamepad_native_debug.bat`
- `tests/test_native_vision_body_state_bridge.py`
- `tests/test_native_vision_runner.py`
- `tests/test_native_vision_targeting_bridge.py`

## Do Not Reopen Unless Needed

- Full-project C++ rewrite
- Full controller C++ rewrite
- Python vision parity refactor
- New detector outputs for torso/chest geometry in this rollout
- Dynamic local-capture windows in this rollout
- A new native background vision thread in this rollout
- Lead / catchup reintroduction before the WarmScan / ActiveTrack comparison is validated
- Pose / segmentation / SLAM / VO/VIO scope creep for this native torso-anchor MVP
- Expanding the yellow UI cue into a global selector or WarmScan feature
- Cleaning Python fallback enhancement / occlusion logic in the same slice
- Introducing `yellow_only_target` or any controller-visible new target-source contract for this slice
- Re-promoting selector-side hold / predicted logic into the native mainline continuity authority

## Notes

- `.agent-context/` is still workspace-local and currently untracked in git status.
- The accepted yellow cue role is "crosshair-near final-stage refinement", not "global detection aid".
- `selector_hold` is now an explicit legacy selector result source; the native mainline no longer treats it as a confirmed observation.
- Live `capture` jitter readings are expected to reflect DXGI frame-phase timing unless capture is structurally decoupled from the consumer loop.
- The COD22 slice intentionally narrows cue support to the single yellow dot configuration; blood-bar compatibility is not the preferred path.
