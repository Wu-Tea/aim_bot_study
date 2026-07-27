# Audio + Visual Fusion Implementation Plan

Last updated: 2026-06-25
Status: implementation plan after networked multi-view review and performance re-audit

## Review Result

The direction is conditionally approved for prototype and implementation planning.

Performance re-audit result:

- Direction remains viable only as a measurement-first prototype path.
- The previous plan is not approved for runtime/canvas/audio integration until G0.5 passes.
- "Record metrics" is not sufficient. Any repeatable regression in game frame time, native vision timing, controller/recoil/output jitter, present mode, or CPU/GPU queue pressure blocks the feature.
- If the performance harness cannot prove noise below the thresholds in this plan, the result is inconclusive and the feature remains disabled.

Approved direction:

- Keep the three-process model.
- Keep audio visual-only by default.
- Keep `fusion_canvas.exe` as visual composition only.
- Use Windows public APIs: WASAPI process loopback, DirectComposition, D3D11, Direct2D, DirectWrite, named file mappings, and named events.
- Keep no injection, no render hook, no driver, no game memory read, no anti-cheat evasion, no hidden capture, no audio-driven fire/recoil/aim authority.

Not approved yet:

- Release builds.
- Default enablement.
- Any runtime/canvas/audio fusion integration before G0.5 passes.
- Any audio-to-gameplay cue bridge.
- Any low-level keyboard hook path.
- Any ONNX path without telemetry/network proof.

## Stop Gates

These gates must close in order. Do not skip a gate because a later visual demo appears to work.

### G0 Boundary Gate

Pass criteria:

