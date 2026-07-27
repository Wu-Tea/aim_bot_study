# Audio + Visual Fusion TODO

Last updated: 2026-06-25
Status: actionable TODO derived from `AUDIO_VISUAL_FUSION_IMPLEMENTATION_PLAN.md`

## Review Closure

- [ ] Record final ADR for the three-process model.
- [ ] Record ADR for v1 hotkey policy: `RegisterHotKey` only, no `WH_KEYBOARD_LL`.
- [ ] Record ADR for audio visual-only authority boundary.
- [ ] Update any stale docs that imply runtime-owned final snapshot.
- [ ] Keep audio and canvas disabled by default until implementation gates pass.
- [ ] Keep runtime/canvas/audio integration blocked until G0.5 no-impact performance gate passes.

## G0.5 Performance Non-Regression

- [ ] Define the fixed benchmark scene, route, replay, or equivalent repeatable workload.
- [ ] Record Windows build, GPU driver, power plan, monitor topology, resolution, refresh, HDR, VRR, FSO, MPO, game preset, model hash, engine hash, config hash, and binary hashes for each run.
- [ ] Add PresentMon CSV or equivalent target-process present capture.
- [ ] Add WPR/ETW/GPUView-style graphics, DWM, context-switch, CPU-sampling, and native-provider trace capture.
- [ ] Add native summary JSON with p50/p95/p99/p99.9/max for vision, controller, recoil, output jitter, publish cost, audio callback, audio worker, and ring backlog.
- [ ] Run 5 paired A/A baseline runs with 2 minutes warmup and 5 minutes capture.
- [ ] Require A/A p95-of-paired-deltas for game p95 frame time <= 0.20 ms.
- [ ] Require A/A p95-of-paired-deltas for game p99 frame time <= 0.50 ms.
- [ ] Run paired A/B for fusion compiled but fully disabled.
- [ ] Run paired A/B for canvas probe process with no HWND.
- [ ] Run paired A/B for visible canvas HWND with zero present.
- [ ] Run paired A/B for canvas sparse primitives at 15 Hz.
- [ ] Run paired A/B for canvas sparse primitives at 30 Hz.
- [ ] Run paired A/B for canvas worst-case mock primitives.
- [ ] Run paired A/B for no-op `VisionFusionChannel` publisher with no reader.
- [ ] Run paired A/B for no-op publisher with mock reader.
- [ ] Run paired A/B for no-op publisher under event storm/malformed reader.
- [ ] Run paired A/B for audio process-loopback plus ring-copy probe.
- [ ] Run paired A/B for audio process-loopback plus DSP/template probe, ONNX off.
- [ ] Run slowed-audio-worker test proving drop-oldest/latest-wins without backlog catch-up.
- [ ] Keep ONNX probe blocked until DSP/template probe passes.
- [ ] Require no repeatable present-mode downgrade from baseline DirectFlip, Independent Flip, or MPO-backed modes.
- [ ] Require game average and median frame-time delta <= +0.10 ms.
- [ ] Require game p95 frame-time delta <= +0.20 ms and <= +1.0%.
- [ ] Require game p99 frame-time delta <= +0.50 ms and <= +2.0%.
- [ ] Require game p99.9 frame-time delta <= +0.75 ms and <= +3.0%.
- [ ] Require 1% low FPS decline <= 1.0%.
- [ ] Require 0.1% low FPS decline <= 2.0%.
- [ ] Require late/dropped present increase <= 0.1% frames and <= 3 events per run.
- [ ] Require native `pre`, `infer`, `gpu`, `wait`, and `age` p95/p99 deltas <= max(0.50 ms, 5.0%).
- [ ] Require controller tick p99 delta <= 0.10 ms.
- [ ] Require recoil stage p99 delta <= 0.10 ms.
- [ ] Require output interval jitter p99 delta <= 0.20 ms.
- [ ] Require publisher p50 <= 0.02 ms, p95 <= 0.10 ms, p99 <= 0.20 ms, max <= 0.50 ms.
- [ ] Require audio callback p95 <= 0.20 ms, p99 <= 0.50 ms, max <= 1.00 ms, and max <= 25% of the active audio engine period.
- [ ] Require DSP/template worker per 10 ms hop p95 <= 1.0 ms and p99 <= 2.0 ms.
- [ ] Require ring backlog p99 <= 20 ms and max <= 40 ms.
- [ ] Define perf tripwire order: reduce canvas rate, stop audio sidecar, stop canvas, stop vision publisher.
- [ ] Add `FUSION_FORCE_OFF=1`.
- [ ] Add launcher/config `--no-fusion` or equivalent.
- [ ] Add hard-off hotkey behavior.
- [ ] Require hard-off to stop publication, stop canvas presents, hide/destroy overlay HWNDs, release/tear down DComp visuals/swapchains, and stop audio workers within 1 second.
- [ ] Require no auto-restart for a feature disabled by tripwire in the same session.

