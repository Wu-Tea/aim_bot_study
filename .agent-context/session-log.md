# Agent Session Log

## 2026-04-29T22:38:34+08:00 - Runtime compatibility note and moving-POV research lead

Goal: Record the latest continuity-relevant findings without reopening architecture decisions.

What changed:
- Re-compressed the current project context into a short handoff-style summary:
  - native C++ vision + Python controller remains the accepted mainline
  - native vision already covers ROI capture, TensorRT inference, selector/color/occlusion/enhancement/autofire, and `VisionResult -> Python controller`
  - mouse tuning remains the active near-term implementation track
- Investigated a native startup failure showing `failed to create TensorRT runtime`.
- Confirmed the failure was not caused by a regenerated `.engine` file.
- Root cause was environment compatibility:
  - the repo-native stack still assumes `CUDA 13.1 + TensorRT 10.15.1.29`
  - the temporary driver change was not compatible with that runtime stack
  - returning to the prior compatible driver restored normal startup
- Discussed a possible future research direction:
  - under a moving POV, separate camera self-motion from target true motion
  - likely first-step candidate is ego-motion compensation / motion residual analysis
  - full 3D modeling / SLAM was discussed as a higher-cost alternative, not a chosen next step

User confirmed:
- Record this continuity update in project-local context.
- The prior compatible driver restores normal runtime behavior.

AI inferred:
- The driver/runtime issue should be treated as an environment compatibility constraint, not a model-export problem.
- If the moving-POV idea is explored later, a low-latency ego-motion compensation prototype is likely a better first experiment than full 3D reconstruction.

Decisions:
- No new decision record created.
- Reason: no new durable project decision was accepted; the motion-separation direction is still exploratory.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- If native runtime issues recur after future driver changes, check `nvidia-smi` and `trtexec --version` before regenerating engines.
- If the moving-POV idea becomes active work, scope it first as an ego-motion-compensation research spike.

## 2026-04-28T19:03:19+08:00 - Mouse live-control fixes and tiered follow tuning

Goal: Compress the latest mouse work into durable project context without forcing the next session to replay the full debugging chain.

What changed:
- Fixed the repeated-observation amplification bug so the host loop no longer re-consumes the same vision frame and turns one correction into repeated mouse displacement.
- Reworked mouse aiming into PD-like trajectory control:
  - desired velocity is computed from target error plus filtered error-rate
  - output is integrated through acceleration-limited substeps instead of raw displacement pulses
  - a small stabilize-only integral term remains available for close residual correction
- Added live-control arbitration fixes around manual input:
  - physical left click now takes priority over auto-fire
  - strong manual drag during ADS triggers a short manual-override window that suspends both aim assist and auto-fire
  - right-button release now force-resets synthetic left hold and drops stale outputs using `input_session_id`
- Added distance-tiered mouse follow tuning inside `controllers/mouse/ai_aim.py`:
  - `control` stays recoil-friendly near center
  - `balanced` raises follow strength just outside the control band
  - `chase` follows harder farther out by shrinking horizon and increasing accel / gain
  - confirmed soft target switches are capped to avoid over-aggressive yanks during target transitions
- Extended tests to cover the new trajectory behavior, input handoff, and config loading.
- Ran the current verification slice:
  - `python -m unittest tests.mouse.test_mouse_ai_aim tests.mouse.test_mouse_ai_aim_sequences tests.mouse.test_mouse_auto_fire tests.mouse.test_mouse_controller_host tests.mouse.test_mouse_recoil tests.mouse.test_mouse_state tests.mouse.test_mouse_plugin_chain tests.test_config_loader tests.mouse.test_mouse_benchmark_runner tests.mouse.test_mouse_benchmark_metrics -v`
  - result: `91` tests passed
- Used the isolated mouse benchmark as the tuning driver and compared several follow-tier variants (`current`, `soft1`, `soft2`, `mid1`, `mid2`, `mid3`).
- Selected `mid1` as the current uncommitted default because it improved reacquire latency and kept smoothness closer to the earlier PD tuning than the more aggressive variants.

User confirmed:
- Mouse issues should be solved in the mouse controller rather than by changing vision.
- The desired behavior is "very fast, but with a visible trajectory", closer to a closed-loop speed controller than direct displacement pulses.
- Manual mouse input should win over auto-fire and aim-assist when the user clicks or drags.
- Slightly farther-off targets should be followed more aggressively than very close targets, while close-range control should remain recoil-friendly.
- User approved the distance-tiered follow plan.
- User explicitly requested a context-compression sync.

AI inferred:
- `mid1` is the best current compromise, but it is still workspace state rather than a recorded project baseline until live validation and doc promotion happen.
- The next highest-value follow-up is live validation of the `control -> balanced -> chase` handoff, not more vision work.

