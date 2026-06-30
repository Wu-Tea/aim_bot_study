# Audio + Visual Fusion Plan

Last updated: 2026-06-25
Status: mature design target after 3 review cycles

## Purpose

This document defines the integrated target scheme for:

1. process-specific game audio capture
2. key audio event detection
3. coarse audio direction estimation
4. fullscreen screen-space visual fusion

The user-facing behavior is a whole-screen assistive canvas. It is not a small HUD window. It can draw directional markers anywhere on the active monitor, can be toggled with a hotkey while the target app has focus, and must not block the existing native vision/controller runtime.

## Hard Boundaries

Allowed by default:

- Windows user-mode public APIs.
- WASAPI process loopback for target process-tree audio capture.
- External sidecar processes.
- DirectComposition / D3D11 / Direct2D / DirectWrite rendering.
- Shared-memory latest-state IPC.
- Local-only DSP and optional local ONNX inference.

Not allowed by default:

- DLL injection, render hooks, swapchain hooks, kernel drivers, game memory reads, packet parsing, or anti-cheat evasion.
- Hidden capture, anti-screenshot, anti-recording, or "undetectable overlay" behavior.
- Raw audio persistence unless an explicit future calibration mode is enabled by the user.
- Audio-driven fire authority, recoil authority, or aim target selection.

## Top-Level Architecture

Use three separately failing components:

```text
target game process tree
  |
  | WASAPI process loopback
  v
audio_direction.exe
  | writes AudioFusionChannel
  v
shared-memory source channels
  ^
  | writes VisionFusionChannel
cod_native_runtime.exe
  |
  v
fusion_canvas.exe
  | reads source channels, visually composes source primitives locally
  | transparent per-monitor DComp/D3D11 canvas
  v
DWM / display path
```

Terminology:

- **Visual composition** means the canvas combines already-authorized display primitives by source, priority, timestamp, TTL, confidence, and user settings.
- **Gameplay fusion** means any association that can affect target selection, aim-assist, fire authority, recoil, or controller output.

In v1, `fusion_canvas.exe` owns visual composition only. `cod_native_runtime.exe` remains the only process that can own gameplay fusion, and audio-to-gameplay fusion is disabled unless a future ADR explicitly enables a weak cue.

### Why three components

- `cod_native_runtime.exe` remains focused on vision, controller, recoil, and gamepad output.
- `audio_direction.exe` can fail, reconnect, or run slower without touching controller timing.
- `fusion_canvas.exe` can render, toggle, resize, or recover graphics devices without affecting either capture or vision.
- Each producer owns one source channel. The canvas visually composes read-only latest source states; there is no shared final snapshot with multiple writers.

## Process Responsibilities

### `audio_direction.exe`

Responsibilities:

- Resolve target process by configured PID or executable name.
- Verify process creation time before attaching.
- Activate WASAPI process loopback for the target process tree.
- Normalize incoming PCM into the canonical processing format.
- Run detection, direction estimation, smoothing, and confidence gating.
- Publish `AudioFusionChannel` latest-state data.
- Publish only metrics and coarse event state, not raw audio.

Hot path rule:

- The WASAPI capture callback copies PCM into a preallocated SPSC ring and updates counters only.
- It must not allocate, log, lock, run FFT, run ONNX, call file I/O, or wait on IPC.

### `cod_native_runtime.exe`

Responsibilities:

- Continue owning native vision, tracker, controller, recoil, and output.
- Publish `VisionFusionChannel` primitives from existing vision state.
- Optionally read audio cue state in the future only through an explicit disabled-by-default weak-cue bridge.
- Never wait for audio or canvas.

Default rule:

- Audio is visual-only. It does not call `set_external_cue(...)` unless a future ADR enables a weak bridge.

### `fusion_canvas.exe`

Responsibilities:

- Own Win32 message loop, global hotkeys, DirectComposition, D3D11, Direct2D, DirectWrite, display topology, DPI/HDR handling, and diagnostics UI.
- Read source channels from vision/audio producers.
- Visually compose primitives by source priority, timestamp, TTL, confidence, and user settings.
- Draw markers over the whole monitor canvas.
- Keep working if either producer disconnects.

## Authority Boundary

The canvas is a display consumer, not a control producer.