## G0 Boundary

- [ ] Add forbidden API source-scan list.
- [ ] Add binary import-table scan plan.
- [ ] Confirm no admin manifest for fusion/audio sidecars.
- [ ] Confirm no `Global\` named objects.
- [ ] Confirm no injection/hook/driver/process-memory APIs.
- [ ] Confirm no hidden capture/anti-capture path.
- [ ] Confirm no network path in runtime/audio/canvas defaults.

## G1 Shared Fusion ABI

- [ ] Create `native/shared_fusion/fusion_channel.h`.
- [ ] Create `native/shared_fusion/fusion_channel.cpp`.
- [ ] Define `FusionSourceType`.
- [ ] Define `FusionPrimitiveType`.
- [ ] Define `FusionCoordinateSpace`.
- [ ] Define `FusionColorSpace`.
- [ ] Define `FusionChannelHeader`.
- [ ] Define `FusionPrimitive`.
- [ ] Define bounded text arena metadata.
- [ ] Define display-topology generation behavior.
- [ ] Define SDR sRGB premultiplied color contract.
- [ ] Define HDR conversion ownership in canvas.
- [ ] Write header validation tests.
- [ ] Write primitive validation tests.
- [ ] Write text-range validation tests.
- [ ] Write enum-range validation tests.
- [ ] Write active-buffer-index validation tests.
- [ ] Write sequence-regression tests.

## G1 IPC

- [ ] Create `native/shared_fusion/fusion_ipc.h`.
- [ ] Create `native/shared_fusion/fusion_ipc.cpp`.
- [ ] Implement `Local\YoloStudy001.Fusion.<nonce>.<source_type>.Mapping`.
- [ ] Implement `Local\YoloStudy001.Fusion.<nonce>.<source_type>.Event`.
- [ ] Implement explicit current-user security descriptor.
- [ ] Reject `ERROR_ALREADY_EXISTS` for producer-owned objects.
- [ ] Open canvas mappings read-only where practical.
- [ ] Implement double-buffer publish.
- [ ] Implement release/acquire memory ordering.
- [ ] Implement reader staging copy.
- [ ] Implement stale/malformed drop counters.
- [ ] Add wrong-nonce test.
- [ ] Add wrong-version test.
- [ ] Add oversized-count test.
- [ ] Add precreated-object fail-closed test.
- [ ] Add producer-restart test.
- [ ] Add malformed/fuzz-style generated tests.

## G2 Lifecycle

- [ ] Decide script supervisor vs native thin supervisor for first slice.
- [ ] Generate one session nonce per launch.
- [ ] Launch runtime with `--fusion-session`.
- [ ] Launch audio sidecar with `--fusion-session`.
- [ ] Launch canvas with `--fusion-session`.
- [ ] Prevent duplicate canvas/audio process launch for one session.
- [ ] Implement bounded canvas restart.
- [ ] Implement bounded audio restart if configured.
- [ ] Define shutdown order.
- [ ] Add lifecycle status output.
- [ ] Test canvas crash/restart.
- [ ] Test audio crash/restart.
- [ ] Test supervisor exit.

## G3 Canvas Shell

- [ ] Create `native/overlay_canvas/main.cpp`.
- [ ] Create `canvas_app`.
- [ ] Create `monitor_manager`.
- [ ] Create `canvas_window`.
- [ ] Create `hotkey_manager`.
- [ ] Set process DPI awareness before HWND creation.
- [ ] Enumerate monitors.
- [ ] Create one `WS_POPUP` HWND per monitor.
- [ ] Add `WS_EX_TOPMOST`.
- [ ] Add `WS_EX_NOACTIVATE`.
- [ ] Add `WS_EX_TOOLWINDOW`.
- [ ] Keep `WS_EX_TRANSPARENT` optional/validated only.
- [ ] Implement `WM_MOUSEACTIVATE -> MA_NOACTIVATE`.
- [ ] Implement locked `WM_NCHITTEST -> HTTRANSPARENT`.
- [ ] Implement diagnostics `WM_NCHITTEST -> HTCLIENT`.
- [ ] Register `Ctrl+Shift+F10` with `MOD_NOREPEAT`.
- [ ] Register `Ctrl+Shift+F11` with `MOD_NOREPEAT`.
- [ ] Add alternate hotkey config.
- [ ] Add `UnregisterHotKey`.
- [ ] Log `GetLastError()` on hotkey failure.
- [ ] Verify no taskbar entry.
- [ ] Verify no Alt-Tab entry.
- [ ] Verify no focus steal.
- [ ] Verify mouse buttons pass through.
- [ ] Verify mouse wheel passes through.
- [ ] Verify game input remains with target.

## G4 Canvas Renderer

- [ ] Create `dcomp_renderer`.
- [ ] Create `d2d_scene_renderer`.
- [ ] Create `graphics_telemetry`.
- [ ] Create D3D11 device with BGRA support.
- [ ] Create Direct2D factory/device.
- [ ] Create DirectWrite factory.
- [ ] Create DirectComposition device/target.
- [ ] Create composition swapchain.
- [ ] Use `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` if using waitable object pacing.
- [ ] Call `SetMaximumFrameLatency(1 or 2)`.
- [ ] Implement DWM-timer fallback.
- [ ] Default live canvas max rate to 30 Hz.
- [ ] Default audio-only cue rendering target to 15 Hz.
- [ ] Implement zero-present idle behavior after retained scene commit.
- [ ] Merge source events into latest-snapshot rendering.
- [ ] Disable diagnostics text in live mode by default.
- [ ] Disable animation/fade in live mode by default unless the performance gate is rerun with them enabled.
- [ ] Add hard-off path that hides or destroys overlay HWNDs.
- [ ] Add hard-off path that stops presents.
- [ ] Add hard-off path that releases or tears down DComp visuals/swapchains.
- [ ] Reject transparent idle HWND as a disabled state.
- [ ] Render mock direction arcs.
- [ ] Render mock target boxes.
- [ ] Render mock confidence rings.
- [ ] Render diagnostics text.
- [ ] Cache brushes/strokes/text formats.
- [ ] Add D3D device removed recovery.
- [ ] Add DComp commit failure recovery.
- [ ] Add DComp device-state recovery check.
- [ ] Add topology/DPI resize handling.
- [ ] Add SDR baseline.
- [ ] Add HDR/scRGB path plan.
- [ ] Add SDR reference white handling.
- [ ] Add render telemetry.
- [ ] Add skipped-present telemetry.
- [ ] Add hard-off latency telemetry.
- [ ] Add present-mode snapshot where available.
- [ ] Forbid desktop duplication/capture-and-replay in live canvas.
- [ ] Forbid CPU readback and staging texture readback in live canvas.
- [ ] Forbid per-frame CPU bitmap upload in live canvas.
- [ ] Forbid WARP/software D3D in live canvas.
- [ ] Forbid D3D debug layer in live performance path.
- [ ] Forbid GDI/`UpdateLayeredWindow` live rendering.
- [ ] Forbid full-screen shader blur/effects in live canvas.
- [ ] Forbid cross-adapter copies in live canvas.

## G5 Audio Capture

- [ ] Create `native/audio_capture_win`.
- [ ] Implement target PID resolver.
- [ ] Validate PID creation time.
- [ ] Add configured executable name resolver.
- [ ] Add privacy-safe target diagnostics.
- [ ] Implement `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`.
- [ ] Implement `AUDIOCLIENT_ACTIVATION_PARAMS`.
- [ ] Implement `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`.
- [ ] Implement `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`.
- [ ] Implement `PROPVARIANT VT_BLOB`.
- [ ] Implement async completion handler.
- [ ] Handle MTA/free-thread callback delivery.
- [ ] Add activation timeout.
- [ ] Add cancellation.
- [ ] Add shutdown-after-completion safety.
- [ ] Implement capture states.
- [ ] Implement endpoint loopback fallback as degraded.
- [ ] Create synthetic audio target process.
- [ ] Test normal capture.
- [ ] Test no render stream -> `Silent`.
- [ ] Test target exit -> `Disconnected`.
- [ ] Test target restart revalidation.
- [ ] Test unsupported API/build message.
- [ ] Test endpoint fallback degraded.
- [ ] Ensure callback has no allocation/log/lock/file I/O.
- [ ] Measure callback wake-to-ring-push p50/p95/p99/max.
- [ ] Require callback p95 <= 0.20 ms.
- [ ] Require callback p99 <= 0.50 ms.
- [ ] Require callback max <= 1.00 ms.
- [ ] Require callback max <= 25% of the active audio engine period.
- [ ] Forbid DSP/resample/template/ONNX work in callback.
- [ ] Forbid callback waiting for worker.
- [ ] Forbid callback COM activation retry.
- [ ] Forbid callback heap allocation.

## G6 Audio Format And DSP

- [ ] Create `native/audio_core`.
- [ ] Define audio format type.
- [ ] Implement int/float conversion.
- [ ] Implement frame timestamp model.
- [ ] Implement SPSC ring.
- [ ] Preallocate and warm-touch PCM ring.
- [ ] Set PCM ring target capacity to 200 ms.
- [ ] Keep writer non-blocking.
- [ ] Drop oldest complete packets on overflow.
- [ ] Implement skip-to-latest when backlog exceeds 40 ms.
- [ ] Forbid historical backlog catch-up CPU bursts.
- [ ] Generate synthetic left/right/center clips.
- [ ] Generate mono/near-mono clips.
- [ ] Generate conflicting-band clips.
- [ ] Generate delayed-channel clips.
- [ ] Generate silence/clipping/NaN cases.
- [ ] Parse `WAVEFORMATEXTENSIBLE`.
- [ ] Test channel mask/order.
- [ ] Decide resampler.
- [ ] Implement or wrap resampler.
- [ ] Implement shared gain normalization.
- [ ] Implement ring overflow/drop-oldest.
- [ ] Report ring backlog age p99/max.
- [ ] Report worker per-hop CPU p95/p99.
- [ ] Require ring backlog p99 <= 20 ms.
- [ ] Require ring backlog max <= 40 ms.
- [ ] Require worker per 10 ms hop p95 <= 1.0 ms.
- [ ] Require worker per 10 ms hop p99 <= 2.0 ms.
- [ ] Require audio event age p95 <= 60 ms.
- [ ] Require audio event age p99 <= 100 ms.
- [ ] Precompute FFT configs, Hann windows, mel banks, and template tensors.
- [ ] Forbid OpenMP/multithreaded FFT in live mode unless a later ADR proves no regression.
- [ ] Forbid `kiss_fft_alloc` or temporary vector allocation in the live hot path.
- [ ] Set `audio_direction.exe` default process priority to below normal.
- [ ] Restrict MMCSS use to packet copy/release work if used.
- [ ] Set DSP/template workers to below-normal or lower priority.
- [ ] Forbid high, realtime, time-critical, busy-spin, and global timer-resolution changes.

## G6 Direction

- [ ] Pin KissFFT version/hash/license.
- [ ] Add KissFFT NOTICE/SBOM entry.
- [ ] Implement Hann window.
- [ ] Implement STFT.
- [ ] Implement broadband ILD.
- [ ] Implement bandwise ILD.
- [ ] Implement GCC-PHAT.
- [ ] Implement near-mono suppression.
- [ ] Implement conflicting-band suppression.
- [ ] Implement confidence formula.
- [ ] Implement dwell/hysteresis.
- [ ] Test synthetic left/right sign accuracy.
- [ ] Test center.
- [ ] Test mono unknown.
- [ ] Test near-mono unknown.
- [ ] Test conflicting-band unknown.
- [ ] Test sample delay.
- [ ] Generate synthetic confusion matrix.

## G7 Detector And Evaluator

- [ ] Define template manifest schema.
- [ ] Implement template bank loader.
- [ ] Implement log-mel features.
- [ ] Implement template similarity.
- [ ] Implement onset gate.
- [ ] Implement class hysteresis.
- [ ] Implement cooldown.
- [ ] Add positive sample profile.
- [ ] Add negative sample profile.
- [ ] Validate `rights` field.
- [ ] Validate `sha256`.
- [ ] Validate `session_id`.
- [ ] Validate `split_group`.
- [ ] Validate audio profile.
- [ ] Implement offline evaluator.
- [ ] Report precision/recall/F1.
- [ ] Report false positives/minute.
- [ ] Report unknown coverage.
- [ ] Report accepted-only accuracy.
- [ ] Report false directional accept rate.
- [ ] Report left/right sign accuracy.
- [ ] Report by profile/background.

## G8 Runtime Publisher

- [ ] Add `fusion_channel_publisher` to `native/runtime_app`.
- [ ] Keep `fusion_channel_publisher` blocked until G0.5 publisher shim passes.
- [ ] Publish `VisionFusionChannel`.
- [ ] Keep publisher disabled by default.
- [ ] Ensure disabled config creates no mapping/event.
- [ ] Ensure disabled config starts no fusion thread.
- [ ] Ensure disabled config adds no measurable hot-path work.
- [ ] Ensure runtime never reads canvas state.
- [ ] Ensure runtime never reads audio state by default.
- [ ] Ensure publish failure is non-fatal.
- [ ] Add publish timing telemetry.
- [ ] Add publish p50/p95/p99/max telemetry.
- [ ] Add dropped publish counter.
- [ ] Add disabled-by-tripwire counter.
- [ ] Require publish p50 <= 0.02 ms.
- [ ] Require publish p95 <= 0.10 ms.
- [ ] Require publish p99 <= 0.20 ms.
- [ ] Require publish max <= 0.50 ms.
- [ ] Add source scan for waits/logging/I/O in hot tick.
- [ ] Add source scan for heap allocation/vector growth/string formatting in hot path.
- [ ] Add source scan for COM/D3D/DXGI/DComp/WASAPI in native hot path.
- [ ] Add source scan for process/thread launch in native hot path.
- [ ] Add source scan for `Create/Open/Map*` IPC calls in native hot path.
- [ ] Add source scan for fusion-added CUDA stream/device synchronization.
- [ ] Forbid publisher reconnect/recreate/open/map/retry from hot path.
- [ ] Disable publisher for the rest of the session after repeated failure or budget violation.
- [ ] Run native pipeline contract verifier.
- [ ] Run baseline A/B.
- [ ] Run vision publisher enabled A/B.
- [ ] Run canvas absent A/B.
- [ ] Run canvas present A/B.
- [ ] Run malformed IPC storm A/B.
- [ ] Require malformed IPC storm to keep runtime CPU, controller p99, and vision age within G0.5 thresholds.

## G8 Audio Sidecar

- [ ] Create `native/audio_direction_app`.
- [ ] Load audio config.
- [ ] Start capture.
- [ ] Start audio worker.
- [ ] Run detector.
- [ ] Run direction estimator.
- [ ] Publish `AudioFusionChannel`.
- [ ] Add capture diagnostics.
- [ ] Add direction diagnostics.
- [ ] Add latency diagnostics.
- [ ] Add callback p50/p95/p99/max diagnostics.
- [ ] Add worker hop p50/p95/p99 diagnostics.
- [ ] Add ring backlog p50/p95/p99/max diagnostics.
- [ ] Add dropped-oldest and skip-to-latest counters.
- [ ] Add process CPU and working-set diagnostics.
- [ ] Ensure no raw audio persistence.
- [ ] Run with canvas absent.
- [ ] Run with no reader.
- [ ] Run with malformed reader/input conditions.

## G9 Integration

- [ ] Launch all three processes with one nonce.
- [ ] Render vision mock/source channel.
- [ ] Render audio mock/source channel.
- [ ] Compose by TTL/confidence/priority.
- [ ] Verify source disconnect clear.
- [ ] Verify canvas crash does not block producers.
- [ ] Verify audio crash clears audio only.
- [ ] Verify runtime hot path is unchanged.
- [ ] Collect integrated telemetry.

## G9 Display Matrix

- [ ] Test windowed.
- [ ] Test borderless fullscreen.
- [ ] Test FSO.
- [ ] Test true FSE if available.
- [ ] Test DirectFlip/MPO diagnostics.
- [ ] Test single monitor.
- [ ] Test mixed DPI.
- [ ] Test mixed refresh.
- [ ] Test HDR off.
- [ ] Test HDR on.
- [ ] Test Auto HDR.
- [ ] Test VRR off.
- [ ] Test VRR on.
- [ ] Test competing overlays.
- [ ] Report `Visible`.
- [ ] Report `PossiblyOccluded`.
- [ ] Report `NotVerifiablyVisible`.

## G10 ONNX Optional

- [ ] Keep ONNX build default off.
- [ ] Keep ONNX live mode blocked until DSP/template accuracy and performance justify a later ADR.
- [ ] Pin ONNX Runtime version/hash/license.
- [ ] Add ONNX NOTICE/SBOM entry.
- [ ] Disable telemetry when creating `OrtEnv` or use telemetry-free build.
- [ ] Run offline/firewalled smoke.
- [ ] Validate model metadata schema.
- [ ] Validate feature config hash.
- [ ] Validate class/bin labels.
- [ ] Validate dataset version.
- [ ] Validate model license/rights.
- [ ] Use CPU provider first.
- [ ] Forbid CUDA, DirectML, TensorRT, or other GPU providers for audio ONNX unless a later ADR proves no regression.
- [ ] Set intra-op threads to 1.
- [ ] Set inter-op threads to 1.
- [ ] Set sequential execution.
- [ ] Disable spinning.
- [ ] Forbid parallel ONNX sessions.
- [ ] Add bounded inference queue.
- [ ] Set ONNX queue depth to 1.
- [ ] Drop stale inference frames.
- [ ] Drop input age > 100 ms.
- [ ] Require ONNX inference p95 <= 3 ms.
- [ ] Require ONNX inference p99 <= 8 ms.
- [ ] Disable ONNX for the rest of the session after repeated p99 budget violations.
- [ ] Report ONNX latency separately.
- [ ] Forbid runtime model downloads.

## G10 Release Gate

- [ ] Generate SBOM.
- [ ] Generate NOTICE.
- [ ] Run source forbidden-API scan.
- [ ] Run binary import scan.
- [ ] Run privacy log audit.
- [ ] Run no-network/default smoke.
- [ ] Run no-raw-audio-persistence smoke.
- [ ] Run 30-minute stress.
- [ ] Document degraded display modes.
- [ ] Document authorized-use boundary.
- [ ] Keep feature disabled by default unless explicitly configured.

## Future Gameplay Cue Gate

- [ ] Do not implement in v1.
- [ ] Create new ADR before any audio-to-vision cue.
- [ ] Add native tests proving no fire authority.
- [ ] Add native tests proving no recoil authority.
- [ ] Add native tests proving no target lock by audio alone.
- [ ] Keep default disabled.