Decisions:
- No new decision record was created because the new mouse defaults and benchmark promotion have not yet been separately confirmed as durable project decisions.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Live-test the current mouse defaults and confirm that the extra midrange follow feels strong but not abrupt.
- If live play is good, update the recorded mouse benchmark baseline / docs to match the current defaults.
- If live play still surfaces issues, retune the `balanced` and `chase` tier scales before reopening any broader architecture questions.

## 2026-04-30T15:25:00+08:00 - Native body-state v1 slice integrated before selector ego pass

Goal: Record the first real body-state / torso-anchor implementation slice so future sessions do not have to rediscover which parts are still scaffolding versus already wired into the live native path.

What changed:
- Added native `EgoMotionEstimator` and `BodyStateTracker` implementations to the C++ build graph and pybind module.
- Extended `VisionResult` with body-state / ego debug metadata:
  - `anchor_confidence`
  - `ego_confidence`
  - `body_state_mode`
  - `anchor_source`
  - `torso_x1/y1/x2/y2`
  - `ego_model`
- Updated `VisionEngine` to:
  - estimate frame-global ego motion from the native ROI
  - run selected detections through body-state after selector
  - use body-state output as the controller-facing anchor when available
  - fall back to bbox-level output when body-state is unavailable or drops
- Updated `vision/native_runner.py` debug overlay to render torso band and print body-state / ego metadata without changing the controller contract.
- Added / exercised focused native bridge coverage for:
  - pybind exposure of ego-motion and body-state classes
  - translation / fully masked ego-motion cases
  - torso-prior init
  - ego-warped short hold
  - torso-band debug overlay rendering
- Verified:
  - `.\tools\build_native_vision.ps1`
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_synthetic_parity tests.test_native_vision_body_state_bridge tests.test_native_vision_runner -v`
  - result: `31` tests passed

User confirmed:
- Continue according to the Native Vision body-state / torso-anchor implementation plan.
- Sync project-local context before the next selector round.

AI inferred:
- The body-state slice is now real end-to-end runtime behavior, not only unhooked scaffolding.
- The most valuable next step is selector-side ego-warp continuity and coarse predict, not more controller work.

Decisions:
- No new decision record created.
- Reason: the larger architecture and scope direction were already specified; this entry captures implementation progress and verified state rather than a newly accepted durable policy change.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Pass `EgoWarp` into selector coarse continuity so large camera pans and short detector blackouts stay on the same person before body-state takes over.
- After that lands, decide whether the next round is selector/body-state `REACQUIRE` or perf/debug instrumentation and docs refresh.

## 2026-04-30T16:05:00+08:00 - Selector continuity updated to use ego-warped coarse state

Goal: Finish the next planned native body-state prerequisite by making coarse selector continuity understand camera ego motion before body-state refinement takes over.

What changed:
- Extended `VisionTargetSelector` selection entry points to accept `EgoWarp`.
- Updated selector-side coarse continuity to use ego-warped prior state for:
  - `last_target_center_` continuity checks
  - active-target matching
  - jump gating / smoothing reference
  - short blackout coarse prediction
  - hold output when the selector is still in bbox-level fallback mode
- Added pybind bridge hooks:
  - `select_xyxy_with_ego(...)`
  - `select_xyxy_rgb_with_ego(...)`
- Updated `VisionEngine` to pass the already-estimated `ego_warp` into selector selection so live runtime behavior follows the same path as the new tests.
- Added deterministic native bridge tests covering:
  - large pan continuity with detector still present
  - short detector blackout prediction with ego warp
- Re-ran:
  - `.\tools\build_native_vision.ps1`
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_synthetic_parity tests.test_native_vision_body_state_bridge tests.test_native_vision_runner -v`
  - result: `33` tests passed

User confirmed:
- Sync context first, then optimize selector.

AI inferred:
- The selector now better matches the intended v1 layering: coarse ego-aware person continuity first, torso/body-state refinement second.
- The next meaningful question is whether to spend the next slice on `REACQUIRE` behavior or on additional debug/perf guardrails.

Decisions:
- No new decision record created.
- Reason: this is an implementation advance inside the already accepted body-state plan, not a new confirmed policy shift.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Add `REACQUIRE` only after confirming whether the current selector/body-state split is sufficient for the intended blackout and pan-stop cases.
- Consider a dedicated wrong-target-neighbor synthetic large-pan test before broadening scope.

## 2026-04-27T22:34:02+08:00 - Mouse refactor verification and startup-contract correction

Goal: Record what is now verified versus what is still only current workspace state, so future sessions do not confuse uncommitted startup/default changes with the committed native-vision baseline.

What changed:
- Ran the focused verification slice for the mouse ADS commit-hold refactor and related vision/config handoff:
  - `py -3 -m unittest tests.test_vision_runner tests.test_native_vision_runner tests.test_config_loader tests.mouse.test_mouse_state tests.mouse.test_mouse_ai_aim tests.mouse.test_mouse_ai_aim_sequences tests.mouse.test_mouse_auto_fire tests.mouse.test_mouse_recoil tests.mouse.test_mouse_plugin_chain tests.mouse.test_mouse_controller_host -v`
  - result: `79` tests passed