- Canvas-composed output is never read back by `cod_native_runtime.exe`.
- Canvas-composed audio markers never feed controller, recoil, fire gate, target tracker, or target selector state.
- `audio_direction.exe` never writes to controller or recoil state.
- If audio ever becomes a weak vision cue, that bridge must be implemented inside `cod_native_runtime.exe`, disabled by default, covered by an ADR, and unable to grant fire or recoil authority.
- The default data flow is one-way: producers -> source channels -> canvas -> DWM.

## Process Lifecycle And Supervision

In v1, a single lifecycle owner starts and monitors sidecars. The lifecycle owner may be the existing launcher script at first and a thin native supervisor later.

Rules:

- `cod_native_runtime.exe` publishes `VisionFusionChannel` only.
- `audio_direction.exe` publishes `AudioFusionChannel` only.
- `fusion_canvas.exe` consumes configured source channels only.
- Producers do not launch, restart, or terminate each other.
- Producers do not race to launch or restart the canvas.
- The lifecycle owner creates the session nonce and launches configured processes with matching channel names.
- If `fusion_canvas.exe` exits, producers continue publishing and the lifecycle owner may relaunch the canvas with bounded backoff.
- If a producer exits, the canvas clears that source after TTL and remains available for other sources.
- If the lifecycle owner exits, already-running processes may continue, but no process assumes new ownership of restart duties.

Initial launch shape:

```text
launcher/supervisor
  |-- cod_native_runtime.exe --fusion-session <nonce> --vision-channel <name>
  |-- audio_direction.exe --fusion-session <nonce> --audio-channel <name>
  |-- fusion_canvas.exe --fusion-session <nonce> --read-channel <vision> --read-channel <audio>
```

## Audio Capture Design

Primary API:

- `ActivateAudioInterfaceAsync`
- `AUDIOCLIENT_ACTIVATION_PARAMS`
- `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`
- `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`

Supported baseline:

- Windows 11 x64 target.
- Windows build must support process loopback activation.
- Endpoint loopback with `AUDCLNT_STREAMFLAGS_LOOPBACK` is fallback only and must be marked degraded because it captures the endpoint mix.

Process-loopback acceptance details:

- Check OS build and API availability before activation; below the supported build, enter `Degraded` or `Error` with a user-actionable message.
- Use `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK` as the activation device interface path.
- Pass `AUDIOCLIENT_ACTIVATION_PARAMS` through a `PROPVARIANT` `VT_BLOB`.
- Use a dedicated COM lifecycle for async activation; the completion handler must tolerate MTA/free-threaded callback delivery, cancellation, timeout, target exit, and shutdown races.
- If activation completes after shutdown begins, the callback must not access released state.
- Treat include-process-tree semantics as target-time semantics: resolve launchers and children deliberately, and verify PID plus creation time before attaching.
- If the target exits, stop capture and clear active events instead of silently attaching to a reused PID.
- If the target has no active render stream, publish `Silent` rather than an error.
- If the endpoint format, channel mask, or default device changes, reconnect with bounded retry and preserve diagnostics.
- Protected/DRM/silent streams and unsupported formats must degrade honestly, not crash or bypass.

Capture states:

| State | Meaning |
|---|---|
| `Idle` | No target configured or found. |
| `ResolvingTarget` | Looking for PID/executable and validating process metadata. |
| `Activating` | Async COM activation pending. |
| `Capturing` | PCM is flowing. |
| `Silent` | Stream exists but RMS is below threshold. |
| `Degraded` | Endpoint fallback, mono input, unsupported channel layout, repeated overflow, or high latency. |
| `Disconnected` | Target exited or render stream disappeared. |
| `Error` | Bounded retries exhausted. |

## Audio Processing Format

Canonical worker format:

- 48 kHz
- `float32`
- interleaved PCM
- preserve input channel count up to 8 channels
- derive a stereo view for baseline detection and direction

Rules:

- Do not independently normalize left/right channels. Use shared gain only.
- Mono may feed event detection, but direction must become `Unknown`.
- `WAVEFORMATEXTENSIBLE` channel masks must be honored when available.
- For 5.1/7.1 input, preserve speaker-channel energy features instead of immediately downmixing away direction cues.
- Prefer accepted capture format `float32` / 48 kHz / `WAVEFORMATEXTENSIBLE` when the endpoint supports it.
- If capture must use mix/client-accepted format, convert to canonical format in `audio_core` and preserve channel-mask metadata.
- Resampler choice is a stop gate before M1 implementation completion; fallback conversion must not silently discard multichannel layout while reporting multichannel capability.

## Key Audio Event Detection

The first production-capable detector is deterministic DSP plus templates.

