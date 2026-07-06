# 2026-06-24 Audio/Visual Fusion Plan Archive

This archive preserves the detailed June 24-25 audio/visual fusion planning notes that were compacted out of `session-log.md` on 2026-07-06.

## Integrated Audio + Visual Fusion Plan

- User goal: read the audio/visual reference materials, produce a mature one-step audio+screen-fusion scheme, and use a `gpt-5.5` subagent for review/discussion with at least three self-review cycles.
- Added integrated authority document: `docs/project/AUDIO_VISUAL_FUSION_PLAN.md`.
- Final process model:
  - `audio_direction.exe` owns process-loopback capture, DSP/event/direction, and `AudioFusionChannel`.
  - `cod_native_runtime.exe` owns vision/controller/recoil authority and `VisionFusionChannel`.
  - `fusion_canvas.exe` only visually composes validated source channels over a fullscreen DirectComposition/D3D11 canvas.
- Authority boundary:
  - Canvas output and audio markers never feed back into controller/recoil/fire/target-selection state.
  - Any future audio-to-vision weak cue requires a separate ADR and stays unable to grant fire or recoil authority.
- Lifecycle model:
  - One launcher or future thin supervisor owns process start/restart and session nonce.
  - Producers do not launch or restart each other or the canvas.
- IPC model:
  - Same-user `Local\YoloStudy001.Fusion.<session_nonce>.<source_type>.<kind>` mappings/events.
  - Producer identity, active buffer index, release/acquire ordering, malformed/stale/wrong-nonce rejection, and per-source counters are part of the design.
- Synced split docs:
  - `docs/project/AUDIO_DIRECTION_PIPELINE.md`
  - `docs/project/FULLSCREEN_CANVAS_FUSION.md`
  - Both use producer source-channel language instead of a runtime-owned final snapshot.
- Review cycles:
  - Cycle 1 fixed visual-composition vs gameplay-fusion ownership.
  - Cycle 2 hardened lifecycle/IPC.
  - Cycle 3 found no P0/P1 blockers and accepted the plan as mature implementation authority.

## Audio Direction Pipeline

- User goal: start exploring the audio feature chain: capture target audio, extract key audio, estimate direction, and mark it on screen.
- Added authoritative project design draft: `docs/project/AUDIO_DIRECTION_PIPELINE.md`.
- Default architecture:
  - WASAPI process loopback captures the configured target process tree.
  - Audio callback writes timestamped PCM to a preallocated ring.
  - DSP/template detector extracts key events.
  - Direction estimator produces head-relative direction/confidence.
  - Runtime publishes `SourceType=Audio` primitives to fullscreen canvas.
- Default behavior deliberately keeps audio as visual direction hints, not controller authority:
  - no recoil input
  - no fire authority
  - no aim-assist target selection unless a future ADR explicitly enables a weak external cue
- Major open decisions recorded:
  - whether audio can ever feed `VisionEngine::set_external_cue`
  - whether multichannel capture is first-class
  - resampler choice
  - template calibration workflow
  - C++17/C++20 target strategy

## Fullscreen Canvas Visual-Fusion Direction

- User clarified the desired visual fusion should behave like the whole screen is a canvas, not like a small constrained overlay window.
- Mature scheme:
  - external fullscreen canvas process
  - transparent top-level HWND(s) only as compositor anchors
  - DirectComposition + D3D11 composition swapchain + Direct2D/DirectWrite drawing
  - user-facing behavior is windowless, focusless, click-through, whole-screen drawing with hotkey toggle
- Important boundary:
  - A reliable user-mode Windows implementation still needs a compositor participant.
  - True direct drawing into final scanout is not available through a normal public API without target swapchain rendering or driver/display-layer work.
- Rejected as default:
  - desktop DC/GDI drawing
  - capture-and-replay compositor
  - DXGI Desktop Duplication as renderer
  - Windows Graphics Capture as renderer
  - arbitrary hardware overlay plane
  - swapchain hooks/injection
- Follow-up architecture review expanded the scheme with DirectFlip/MPO, FSE, VRR, HDR, multi-monitor DPI, hotkey fallback, IPC, frame pacing, crash recovery, telemetry, and acceptance gates.
- Authoritative project design: `docs/project/FULLSCREEN_CANVAS_FUSION.md`.
- Research checkpoint: `research-2026-06-24-fullscreen-canvas-fusion.md`.

## Visual Fusion And Audio-Direction Research Checkpoint

- User goal: research two native C++ feature tracks:
  - high-performance unobtrusive visual fusion / screen markers
  - process-audio capture with target sound recognition plus direction assistance
- Local baseline:
  - `native/voice/SoundDirectionAssist_Codex_Pack` proposes Win11/C++20, WASAPI process-loopback capture, deterministic DSP before ONNX, and external no-injection overlay.
- Recommended visual path:
  - external DirectComposition + D3D11/Direct2D overlay process
  - prototype may use transparent layered HWND + Direct2D/D3D11 or Dear ImGui DX11 diagnostics
  - avoid swapchain hooks as default because of stability and anti-cheat risk
- Recommended audio path:
  - WASAPI process-specific loopback via `ActivateAudioInterfaceAsync` and `AUDIOCLIENT_ACTIVATION_PARAMS`
  - then KissFFT-style DSP baseline with ILD/bandwise ILD/GCC-PHAT features
  - add ONNX Runtime only after real captures prove DSP is insufficient
- Feasibility caveat:
  - game loopback is rendered stereo/multichannel mix, not microphone-array input
  - stereo can usually support left/right bias with confidence, but reliable absolute azimuth/front-back requires real validation or multichannel data
- Integration note:
  - existing `vision_native::VisionEngine::set_external_cue(...)` can serve as the first bridge from audio direction to current native vision/selector path.
- Detailed checkpoint: `research-2026-06-24-visual-audio-fusion.md`.

## Audio-Direction GitHub Scan

- Added scanner config under `E:\AI\resp_scanner\config\audio-direction-scan\`.
- Wrote report: `E:\AI\resp_scanner\reports\audio-direction-scan\2026-06-24.md`.
- First-pass scan found useful references for process-level WASAPI capture, FFT/IPC visualization, GCC-PHAT/DOA algorithms, and D3D11/DirectComposition overlays.
- Key conclusion:
  - no single repository covers capture -> event extraction -> direction -> fullscreen marker rendering
  - keep a custom native C++ pipeline
  - use `bozbez/win-capture-audio`, `I-AM-ENGINEER/ProcessAudioCaptureWin`, `thomas-quant/wasapi-loopback`, `art-jin/ESP32_S3_CAM_Mic3_PMW1`, and `anzhelion/win-overlay-d3d11` as references with license/maturity caution
- Detailed notes: `research-2026-06-24-audio-direction-github-scan.md`.