- Ran the CLI / startup safety slice:
  - `py -3 -m unittest tests.test_main_cli tests.test_startup_scripts -v`
  - initial result: one failure due to startup-contract mismatch
- Root cause of the failure:
  - committed history and the accepted native-vision decision still describe `gamepad_start.bat` at `VISION_CAPTURE_FPS=140`
  - the current workspace had already changed `gamepad_start.bat` to `VISION_CAPTURE_FPS=100`
  - `tests/test_startup_scripts.py` and some project docs had not yet been aligned to that workspace state
- Applied a minimal alignment for the current workspace:
  - updated `tests/test_startup_scripts.py`
  - updated `docs/project/NATIVE_VISION.md`
  - updated `docs/project/WORKLOG.md`
  - kept the change scoped to startup-contract consistency rather than changing mouse logic
- Re-ran `py -3 -m unittest tests.test_main_cli tests.test_startup_scripts -v`
  - result: `14` tests passed

User confirmed:
- "整理一下，有要同步的就同步"

AI inferred:
- The mouse refactor is verified well enough for commit-shape discussion.
- The `gamepad_start.bat = 100` change should still be treated as workspace state until the user intentionally lands or reverts it.

## 2026-04-30T21:05:00+08:00 - Native hot-path consolidation plan before center yellow cue

Goal: Record the accepted implementation order for the next native vision slice so future sessions can resume without replaying the full planning thread.

What changed:
- Re-grounded the current native controller-facing path and confirmed it is:
  - ROI capture
  - WarmScan / ActiveTrack scheduling
  - TensorRT person detection
  - host BGRA frame
  - EgoMotionEstimator
  - VisionTargetSelector
  - BodyStateTracker
  - AimEnhancement (damping-only)
  - controller
- Identified three concrete native-path inefficiencies:
  - repeated grayscale conversion of the same host frame across `VisionEngine`, `EgoMotionEstimator`, and `BodyStateTracker`
  - overlapping continuity responsibilities where `BodyStateTracker` is already the effective native continuity authority but `VisionTargetSelector` still carries legacy `try_predict / hold_or_reset` behavior
  - risk of further thickening the CPU-side host-frame path if the center cue is inserted naively
- Clarified the yellow cue semantics:
  - it is not a world-space marker above the enemy
  - it only appears inside an approximately `250x250` region near the crosshair
  - outside that region, target acquisition still depends on YOLO/body-state/user input
- Based on that clarification, re-scoped the yellow cue from a global candidate-selection idea into a near-center final-stage refiner:
  - no WarmScan usage
  - no global target discovery
  - no standalone target generation
  - only refine an already selected body-state target near center
- Locked the implementation order:
  - use plan B: hot-path consolidation first, center cue second
  - cleanup scope stays limited to the native mainline path only
  - Python fallback legacy enhancement / occlusion logic remains untouched in this slice

User confirmed:
- Use plan B rather than stacking the yellow cue directly onto the current path.
- The older YOLO-only style motion-compensation path in vision is considered legacy and should not remain part of the native mainline path.
- Cleanup scope should be limited to the native formal hot path, not Python fallback.
- Persist this plan into project-local context to avoid rereading the full planning thread later.

AI inferred:
- The right implementation order is to remove duplicate frame-derived work and collapse continuity authority onto `BodyStateTracker` before adding any new center-cue analysis.
- The yellow cue should primarily correct near-center final aim, especially on the vertical axis, rather than acting as a new global target source.
- Native `AimEnhancementPipeline::process(...)` can be treated as a legacy path during this slice as long as the formal runtime remains on `process_damping_only(...)`.

