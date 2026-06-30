# Visual Fusion And Audio Direction Research Checkpoint

Date: 2026-06-24
Scope: proposed feature tracks for the native C++ runtime:
- high-performance visual fusion / screen markers
- process audio capture, target sound recognition, and direction assistance

Update: fullscreen visual fusion has since been refined into the authoritative project design at `docs/project/FULLSCREEN_CANVAS_FUSION.md`. Use that file for the visual-fusion implementation target; this note remains the broader audio+visual research checkpoint.

## Current Repo Baseline

- The live gamepad/vision path is native C++ under `native/runtime_app`, `native/vision_native`, and `native/controller_native`.
- Existing reference package: `native/voice/SoundDirectionAssist_Codex_Pack`.
- The voice package already proposes:
  - Windows 11 / C++20 MVP.
  - WASAPI process-loopback capture first.
  - deterministic DSP baseline before ML.
  - external Win32 overlay with no injection or swapchain hook.
  - local/private-by-default logging and no raw audio persistence by default.
- Existing runtime integration point: `vision_native::VisionEngine::set_external_cue(...)` can accept an external cue, so audio direction can initially be bridged into the current vision/selector path without changing the controller/recoil contract.

## Visual Fusion / Overlay Finding

Recommended path:

1. External DirectComposition + D3D11/Direct2D overlay process.
2. Prototype can use transparent layered HWND + Direct2D/D3D11 or Dear ImGui DX11 for diagnostics.
3. Publish lightweight overlay snapshots from the runtime to the overlay process by shared memory, ring buffer, named pipe, or equivalent IPC.
4. Do not block capture, inference, or controller ticks on overlay rendering.

Rationale:

- DirectComposition is the Windows compositor path intended for high-performance bitmap/visual composition.
- External overlay avoids injecting into the game process and avoids swapchain hooks.
- Sparse markers, arcs, labels, and confidence bands should be cheap if batched and GPU-rendered.

Rejected or limited:

- DXGI Desktop Duplication and Windows Graphics Capture are capture APIs, not drawing APIs. They are useful for screen acquisition or diagnostics, not the primary overlay renderer.
- Swapchain `Present` hooks can have excellent visual timing but carry high stability, compatibility, anti-cheat, and operational risk. Treat as offline/internal only unless explicitly authorized by the target environment.
- CPU `UpdateLayeredWindow` style per-frame bitmap upload should not be the first high-refresh implementation.

## Audio Capture / Recognition / Direction Finding

Recommended path:

1. Direct WASAPI process-specific loopback capture using `ActivateAudioInterfaceAsync` plus `AUDIOCLIENT_ACTIVATION_PARAMS`.
2. Capture target process tree where possible; use global endpoint loopback only as degraded fallback.
3. Normalize to 48 kHz stereo float32 interleaved and write into a preallocated SPSC ring.
4. Run deterministic DSP first:
   - frame/window
   - band energy
   - spectral flux / onset
   - matched templates or log-mel template similarity
   - ILD and bandwise ILD for left/right direction confidence
   - GCC-PHAT as a supporting feature, not a sole truth source
5. Add ONNX Runtime only after the baseline shows which target sounds are separable and where deterministic DSP fails.

Important constraint:

- Game loopback audio is already a rendered mix, not microphone-array input. Stereo can usually support left/right bias and confidence, but not reliable absolute world azimuth, front/back, or elevation. Multichannel output may improve coarse direction if the game and endpoint preserve channel layout.

Candidate libraries:

- Capture: official Microsoft ApplicationLoopback sample as primary reference.
- FFT: KissFFT first because it is small and permissively licensed.
- Optional features: aubio if ready-made onset/MFCC saves implementation time.
- ML runtime: ONNX Runtime C++ API, CPU provider first.
- Avoid by default: FFTW GPL path, Essentia AGPL path, heavy research libraries unless licensing and dependency costs are accepted.

## Integration Shape

Proposed native modules:

- `audio_capture_win`: WASAPI process loopback and endpoint fallback.
- `audio_core`: audio format, blocks, ring buffer, timestamps.
- `audio_dsp`: STFT, band energy, ILD, GCC-PHAT.
- `audio_detector`: event detector and target-sound templates.
- `audio_direction`: direction/confidence estimator.
- `audio_fusion`: event smoothing and presentation snapshot.
- `overlay_win`: external overlay executable or library.

Runtime bridge:

- Audio producer/worker threads publish an immutable latest snapshot.
- `RuntimeLoop::run_once()` reads that snapshot non-blockingly.
- First integration can map audio direction to `VisionEngine::set_external_cue(...)`.
- Overlay reads a presentation snapshot independently and never feeds back into recoil/controller timing.

## Risks To Validate

- Process loopback availability and stability across actual games and launcher child-process trees.
- Whether target game stereo output preserves enough ILD/phase/channel information after HRTF, dynamic range compression, Windows spatial audio, and headset processing.
- Whether an external overlay is visible above the target game's chosen display mode, especially legacy exclusive fullscreen.
- Added C++20 requirement: current `native/vision_native/CMakeLists.txt` uses C++17, so audio/overlay should either be isolated as C++20 targets or the native tree should be deliberately upgraded.
- Compliance risk: keep no-injection/no-hook as the default boundary and expose a hard disable.

## Next Recommended Slice

1. Build a read-only `audio_smoke` prototype that records process-loopback PCM from a selected PID and reports format, RMS L/R, silence, dropped buffers, and latency.
2. Add synthetic audio tests for ILD sign, unknown-on-mono, low-energy suppression, and basic GCC-PHAT sanity.
3. Build an external overlay spike showing a static direction arc from a mock snapshot, then measure render overhead and display-mode compatibility.
4. Only after real captures exist, decide whether deterministic DSP is enough or whether a small ONNX classifier is justified.

## Sources And Evidence

- Local baseline: `native/voice/SoundDirectionAssist_Codex_Pack/docs/02_architecture.md`
- Local baseline: `native/voice/SoundDirectionAssist_Codex_Pack/docs/03_audio_and_ml_spec.md`
- Local baseline: `native/voice/SoundDirectionAssist_Codex_Pack/docs/04_overlay_ui_spec.md`
- Local baseline: `native/voice/SoundDirectionAssist_Codex_Pack/docs/05_security_compliance.md`
- Local runtime: `native/vision_native/include/vision_native/vision_engine.h`
- Local runtime: `native/runtime_app/runtime_loop.cpp`
- Official docs researched:
  - Microsoft ApplicationLoopback sample and WASAPI process loopback activation structs.
  - Microsoft DirectComposition and composition swapchain docs.
  - Microsoft Desktop Duplication / Windows Graphics Capture docs.
  - ONNX Runtime C++ docs.
  - KissFFT, Dear ImGui DX11 backend, OBS capture source references.