Pipeline:

1. shared gain normalization
2. optional DC removal
3. 20-25 ms Hann window
4. 10 ms hop
5. STFT via KissFFT
6. band energy and spectral flux
7. log-mel template similarity
8. onset gate
9. class-specific hysteresis
10. cooldown and event tail suppression

Template bank:

```text
config/audio_templates/<profile>/
  manifest.json
  positives/<class_name>/*.wav
  negatives/background/*.wav
  negatives/ui/*.wav
  negatives/voice/*.wav
```

ONNX is optional and later:

- default build must work with ONNX disabled
- CPU provider first
- bounded inference queue
- no runtime model download
- model metadata must include feature config hash, labels, dataset version, license, and thresholds
- ONNX telemetry must be explicitly disabled when `OrtEnv` is created, or the build must be proven telemetry-free.
- ONNX enablement requires an offline/firewalled smoke test showing no outbound network attempt.

## Direction Estimation

The direction estimator outputs confidence-gated listener-relative bins, not world coordinates.

```cpp
enum class AudioDirectionBin {
    Unknown,
    Left,
    Center,
    Right,
    FrontLeft,
    Front,
    FrontRight,
    RearLeft,
    Rear,
    RearRight,
};
```

### Stereo baseline

Use:

- broadband ILD
- bandwise ILD median, variance, and sign consistency
- GCC-PHAT lag and peak ratio as supporting evidence
- near-mono suppression
- conflicting-band suppression

Stereo output contract:

- reliable target is `Left / Center / Right / Unknown`
- front/back/elevation are not promised from stereo loopback
- direction confidence must fall to `Unknown` when the evidence conflicts
- "No direction" is better than a wrong direction: low-confidence, conflicting, near-mono, or multi-source windows must become `Unknown`.
- Evaluation must report unknown coverage, accepted-only accuracy, false directional accept rate, and left/right sign accuracy together.

### Multichannel path

If process loopback provides 5.1/7.1 channel layout:

- compute target-band energy per channel
- map dominant stable channel patterns to coarse bins
- allow `Front*` and `Rear*` bins only when channel mask and event confidence are valid

Multichannel is opportunistic in v1:

- Preserve source channels and channel masks from day one.
- Ship reliable stereo `Left / Center / Right / Unknown` first.
- Treat 5.1/7.1 front/rear bins as experimental until real process-loopback captures prove stable channel content for the target profile.

### Confidence

```text
confidence =
  event_probability
  * snr_score
  * direction_consistency
  * channel_separation_score
  * stability_score
```

Below threshold:

- publish `Unknown`
- suppress normal marker
- show debug-only diagnostics if enabled

## Fusion Source Channels

Use one shared-memory channel per producer.

Producer channels in v1:

- `VisionFusionChannel`: written by `cod_native_runtime.exe`.
- `AudioFusionChannel`: written by `audio_direction.exe`.

The canvas may visually compose multiple channels, but no producer writes another producer's channel.

Source channel IPC contract:

- Name mappings and events as `Local\YoloStudy001.Fusion.<session_nonce>.<source_type>.<kind>`.
- `<kind>` is `Mapping` or `Event`.
- The session nonce is generated by the lifecycle owner per launch session.
- Producers create their own mapping and event; the canvas opens them as a reader.
- Security descriptors restrict access to the current user SID by default.
- No `Global\` objects or administrator privileges are required in v1.
- If `CreateFileMappingW` or `CreateEventW` reports `ERROR_ALREADY_EXISTS` for a producer-owned object, fail the session closed instead of attaching to a possibly pre-created object.
- The canvas opens producer mappings read-only where possible.
- Producer identity is bound to expected PID, creation time, and configured image path or image-path hash.
- Header validation includes magic, version, total size, source type, primitive count, active buffer index, sequence, QPC frequency, and producer identity.
- Producers publish with release ordering; the canvas reads with acquire ordering.
- Producer restart changes producer identity; the canvas treats it as a fresh channel even if sequence resets.
- Stale, malformed, wrong-version, wrong-nonce, sequence-regressed, or oversized channels are dropped and counted.
- A same-user process can still write malformed shared memory if it has access; the canvas treats this as hostile input and never trusts string offsets, sizes, or primitive counts without bounds checks.
- Do not put raw audio, full process command lines, usernames, window titles, raw process lists, or full filesystem paths in shared-memory diagnostics.

Common channel header:

```cpp
constexpr uint32_t kFusionChannelMagic = 0x4655534E; // FUSN
constexpr uint32_t kFusionChannelVersion = 1;
constexpr uint32_t kFusionMaxPrimitives = 512;