Decisions:
- Added `.agent-context/decisions/DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md`.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md`

Follow-up:
- Implement shared grayscale derivation inside `VisionEngine`.
- Remove native-path dependence on selector-side legacy continuity.
- Keep native enhancement on damping-only.
- Add the center yellow cue only after the above consolidation is in place.

Decisions:
- No new decision record was created because the `100` default has not been separately confirmed as a durable project decision.

Corrections:
- Prior handoff wording that treated `VISION_CAPTURE_FPS=100` as committed state was inaccurate.
- The accurate split is:
  - committed baseline / accepted decision: `gamepad_start.bat` at `140`
  - current workspace: `gamepad_start.bat` changed to `100`, with tests/docs aligned to that workspace state

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Decide whether to keep `gamepad_start.bat` at `100` or restore `140` before commit.
- If `100` is kept, consider whether the accepted native-vision decision record should later be superseded or corrected once the change is committed.

## 2026-04-27T00:00:00+08:00 - Documentation consolidation and context refresh

Goal: Capture the repo-wide documentation cleanup so future sessions can orient from one current entry point instead of rediscovering startup scripts, native build notes, and code layout.

What changed:
- Added a new root `README.md` as the main project entry point.
- Added `docs/project/README.md` as a local docs index for architecture notes, benchmarks, and historical plan/spec context.
- Updated:
  - `docs/project/CONTROLLER_OVERVIEW.md`
  - `docs/project/VISION_OVERVIEW.md`
  - `docs/project/GAMEPAD_OVERVIEW.md`
- Consolidated the following into current docs:
  - startup-script behavior and defaults
  - TensorRT / CUDA / native build prerequisites
  - direct CLI examples
  - top-level repository structure
  - which project docs to read first
- Corrected the current documentation view of startup defaults so `gamepad_start.bat` is represented as `VISION_BACKEND=native`, `VISION_CAPTURE_FPS=100`, `VISION_QUIT_KEY=0`, while the debug and mouse-native flows remain on their higher debug-oriented FPS defaults.

User confirmed:
- Sync this documentation pass into `.agent-context/`.

AI inferred:
- None beyond direct file evidence.

Decisions:
- No new decision record was needed because this was a documentation consolidation pass, not an architecture or scope change.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Keep `README.md` and `docs/project/README.md` aligned with any future startup-script or native-runtime changes.
- The main engineering follow-up remains verifying the in-progress mouse refactor and deciding whether to commit it as a separate change.

## 2026-04-22T20:03:07+08:00 - Native vision migration chain and current mouse follow-up snapshot

Goal: Record the recent end-to-end work chain so future sessions can resume from project-local context instead of replaying the full conversation.

What changed:
- Started from repeated gameplay perf triage on `[Perf][ADS]` and `[Perf][TRACK]` logs; the user explicitly prioritized latency and initially wanted optimization focused on screenshot + recognition rather than target/controller behavior.
- Investigated screenshot overhead, capture timing, `wait_ms`, and `age_ms`; full-screen capture plus crop was identified as wasteful, and the runtime converged around a centered `640x512` ROI with production-oriented capture settings.
- Explored Python-side structural optimizations first, including capture/inference separation and other latency-oriented cleanup, but the hot path still remained bottlenecked by Python-side capture and glue overhead.
- Pivoted to a staged native C++ vision migration:
  - `2fd5ab0` `Add native DXGI ROI capture scaffold`
  - `a0cd340` `Add native vision engine foundation`
  - `8dd3046` `Implement native capture-to-inference bridge`
  - `3e44d96` `Add native target selector core`
  - `cd90bd7` `Add native selector color classification`
  - `8c5033f` `Add native selector occlusion compensation`
  - `3251832` `Add native vision enhancement and autofire parity`
  - `ef2d361` `Add native vision synthetic parity harness`
  - `2d5ecfe` `Finalize native vision runner integration`
- Added `vision/native_runner.py`, `--vision-backend native`, `gamepad_native_debug.bat`, native startup defaults, and Python-controller bridging so the real gamepad startup path now uses native vision while Python vision remains available as a fallback.
- After accidental self-exits were observed during gameplay, disabled the default quit hotkey in startup flows with `VISION_QUIT_KEY=0`.
- Native live testing showed a large practical improvement versus the earlier Python path; the most important field for interpreting the result became lower `age_ms`, not only lower `infer_ms`.
- Reviewed whether the controller or the entire repo should also move to C++; current conclusion was to keep the hybrid architecture and not start a full controller/project rewrite now.
- The workspace now contains a separate, still-uncommitted mouse refactor covering ADS entry assist, commit-hold, reacquire bridge, `ControllerTarget.target_source` plumbing, and mouse native startup/debug scripts.

User confirmed:
- Prioritize latency over broader cleanup.
- Early optimization scope should focus on screenshot + recognition.
- Native C++ vision is acceptable and should be integrated into the real gamepad startup path.
- The default quit hotkey should be disabled on gamepad startup to avoid accidental exits.
- Do not pursue the full-controller or full-project C++ rewrite now.

AI inferred:
- `age_ms` is the best single live latency indicator for this pipeline because it reflects end-to-end freshness from capture to controller-visible output.
- A full controller/project C++ rewrite is currently lower ROI and higher hand-feel regression risk than the native vision migration was.
- The current mouse refactor should not be treated as durable project state until it is fully verified and committed.

Subagent results:
- Subagents were used selectively for bounded implementation and documentation slices during the broader native-vision work. The main thread integrated and reviewed those outputs before landing changes.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`

Follow-up:
- Verify the in-progress mouse refactor and decide whether to commit it as a separate change.
- Continue long-play validation of the native gamepad path.
- Consider exposing `preprocess_ms` directly in the printed `[Perf]` line later.

## 2026-04-30T16:13:20+08:00 - Dual-rate native vision plan synced against current workspace

Goal: Reconcile the current workspace with the new native vision dual-module rollout plan and refresh project-local continuity so the next session can tell what is already implemented versus what is still cleanup or validation work.

