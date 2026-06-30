# Audio Direction Pipeline

Last updated: 2026-06-24
Status: mature design target for Windows native C++ integration

## Purpose

This document defines the project-level audio pipeline:

```text
capture target process audio
  -> normalize and buffer PCM
  -> extract key target audio events
  -> estimate direction and confidence
  -> publish audio primitives
  -> mark direction on the fullscreen fusion canvas
```

The audio pipeline is a companion to native vision. It should not be treated as a precise world-position sensor. Game loopback audio is already a rendered game mix; in stereo it normally supports left/right/center bias with confidence, not reliable absolute world azimuth or front/back/elevation.

Default behavior:

- Audio renders visual direction hints on the fullscreen canvas.
- Audio does not control recoil.
- Audio does not directly grant fire authority.
- Audio does not feed aim-assist target selection unless a later explicit decision enables a weak-cue bridge.

## Architecture

```text
target game process / process tree
  |
  | WASAPI process loopback
  v
audio_capture_win
  |
  | timestamped PCM blocks
  v
audio_core ring buffer
  |
  | normalized float frames
  v
audio_detector + audio_direction
  |
  | event + direction + confidence
  v
audio_fusion
  |
  | FusionPrimitive(Source=Audio)
  v
AudioFusionChannel shared-memory source channel
  |
  v
fusion_canvas.exe visual composition
  |
  v
fullscreen canvas renders direction arcs / diagnostics
```

Thread model:

- Capture thread/callback: receives WASAPI buffers and writes into a preallocated SPSC ring. No allocation, logging, blocking I/O, model inference, or locks in the hot callback.
- Audio worker: normalizes, frames, computes features, detects target events, estimates direction, publishes latest audio state.
- Optional inference worker: runs ONNX Runtime on bounded queues. Queue overflow drops stale inference work instead of adding latency.
- Audio publisher thread: converts latest validated audio events into `FusionPrimitive(Source=Audio)` entries and publishes `AudioFusionChannel` without waiting for the canvas.

## Stage 1: Audio Acquisition

Primary API:

- Windows WASAPI process loopback.
- `ActivateAudioInterfaceAsync`.
- `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK` activation path.
- `AUDIOCLIENT_ACTIVATION_PARAMS`.
- `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`.
- `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`.
- `PROPVARIANT` `VT_BLOB` carrying activation params.

Primary target:

- Capture the configured game process and child process tree.
- Use target PID plus process creation time to avoid accidentally attaching to a reused PID.
- Support explicit PID, executable name, and future auto-attach by the same target selection used by the vision runtime.

Fallback:

- Standard endpoint loopback using `AUDCLNT_STREAMFLAGS_LOOPBACK`.
- Fallback is marked degraded because it captures the endpoint mix, not a clean per-process stream.
- Session enumeration may identify candidate processes for diagnostics, but it cannot recover isolated PCM after endpoint mixing.

Lifecycle:

- `Idle`: no target.
- `Starting`: process found, COM activation pending.
- `Capturing`: PCM is flowing.
- `Silent`: stream exists but RMS below threshold.
- `Degraded`: fallback mode, mono/near-mono, unsupported format, or overflow.
- `Disconnected`: target process exited or render stream disappeared.
- `Error`: repeated HRESULT/device failure after bounded retries.

Reconnect:

- If the known target process exits, stop capture and clear audio events.
- If the same configured executable relaunches, reconnect only after verifying process path and creation time.
- Do not silently attach to an unrelated process with a reused PID.

Activation lifecycle requirements:

- Use an async completion handler that is safe for MTA/free-threaded callback delivery.
- Support activation timeout and cancellation.
- If activation completes during or after shutdown, do not touch released capture state.
- Runtime OS checks are advisory; final support is determined by API availability and activation HRESULT/smoke result.

## Stage 2: Format Normalization And Buffering

Canonical processing format:

- 48,000 Hz.
- `float32`.
- Interleaved PCM.
- Preserve source channel count up to 8 channels when available.
- Always derive a stereo view for baseline detection/direction.
- If input is mono, detection may continue but direction must become unavailable.