struct FusionChannelHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t source_type;
    uint32_t primitive_count;
    uint32_t active_buffer_index;
    uint32_t flags;
    uint64_t producer_id;
    uint64_t sequence;
    uint64_t qpc_timestamp;
    uint64_t qpc_frequency;
};
```

Coordinate and color ABI:

- Primitive coordinates are virtual-desktop physical pixels unless the primitive flags explicitly mark monitor-local coordinates.
- Each channel includes or references a display-topology generation. The canvas drops or remaps stale primitives when monitor topology, DPI, refresh, or HDR state changes.
- Producers that know their source viewport publish the source rect in diagnostics or a future channel extension before ABI freeze.
- `rgba_premul` is SDR sRGB premultiplied alpha by default. HDR/scRGB conversion belongs to `fusion_canvas.exe`, not to producers.
- Per-monitor output identity is a canvas concern; producer channels should avoid hard-coding monitor handles unless a future ABI extension adds validated monitor ids.

Primitive shape:

```cpp
enum class FusionPrimitiveType : uint32_t {
    DirectionArc = 1,
    TargetBox = 2,
    ConfidenceRing = 3,
    CrosshairCue = 4,
    TrajectoryHint = 5,
    DebugText = 6,
};

enum class FusionSourceType : uint32_t {
    Vision = 1,
    Audio = 2,
    Debug = 3,
    Replay = 4,
};

struct alignas(64) FusionPrimitive {
    FusionPrimitiveType type;
    FusionSourceType source;
    uint32_t ttl_ms;
    uint32_t flags;
    float confidence;
    float priority;
    float x0;
    float y0;
    float x1;
    float y1;
    float x2;
    float y2;
    float radius;
    float angle0_deg;
    float angle1_deg;
    uint32_t rgba_premul;
    uint32_t text_offset;
    uint32_t text_length;
    uint32_t reserved;
};
```

`DebugText` uses a bounded per-channel text arena defined by `shared_fusion`. In v1, producers set `text_offset` and `text_length` to zero unless diagnostics are enabled, and readers validate all text ranges before use.

Publication protocol:

1. Producer fills inactive buffer.
2. Producer increments sequence.
3. Producer atomically publishes active buffer index.
4. Producer signals a named event.
5. Canvas copies latest valid source-channel data into local staging memory.
6. Canvas drops malformed, stale, or incompatible channel data.

The producer never waits for canvas acknowledgement.

## Screen-Space Mapping

Audio markers are head-relative:

| Audio bin | Canvas behavior |
|---|---|
| `Left` | left-edge arc or wedge pointing inward |
| `Right` | right-edge arc or wedge pointing inward |
| `Center` | subtle top/center or crosshair-adjacent pulse |
| `FrontLeft` / `FrontRight` | upper-left / upper-right directional arc |
| `RearLeft` / `RearRight` | lower-left / lower-right directional arc |
| `Rear` | lower-center warning arc |
| `Unknown` | suppress marker outside diagnostics |

Visual policy:

- audio hints use distinct cyan/blue family
- degraded hints use amber
- opacity and stroke width scale with confidence
- TTL defaults to 300 ms with 150-250 ms fade
- audio never draws vision-like target boxes unless vision confirms a target
- markers must not cover the crosshair with large opaque shapes

## Fullscreen Canvas Rendering

Implementation:

- one transparent top-level popup HWND per monitor
- HWNDs are compositor anchors, not user-facing normal windows
- DirectComposition visual tree
- D3D11 composition swapchain
- Direct2D / DirectWrite drawing

Required HWND behavior:

- no caption
- no taskbar entry
- no Alt-Tab entry
- no activation on click
- locked mode click-through
- diagnostics mode receives input
- monitor-sized in physical pixels

Required styles:

- `WS_POPUP`
- `WS_EX_TOPMOST`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOOLWINDOW`
- `WS_EX_TRANSPARENT` only as an optional empirical aid after validation
- test `WS_EX_NOREDIRECTIONBITMAP` where compatible

Hotkeys:

- `Ctrl+Shift+F10`: enable/disable canvas
- `Ctrl+Shift+F11`: lock/unlock diagnostics
- use `RegisterHotKey` with `MOD_NOREPEAT` as the only v1 hotkey path
- unregister hotkeys on shutdown and rebind
- log `GetLastError()` on registration failure
- configurable alternate hotkeys handle conflicts
- low-level keyboard hook fallback is not part of v1; any `WH_KEYBOARD_LL` path requires a separate ADR, default-off build flag, and binary import review

