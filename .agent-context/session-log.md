# Agent Session Log Index

Last updated: 2026-06-25T00:45:00+08:00
Updated by: Codex
Purpose: quick navigation for project continuity. The complete historical log is preserved in `session-log-full.md`.

## Reading Order

1. Read `handoff.md` for the current active objective and next action.
2. Read this index for recent context.
3. Open `decisions/` for durable architecture or scope decisions.
4. Open `session-log-full.md` only when deeper history is needed.

## Current Active Thread

- 2026-06-25T00:45:00+08:00 - Integrated audio + visual fusion plan matured through 3 review cycles.
  - User goal: read the audio/visual reference materials, produce a mature one-step audio+screen-fusion scheme, and use a `gpt-5.5` subagent for review/discussion with at least three self-review cycles.
  - Added integrated authority document: `docs/project/AUDIO_VISUAL_FUSION_PLAN.md`.
  - Final process model: `audio_direction.exe` owns process-loopback capture, DSP/event/direction, and `AudioFusionChannel`; `cod_native_runtime.exe` owns vision/controller/recoil authority and `VisionFusionChannel`; `fusion_canvas.exe` only visually composes validated source channels over a fullscreen DirectComposition/D3D11 canvas.
  - Authority boundary: canvas output and audio markers never feed back into controller/recoil/fire/target-selection state; any future audio-to-vision weak cue requires a separate ADR and stays unable to grant fire or recoil authority.
  - Lifecycle model: one launcher/future thin supervisor owns process start/restart and session nonce; producers do not launch or restart each other or the canvas.
  - IPC model: same-user `Local\YoloStudy001.Fusion.<session_nonce>.<source_type>.<kind>` mappings/events, producer identity, active buffer index, release/acquire ordering, malformed/stale/wrong-nonce rejection, and per-source counters.
  - Synced split docs: `docs/project/AUDIO_DIRECTION_PIPELINE.md` and `docs/project/FULLSCREEN_CANVAS_FUSION.md` now use producer source-channel language instead of a runtime-owned final snapshot.
  - `gpt-5.5` subagent review cycles: cycle 1 fixed visual-composition vs gameplay-fusion ownership, cycle 2 hardened lifecycle/IPC, cycle 3 found no P0/P1 blockers and accepted the plan as mature implementation authority.

- 2026-06-24T23:55:50+08:00 - Audio direction pipeline requirements and mature scheme drafted.
  - User goal: start exploring the audio feature chain: capture target audio -> extract key audio -> estimate direction -> mark it on screen.
  - Added authoritative project design draft: `docs/project/AUDIO_DIRECTION_PIPELINE.md`.
  - Default architecture: WASAPI process loopback captures configured target process tree; audio callback writes timestamped PCM to a preallocated ring; DSP/template detector extracts key events; direction estimator produces head-relative direction/confidence; runtime publishes `SourceType=Audio` primitives to fullscreen canvas.
  - Default behavior deliberately keeps audio as visual direction hints, not controller authority: no recoil input, no fire authority, and no aim-assist target selection unless a future ADR explicitly enables a weak external cue.
  - Major open decisions recorded: whether audio can ever feed `VisionEngine::set_external_cue`, whether multichannel capture is first-class, resampler choice, template calibration workflow, and C++17/C++20 target strategy.
- 2026-06-24T23:21:48+08:00 - Fullscreen canvas visual-fusion direction refined.
  - User clarified the desired visual fusion should behave like the whole screen is a canvas, not like a small constrained overlay window.
  - Current mature scheme: an external fullscreen canvas process using transparent top-level HWND(s) only as compositor anchors, with DirectComposition + D3D11 composition swapchain + Direct2D/DirectWrite drawing. User-facing behavior is windowless, focusless, click-through, whole-screen drawing with hotkey toggle.
  - Important boundary: a reliable user-mode Windows implementation still needs a compositor participant; true direct drawing into final scanout is not available through a normal public API without target swapchain rendering or driver/display-layer work.
  - Rejected as default: desktop DC/GDI drawing, capture-and-replay compositor, DXGI Desktop Duplication as renderer, Windows Graphics Capture as renderer, arbitrary hardware overlay plane, and swapchain hooks/injection.
  - Follow-up architecture review expanded the scheme with DirectFlip/MPO, FSE, VRR, HDR, multi-monitor DPI, hotkey fallback, IPC, frame pacing, crash recovery, telemetry, and acceptance gates.
  - Authoritative project design: `docs/project/FULLSCREEN_CANVAS_FUSION.md`; research checkpoint: `research-2026-06-24-fullscreen-canvas-fusion.md`.