Channel handling:

- Read `WAVEFORMATEXTENSIBLE` channel mask when present.
- For stereo: map L/R explicitly and test that "left channel stronger" maps to `Left`.
- For 5.1/7.1: preserve channel energies and use channel-mask-aware coarse direction in addition to stereo downmix.
- Do not independently normalize L and R. Shared gain only, because independent channel gain destroys ILD.
- Prefer 48 kHz float32 `WAVEFORMATEXTENSIBLE` when accepted by the client/device path.
- If capture uses mix/client-accepted format, convert to canonical format in `audio_core` while preserving channel mask/order metadata.
- Resampler choice is a stop gate before completing the first capture milestone.

Ring buffer:

- Preallocate enough PCM for at least 500 ms of worst-case internal format.
- Overflow policy: drop oldest unread audio, increment an overflow counter, and enter degraded diagnostics if repeated.
- Worker must process latest available audio and avoid building unbounded latency.

Timing:

- Capture records QPC timestamp and frame index.
- Detection publishes:
  - `audio_capture_qpc`
  - `feature_ready_qpc`
  - `detection_ready_qpc`
  - `channel_publish_qpc`
- Canvas adds `render_qpc`.
- All latency metrics are computed from the same QPC clock.

## Stage 3: Key Audio Extraction

Goal:

- Detect configured target sound classes while rejecting background, music, UI sounds, voice chat, and unrelated game audio.

Baseline DSP detector:

1. Shared gain normalization.
2. Optional DC removal.
3. 20-25 ms Hann windows with 10 ms hop.
4. STFT via KissFFT.
5. Band energy and spectral flux.
6. Target band templates.
7. Log-mel template similarity.
8. Onset gate plus class-specific hysteresis.
9. Cooldown to prevent repeated triggers from one event tail.

Template bank:

```text
config/audio_templates/<game_profile>/
  manifest.json
  positives/
    <class_name>/*.wav
  negatives/
    background/*.wav
    ui/*.wav
    voice/*.wav
```

Manifest fields:

- profile name.
- sample rate.
- channel layout.
- target event classes.
- feature configuration hash.
- template file hashes.
- rights/collection notes.
- calibration thresholds.

Optional ML detector:

- ONNX Runtime CPU provider first.
- Disable ONNX Runtime telemetry when creating `OrtEnv`, or use a proven telemetry-free build.
- Input features:
  - `logmel_L`
  - `logmel_R`
  - `logmel_L_minus_logmel_R`
  - optional bandwise ILD/phase summary vector.
- Window: 0.25-0.50 s.
- Outputs:
  - `class_logits`.
  - `direction_logits`.
  - optional `quality`.
- ONNX is optional and must not be required to build the baseline pipeline.
- ONNX enablement requires an offline/firewalled smoke proving no outbound network attempt.

Decision rule:

- DSP is the default live path until real capture evaluation proves it is not robust enough.
- ML is added for target sounds that vary too much across weapons, maps, effects, skins, audio profiles, or dynamic range settings.

## Stage 4: Direction Estimation

Direction output:

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

Stereo baseline:

- Broadband ILD.
- Bandwise ILD with median and consistency score.
- GCC-PHAT lag and peak ratio as supporting evidence.
- Near-mono detector suppresses direction when L/R separation is too weak.
- Conflicting bands reduce confidence or force `Unknown`.

Multichannel path:

- If channel mask is valid and channels are preserved, compute target-band energy per speaker channel.
- Map channel vector to coarse bins.
- Multichannel confidence is higher only when the target event has a dominant, stable channel pattern.

Confidence factors:

- event probability.
- event SNR.
- ILD magnitude.
- ILD sign stability.
- band consistency.
- GCC-PHAT peak ratio.
- multichannel energy dominance when available.
- recent direction stability after smoothing.

Default confidence formula:

```text
confidence =
  event_probability
  * snr_score
  * direction_consistency
  * channel_separation_score
  * stability_score
```

Below threshold:

- Publish `Unknown`.
- Do not draw directional arc unless diagnostics mode is enabled.

Smoothing:

- Direction changes require a minimum stable dwell.
- Very short isolated flips are ignored.
- Confidence decays with event age.

## Stage 5: Screen-Space Mapping

Default mapping is head-relative, not world-relative.

Reason:

- WASAPI loopback receives the game-rendered audio mix.
- Stereo direction describes how the player hears the event, not a world coordinate.
- Without game camera/world state, exact screen-space projection is not available through the allowed no-injection path.

Canvas behavior:

- `Left`: draw left-edge direction arc and optional wedge toward the center.
- `Right`: draw right-edge direction arc.
- `Center`: draw subtle top/center or crosshair-adjacent pulse.
- `Unknown`: suppress normal visual marker.
- `FrontLeft/FrontRight/Rear*`: only used when multichannel or calibrated model supports them.

Vision bridge:

- Audio may publish visual primitives immediately.
- Audio-to-vision cue bridge is disabled by default.
- If enabled later, audio can become a weak external cue only:
  - no fire authority.
  - no recoil input.
  - no target lock by itself.
  - only helps rank/continue vision candidates when confidence and timing are valid.

`VisionEngine::set_external_cue(...)` remains a possible integration point, but it is not the default audio behavior.

## Stage 6: Fusion Canvas Marking

Audio publishes `FusionPrimitive(Source=Audio)` entries into `AudioFusionChannel`; the canvas visually composes that channel with other valid source channels.

Primitive defaults:

- `SourceType = Audio`.
- `DirectionArc` for direction.
- `ConfidenceRing` for high-confidence center events.
- `DebugText` only in diagnostics mode.

Visual policy:

- Cyan/blue for normal audio hints.
- Amber for lower-confidence/degraded hints.
- Opacity and stroke width scale with confidence.
- TTL defaults to 300 ms after detection, with 150-250 ms fade.
- Do not cover the crosshair with large opaque graphics.
- Do not use audio markers to mimic vision target boxes unless vision confirms a target.

Example mapping:

```text
Audio event: footsteps, Left, confidence 0.78
Canvas:
  DirectionArc(left edge, confidence 0.78, ttl 300 ms)
  DebugText("AUDIO LEFT 78%", diagnostics only)
```

Source-channel ownership:

- `audio_direction.exe` owns the `AudioFusionChannel`.
- `cod_native_runtime.exe` owns the `VisionFusionChannel`.
- `fusion_canvas.exe` visually composes validated source channels into one rendered scene.
- Avoid multiple writers to the same shared-memory channel.
- Canvas-composed output never feeds back into runtime/controller state.

## Diagnostics

Audio diagnostics fields:

- capture mode: process loopback / endpoint fallback / WAV replay / synthetic.
- target PID and process creation time.
- sample rate, channels, channel mask, sample type.
- RMS per channel.
- stereo separation score.
- ILD dB.
- bandwise ILD median and variance.
- GCC lag and peak ratio.
- detector class probability.
- direction bin and confidence.
- ring overflow count.
- dropped inference frames.
- latency p50/p95/p99:
  - capture to feature.
  - feature to detection.
  - detection to publish.
  - publish to canvas render.

Diagnostics are off by default and never require raw audio persistence.

## Privacy And Safety

Defaults:

- No network.
- No telemetry upload.
- No raw audio persistence.
- No spectrogram persistence by default.
- Logs contain metrics and statuses, not reconstructable audio.

Recording:

- If raw audio recording is added for calibration, it must be explicit, temporary, user-triggered, and clearly stored under a configured local path.
- Never commit game audio, voice chat, or user-provided clips into the repository.

Boundaries:

- No process injection for audio capture.
- No game memory reads.
- No kernel driver.
- No anti-cheat evasion.
- No hidden capture outside the configured target/fallback mode.

## Configuration

Add an `AudioRuntimeConfig` equivalent:

```toml
[audio]
enabled = false
mode = "process_loopback" # process_loopback | endpoint_loopback | wav_replay | synthetic
target_process_name = ""
target_pid = 0
include_process_tree = true
endpoint_id = ""
internal_sample_rate_hz = 48000
max_channels = 8
ring_buffer_ms = 500
enable_direction = true
enable_canvas_markers = true
enable_external_vision_cue = false
template_profile = "default"
min_event_confidence = 0.70
min_direction_confidence = 0.60
marker_ttl_ms = 300
marker_fade_ms = 200
diagnostics = false
```

Audio runtime default is disabled until configured.

## Testing And Acceptance

Unit tests:

- format conversion preserves L/R ordering.
- mono input disables direction.
- near-mono input suppresses direction.
- shared gain normalization preserves ILD sign.
- ring overflow increments counters and drops oldest.
- ILD sign tests with synthetic left/right clips.
- GCC-PHAT sanity with synthetic sample delay.
- confidence drops on conflicting bands.
- TTL/fade policy converts to expected canvas primitives.

Integration tests:

- WAV replay with generated synthetic clips.
- WASAPI process-loopback smoke against a local synthetic audio process.
- endpoint fallback reports degraded.
- audio sidecar publishes audio canvas primitives without blocking.
- canvas receives and renders `SourceType=Audio` primitives.

Evaluation gates:

- Process-loopback startup succeeds on supported Windows build.
- Process-loopback startup uses `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`, `PROPVARIANT VT_BLOB`, and a shutdown-safe async completion handler.
- Format negotiation preserves channel mask/order and resampler conversion is tested.
- Capture-to-detection p95 under 150 ms for DSP path.
- Detection-to-canvas-render p95 under 100 ms.
- Full onset-to-render p95 under 250 ms.
- Synthetic L/R sign accuracy 100%.
- Curated real-capture L/R accepted accuracy at least 90% where confidence threshold accepts.
- False positives no more than configured threshold on negative capture sessions.
- Unknown coverage, accepted-only accuracy, false directional accept rate, and left/right sign accuracy are reported together.
- Ring backlog age p99/max, worker per-hop CPU p95/p99, stale audio drops, and reconnect count are reported.
- Runtime/controller/recoil timing does not regress when audio is enabled.

## Implementation Units

```text
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

native/audio_fusion/
  audio_fusion_state.h/.cpp
  audio_canvas_primitives.h/.cpp

native/audio_direction_app/
  main.cpp
  audio_service.h/.cpp
  audio_channel_publisher.h/.cpp
  diagnostics.h/.cpp
```

## Implementation Order

1. Define audio domain types, config, and tests.
2. Add WAV/synthetic replay source and generated test clips.
3. Add SPSC ring and timestamp model.
4. Add ILD/GCC/direction tests using synthetic clips.
5. Add template-bank detector and negative-sample tests.
6. Add process-loopback capture smoke tool.
7. Add `audio_direction.exe` service behind `audio.enabled=false`.
8. Add `AudioFusionChannel` primitive publication.
9. Add diagnostics and latency logging.
10. Evaluate real target-game captures before enabling ML or external vision cue.

## Open Decisions

These should become explicit ADRs before implementation changes controller behavior:

1. Whether audio is ever allowed to become a weak vision cue via `set_external_cue`.
2. Whether to preserve multichannel capture as a first-class path from day one.
3. Resampler choice for non-48 kHz endpoints.
4. Template calibration workflow and where user-provided samples live.
5. C++ standard strategy: isolate audio targets as C++20 or upgrade the native tree.

## References

- Existing standalone audio package: `native/voice/SoundDirectionAssist_Codex_Pack`.
- Fullscreen canvas target: `docs/project/FULLSCREEN_CANVAS_FUSION.md`.
- Microsoft ApplicationLoopback audio sample: <https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/>
- `AUDIOCLIENT_ACTIVATION_PARAMS`: <https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_activation_params>
- `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`: <https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params>
- WASAPI loopback recording: <https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording>
- ONNX Runtime C++ API: <https://onnxruntime.ai/docs/get-started/with-cpp.html>
- KissFFT: <https://github.com/mborgerding/kissfft>