Compatibility:

- windowed and borderless fullscreen are primary targets
- true exclusive fullscreen, DirectFlip, independent flip, and MPO can occlude the canvas
- degraded mode must report the limitation rather than attempting injection or evasion
- DirectFlip/MPO/FSE visibility can be detected only where the platform exposes enough evidence; otherwise report "not verifiably visible" rather than claiming success

## Performance Targets

Audio:

- capture callback p95 under 1 ms
- capture-to-detection p95 under 150 ms for DSP path
- detection-to-canvas-render p95 under 100 ms
- full onset-to-render p95 under 250 ms
- ring overflow is zero in normal 30-minute runs
- ring backlog age p99/max is reported
- worker per-hop CPU p95/p99 is reported
- callback allocation, lock, logging, and file-I/O counters remain zero
- stale audio drop count and device/channel-layout reconnect count are reported

Canvas:

- producer blocking on canvas: 0
- canvas render p95 under 1 ms
- canvas render p99 under 2 ms
- hotkey clear/restore p95 under 100 ms
- normal draw calls under 10 per frame
- no GPU readbacks
- no per-frame CPU bitmap uploads

Native runtime:

- vision/controller/recoil timing must not regress when audio and canvas are enabled
- audio and canvas telemetry must be measured separately from native vision/TensorRT timings
- A/B evidence must cover baseline native runtime, fusion disabled, vision publisher enabled, audio sidecar enabled, canvas enabled, producer crash, canvas crash, and malformed IPC storm.
- Controller tick p50/p95/p99, recoil stage duration, output interval jitter, vision age, publish cost, lock waits, and IPC waits must be recorded.
- Any wait, lock, file I/O, or logging in capture callback or controller/recoil hot ticks fails the gate.

## Diagnostics

Audio diagnostics:

- capture mode
- target PID and creation time
- format, channel count, channel mask
- RMS per channel
- stereo separation
- ILD dB
- bandwise ILD consistency
- GCC lag and peak ratio
- detector class probability
- direction bin and confidence
- ring overflow
- dropped inference frames
- latency p50/p95/p99

Canvas diagnostics:

- active monitor topology
- DPI/HDR state
- primitive counts by source
- source-channel age by source
- dropped stale channel updates
- frame pacing metrics
- D3D device removed count
- DComp commit failure count
- degraded fullscreen/DirectFlip/MPO warning

Diagnostics are disabled by default.

Privacy and process metadata:

- No outbound network by default.
- No raw audio persistence by default.
- No administrator requirement by default.
- Logs avoid full paths, command lines, usernames, window titles, and raw process lists unless diagnostics are explicitly enabled.
- Explicit recording/calibration mode, if added later, must show recording state continuously and store data only under a configured local path.

Dependency policy:

- Production dependencies default to Windows SDK, standard library, and permissive libraries such as BSD/MIT/BSL.
- GPL/AGPL repositories, headers, snippets, model weights, and training assets are reference-only unless a later licensing decision accepts them.
- Unknown-source models are not loaded by default.
- Runtime never downloads models or dependencies.
- Dependency enablement requires pinned versions, hashes, license records, NOTICE/SBOM output, and explicit build flags for optional components.

## Implementation Units

```text
native/shared_fusion/
  fusion_channel.h/.cpp
  fusion_primitive.h
  fusion_ipc.h/.cpp
  fusion_time.h/.cpp

native/audio_core/
  audio_format.h
  audio_ring_buffer.h/.cpp
  audio_clock.h/.cpp
  audio_frame.h

native/audio_capture_win/
  wasapi_process_loopback.h/.cpp
  wasapi_endpoint_loopback.h/.cpp
  audio_process_resolver.h/.cpp

native/audio_dsp/
  stft.h/.cpp
  band_energy.h/.cpp
  ild_estimator.h/.cpp
  gcc_phat.h/.cpp
  logmel.h/.cpp

native/audio_detector/
  template_bank.h/.cpp
  target_event_detector.h/.cpp
  audio_event_state_machine.h/.cpp

native/audio_direction/
  direction_estimator.h/.cpp
  direction_smoother.h/.cpp
  audio_confidence.h/.cpp

native/audio_direction_app/
  main.cpp
  audio_service.h/.cpp
  diagnostics.h/.cpp

native/overlay_canvas/
  main.cpp
  canvas_app.h/.cpp
  monitor_manager.h/.cpp
  canvas_window.h/.cpp
  dcomp_renderer.h/.cpp
  d2d_scene_renderer.h/.cpp
  hotkey_manager.h/.cpp
  fusion_channel_reader.h/.cpp
  overlay_diagnostics.h/.cpp
```