What changed:
- Re-read the existing handoff, recent session log, and accepted decision records before scanning repo docs, git state, and the current native vision diff.
- Confirmed that the workspace already implements most of the posted dual-rate design in code:
  - `VisionEngine` now exposes `Idle`, `WarmScan`, and `ActiveTrack`
  - the Python bridge uses `set_mode(...)` when available and still keeps `set_aiming(bool)` as the compatibility path
  - `WarmScan` refreshes `WarmScanSnapshot` on a `50ms` cadence and does not emit controller targets when aim is off
  - `WarmScanSnapshot` stores up to three ranked candidates as `TargetKeyframe` plus score
  - `TargetKeyframe` already includes body box, torso box, anchor prior, torso patch, frame/time metadata, score, and source, with torso data still derived from the current person box rather than new detector outputs
  - aim entry can prime `BodyStateTracker` from a recent warm candidate and set `prewarm_used=true`, while still forcing a real active scan before treating the target as confirmed controller output
  - non-scan active updates run through `BodyStateTracker.update_interframe(...)`, and true scan misses advance recovery through `update_scan_miss(...)`
  - controller-facing native output now goes through the damping-only enhancement path instead of lead plus catchup plus damping
- Ran focused verification against the current workspace:
  - `py -3 -m unittest tests.test_native_vision_runner tests.test_native_vision_targeting_bridge tests.test_startup_scripts -v`
    - result: `32` passed, `1` failed
    - failure: `tests.test_startup_scripts.StartupScriptTests.test_gamepad_native_debug_uses_native_backend_and_debug_window` still expects `set "VISION_CAPTURE_FPS=140"` while `gamepad_native_debug.bat` now defaults to `240`
  - `py -3 -m unittest tests.test_native_vision_body_state_bridge -v`
    - result: `13` passed
- Confirmed the existing green native coverage already reaches the plan's main guardrail themes:
  - ego-warped large-pan continuity
  - short detector blackout prediction
  - wrong-target neighbor protection during ego pan
  - pan-stop hold overshoot limiting
  - lower-screen muzzle-flash masking
  - interframe updates not consuming scan-miss budget
  - scan-miss updates reaching `drop` only after the intended budget is exhausted
- Added a new accepted decision record for the dual-rate `WarmScan` / `ActiveTrack` rollout plan so the architecture choice is recorded separately from implementation progress.

User confirmed:
- The posted native vision dual-module plus non-aim prewarm rollout plan is the current plan that project-local context should reflect.
- Update `handoff`, `session-log`, and related context files based on the current workspace and that plan.

AI inferred:
- Most of the first-pass architecture is already present in the workspace; the highest-value remaining work is contract cleanup and rollout validation rather than inventing a new design.
- The `240` startup default looks intentional because the posted acceptance entry and current scripts both point to `240`, but one startup-script assertion has not been aligned yet.
- The new plan should be recorded as an architecture decision separately from the current implementation snapshot so unfinished cleanup is not mistaken for missing design direction.

Decisions:
- Created `.agent-context/decisions/DEC-2026-04-30-001-native-vision-dual-rate-warmscan-active-track.md`.

Subagent results:
- None.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-30-001-native-vision-dual-rate-warmscan-active-track.md`

Follow-up:
- Decide whether `gamepad_start.bat` and `gamepad_native_debug.bat` are both intended to stay at `VISION_CAPTURE_FPS=240`.
- If `240` is correct, align `tests/test_startup_scripts.py` and any remaining docs that still assume `140`.
- Re-run the native verification slice after that cleanup, then validate the live `gamepad_start.bat` / `gamepad_native_debug.bat` paths against the WarmScan / ActiveTrack acceptance scenarios.

## 2026-04-30T19:55:47+08:00 - DEC-002 execution closed with selector-hold isolation

Goal: Record the actual completion state of `DEC-2026-04-30-002` so future sessions do not treat the native hot-path consolidation as still-planned work.

What changed:
- Audited the accepted `DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md` requirements against the current native workspace.
- Confirmed the larger implementation pieces were already present in code:
  - `VisionEngine` owns and shares per-frame grayscale derivation
  - `BodyStateTracker` gray-frame entry points are wired into the native hot path
  - `EgoMotionEstimator` consumes shared gray input
  - `CenterCueRefiner` already runs after body-state and before damping-only enhancement
  - native perf timing and dual-rate cadence wiring are already in place
- Identified the remaining mainline cleanup gap:
  - standalone selector legacy hold output was still surfacing as `target_source="observed"`
  - `VisionEngine::observation_confirmed(...)` still treated anything except `"predicted"` as a confirmed scan observation
- Closed that gap with a narrow native-path fix:
  - marked selector legacy hold output as explicit `target_source="selector_hold"`
  - tightened `VisionEngine::observation_confirmed(...)` to accept only `observed` and `reconstructed`
  - left legacy selector hold behavior available for standalone selector bridge/testing paths without letting it reclaim mainline continuity authority
- Updated bridge expectations to match the corrected semantics:
  - large pan without ego warp now surfaces `selector_hold`
  - reacquire after an empty detector gap requires a fresh pickup confirmation again
- Rebuilt the native module after the change.
- Verified:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
    - result: native build succeeded
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_body_state_bridge tests.test_native_vision_runner -v`
    - result: `46` passed