- No DLL injection, remote thread, APC injection, render hook, swapchain hook, kernel driver, anti-capture, hidden overlay, or anti-cheat evasion code.
- No admin manifest.
- No `Global\` kernel objects.
- No `PROCESS_VM_*`, `WriteProcessMemory`, `CreateRemoteThread`, or equivalent game-process access path.
- `WH_KEYBOARD_LL` absent from v1 builds unless a later ADR explicitly enables it.
- Audio cannot grant fire, recoil, target lock, or aim-assist authority.

Evidence:

- Source scan.
- Binary import-table scan for release candidates.
- Config defaults show all audio/fusion features disabled unless explicitly configured.

### G0.5 Performance Non-Regression Gate

This is a blocking gate before `shared_fusion`, `fusion_canvas.exe`, `audio_direction.exe`, or `VisionFusionChannel` integration work can be treated as viable.

Pass criteria:

- A/A baseline noise is measured first with the current native runtime, target game or repeatable flip-model app, target display mode, and current vision model/engine.
- A/A requires at least 5 paired baseline runs. Each run uses the same fixed scene, at least 2 minutes warmup, and at least 5 minutes capture.
- A/A p95-of-paired-deltas for game p95 frame time is <= 0.20 ms, and p95-of-paired-deltas for game p99 frame time is <= 0.50 ms. If the harness is noisier than this, no A/B case can pass.
- A/B cases are paired and order-balanced. Required cases:
  - baseline
  - fusion compiled but fully disabled
  - idle/no-HWND canvas probe
  - visible HWND with zero present
  - sparse canvas primitives at 15 Hz and 30 Hz
  - worst-case mock canvas primitives
  - no-op `VisionFusionChannel` publisher with no reader
  - no-op publisher with mock reader/event storm
  - audio process-loopback + ring-copy probe
  - audio DSP/template probe with ONNX off
  - malformed IPC storm
- PresentMon, WPR/ETW or equivalent trace, native perf logs, and config/binary hashes are captured for every run.
- Target game present mode must not repeatably degrade. If baseline is `Hardware: Independent Flip`, `Hardware Composed: Independent Flip`, DirectFlip, or MPO-backed, canvas-enabled cases must keep the same class or the canvas path is blocked for that display mode.
- Game frame-time paired delta must pass all limits:
  - average and median <= +0.10 ms
  - p95 <= +0.20 ms and <= +1.0%
  - p99 <= +0.50 ms and <= +2.0%
  - p99.9 <= +0.75 ms and <= +3.0%
  - 1% low FPS decline <= 1.0%
  - 0.1% low FPS decline <= 2.0%
  - late/dropped present increase <= 0.1% frames and <= 3 events per run
- Native vision/runtime paired delta must pass all limits:
  - `pre`, `infer`, `gpu`, `wait`, and `age` p95/p99 deltas <= max(0.50 ms, 5.0%)
  - controller tick p99 delta <= 0.10 ms
  - recoil stage p99 delta <= 0.10 ms
  - output interval jitter p99 delta <= 0.20 ms
  - no new repeating p99 spike cluster
- Publisher hot path:
  - disabled config creates no fusion IPC objects, no fusion thread, and no fusion hot-path work
  - enabled publish p50 <= 0.02 ms, p95 <= 0.10 ms, p99 <= 0.20 ms, max <= 0.50 ms
  - hot path lock wait, IPC wait, file I/O, synchronous logging, heap allocation, COM, D3D, DXGI, DComp, WASAPI, process launch, and thread launch counts are zero
- Canvas/DWM GPU:
  - no GPU readback, CPU bitmap upload loop, desktop duplication, WARP, GDI layered-window render path, cross-adapter copy, or full-screen shader effect is used in live mode
  - canvas presents only on source changes or TTL expiry, never on controller tick
  - idle/no-change canvas issues zero presents after the retained scene is committed
  - canvas plus DWM GPU work must not create queue delay overlapping target game present or TensorRT inference in ETW/GPUView-style traces
- Audio sidecar:
  - capture callback p95 <= 0.20 ms, p99 <= 0.50 ms, max <= 1.00 ms, and max <= 25% of the active audio engine period
  - capture callback only copies packets into the preallocated ring and releases them
  - DSP/template worker per 10 ms hop p95 <= 1.0 ms and p99 <= 2.0 ms
  - ring backlog p99 <= 20 ms and max <= 40 ms
  - backlog overflow drops old packets or jumps to the latest window; it never catches up historical backlog with a CPU burst
- Kill switch:
  - `FUSION_FORCE_OFF=1`, config off, launcher `--no-fusion` or equivalent, and hotkey hard-off are supported
  - hard-off stops source publication, stops canvas presents, hides or destroys overlay HWNDs, releases or tears down DComp visuals/swapchains, and stops audio workers within 1 second
  - any performance tripwire over budget for 2 consecutive 10-second windows degrades in order: reduce canvas rate, stop audio sidecar, stop canvas, stop vision publisher
  - after a tripwire, the supervisor does not auto-restart the disabled feature in the same session

Evidence:

- A/A and A/B summary JSON.
- PresentMon CSV or equivalent present metrics.
- WPR/ETW/GPUView-style trace for at least baseline, canvas probe, publisher probe, and audio probe.
- Native runtime perf logs with p50/p95/p99/p99.9/max.
- Kill-switch latency and post-disable recovery report.
- Source scan for forbidden hot-path calls.

### G1 Shared Fusion ABI Gate

Pass criteria:

- `shared_fusion` ABI defines source type, primitive type, coordinate space, color space, display-topology generation, producer identity, active buffer index, sequence, QPC timestamp/frequency, and bounded text arena.
- Coordinates default to virtual-desktop physical pixels.
- `rgba_premul` is SDR sRGB premultiplied alpha; HDR/scRGB conversion is owned by the canvas.
- Wrong nonce, version, size, primitive count, active buffer index, text offset/length, source type, coordinate space, and sequence regression are rejected.
- Named object pre-creation returns `ERROR_ALREADY_EXISTS` and fails closed.

Evidence:

- Unit tests for struct validation.
- IPC parser fuzz or generated malformed-input tests.
- Same-user precreation test.
- No producer waits on a reader.

### G2 Lifecycle Gate

Pass criteria:

- A single launcher/supervisor creates the session nonce.
- Producers do not launch, restart, or terminate each other.
- Producers do not launch or restart `fusion_canvas.exe`.
- Canvas exit does not block producers.
- Producer exit clears that source after TTL and keeps other sources alive.
- Duplicate process launch is prevented for one session.

Evidence:

- Lifecycle smoke tests.
- Crash/restart tests for producer and canvas.
- Bounded backoff logs.

### G3 Canvas/Input Gate

Pass criteria:

- `fusion_canvas.exe` creates one transparent top-level popup HWND per monitor.
- Locked mode is click-through through `WM_NCHITTEST -> HTTRANSPARENT`.
- `WS_EX_TRANSPARENT` is optional/empirical, not the authority for input-through.
- No taskbar entry, no Alt-Tab entry, no activation on click.
- Mouse buttons, mouse wheel, focus, and game input stay with the target app in locked mode.
- Hotkeys use `RegisterHotKey` with `MOD_NOREPEAT`.
- `UnregisterHotKey` runs on shutdown/rebind.
- Low-level keyboard hooks are absent from v1.

Evidence:

- Canvas shell smoke test.
- Manual focus/input test.
- Automated window-style inspection where practical.

### G4 Renderer/Pacing Gate

Pass criteria:

- D3D11 device uses `D3D11_CREATE_DEVICE_BGRA_SUPPORT`.
- DComp visual tree attaches a DXGI composition swapchain.
- If using `IDXGISwapChain2::GetFrameLatencyWaitableObject`, the swapchain is created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`.
- If that flag/path is unavailable, renderer explicitly reports DWM timing / waitable timer fallback.
- Frame pacing is source-channel/TTL-driven, not a busy render loop.
- Default live canvas rate is capped at 30 Hz, audio-only cues target 15 Hz, and diagnostics text/animations/fades are off by default.
- No source change means no present after the retained scene is committed.
- Renderer never uses desktop duplication, capture-and-replay, CPU readback, staging texture readback, per-frame CPU bitmap upload, WARP/software D3D, D3D debug layer in live mode, GDI/`UpdateLayeredWindow` live rendering, full-screen blur/effects, or cross-adapter copies.
- Hard-off destroys or hides overlay HWNDs and stops presents; a transparent idle window is not considered disabled.
- Renderer handles device removed, DComp commit failure, topology change, DPI change, HDR state change, and DComp device-state recovery.