- 2026-06-24T23:17:45+08:00 - Visual fusion and audio-direction research checkpoint recorded.
  - User goal: research two proposed native C++ feature tracks: high-performance unobtrusive visual fusion / screen markers, and process-audio capture with target sound recognition plus direction assistance.
  - Local baseline: `native/voice/SoundDirectionAssist_Codex_Pack` already proposes Win11/C++20, WASAPI process-loopback capture, deterministic DSP before ONNX, and external no-injection overlay.
  - Current recommended visual path: external DirectComposition + D3D11/Direct2D overlay process; prototype may use transparent layered HWND + Direct2D/D3D11 or Dear ImGui DX11 diagnostics. Avoid swapchain hooks as a default path because of stability and anti-cheat risk.
  - Current recommended audio path: WASAPI process-specific loopback via `ActivateAudioInterfaceAsync` and `AUDIOCLIENT_ACTIVATION_PARAMS`, then KissFFT-style DSP baseline with ILD/bandwise ILD/GCC-PHAT features; add ONNX Runtime only after real captures prove DSP is insufficient.
  - Key feasibility caveat: game loopback is rendered stereo/multichannel mix, not microphone-array input. Stereo can usually support left/right bias with confidence, but reliable absolute azimuth/front-back requires real validation or multichannel data.
  - Integration note: existing `vision_native::VisionEngine::set_external_cue(...)` can serve as the first bridge from audio direction to the current native vision/selector path.
  - Detailed checkpoint: `research-2026-06-24-visual-audio-fusion.md`.
- 2026-06-15T16:35:00+08:00 - Native tracker/controller/recoil boundary contract implemented and documented.
  - User goal: make `vision -> tracker -> controller -> recoil` directly verifiable so controller changes stop breaking recoil feel.
  - Recoil boundary: native recoil is now final feed-forward playback and must not consume target dx/dy, tracker state, target freshness, or controller correction errors.
  - Tracker boundary: tracker receives component-aware final camera motion for ego projection while manual/assist/dynamics/recoil/final output components remain separately attributed.
  - Removed the current runtime contract around recoil target-direction yield and old pre/post recoil tracker toggles; future changes must not reintroduce them without evidence and focused native contract tests.
  - Added/updated native tests for recoil input isolation, deterministic recoil playback without controller target state, component-aware final tracker motion, and recoil as final independent component.
  - Added `scripts\verify\native_pipeline_contract.bat` / `.ps1`; the script rejects known recoil/controller coupling patterns, builds native controller/runtime/benchmark targets, runs native controller tests, checks runtime `tracker_motion=component_aware_final`, and runs a short benchmark smoke.
  - `tools\check_native_cpp_gamepad_runtime.ps1` now calls the core pipeline contract by default; pass `-SkipPipelineContract` only when isolating launcher/scaffold checks.
  - Verification during this work: `scripts\verify\native_pipeline_contract.bat` PASS and `powershell -ExecutionPolicy Bypass -File tools\check_native_cpp_gamepad_runtime.ps1 -SkipPythonTests` PASS. Live gameplay recoil feel still requires user validation.
- 2026-06-04T23:42:57+08:00 - Accepted next controller-feel direction: recoil despike plus aim-assist dynamics.
  - User clarified that the shaky feel mainly appears on weapons with recoil enabled, and that current recoil parameters were tuned carefully; the goal is to remove curve spikes/micro-jitter without changing overall recoil strength, timing, or feel.
  - Accepted direction: at recoil profile read/activation time, generate a conservative despiked playback cache from profile deltas. Do not overwrite original recoil files, and do not apply broad low-pass smoothing that would soften the whole weapon curve.
  - Accepted direction: add an `AimAssistDynamicsPlugin` in the plugin list after `AIAimPlugin` and before recoil playback. It should smooth only AI assist delta (`output.right_stick - frame.manual_right_stick`) so manual input remains immediate and recoil output is not delayed.
  - Decision recorded: `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`.