User confirmed:
- Record this completion state in project-local context.

AI inferred:
- `DEC-2026-04-30-002` can now be treated as completed implementation work, not only an accepted plan.
- The next high-value work is live validation and cadence tuning, not more native hot-path architecture cleanup.
- Keeping `selector_hold` as an explicit legacy source is useful for bridge/testing clarity even though the native mainline no longer treats it as a confirmed observation.

Decisions:
- No new decision record created.
- Reason: this was execution and verification of the already accepted `DEC-2026-04-30-002`, not a newly accepted architectural change.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Live-validate `gamepad_start.bat` / `gamepad_native_debug.bat` with the current cadence defaults (`capture=240`, `track=160`, `scan=80`, `recovery=125`).
- Decide later whether standalone selector legacy hold should be removed entirely or kept as a non-mainline/testing behavior.

## 2026-04-30T20:22:00+08:00 - COD22 yellow-dot mixed acquisition accepted before implementation

Goal: Preserve the newly approved COD22 cue-acquisition direction in project-local context before implementation starts, so a broken session does not force the next run to reconstruct the design from screenshots and chat.

What changed:
- User clarified the relevant COD22 marker behavior:
  - the preferred in-game configuration shows a **single yellow dot** above enemy heads
  - the earlier blood-bar style marker is not the target compatibility mode for this slice
  - the yellow dot is enemy-only
- Discussed three acquisition stances:
  - cue only as a late-stage refiner
  - fully cue-driven standalone targeting
  - mixed acquisition with cue-only provisional seeds plus later person/body-state fusion
- User selected the **mixed** scheme.
- Locked the intended semantics:
  - yellow dot alone can participate in acquisition as a provisional seed
  - provisional seeds may bias search / scan / aim-entry pickup
  - provisional seeds do not directly become confirmed controller targets or auto-fire triggers
  - once person/body-state is attached, the yellow dot becomes a true fused 2D cue for both `x` and `y`
- Recorded the freshly committed native baseline as the implementation starting point:
  - `ac224b1` `Consolidate native vision hot path and cadence control`
- Added a new accepted decision record for the COD22 yellow-dot mixed-acquisition strategy.

User confirmed:
- For COD22, compatibility can focus on the single yellow dot configuration.
- The yellow dot should be treated as a useful 2D cue close to the head.
- Acquisition should use the mixed scheme, not a pure refiner-only or pure cue-only controller path.
- Record this direction before implementation proceeds.

AI inferred:
- Dot-shape gating should replace the earlier more generic yellow-pixel cue assumption in the COD22 path.
- The safest first implementation is to keep provisional cue seeds inside native scan/search state until person/body-state confirmation arrives.
- Supporting blood-bar mode in the same slice would add complexity without matching the user’s preferred runtime configuration.

Decisions:
- Added `.agent-context/decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`

Follow-up:
- Add failing tests for compact yellow-dot gating and provisional-seed acquisition behavior.
- Implement the minimal native-path cue seed + fused confirmation flow on top of commit `ac224b1`.

## 2026-04-30T21:08:00+08:00 - COD22 yellow-dot mixed acquisition v1 implemented

Goal: Record the first concrete COD22 yellow-dot implementation slice so a later session can resume from code reality rather than only the earlier design record.

What changed:
- Added a distinct yellow-cue detection stage inside `CenterCueRefiner`.
- Narrowed COD22 cue acceptance to compact dot-like yellow blobs:
  - dot-shaped yellow regions are accepted
  - wide blood-bar style yellow regions are rejected
- Exposed `NativeCenterCueRefiner.detect_rgb(...)` through pybind so cue-shape behavior can be regression-tested directly.
- Added an optional cue-seed path to `VisionTargetSelector`:
  - selector scoring can now prefer a candidate aligned with the cue seed over a purely crosshair-closer neighbor
  - pybind exposes this for tests via `NativeTargetSelector.select_xyxy_with_cue(...)`
- Updated `VisionEngine` active-track flow:
  - detect yellow cue on active host frames even before a target is confirmed
  - surface yellow cue debug metadata in `VisionResult` when the cue is present
  - pass a provisional cue seed into selector scans when there is no active target or only weak/reacquire/drop continuity
  - keep controller-facing behavior conservative: provisional cue seeds still do not become standalone confirmed targets
  - preserve fused cue refinement after body-state once a real target exists
- Wrote and verified new regression coverage for:
  - compact-dot detection
  - blood-bar rejection
  - cue-seed candidate preference over a crosshair-closer neighbor