## Delivery Order

This is an implementation order, not a reduced final scope.

1. Define `shared_fusion` channel structs, validation, and tests.
2. Build synthetic/WAV audio source and generated test clips.
3. Build SPSC ring, format conversion, timestamp model, and tests.
4. Build ILD/GCC-PHAT direction estimator with synthetic sign tests.
5. Build template-bank detector with positive/negative replay tests.
6. Build `audio_direction.exe` process-loopback smoke tool.
7. Build `fusion_canvas.exe` shell, hotkeys, click-through mode, and mock primitives.
8. Add DComp/D3D11/D2D renderer, frame pacing, and telemetry.
9. Wire audio and vision source channels into canvas visual composition.
10. Add display topology, DPI, HDR, device-lost, and degraded fullscreen diagnostics.
11. Evaluate real authorized captures before enabling ONNX or any weak vision cue.

## Acceptance Gates

The plan is not implemented until evidence proves:

- process loopback can capture the target process tree on supported Windows builds
- process-loopback activation uses `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`, `PROPVARIANT VT_BLOB`, and a shutdown-safe async completion handler
- unsupported builds produce a clear degraded/error message
- endpoint fallback is clearly marked degraded
- format negotiation preserves channel masks and canonical conversion; resampler decision is closed before M1 completion
- synthetic left/right sign accuracy is 100%
- mono and near-mono input suppress direction
- conflicting bands suppress direction
- unknown coverage, accepted-only accuracy, false directional accept rate, and left/right sign accuracy are reported together
- template detector reports false positives on negative captures
- audio markers render on the fullscreen canvas with expected TTL/fade
- canvas toggles while the target app has focus
- locked canvas is click-through and non-activating
- canvas is absent from taskbar and Alt-Tab
- runtime never waits for audio or canvas
- canvas output is never consumed by runtime/controller state
- vision/controller/recoil timing does not regress
- DirectFlip/MPO/FSE degradation is detected where possible or clearly reported as not verifiably visible
- raw audio is not persisted by default
- GPL/AGPL code is not linked into production binaries
- audio and vision source channels render concurrently without either producer waiting on canvas
- malformed, stale, wrong-version, wrong-nonce, oversized, and sequence-regressed channels are rejected
- producer crash/restart clears or restores that source without crashing canvas
- canvas crash/restart does not block producer hot paths
- lifecycle owner does not launch duplicate canvas/audio processes for one session
- same-user IPC access is enforced and administrator privileges are not required
- named-object precreation with `ERROR_ALREADY_EXISTS` fails closed
- malformed IPC parser/fuzz tests cover wrong nonce, version, size, count, text offset, active buffer index, and sequence regression
- source-channel age, malformed count, stale count, and drop counters are reported per source
- `WH_KEYBOARD_LL` is absent from v1 builds unless a later ADR explicitly enables it
- ONNX telemetry is disabled or absent before ONNX can be enabled
- native runtime A/B hot-path evidence proves no controller/recoil/vision regression

## Reference Basis

- Existing split designs: `docs/project/AUDIO_DIRECTION_PIPELINE.md` and `docs/project/FULLSCREEN_CANVAS_FUSION.md`
- Local reference package: `native/voice/SoundDirectionAssist_Codex_Pack`; this is reference material and is superseded by the integrated project docs when it conflicts on source-channel IPC, multichannel preservation, or DirectComposition canvas behavior.
- GitHub scan notes: `.agent-context/research-2026-06-24-audio-direction-github-scan.md`
- Microsoft Application Loopback sample: <https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/>
- `AUDIOCLIENT_ACTIVATION_PARAMS`: <https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_activation_params>
- `PROCESS_LOOPBACK_MODE`: <https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-process_loopback_mode>
- WASAPI loopback recording: <https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording>
- DirectComposition: <https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-portal>
- `CreateSwapChainForComposition`: <https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition>
- Composition swapchain guide: <https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain>
- DXGI flip model / DirectFlip / MPO guidance: <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model>
- `RegisterHotKey`: <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey>