Evidence:

- Renderer smoke test with mock primitives.
- Telemetry for present duration, wait time, source age, draw count, device recreate count.

### G5 Audio Capture Gate

Pass criteria:

- Process loopback uses `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`.
- `AUDIOCLIENT_ACTIVATION_PARAMS` is passed through `PROPVARIANT` `VT_BLOB`.
- Completion handler tolerates MTA/free-threaded callback delivery.
- Activation timeout, cancellation, target exit, and shutdown-after-callback races are safe.
- PID plus creation time are validated.
- Target no-render stream becomes `Silent`, not crash.
- Endpoint fallback is clearly degraded.
- Runtime support uses API/HRESULT/smoke result, not only a hardcoded build number.
- Capture callback p95 <= 0.20 ms, p99 <= 0.50 ms, max <= 1.00 ms, and max <= 25% of the active audio engine period.
- Capture callback performs no DSP, resampling, template matching, ONNX inference, allocation, logging, file I/O, worker wait, or COM activation retry.

Evidence:

- Synthetic audio process smoke test.
- Target exit/restart test.
- Unsupported/fallback test.
- COM lifecycle test.

### G6 Audio Format/DSP Gate

Pass criteria:

- `WAVEFORMATEXTENSIBLE` channel mask and interleaved order are parsed and tested.
- Canonical processing format is 48 kHz float32, preserving up to 8 channels.
- Resampler choice is closed before completing the first capture milestone.
- Shared gain normalization preserves ILD.
- Mono and near-mono suppress direction.
- Conflicting bands suppress direction.
- ILD sign tests pass for synthetic left/right clips.
- GCC-PHAT sanity tests pass for synthetic sample delay.
- PCM ring is preallocated, warm-touched, fixed-capacity, and writer-nonblocking.
- PCM ring target capacity is 200 ms unless a benchmark proves a smaller bounded capacity is safer.
- Ring backlog p99 <= 20 ms and max <= 40 ms.
- DSP/template worker is single-worker by default; per 10 ms hop p95 <= 1.0 ms and p99 <= 2.0 ms.
- Audio event age p95 <= 60 ms and p99 <= 100 ms.
- FFT plans/config, Hann windows, mel banks, and template tensors are precomputed before live processing.
- No OpenMP or multithreaded FFT is used in the live path unless a later performance ADR proves no regression.

Evidence:

- Unit tests with generated clips.
- Offline WAV replay deterministic results.
- Direction confusion matrix for synthetic cases.

### G7 Detector/Evaluation Gate

Pass criteria:

- Template detector uses positives and negatives from the first detector milestone.
- Negative sets include background, UI, voice, music, and overlap/noise where available.
- Evaluation reports unknown coverage, accepted-only accuracy, false directional accept rate, left/right sign accuracy, false positives/minute, and profile/background breakdown.
- Real captures include rights, sha256, session id, split group, audio profile, event class, and direction label/unknown.
- No unauthorized game audio or voice chat is committed.

Evidence:

- Offline evaluator output.
- Dataset manifest validation.
- Negative-session report.

### G8 Hot Path Gate

Pass criteria:

- Native runtime hot path never waits on audio or canvas.
- Native capture, CUDA preprocess, TensorRT enqueue/wait, target selection, controller, recoil, ViGEm output, and `VisionFusionChannel` publication are all treated as hot path for this gate.
- Producer publish p50 <= 0.02 ms, p95 <= 0.10 ms, p99 <= 0.20 ms, max <= 0.50 ms.
- Controller tick p50/p95/p99, recoil stage duration, output interval jitter, vision age, publish cost, lock waits, and IPC waits are recorded.
- A/B cases include baseline, fusion disabled, vision publisher enabled, audio sidecar enabled, canvas enabled, producer crash, canvas crash, and malformed IPC storm.
- `fusion disabled` means no session argument, no mapping/event creation, no fusion thread, and no publisher hot-path branch beyond static config checks already proven below noise.
- Any wait, lock, heap allocation, vector growth, string formatting, file I/O, synchronous logging, COM, D3D, DXGI, DComp, WASAPI, process/thread launch, `Create/Open/Map*` IPC call, network call, or fusion-added `cudaStreamSynchronize`/`cudaDeviceSynchronize` in the native hot path fails the gate.
- Publisher never reconnects, recreates, opens, maps, or retries IPC objects from the hot path.
- Reader absence, canvas crash, audio crash, malformed IPC, wrong nonce, and IPC event storms must not change runtime output, controller p99, or vision age beyond G0.5 thresholds.
- Repeated publish failure or budget violation disables the publisher for the rest of the session.

Evidence:

- Native runtime A/B benchmark logs.
- Fault-injection logs.
- Source scan for forbidden hot-path calls.

### G9 Display Honesty Gate

Pass criteria:

- Borderless/windowed/FSO validation is documented.
- True FSE, DirectFlip, independent flip, and MPO are reported as `Visible`, `PossiblyOccluded`, or `NotVerifiablyVisible`.
- Degraded visibility never triggers injection, hook, anti-capture, or evasion workarounds.
- HDR and SDR reference white are handled or clearly degraded.
- VRR on/off is tested.

Evidence:

- Display matrix report.
- PresentMon/ETW diagnostic report for every supported display mode in development builds.
- In-app/tray diagnostic status.

### G10 License/Privacy/Release Gate

Pass criteria:

- SBOM/NOTICE exists.
- Dependency versions and hashes are pinned.
- KissFFT license record is BSD-3-Clause.
- ONNX Runtime is MIT and optional/default-off.
- ONNX stays disabled in live mode until DSP/template performance and accuracy justify it in a later ADR.
- ONNX live mode, if ever enabled, uses CPU provider only, `intra_op_num_threads=1`, `inter_op_num_threads=1`, sequential execution, spinning disabled, queue depth 1, latest-wins dropping, no parallel sessions, and no runtime model download.
- ONNX inference p95 <= 3 ms and p99 <= 8 ms; input age > 100 ms is dropped.
- CUDA, DirectML, TensorRT, or other GPU execution providers are prohibited for audio ONNX unless a later ADR proves no game/vision regression.
- GPL/AGPL repositories remain reference-only.
- Raw audio/spectrogram persistence is off by default.
- Logs avoid usernames, full paths, command lines, window titles, and raw process lists unless diagnostics are explicitly enabled.
- ONNX telemetry is disabled or absent before ONNX can be enabled.
- Offline/firewalled smoke proves ONNX path has no outbound network attempt.

Evidence:

- License bundle.
- Dependency manifest.
- Privacy log audit.
- Network-off smoke.

### G11 Gameplay Cue Gate

Pass criteria:

- Not part of v1.
- Any audio-to-vision cue needs a new ADR.
- Default remains disabled.
- It cannot grant fire authority, recoil authority, target lock, or aim authority by itself.

Evidence:

- ADR.
- Native tests proving weak cue cannot trigger fire/recoil/lock authority.

## Implementation Plan

### Phase 0: Baseline And Branch Hygiene

Goal:

Prepare the workspace so implementation can be measured against the current native runtime without mixing unrelated changes.

Steps:

1. Inspect current `git status`.
2. Identify unrelated dirty files and leave them untouched.
3. Confirm the native runtime build entry points:
   - `scripts\launch\gamepad_start.bat`
   - `scripts\launch\gamepad_native_cpp_start.bat`
   - `native\runtime_app\`
   - `native\controller_native\`
4. Capture baseline verification commands:
   - `scripts\verify\native_pipeline_contract.bat`
   - targeted native controller tests currently used in this project
   - any existing native CMake build command
5. Record baseline timing fields available today.
6. Define a feature branch or worktree before implementation if the user requests code changes.

Deliverables:

- Baseline command list.
- Baseline timing/log fields.
- Confirmed dirty-worktree notes.

Exit criteria:

- G0 scan has no new violation.
- The implementation branch/worktree plan is clear.

### Phase 0.5: No-Impact Performance Feasibility

Goal:

Prove the feature can exist without measurable game or native vision regression before building the shared ABI or real sidecars.

Steps:

1. Build or select the measurement harness:
   - PresentMon CSV or equivalent target-process present metrics.
   - WPR/ETW/GPUView-style capture for graphics, DWM, context switches, CPU sampling, and native providers.
   - Native perf logs with `pre`, `infer`, `gpu`, `wait`, `age`, controller tick, recoil stage, output jitter, and publisher cost.
2. Record immutable run metadata:
   - Windows build.
   - GPU driver.
   - power plan.
   - monitor topology.
   - resolution and refresh rate.
   - HDR, VRR, FSO, and MPO state where available.
   - game graphics preset.
   - model/engine hash.
   - config hash.
   - binary hashes.
3. Run A/A baseline:
   - at least 5 paired runs.
   - 2 minutes warmup plus 5 minutes capture per run.
   - one fixed training range, replay, scripted route, or equivalent repeatable scene.
4. Run `fusion_canvas_perf_probe.exe` or equivalent disposable probe:
   - process exists but no HWND.
   - visible full-screen click-through HWND with zero present.
   - sparse primitives at 15 Hz.
   - sparse primitives at 30 Hz.
   - worst-case mock primitive set.
5. Run `VisionFusionChannel` no-op publisher shim:
   - compiled/config off.
   - publisher enabled with no reader.
   - publisher enabled with mock reader.
   - event storm/malformed reader.
6. Run `audio_perf_probe.exe`:
   - synthetic target process at 48 kHz stereo and, where possible, 8-channel output.
   - process loopback + preallocated ring copy.
   - capture + STFT/KissFFT + log-mel/template, ONNX off.
   - intentionally slowed worker to prove drop-oldest/latest-wins behavior.
   - optional ONNX only after the DSP/template case passes.
7. Apply G0.5 pass/fail thresholds to every A/B case.
8. If any case fails:
   - keep all fusion/audio/canvas features disabled.
   - record the blocked mode and failure metric.
   - do not proceed to `shared_fusion` implementation unless the failure is fixed and the full gate is rerun.

Deliverables:

- Performance harness command list.
- A/A noise report.
- Canvas performance probe report.
- Vision publisher shim report.
- Audio performance probe report.
- Kill-switch/tripwire behavior spec.

Exit criteria:

- G0.5 complete.
- If G0.5 is inconclusive or failed, the implementation stops at performance research and does not proceed to production sidecars.

### Phase 1: Policy, ADR, And Build Flags

Goal:

Freeze implementation boundaries before code can drift toward unsafe paths.

Steps:

1. Add a decision record for the integrated audio+visual fusion process model.
2. Add a decision record for v1 hotkey policy:
   - `RegisterHotKey` only.
   - no `WH_KEYBOARD_LL` in v1.
3. Add build/config flags:
   - `FUSION_ENABLE_CANVAS=OFF` by default if using CMake option style.
   - `AUDIO_ENABLE_RUNTIME=OFF` by default.
   - `AUDIO_ENABLE_ONNX=OFF` by default.
   - diagnostics flags default off.
4. Add source-scan checklist for forbidden APIs:
   - injection/hook/driver/process memory APIs
   - anti-capture APIs
   - outbound network in runtime paths
   - `Global\` mappings
5. Decide whether to use C++17 compatibility or isolate new targets as C++20.

Deliverables:

- ADR files.
- Build flag plan.
- Boundary scan checklist.

Exit criteria:

- G0 complete.

### Phase 2: `native/shared_fusion`

Goal:

Create the ABI before either audio or canvas depends on it.

Steps:

1. Create `native/shared_fusion/fusion_channel.h`.
2. Define:
   - `FusionChannelMagic`
   - `FusionChannelVersion`
   - `FusionSourceType`
   - `FusionPrimitiveType`
   - `FusionCoordinateSpace`
   - `FusionColorSpace`
   - `FusionChannelHeader`
   - `FusionPrimitive`
   - optional text arena metadata
3. Include:
   - `source_type`
   - `primitive_count`
   - `active_buffer_index`
   - `flags`
   - `producer_id`
   - `sequence`
   - `qpc_timestamp`
   - `qpc_frequency`
   - `display_topology_generation`
4. Define coordinate contract:
   - default virtual-desktop physical pixels
   - optional monitor-local physical pixels
   - stale generation behavior
5. Define color contract:
   - producers output SDR sRGB premultiplied alpha
   - canvas converts for HDR/scRGB
6. Create validation helpers:
   - header bounds
   - primitive count
   - active buffer index
   - text range
   - enum range
   - sequence monotonicity per producer identity
7. Create unit tests for all validation failure modes.
8. Create malformed input/fuzz-style generated tests for headers and text ranges.

Deliverables:

- `fusion_channel.h/.cpp`
- validation tests
- ABI notes in docs if the structs diverge from the plan

Exit criteria:

- G1 validation tests pass.

### Phase 3: `native/shared_fusion` IPC

Goal:

Implement same-user source-channel IPC with fail-closed creation and non-blocking publication.

Steps:

1. Create `fusion_ipc.h/.cpp`.
2. Generate channel names:
   - `Local\YoloStudy001.Fusion.<nonce>.<source_type>.Mapping`
   - `Local\YoloStudy001.Fusion.<nonce>.<source_type>.Event`
3. Build explicit current-user security descriptor.
4. Producer path:
   - `CreateFileMappingW`
   - reject `ERROR_ALREADY_EXISTS`
   - `CreateEventW`
   - reject `ERROR_ALREADY_EXISTS`
   - map writable view
5. Canvas path:
   - open mapping read-only where possible
   - open event synchronize-only where possible
6. Implement double-buffer publication:
   - fill inactive buffer
   - publish with release ordering
   - signal event
7. Implement reader:
   - acquire ordering
   - copy to local staging
   - validate
   - reject stale or malformed data
8. Add tests:
   - precreated mapping fails closed
   - wrong nonce cannot attach
   - wrong version rejected
   - oversized primitive count rejected
   - sequence regression counted
   - producer restart with new `producer_id` accepted as fresh

Deliverables:

- `fusion_ipc.h/.cpp`
- IPC unit tests
- IPC smoke test process pair

Exit criteria:

- G1 IPC gate complete.

### Phase 4: Lifecycle Launcher / Supervisor

Goal:

Provide one owner for session nonce and process lifecycle.

Steps:

1. Start with a script-based supervisor if that matches current launcher style.
2. Generate session nonce once.
3. Launch:
   - `cod_native_runtime.exe --fusion-session <nonce> --vision-channel <name>`
   - `audio_direction.exe --fusion-session <nonce> --audio-channel <name>`
   - `fusion_canvas.exe --fusion-session <nonce> --read-channel <vision> --read-channel <audio>`
4. Add duplicate-session guard.
5. Add bounded restart policy:
   - canvas restart allowed
   - audio sidecar restart allowed if configured
   - runtime restart should remain a user-level action unless explicitly approved
6. Add shutdown ordering:
   - request canvas stop
   - request audio stop
   - leave runtime behavior unchanged unless launcher owns runtime
7. Add status output:
   - process running
   - process exited
   - restart count
   - last error

Deliverables:

- launcher/supervisor script or native supervisor skeleton
- lifecycle smoke test

Exit criteria:

- G2 complete.

### Phase 5: `fusion_canvas.exe` Shell

Goal:

Create the visible full-screen canvas process without renderer complexity.

Steps:

1. Create target:
   - `native/overlay_canvas/main.cpp`
   - `canvas_app`
   - `monitor_manager`
   - `canvas_window`
   - `hotkey_manager`
2. Set DPI awareness before any window creation.
3. Enumerate monitors and create one HWND per monitor.
4. Use:
   - `WS_POPUP`
   - `WS_EX_TOPMOST`
   - `WS_EX_NOACTIVATE`
   - `WS_EX_TOOLWINDOW`
5. Keep `WS_EX_TRANSPARENT` behind a tested compatibility flag, not as the input contract.
6. Implement:
   - `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
   - `WM_NCHITTEST -> HTTRANSPARENT` in locked mode
   - `WM_NCHITTEST -> HTCLIENT` in diagnostics mode