- Rebuilt the native module after implementation.
- Verified:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
    - result: native build succeeded
  - `py -3 -m unittest tests.test_native_vision_body_state_bridge.NativeVisionBodyStateBridgeTests.test_center_cue_refiner_detects_compact_yellow_dot_for_cod22_mode tests.test_native_vision_body_state_bridge.NativeVisionBodyStateBridgeTests.test_center_cue_refiner_rejects_blood_bar_shape_in_cod22_dot_mode tests.test_native_vision_targeting_bridge.NativeVisionTargetingBridgeTests.test_selector_prefers_candidate_aligned_with_cue_seed_over_crosshair_closer_neighbor -v`
    - result: `3` passed
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_body_state_bridge tests.test_native_vision_runner -v`
    - result: `49` passed

User confirmed:
- Proceed with implementation after recording the mixed COD22 cue strategy.
- Keep the acquisition path mixed rather than cue-only controller targeting.

AI inferred:
- This is a meaningful v1 of mixed acquisition, but still a conservative one: cue seeds currently bias scan selection rather than creating a stronger standalone pre-lock attraction.
- The next engineering value is live tuning, especially around how strong cue-seed bias should be when the cue appears before the person detector stabilizes.

Decisions:
- No new decision record created.
- Reason: this entry implements the already accepted `DEC-2026-04-30-003`, rather than introducing a new durable policy change.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`

Follow-up:
- Run live COD22 validation with the yellow-dot UI setting enabled.
- Decide whether the next round should strengthen provisional cue influence or keep it limited to selector scan bias.

## 2026-05-01T12:52:40+08:00 - Article review recorded against current native vision birth path

Goal: Compare the article in `D:/Downloads/deep-research-report (4).md` against the local native vision implementation, then preserve the useful optimization direction in project-local context before the next tuning round loses the reasoning.

What changed:
- Re-read `.agent-context/handoff.md`, the COD22 mixed-acquisition decision, and the recent session log before comparing the article against the current native implementation.
- Read the article as a safety-neutral real-time perception architecture review focused on detector birth path, local-tracker continuation, auxiliary-cue gating, and output-tier separation.
- Compared the article's recommendations against the current native stack in:
  - `native/vision_native/src/vision_engine.cpp`
  - `native/vision_native/src/target_selector.cpp`
  - `native/vision_native/src/body_state_tracker.cpp`
  - `native/vision_native/src/center_cue_refiner.cpp`
  - `vision/native_runner.py`
- Confirmed that the current workspace already matches the article's broad architecture direction:
  - detector, tracker, and cue are separated into distinct stages
  - body-state owns continuation quality
  - cue remains useful as scan bias and post-body-state refinement
- Identified the main remaining optimization opportunity as the birth path rather than the continuation path:
  - `VisionTargetSelector` still requires two pickup-confirm frames before first lock because `kPickupConfirmFrames=2`
  - cue alignment currently acts as a hard gating condition in cue-seeded selection paths instead of only adding preference or acceleration
  - `VisionEngine` still creates a `yellow_cue` fallback target when no other target exists
  - `vision/native_runner.py` currently forwards any native `has_target` result to `controller.update(...)`, so the fallback cue result is not yet cleanly tiered away from controller-facing output
- Ranked the highest-ROI follow-up options:
  1. restore a detector-owned provisional fast path for first valid person observations
  2. demote cue alignment from hard veto to soft bonus outside weak / reacquire handling
  3. split provisional `yellow_cue` visibility from controller-trusted output
  4. consider confidence-triggered or confidence-weighted scan refresh only after the birth path is cleaned up

User confirmed:
- Record this comparison in project-local context.

AI inferred:
- The current native design is not missing a major subsystem; it is mostly paying extra latency through conservative birth-path gates.
- The largest practical mismatch between accepted COD22 intent and current implementation is that `yellow_cue` can still surface as a downstream `has_target` result even though the accepted intent was to keep provisional cue seeds out of standalone controller-facing authority.
- A narrow birth-path optimization slice is higher ROI than adding more continuation machinery right now.

