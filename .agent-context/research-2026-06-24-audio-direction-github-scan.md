# 2026-06-24 Audio Direction GitHub Scan

## Scope

Use `E:\AI\resp_scanner` to look for GitHub projects that can inform the planned audio-direction pipeline:

1. capture target process audio
2. extract key audio features/events
3. infer direction where feasible
4. publish screen overlay markers

Scanner config and artifacts:

- Config: `E:\AI\resp_scanner\config\audio-direction-scan\sources.yml`
- Scoring: `E:\AI\resp_scanner\config\audio-direction-scan\scoring.yml`
- Database: `E:\AI\resp_scanner\data\audio-direction-scan.sqlite`
- Report: `E:\AI\resp_scanner\reports\audio-direction-scan\2026-06-24.md`

The third scan pass hit the unauthenticated GitHub API rate-limit floor, so this is a useful first pass, not an exhaustive corpus.

## Strongest References

### Process / loopback capture

- `bozbez/win-capture-audio` - OBS plugin for independent application audio streams on Windows.
  - Useful for architecture and failure cases around per-application capture.
  - GPL-2.0: treat as reference only unless the licensing decision changes.
- `I-AM-ENGINEER/ProcessAudioCaptureWin` - small MIT C++ example for capturing audio from an app PID and sending volume over UDP.
  - Useful as a minimal Windows PID-capture sample.
  - Low maturity and low stars; inspect implementation before reusing any shape.
- `thomas-quant/wasapi-loopback` - MIT Rust project using WASAPI Application-Loopback include/exclude-tree semantics.
  - Useful to study modern Windows process-loopback behavior.
  - Not a C++ dependency candidate.
- `wujelly701/node-windows-audio-capture` - MIT Node native addon for per-application WASAPI capture.
  - Useful for native-addon API boundary ideas and Windows capture plumbing.

### Feature extraction / visualization

- `CorpseCode/system_audio_visualizer` - MIT Windows WASAPI loopback plus FFT in a Flutter plugin.
  - Useful for loopback-to-FFT plumbing, not target-specific event detection.
- `ALunfb/tracklist-link` - MIT companion app capturing system audio, streaming FFT over localhost websocket to overlays/visualizers/game integrations.
  - Useful for IPC and visualization pipeline patterns.

### Direction / source localization

- `art-jin/ESP32_S3_CAM_Mic3_PMW1` - Apache-2.0 C project using GCC-PHAT on a 3-mic MEMS array.
  - Compact modern GCC-PHAT reference.
  - Physical microphone-array TDOA does not directly solve game-loopback direction.
- `WenzheLiu-Speech/sound-source-localization-algorithm_DOA_estimation` - Apache-2.0 MATLAB collection of traditional DOA methods.
  - Good theory/reference for GCC-PHAT, MUSIC, and SRP-PHAT style approaches.
  - Stale and not a runtime dependency.
- `introlab/manyears` / ODAS-style projects are relevant conceptually for multi-source tracking, but they target microphone arrays, not game-rendered stereo/7.1 loopback.

### Overlay marker path

- `anzhelion/win-overlay-d3d11` - Unlicense D3D11/DirectComposition overlay sample.
  - Relevant to fullscreen marker rendering experiments.
  - Low maturity; keep the project default as the separate `fusion_canvas.exe` DirectComposition/D3D11 design from `docs/project/FULLSCREEN_CANVAS_FUSION.md`.

## Decision

No scanned repository covers the whole pipeline. The mature direction remains a custom native C++ pipeline:

- WASAPI process loopback capture using Windows application-loopback APIs where available.
- In-house feature/event detector over PCM frames, with FFT/template detection first and optional ONNX classifier later.
- Direction estimation should start from game-channel information and stereo/7.1 energy cues; GCC-PHAT/DOA repos are algorithm references only unless a real microphone array becomes part of the design.
- Audio events publish low-authority `SourceType=Audio` markers into the existing fullscreen canvas overlay. They should remain visual hints by default, not aim/fire authority.

## Follow-Up Scan

Run another `resp_scanner` pass with `GITHUB_TOKEN` set before treating this as complete. Suggested next query groups:

- `AUDIOCLIENT_ACTIVATION_PARAMS`, `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`, `ActivateAudioInterfaceAsync`
- `miniaudio`, `aubio`, `essentia`, `onnxruntime audio event detection`
- `GCC-PHAT C++`, `SRP-PHAT C++`, `ODAS`, `ManyEars`
- `DirectComposition overlay`, `D3D11 transparent overlay`, `fullscreen overlay hotkey`