7. Register hotkeys:
   - `Ctrl+Shift+F10`
   - `Ctrl+Shift+F11`
   - `MOD_NOREPEAT`
8. Add alternate hotkey config.
9. Add `UnregisterHotKey` on shutdown/rebind.
10. Add diagnostics state:
   - locked/unlocked
   - active monitors
   - input-through status
11. Validate:
   - no taskbar entry
   - no Alt-Tab entry
   - no activation
   - click-through
   - mouse wheel and focus stay with target

Deliverables:

- `fusion_canvas.exe` shell
- canvas shell smoke test

Exit criteria:

- G3 complete.

### Phase 6: Canvas Renderer

Goal:

Render mock primitives over the whole monitor canvas using DirectComposition and D3D11.

Steps:

1. Create:
   - `dcomp_renderer`
   - `d2d_scene_renderer`
   - `graphics_telemetry`
2. Create D3D11 device with BGRA support.
3. Create Direct2D/DirectWrite resources from the same DXGI device.
4. Create DirectComposition device and target.
5. Create composition swapchain:
   - `DXGI_FORMAT_B8G8R8A8_UNORM` for SDR
   - premultiplied alpha
   - flip sequential
   - `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` if using waitable object pacing
6. Call `SetMaximumFrameLatency(1 or 2)` when using waitable object pacing.
7. If waitable object path fails, enable DWM timing / waitable timer fallback and log it.
8. Render mock primitives:
   - left/right/center arcs
   - target boxes
   - confidence rings
   - diagnostics text
   - diagnostics text must be disabled in live mode unless performance gates are rerun with it enabled
9. Implement retained scene resources:
   - brushes
   - stroke styles
   - text formats
   - prebuilt geometry for repeated arcs/rings
10. Implement device-loss recovery:
   - D3D device removed
   - DComp commit failure
   - DComp device-state checks during recovery/paint paths
11. Add telemetry:
   - CPU draw prep
   - present duration
   - wait time
   - source age
   - draw count
   - device recreate count
   - hard-off latency
   - skipped-present count
   - present-mode snapshot where available

Deliverables:

- DirectComposition renderer
- mock primitive renderer
- telemetry output

Exit criteria:

- G4 complete for SDR single monitor.

### Phase 7: Canvas Source-Channel Reader

Goal:

Connect canvas rendering to real source-channel IPC.

Steps:

1. Create `fusion_channel_reader`.
2. Open configured source channels read-only.
3. Wait on per-source events with timeout.
4. Copy to local staging buffers.
5. Validate each source channel.
6. Drop malformed/stale/wrong-generation channels.
7. Compose by:
   - source priority
   - TTL
   - confidence
   - primitive type
   - diagnostics settings
8. Implement per-source metrics:
   - age
   - valid count
   - stale count
   - malformed count
   - dropped count
9. Implement source disconnect:
   - clear source after TTL
   - keep other sources alive
10. Add a mock publisher test process.

Deliverables:

- real source-channel reader
- mock publisher
- concurrent source rendering test

Exit criteria:

- canvas renders two mock source channels concurrently without blocking producers.

### Phase 8: Vision Publisher In `cod_native_runtime.exe`

Goal:

Add visual source publication to native runtime without changing controller/recoil authority.

Steps:

1. Add `fusion_channel_publisher` integration in `native/runtime_app`.
   - only after Phase 0.5 publisher shim passes.
2. Publish only screen-space primitives from existing vision state.
3. Do not read canvas state.
4. Do not read audio state.
5. Do not block runtime loop on IPC.
6. Put publish behind disabled-by-default config.
7. Add publish timing telemetry.
   - p50/p95/p99/max.
   - dropped publish count.
   - disabled-by-tripwire count.
8. Add tests/static checks proving:
   - controller/recoil code does not depend on canvas
   - publish failure does not affect runtime output
   - no wait/log/file I/O in hot tick