- 2026-05-30T17:43:18+08:00 - Live-tested targeting/controller upgrade checkpointed as a version.
  - User live-tested the current weak-association/source-aware controller changes and reported the effect felt very strong, possibly too strong, but good enough to commit as a version.
  - Search path recorded: high-FPS detection was insufficient in practice; `deep-research-report (14).md` pointed toward detector-led short-horizon continuity; subagents split native selector, controller motion/projection, and contract/safety; GitHub/open-source comparison showed most FPS YOLO projects stay shallower; final direction became active-only weak association plus explicit target authority.
  - Added/verified source-aware controller behavior, auto-fire aim-readiness settling, weak/cue force scaling, native low-score continuation, yellow cue hold, and chest-biased `0.43` aim point.
  - Verification before commit: native build OK, 222 broader native/controller tests OK, 128 related targeting/controller/config tests OK, py_compile OK, `git diff --check` OK with LF/CRLF warnings only.
- 2026-05-29T15:25:24+08:00 - Single-target weak association and authority gating implemented in working tree.
  - Added native/Python authority fields, runner/controller fail-closed fire gates, and aim-authority target clearing.
  - Added native active-only low-score continuation with `associated_weak` output and no fire authority.
  - Kept yellow cue as auxiliary `cue_hold` continuation with no fire authority.
  - Made gamepad controller source-aware: weak/low-score does not refresh projection velocity; weak association cannot trigger ADS snap; weak/cue body-lock is lighter; predicted/no-authority targets stay manual.
  - Added config knobs for weak/cue body-lock force scale.
  - Verification: native build OK, 204 targeted regression tests OK, py_compile OK, `git diff --check` OK with LF/CRLF warnings only.
  - Worktree is dirty and not yet committed.

## Recent Implementation Baseline

- 2026-05-29 - `a196f92 Improve gamepad body-lock lateral hold`.
  - Added body-lock near-lock lateral motion assist for gamepad aiming.
  - Validation before commit: 209 gamepad tests OK, focused config/gamepad tests OK, py_compile OK, and benchmark deltas did not regress.
- 2026-05-29T14:50:45+08:00 - Targeting research and subagent review consolidated.
  - User provided `D:/Downloads/deep-research-report (14).md`.
  - Three subagents reviewed native selector, controller motion/projection, and contract/safety.
  - Decision recorded: `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`.
- 2026-05-23 - Recoil profile playback lead trial.
  - Added `[gamepad.recoil].profile_lead_ms` and dry-run/config/test coverage.
  - Live tuning of `profile_lead_ms` remained pending.
- 2026-05-19 - Native/gamepad contract hardening implemented.
  - Added atomic controller vision-state submission, source freshness, neutral gamepad shutdown, timing metrics, explicit native empty-gap clearing, config/docs cleanup, and benchmark config snapshots.
  - Split targeted verification passed; full unittest discovery timed out in this environment.

## Current Follow-Up

- 2026-06-25T01:23:41+08:00 - Performance-first fusion canvas execution decision recorded.
  - User asked to record the decision and begin execution toward a usable version.
  - Decision recorded: `decisions/DEC-2026-06-25-001-performance-first-fusion-canvas.md`.
  - Current execution direction: do not build full audio+visual fusion first and optimize later; instead build a usable vision-target canvas/publisher skeleton with performance and kill-switch boundaries from the start.
  - First usable target: show native vision target or all vision detections on a full-screen canvas while preserving native runtime hot-path isolation and keeping audio visual-only for later phases.