Decisions:
- Added proposed decision `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md`.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md`

Follow-up:
- Decide whether to implement the birth-path optimization slice before the next COD22 live-validation run.
- If implemented, add regression coverage for immediate provisional person pickup, cue-seed soft-bonus behavior, and controller gating that excludes `yellow_cue` provisional outputs.

## 2026-05-01T13:33:34+08:00 - Article review recorded for side-running enemy recognition and tracking

Goal: Compare the article in `D:/Downloads/deep-research-report (5).md` against the local native implementation, with emphasis on sideways-running enemies, short occlusion, lateral pan continuity, and neighbor-switch resistance.

What changed:
- Re-read `.agent-context/handoff.md` before the comparison so the new article review stayed aligned with the existing COD22 native architecture and the earlier birth-path notes.
- Read the article as a low-latency FPS tracking adaptation of BoT-SORT / OC-SORT / ByteTrack ideas, with emphasis on camera-motion compensation, selected-target torso-local tracking, and lightweight reacquire policy instead of a full MOT stack.
- Compared the article's recommendations against the current native stack in:
  - `native/vision_native/src/body_state_tracker.cpp`
  - `native/vision_native/src/ego_motion.cpp`
  - `native/vision_native/src/vision_engine.cpp`
  - `native/vision_native/src/target_selector.cpp`
  - `tests/test_native_vision_body_state_bridge.py`
  - `tests/test_native_vision_targeting_bridge.py`
- Confirmed that the current workspace already matches the article's preferred high-level insertion order for lateral targets:
  - ego-motion runs before selector arbitration
  - only the selected target receives torso-local continuation work
  - the native tests already cover large pan continuity, partial occlusion, hold/reacquire behavior, and pan-stop overshoot
- Identified the likely highest-ROI improvements for sideways-running enemies:
  1. add masked torso-local patch tracking because the current patch matcher is unmasked in both observed and unobserved paths
  2. add explicit reacquire-time residual-velocity reset behavior rather than relying only on conservative velocity decay through loss
  3. retune lateral recovery window and velocity limits if live tests still show sideways-run dropouts
  4. consider reducing ego-motion freedom from full affine toward similarity / partial affine only after the local tracker changes are measured
  5. keep torso-local weak color cue and reacquire-only low-score detections as secondary refinements
- Explicitly rejected the idea that this article implies importing full BoT-SORT, ReID, dense optical flow, or SLAM machinery into the hot path.

User confirmed:
- Record this comparison in project-local context.

AI inferred:
- The current native stack is already architecturally close to the article's recommendation; the remaining lateral-motion value is in tracker hygiene and reacquire policy, not in a missing major subsystem.
- The cleanest next experiment for side-running targets is probably inside `BodyStateTracker` rather than `VisionTargetSelector` or the controller-facing contract.
- Birth-path latency and lateral continuity remain related but separable optimization tracks, so recording them as separate proposed decisions is useful.

Decisions:
- Added proposed decision `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md`.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md`

Follow-up:
- Decide whether the next engineering slice should stay focused on birth-path latency first, or whether side-running continuity now justifies a narrow `BodyStateTracker` experiment in parallel.
- If the lateral slice is implemented, add focused regression coverage around masked patch behavior, sideways-run reacquire latency, and neighbor-switch resistance during lateral cross scenes.

## 2026-05-01T16:48:00+08:00 - Corrected rollback to the native pre-hotpath baseline after an intermediate backend mistake

Goal: Restore the controller-facing default runtime to the last known-good native YOLO baseline rather than the newer 2026-04-30 evening experiment branch, and record the rollback error honestly so later sessions do not confuse the Python fallback with the requested native rollback.

What changed:
- The user reported that the current ROI/native experiment branch was performing materially worse than the earlier pure detector behavior on live material and asked for a rollback.
- I first misinterpreted that request and briefly changed the startup defaults toward the Python backend instead of keeping the runtime native.
- The user corrected the intent explicitly: the rollback target should remain the native YOLO path, not the Python path.
- Investigated git history to identify the relevant boundaries:
  - native runtime integration began on 2026-04-21
  - the later hotpath/cue experiment branch landed on 2026-04-30 via `ac224b1` and `2b8a35b`
  - the practical pre-hotpath rollback target is commit `708c253` (`Prune mouse benchmark artifacts`, 2026-04-30 13:22:09 +0800)
- Restored the runtime, startup scripts, tests, and native source files affected by the 2026-04-30 evening experiment branch back to the `708c253` state.
- Verified the corrected rollback with fresh evidence:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
    - result: native build succeeded
  - `py -3 -m unittest tests.test_startup_scripts tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_targeting_bridge -v`
    - result: `44` passed

User confirmed:
- The rollback target should be the native YOLO path, not the Python backend.
- Commit the corrected rollback.
- Record the mistake and the resulting decision in project-local context.

AI inferred:
- The earlier Python-default rollback was an execution error, not a product decision, and should be recorded as a correction rather than treated as durable policy.
- The 2026-04-30 evening hotpath/yellow-cue branch is no longer trustworthy as the live baseline until the reverted native baseline is re-validated in live play.
- The 2026-05-01 article-driven optimization proposals remain useful but are now deferred behind baseline recovery.

Decisions:
- Added accepted decision `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`.
- Marked `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md` as superseded for the current default runtime path.
- Marked `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md` as superseded for the current default runtime path.

Context files updated:
- `.agent-context/handoff.md`
- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`

Follow-up:
- Run live COD22 validation on the reverted native baseline before reviving any yellow-cue or continuity-heavy experiment branch.
- If a later experiment restarts yellow-cue support, introduce it as an auxiliary native YOLO layer rather than assuming the 2026-04-30 evening architecture should resume unchanged.