9. Run native pipeline contract verifier.
10. Run A/B timing:
   - baseline
   - vision publisher enabled
   - canvas absent
   - canvas present
   - malformed IPC storm
11. Fail the phase if any G0.5 native/runtime threshold regresses.

Deliverables:

- `VisionFusionChannel` publisher
- timing metrics
- native contract tests/checks

Exit criteria:

- G8 vision half passes.

### Phase 9: Audio Core And Synthetic/WAV Harness

Goal:

Build deterministic audio processing before touching live capture.

Steps:

1. Create `native/audio_core`.
2. Define:
   - audio format
   - sample type conversion
   - frame timestamps
   - ring buffer
3. Create generated synthetic clips:
   - left stronger
   - right stronger
   - center
   - mono
   - near-mono
   - conflicting bands
   - delayed channel impulse
   - silence
   - clipping/NaN/Inf rejection
4. Implement WAV/synthetic source.
5. Implement shared gain normalization.
6. Implement channel mask parser tests.
7. Decide and document resampler.
8. Implement or wrap resampler.
9. Ensure ring overflow drops oldest and increments counters.
10. Add no-allocation/no-lock instrumentation for producer path where practical.
11. Add backlog skip-to-latest behavior and prove the worker never catches up historical backlog with a CPU burst.
12. Add thread-priority policy:
    - `audio_direction.exe` defaults to below-normal priority.
    - capture packet thread may use MMCSS only for packet copy/release work.
    - DSP/template and ONNX workers stay below-normal or lower.
    - high, realtime, time-critical, busy-spin, and global timer-resolution changes are prohibited.

Deliverables:

- audio core library
- WAV/synthetic replay tool
- generated test clips/scripts

Exit criteria:

- G6 format/ring tests pass.

### Phase 10: DSP Direction Baseline

Goal:

Implement explainable left/center/right/unknown before ML.

Steps:

1. Add KissFFT dependency with pinned version/hash/license.
2. Implement Hann window and STFT.
3. Implement broadband ILD.
4. Implement bandwise ILD.
5. Implement GCC-PHAT as supporting evidence.
6. Implement near-mono suppression.
7. Implement conflicting-band suppression.
8. Implement confidence calculation.
9. Implement direction dwell/hysteresis.
10. Run synthetic tests:
    - left/right sign
    - center
    - near-mono unknown
    - conflicting bands unknown
    - delayed channel sanity
11. Generate confusion matrix for synthetic clips.

Deliverables:

- `native/audio_dsp`
- `native/audio_direction`
- unit tests and synthetic report

Exit criteria:

- Synthetic left/right sign accuracy is 100%.
- Unknown behavior works on mono/near-mono/conflict.

### Phase 11: Template Detector

Goal:

Detect target classes with deterministic baseline and negatives.

Steps:

1. Define template manifest schema.
2. Implement template bank loader.
3. Implement log-mel/template features.
4. Implement onset gate.
5. Implement class-specific hysteresis.
6. Implement cooldown.
7. Add positives and negatives in a local authorized profile.
8. Add dataset manifest validation:
   - clip id
   - sha256
   - rights
   - session id
   - split group
   - audio profile
   - event class
   - direction label or unknown
9. Add offline evaluator:
   - precision/recall/F1
   - false positives/minute
   - unknown coverage
   - accepted-only accuracy
   - false directional accept rate
   - left/right sign accuracy
   - profile/background breakdown
10. Run negative sessions before considering real gameplay testing.

Deliverables:

- template detector
- manifest schema/tool
- offline evaluator

Exit criteria:

- G7 baseline detector gate passes on synthetic/local authorized samples.

### Phase 12: WASAPI Process Loopback Capture

Goal:

Capture target process-tree audio with Windows public APIs.

Steps:

1. Create `native/audio_capture_win`.
2. Implement target process resolver:
   - explicit PID
   - process creation time
   - configured executable name
   - path/hash diagnostics without logging full paths by default
3. Implement process loopback activation:
   - `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`
   - `AUDIOCLIENT_ACTIVATION_PARAMS`
   - `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`
   - `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`
   - `PROPVARIANT VT_BLOB`
4. Implement async completion handler:
   - MTA/free-thread safe
   - cancellation safe
   - timeout safe
   - shutdown-after-completion safe
5. Implement capture states.
6. Implement fallback endpoint loopback as degraded only.
7. Use synthetic audio process as target.
8. Test:
   - normal capture
   - no render stream -> `Silent`
   - target exit -> `Disconnected`
   - target restart -> PID/creation-time revalidate
   - unsupported OS/API -> degraded/error
   - endpoint fallback -> degraded
9. Publish PCM into SPSC ring without allocation/log/lock.

Deliverables:

- process-loopback capture library
- capture smoke tool
- synthetic target audio process

Exit criteria:

- G5 complete.

### Phase 13: `audio_direction.exe`

Goal:

Turn audio pipeline into a sidecar that publishes `AudioFusionChannel`.

Steps:

1. Create `native/audio_direction_app`.
2. Load audio config.
3. Resolve target process.
4. Start capture.
5. Start audio worker.
6. Run detector and direction estimator.
7. Convert validated events into audio primitives.
8. Publish to `AudioFusionChannel`.
9. Expose diagnostics:
   - capture status
   - format
   - channel mask
   - RMS
   - ILD
   - GCC
   - event confidence
   - direction confidence
   - ring backlog age
   - dropped stale frames
   - reconnect count
10. Ensure no raw audio persistence.
11. Run with canvas absent; ensure publish remains non-blocking.
12. Run with malformed canvas or no reader; ensure no crash/wait.

Deliverables:

- `audio_direction.exe`
- audio channel publisher
- diagnostics output

Exit criteria:

- Audio sidecar publishes valid source channel and passes latency gates in synthetic/WAV mode.

### Phase 14: Integrated Canvas + Audio + Vision

Goal:

Run all source channels together while preserving native runtime timing.

Steps:

1. Launch supervisor with one nonce.
2. Start runtime with vision publisher enabled.
3. Start audio sidecar with WAV/synthetic source first.
4. Start canvas reading both sources.
5. Verify:
   - vision primitives draw
   - audio primitives draw
   - TTL/fade works
   - per-source metrics work
   - source disconnect clears only that source
6. Run fault injection:
   - audio exit/restart
   - canvas exit/restart
   - malformed IPC storm
   - wrong nonce source
7. Run native hot-path A/B.

Deliverables:

- integrated local demo
- metrics report
- fault-injection report

Exit criteria:

- G8 complete.

### Phase 15: Display Matrix

Goal:

Validate visibility and degradation honesty.

Steps:

1. Test windowed mode.
2. Test borderless fullscreen.
3. Test FSO.
4. Test true FSE if target exposes it.
5. Test mixed DPI.
6. Test mixed refresh rate.
7. Test HDR off/on/Auto HDR.
8. Test VRR off/on.
9. Test competing overlays where available.
10. Add PresentMon/ETW diagnostics for development builds.
11. Report status:
    - `Visible`
    - `PossiblyOccluded`
    - `NotVerifiablyVisible`
12. Ensure degraded states never propose injection/hook workarounds.

Deliverables:

- display matrix report
- degraded visibility diagnostics

Exit criteria:

- G9 complete for available hardware/modes.

### Phase 16: Optional ONNX Phase

Goal:

Only after DSP/template evaluation proves a need, add local ML inference safely.

Steps:

1. Add ONNX Runtime as optional dependency with pinned version/hash/license.
2. Keep build default off.
3. Disable telemetry when creating `OrtEnv`, or use a verified telemetry-free build.
4. Run offline/firewalled smoke.
5. Validate model metadata:
   - schema version
   - sample rate
   - feature config hash
   - class/bin labels
   - dataset version
   - license/rights
   - thresholds
6. Use CPU provider first.
7. Configure CPU provider as single-threaded:
   - intra-op threads = 1
   - inter-op threads = 1
   - sequential execution
   - spinning disabled
8. Use bounded queue depth 1.
9. Drop stale inference frames.
10. Drop input age > 100 ms.
11. Never download model at runtime.
12. Report ONNX latency separately from DSP and capture.
13. Disable ONNX for the session after repeated p99 budget violations.

Deliverables:

- optional ONNX detector
- metadata validator
- no-network smoke report

Exit criteria:

- G10 ONNX conditions complete.

### Phase 17: Release Candidate Gate

Goal:

Decide whether the feature can be offered as a disabled-by-default experimental feature.

Steps:

1. Run G0-G10.
2. Generate SBOM and NOTICE.
3. Run privacy audit.
4. Run binary import scan.
5. Run source scan for forbidden APIs.
6. Run native hot-path A/B.
7. Run 30-minute stress test.
8. Document degraded display modes.
9. Document authorized-use boundary.
10. Keep audio disabled by default unless user explicitly configures it.

Deliverables:

- release gate report
- known limitations
- safety and privacy notes

Exit criteria:

- Release candidate is either approved as disabled-by-default experimental or blocked with explicit reasons.

## First Coding Slice Recommendation

Start with this exact bounded slice before production implementation:

1. Performance harness commands and summary format
2. `fusion_canvas_perf_probe.exe` or equivalent disposable canvas probe
3. `VisionFusionChannel` no-op publisher shim
4. `audio_perf_probe.exe`
5. G0.5 A/A and A/B reports

Only after G0.5 passes, continue with the shared ABI slice:

1. `native/shared_fusion/fusion_channel.h/.cpp`
2. `native/shared_fusion/fusion_ipc.h/.cpp`
3. `fusion_channel_tests`
4. mock producer/consumer smoke

Reason:

- The user's acceptance criterion is no game or native vision regression. That must be proven before building permanent integration points.
- It closes the highest shared-risk ABI before audio, runtime, and canvas depend on incompatible assumptions.
- It allows testing source-channel security, malformed input, and non-blocking publish without touching native controller/recoil code.
- It gives all later phases one stable contract.