- 2026-06-24T23:59:00+08:00 - Audio-direction GitHub scan with `E:\AI\resp_scanner`.
  - Added scanner config under `E:\AI\resp_scanner\config\audio-direction-scan\` and wrote report `E:\AI\resp_scanner\reports\audio-direction-scan\2026-06-24.md`.
  - First-pass scan found useful references for process-level WASAPI capture, FFT/IPC visualization, GCC-PHAT/DOA algorithms, and D3D11/DirectComposition overlays.
  - Key conclusion: no single repository covers capture -> event extraction -> direction -> fullscreen marker rendering. Keep a custom native C++ pipeline; use `bozbez/win-capture-audio`, `I-AM-ENGINEER/ProcessAudioCaptureWin`, `thomas-quant/wasapi-loopback`, `art-jin/ESP32_S3_CAM_Mic3_PMW1`, and `anzhelion/win-overlay-d3d11` as references with license/maturity caution.
  - Detailed notes: `research-2026-06-24-audio-direction-github-scan.md`.

- Treat the committed native targeting/controller upgrade as the current live baseline; current target point is `body_lock_upper_body_ratio = 0.40`.
- Next implementation focus:
  - recoil playback despike at profile read/activation time, preserving tuned recoil feel
  - `AimAssistDynamicsPlugin` after `AIAimPlugin`, smoothing only AI assist delta and not manual input or recoil
- If targeting feels too strong later, tune weak/cue force scales and native weak association gates separately from recoil/assist smoothing.
- Longer next step after this commit: add richer replay/benchmark logging for source/tier, weak gate counts, cue age/score, fire request vs gate result, and controller final output.

- 2026-06-14T21:50:00+08:00 - User-confirmed C++ testing rule and next controller-mixing direction.
  - User requested recording in Agent Context Sync that C++ changes should include unit tests while modifying behavior.
  - Durable working rule: C++ controller/runtime behavior edits should add or update focused native unit tests in the same change.
  - User hypothesis for current ADS overpull: the issue is likely mixed user input arbitration, not simply ADS force being too high.
  - Next controller-feel direction: keep or increase ADS/body-lock correction strength where useful, but improve mixed-input adjudication so manual input that bends away from the AI correction is partially suppressed while helpful/aligned input is preserved.
- 2026-06-16T00:58:00+08:00 - Recoil playback feel restored after cumulative-Y regression.
  - Current structure still keeps recoil independent from tracker/controller target feedback; `NativeRecoilInput` must not grow target dx/dy/freshness fields.
  - Adapted the dev-branch recoil presentation by using per-sample Y profile delta for uncalibrated playback, instead of cumulative Y from fire start.
  - Restored live fallback feedback to a constant 30% down-pull in config.
  - Added native tests for recoil target-input exclusion, deterministic profile playback, constant fallback over 500ms+, and per-sample delta playback.
  - Verification: `cod_native_controller_tests`, `scripts\verify\native_pipeline_contract.bat`, and `git diff --check` passed.

## Full Archive Map

Use `session-log-full.md` for the full text or compact summaries of these ranges:

- 2026-05-29 - Targeting research/subagent consolidation, decision, and current weak-association implementation.
- 2026-05-20 to 2026-05-23 - Recoil runtime/profile playback follow-ups.
- 2026-05-19 - Native/gamepad contract hardening implementation and verification.
- 2026-05-18 - Multi-POV native/gamepad review and hardening plan.
- 2026-05-12 - Auto-fire manual takeover, commit discipline, native target freshness, and wide-low posture selector parity.
- 2026-05-11 - Gamepad release-tail, benchmark coverage, native/gamepad review findings, recoil app handoff.
- 2026-05-05 - Native-only vision scope, upper-body regression coverage, external cue bridge, sidecar fallback, ROI-only color copy, same-target auto-fire fix.
- 2026-05-01 - Native hotpath article reviews, rollback to native baseline, simplified native baseline, yellow-cue continuation hold.
- 2026-04-30 - Body-state v1, selector ego-warp continuity, native hotpath consolidation, COD22 yellow-dot mixed acquisition.
- 2026-04-22 to 2026-04-29 - Native vision migration chain, controller C++ rewrite deferral, and mouse live-control fixes.

## Relevant Decision Records

- `decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `decisions/DEC-2026-04-30-001-native-vision-dual-rate-warmscan-active-track.md`
- `decisions/DEC-2026-04-30-002-native-hotpath-consolidation-before-center-cue.md`
- `decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`
- `decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `decisions/DEC-2026-05-01-004-simplify-native-baseline-remove-compensation-and-restore-gray-helpers.md`
- `decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`
- `decisions/DEC-2026-05-01-006-prioritize-native-hotpath-copy-reduction-over-full-controller-cpp-rewrite.md`
- `decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`
- `decisions/DEC-2026-05-05-002-scope-active-vision-work-to-native.md`
- `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`
- `decisions/DEC-2026-06-25-001-performance-first-fusion-canvas.md`

## 2026-06-25 - Fusion Canvas Usability Correction

- User rejected the first overlay-marker result as unusable: too many markers and positions not tied to the actual game view.
- Root cause found in code and confirmed against 1920x1080 capture video: the canvas stretched normalized vision/capture coordinates across the whole screen, which magnified offsets and made markers visually unrelated to the target.
- Corrected default mode to target-dot only:
  - `FUSION_SHOW_ALL_DETECTIONS` now defaults to `0` in the launcher and runtime config.
  - `FusionChannelPublisher` now respects `show_all_` and publishes zero detections unless explicitly enabled.
  - `fusion_canvas` no longer draws the body box or bottom status text in default target mode.
  - selected target marker is rendered as a small circle using screen center plus the normalized `dx/dy` restored to capture pixels.
- Verification:
  - `fusion_canvas` Release build passed.
  - `cod_native_runtime` Release build passed after stopping the existing locked runtime process.
  - `native\vision_native\build\Release\cod_native_controller_tests.exe` passed.
  - `git diff --check` only reported existing LF/CRLF warnings.

## 2026-06-25 - Fusion Canvas Idle Behavior And Capture Direction

- User accepted that target mapping is now roughly correct, then requested two optimizations:
  - Consider recognizing only the target process/game picture rather than generic desktop screenshot capture.
  - When vision data is unavailable, canvas should either show a center crosshair or disappear.
- Current capture architecture review:
  - Native vision uses DXGI Desktop Duplication in `native\vision_native\src\dxgi_capture.cpp`.
  - It already copies only a center ROI texture, not a full GDI screenshot.
  - The next process-specific step should be window/output-aware ROI alignment: find target process HWND/window rect, map it to the selected DXGI output, and center/crop within that game rect.
  - Avoid swapchain injection/Present hooking as the first step because it is higher risk for game stability, anti-cheat compatibility, and performance isolation.
- Implemented canvas idle mode:
  - `fusion_canvas` now treats data as stale after 250ms without a new frame and clears the previous target instead of holding a stale dot.
  - Default idle behavior is `hide` (transparent/no marker).
  - Optional center crosshair mode is available with `FUSION_IDLE_MODE=crosshair` or `--idle-mode crosshair`.
  - `scripts\launch\gamepad_fusion_canvas_start.bat` defaults `FUSION_IDLE_MODE=hide` and passes `--idle-mode`.
- Verification:
  - `fusion_canvas` Release build passed.
  - Print-only launcher check shows `--idle-mode hide`.
  - `git diff --check` only reported existing LF/CRLF warnings.

## 2026-06-25 - Prevent Canvas Feedback Into Vision Capture

- User clarified that the target-process capture idea is mainly about avoiding canvas interference with vision.
- Risk model:
  - DXGI Desktop Duplication can observe the composed desktop rather than a raw game-only frame.
  - If the overlay is visible to that capture path, the marker/crosshair may be fed back into the next vision frame.
- Implemented first-line isolation:
  - `fusion_canvas` now calls `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` on its top-level overlay window.
  - Successful or failed display-affinity setup is logged in the canvas log.
  - This keeps the lower-risk current DirectComposition overlay path while trying to exclude the overlay from Windows capture APIs.
- Verification:
  - `fusion_canvas` Release build passed.
  - `git diff --check` only reported existing LF/CRLF warnings.
- Follow-up if interference remains:
  - Check `runs\fusion_canvas\fusion_canvas.log` for `display_affinity=exclude_from_capture`.
  - If unsupported/ineffective, implement process-window-aware DXGI ROI alignment next.

## Maintenance Notes

- Treat `session-log-full.md` as the append-only full archive.
- Keep this index short and current.
- Do not store secrets, tokens, cookies, keys, or unnecessary personal data.
- Do not use `.agent-context/` as a task ledger, scheduler, or external issue tracker.
