# Fullscreen Canvas Visual Fusion

Last updated: 2026-06-24
Status: mature design target for a Windows native C++ implementation

## Purpose

This document defines the target design for a visual-fusion layer that behaves as if the whole screen is a drawable canvas.

The user-facing requirement is not a small HUD window:

- Draw anywhere on the target monitor or virtual desktop.
- Toggle the whole visual-fusion layer with a hotkey while the game has focus.
- Do not steal focus, appear in Alt-Tab, appear in the taskbar, or block input when locked.
- Do not block the native vision/controller runtime.
- Prefer the most complete, production-grade design, not a minimal prototype.

## Non-Negotiable Windows Boundary

A normal Windows user-mode app cannot reliably draw pixels directly into the final scanout buffer without participating in one of the graphics/composition paths.

There is no public API that exposes a writable "final desktop surface" or arbitrary hardware overlay plane to a normal process. On modern Windows, visible desktop pixels are owned by the DWM compositor, the DXGI presentation path, the kernel graphics stack, and the display driver. Therefore every practical user-mode approach must use one of these:

1. A compositor participant: HWND, DirectComposition target, or composition swapchain.
2. The target app's own swapchain, which requires injection/hooking or app cooperation.
3. A kernel/display-driver path.

The mature design for this project is therefore:

> Windowless in user experience, compositor-backed in implementation.

The implementation uses full-monitor transparent compositor surfaces. It does not behave like a small normal window.

## Final Architecture

```text
native runtime / audio sidecar
  |
  | non-blocking shared-memory source channel(s) + event signal(s)
  v
fusion_canvas.exe
  |
  | per-monitor transparent top-level HWNDs as compositor anchors
  | DirectComposition visual tree
  | D3D11 composition swapchain
  | Direct2D / DirectWrite drawing
  v
DWM final composition
```

### Process Model

Use a separate `fusion_canvas.exe` process.

Reasons:

- Crash isolation from `cod_native_runtime.exe`.
- Independent UI/message pump and hotkey handling.
- Independent D3D/DComp device lifetime.
- Producers can publish source channels without waiting on rendering.
- Canvas process can restart without neutralizing controller output or disrupting vision capture.

Lifecycle owner responsibilities:

- Optionally launch `fusion_canvas.exe`.
- Launch configured producers and canvas with one shared session nonce.
- Watch the canvas process handle if it launched the process.
- Restart the canvas process with bounded exponential backoff if it crashes.
- Do not let multiple producers race to launch or restart the canvas.

Producer responsibilities:

- Publish a versioned latest source channel into shared memory.
- Signal a named event when new source data is available.
- Never block controller, recoil, vision, or audio capture ticks on canvas availability.

Canvas responsibilities:

- Own all Win32, DirectComposition, D3D11, Direct2D, DirectWrite, hotkey, display topology, and diagnostics UI state.
- Keep running if a producer exits; show a passive "waiting for sources" diagnostics state when unlocked.
- Destroy and recreate graphics resources on device removed, DWM restart, DPI change, display topology change, or HDR/color-space change.

## Public-API Path Ranking

| Rank | Path | Verdict |
|---|---|---|
| 1 | DirectComposition fullscreen canvas | Default production path. Best public, user-mode, GPU-composited route for arbitrary full-screen drawing without injection. |
| 2 | GPU-rendered layered HWND | Close fallback. Similar DWM dependency, sometimes worth benchmarking against DComp for compatibility. |
| 3 | DirectComposition attached to shell/desktop HWND | Research-only. Not a documented stable Microsoft pattern. |
| 4 | Magnification API | Not suitable. It can transform/color the desktop but does not draw arbitrary primitives. |
| 5 | Swapchain hook / in-game render injection | Highest visual fidelity, but rejected as default due to injection, stability, compatibility, and anti-cheat risk. |
| 6 | Capture-and-replay compositor | Too much latency and GPU bandwidth; fights the real game window. |
| 7 | Kernel/display-driver/hardware overlay plane | Infeasible for this project; requires driver work and platform signing. |
| 8 | Desktop DC / GDI direct drawing | Rejected. Not persistent under DWM, CPU-heavy, flickers, no reliable alpha/vsync. |

## Window Shell

Use one top-level transparent popup HWND per monitor.

Per-monitor HWNDs are preferred over one virtual-desktop HWND because they handle mixed DPI, mixed refresh rate, mixed HDR, and monitor add/remove more cleanly.

Required styles:

- `WS_POPUP`
- `WS_EX_TOPMOST`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOOLWINDOW`
- `WS_EX_TRANSPARENT` only as an optional empirical aid after validation
- `WS_EX_NOREDIRECTIONBITMAP` after validating DirectComposition-only content on the target Windows versions

Required behavior:

- No caption.
- No taskbar entry.
- No Alt-Tab entry.
- Never activates on mouse interaction.
- Locked mode is click-through.
- Unlocked diagnostics mode can receive mouse input.
- Exact monitor bounds in virtual desktop coordinates.
- Recreate or resize on `WM_DISPLAYCHANGE`, `WM_DPICHANGED`, session changes, and monitor topology changes.

Input handling:

- `WM_MOUSEACTIVATE`: return `MA_NOACTIVATE`.
- `WM_NCHITTEST`: return `HTTRANSPARENT` when locked; return `HTCLIENT` when diagnostics/settings are unlocked.
- Treat `WM_NCHITTEST` as the authoritative click-through contract. Validate mouse buttons, mouse wheel, focus, and game input remain with the target app in locked mode.
- Do not register raw mouse input in the canvas process.

## Graphics Stack

### Device Creation

Create one graphics context per monitor.

Recommended order:

1. `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` before creating any window.
2. Create D3D11 hardware device with:
   - `D3D11_CREATE_DEVICE_BGRA_SUPPORT`
   - `D3D11_CREATE_DEVICE_DEBUG` in debug builds only
   - feature level 11.0 minimum
3. Query `IDXGIDevice`.
4. Create Direct2D factory and device from the same DXGI device.
5. Create DirectWrite factory.
6. Create DirectComposition device with `DCompositionCreateDevice3` where available; fall back only if required by the configured Windows support range.
7. Create `IDCompositionTarget` for the per-monitor HWND.
8. Create DXGI composition swapchain with `CreateSwapChainForComposition`.
9. Attach the swapchain as DirectComposition visual content.
10. Commit the visual tree.

### Swapchain Parameters

Default SDR composition swapchain:

```cpp
DXGI_SWAP_CHAIN_DESC1 desc = {};
desc.Width = monitor_width_px;
desc.Height = monitor_height_px;
desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
desc.Stereo = FALSE;
desc.SampleDesc.Count = 1;
desc.SampleDesc.Quality = 0;
desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
desc.BufferCount = 2;
desc.Scaling = DXGI_SCALING_STRETCH;
desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
```

If a target Windows/GPU path cannot support `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, the renderer must explicitly fall back to DWM timing / waitable timer pacing and disable the `GetFrameLatencyWaitableObject` path.

HDR-aware mode requires a separate color-pipeline decision:

- Detect output color space with `IDXGIOutput6::GetDesc1`.
- Account for SDR reference white on HDR desktops, for example through `QueryDisplayConfig(DISPLAYCONFIG_SDR_WHITE_LEVEL)` or an equivalent tested path.
- Prefer `DXGI_FORMAT_R16G16B16A16_FLOAT` for HDR monitors.
- Set HDR color space on `IDXGISwapChain3::SetColorSpace1` when supported.
- Author overlay UI colors in a luminance-aware space so markers do not become too dim or too bright on HDR displays.

HDR support must be designed deliberately. Do not accidentally ship SDR-only colors that become unreadable in HDR games.

### Rendering Model

Use a small retained scene model and batched draw calls.

Primitive families:

- `DirectionArc`
- `TargetBox`
- `ConfidenceRing`
- `CrosshairCue`
- `TrajectoryHint`
- `DebugText` only when diagnostics are enabled

Every primitive has:

- source kind: vision, audio, debug, replay
- screen-space physical-pixel coordinates
- QPC timestamp
- sequence number
- TTL
- confidence
- visual priority
- optional animation/fade parameters

Direct2D/DirectWrite resources that must be retained:

- brushes
- stroke styles
- path geometries
- text formats
- reusable text layouts for stable labels

Do not upload CPU bitmaps per frame. Do not read back GPU output.

## IPC Protocol

Use shared memory with a latest-source-channel protocol and a named event.

Reasons:

- Low latency.
- No per-frame allocation.
- Producers can overwrite stale source states.
- Canvas can drop frames when rendering cannot keep up.

Baseline layout:

```cpp
constexpr uint32_t kFusionChannelMagic = 0x4655534E; // "FUSN"
constexpr uint32_t kFusionChannelVersion = 1;
constexpr uint32_t kFusionMaxPrimitives = 512;

enum class FusionCoordinateSpace : uint32_t {
    VirtualDesktopPhysicalPixels = 1,
    MonitorLocalPhysicalPixels = 2,
};

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
    uint64_t display_topology_generation;
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
    FusionCoordinateSpace coordinate_space;
};
```

Coordinates default to virtual-desktop physical pixels. `rgba_premul` defaults to SDR sRGB premultiplied alpha; HDR/scRGB conversion is performed inside the canvas.

Producer writes:

1. Fill inactive buffer.
2. Increment sequence.
3. Atomically publish active index.
4. Signal named event.

Canvas reads each configured source channel:

1. Read active index and sequence.
2. Copy channel data into canvas-local staging memory.
3. Re-read sequence if using a lock-free double-buffer scheme.
4. Drop the channel if malformed, too old, or incompatible version.
5. Visually compose valid source primitives by TTL, confidence, priority, and user settings.

Producers never wait for canvas acknowledgement.

## Hotkey Design

Primary mechanism:

- `RegisterHotKey` in the canvas process message loop.

Defaults:

- `Ctrl+Shift+F10`: enable/disable visual fusion.
- `Ctrl+Shift+F11`: lock/unlock diagnostics/settings mode.

Fallback:

- If `RegisterHotKey` fails with `ERROR_HOTKEY_ALREADY_REGISTERED`, log the conflict and load a configured alternate hotkey.
- Low-level keyboard hooks are not part of v1. Any `SetWindowsHookEx(WH_KEYBOARD_LL)` path requires a separate ADR, default-off build flag, and binary import review.

Operational requirements:

- Register hotkeys with `MOD_NOREPEAT` where supported.
- Call `UnregisterHotKey` on shutdown and rebind.
- Log `GetLastError()` on registration failure.

Toggle requirement:

- Hotkey press to fully cleared transparent frame: p95 under 100 ms.
- Hotkey press to restored rendering from latest valid source channels: p95 under 100 ms.

## Frame Pacing

The canvas should submit at most one rendered frame per DWM composition interval.

Preferred:

- Use DirectComposition compositor timing APIs where available.
- Use `IDXGISwapChain2::GetFrameLatencyWaitableObject` to avoid submitting while too many frames are queued.

Fallback:

- Use `DwmGetCompositionTimingInfo` to estimate composition cadence.
- Use a high-resolution waitable timer.

Rules:

- Source-channel-driven invalidation, not a fixed busy render loop.
- Render on new source data, TTL/fade animation tick, display change, or diagnostics update.
- Drop stale pending overlay frames.
- Do not render faster than the monitor/DWM can present.
- Do not synchronize runtime tick rate to overlay rendering.

Telemetry:

- CPU render preparation time.
- GPU draw time using D3D11 timestamp queries.
- Present call duration.
- Frame latency wait time.
- Source-channel age p50/p95/p99.
- Dropped overlay frames.
- Active primitive count and draw-call count.
- Device removed/recreate count.

## Display Mode Compatibility Matrix

| Mode | Expected DWM Path | Canvas Visibility | Required Behavior |
|---|---|---|---|
| Windowed | DWM composed | Supported | Full operation. |
| Borderless fullscreen | DWM composed | Supported | Full operation. |
| Fullscreen Optimizations | Usually DWM-compatible | Supported if not promoted beyond DWM overlay visibility | Full operation or degraded if occluded. |
| True exclusive fullscreen | May bypass DWM composition | Unsupported/degraded | Report clearly. Do not crash. Suggest borderless mode. |
| Independent flip / DirectFlip | Game may bypass DWM composition | Degraded risk | Detect heuristically or via telemetry; report possible occlusion. |
| Multi-plane overlay promotion | Game may occupy hardware plane above desktop | Degraded risk | Report possible MPO occlusion. Do not attempt evasion. |
| Remote Desktop / capture session | DWM behavior differs | Degraded risk | Lower refresh or disable if unstable. |

The design target is excellent behavior in borderless/windowed/FSO paths and explicit, honest degraded reporting in true FSE/independent flip/MPO cases.

## Independent Flip, DirectFlip, And MPO

This is the main real-world compatibility risk.

When a game uses a flip-model swapchain that matches display requirements, DWM and the display driver may promote it to DirectFlip or a hardware multi-plane overlay. The game frame may be scanned out directly while the DWM desktop remains a separate plane. In that state, the canvas can be alive, topmost, and rendering correctly but still be visually occluded by the game plane.

Mitigation:

- Detect degraded visibility with diagnostics and optional self-test primitives.
- Integrate PresentMon/ETW-style diagnostics for development builds if practical.
- Expose a clear status: "canvas possibly occluded by fullscreen/independent flip; use borderless/windowed mode."
- Keep the default architecture no-injection/no-hook.

Do not attempt anti-cheat or anti-capture evasion to force visibility.

## Multi-Monitor, DPI, HDR, And VRR

### Multi-Monitor

Use one canvas HWND and swapchain per monitor.

Handle:

- `WM_DISPLAYCHANGE`
- monitor add/remove
- mixed origins in virtual desktop coordinates
- mixed refresh rates
- mixed DPI
- mixed SDR/HDR state

### DPI

Requirements:

- Per-monitor DPI awareness v2 before window creation.
- Handle `WM_DPICHANGED`.
- Size HWNDs in physical monitor pixels.
- Convert between physical pixels, virtual desktop coordinates, and Direct2D DIPs deliberately.

### HDR

Requirements:

- Detect HDR per output.
- Do not assume SDR markers are readable over HDR content.
- Define marker luminance policy.
- Test HDR off, HDR on, and Auto HDR.

### VRR

VRR can decouple game scanout timing from DWM composition timing, especially with independent flip.

Requirements:

- Test G-Sync/FreeSync on and off.
- Record overlay pacing relative to DWM composition, not just nominal monitor refresh.
- Do not use tearing flags by default on composition swapchains; benchmark before enabling any VRR-specific mode.

## Degradation Ladder

| State | Meaning | Behavior |
|---|---|---|
| Full operation | Canvas visible and within timing budget | Render all enabled primitives. |
| Reduced fidelity | Canvas visible but GPU/CPU pressure high | Disable nonessential animation, lower diagnostic update rate, keep critical markers. |
| Occluded/degraded | Canvas likely hidden by FSE/DirectFlip/MPO | Stop wasting render work; show tray/diagnostic status when possible. |
| Disconnected | No producer publishing source data | Keep hotkeys alive; show waiting state only when unlocked. |
| Device lost | GPU device removed/TDR/DWM reset | Recreate graphics stack. |
| Emergency off | User disables visual fusion | Clear transparent frame and suspend rendering until re-enabled. |

## Error Handling Requirements

| Failure | Response |
|---|---|
| `RegisterHotKey` conflict | Log, use configured alternate, optionally low-level keyboard fallback. |
| D3D device removed | Destroy and recreate D3D/D2D/DComp resources. |
| DComp commit failure | Recreate visual tree; enter degraded state if repeated. |
| DComp/DXGI device state changed | Check DirectComposition device state during paint/recovery paths and recreate graphics stack when required. |
| Swapchain resize failure | Destroy and recreate monitor graphics context. |
| Producer IPC malformed | Ignore channel, increment malformed counter, keep last valid or clear after TTL. |
| A producer exits | Clear that source after TTL; keep other sources alive. |
| Canvas exits | Lifecycle owner may relaunch with bounded backoff; producers keep publishing. |
| Session lock | Hide and stop rendering until unlock. |
| Display topology change | Re-enumerate monitors and recreate contexts. |

## Performance Targets

For normal marker counts on target hardware:

- Canvas render p95: under 1 ms.
- Canvas render p99: under 2 ms.
- Hotkey visual toggle p95: under 100 ms.
- IPC publish path in each producer: under 0.1 ms p95.
- Producer blocking on canvas: exactly 0 by design.
- Primitive budget: 512 primitives per source channel initially.
- Core overlay draw calls: target under 10 per frame.

Metrics must be measured independently from the native vision/TensorRT timings so overlay cost cannot hide inside vision latency.

## Testing Matrix

Minimum coverage before calling the implementation mature:

| Dimension | Required Values |
|---|---|
| Windows | Windows 10 22H2, Windows 11 23H2/24H2 where available |
| GPU | NVIDIA primary target, plus at least one AMD or Intel validation machine if available |
| Display mode | Windowed, borderless, FSO, true FSE if the game exposes it |
| Monitor topology | Single monitor, mixed DPI, mixed refresh, mixed SDR/HDR |
| DPI scale | 100%, 125%, 150%, 200% |
| HDR | Off, on, Auto HDR |
| VRR | G-Sync/FreeSync on and off |
| Competing overlays | Steam, Discord, Game Bar, ShadowPlay/OBS where installed |
| Stress | Repeated hotkey toggle, game alt-tab, monitor sleep/wake, display topology change, multi-hour run |
| Failure | runtime crash, canvas crash, GPU device removed simulation if possible |

## Implementation Units

Recommended file/module layout:

```text
native/shared_fusion/
  fusion_channel.h
  fusion_channel.cpp
  fusion_ipc.h
  fusion_ipc.cpp
  fusion_time.h

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
  graphics_telemetry.h/.cpp

native/runtime_app/
  fusion_channel_publisher.h/.cpp
  runtime_loop.cpp integration point
```

Build targets:

- `fusion_shared`
- `fusion_canvas`
- `fusion_channel_tests`
- `fusion_canvas_smoke`

Core dependencies:

- Windows SDK
- D3D11
- DXGI
- DirectComposition
- Direct2D
- DirectWrite
- optional WIL for COM/resource helpers
- optional Dear ImGui diagnostics behind a compile flag, not core rendering

## Implementation Order

The order below is not a reduced-scope MVP. It is the dependency order for building the full design without guessing.

1. Define versioned source-channel structs and tests.
2. Implement shared-memory latest-source-channel IPC and tests.
3. Implement canvas process shell, hotkey handling, and click-through behavior.
4. Implement per-monitor enumeration and per-monitor HWND management.
5. Implement D3D11/D2D/DComp renderer with mock primitives.
6. Implement frame pacing and telemetry.
7. Implement display topology, DPI, and device-lost recovery.
8. Implement runtime publisher integration.
9. Implement HDR/color policy.
10. Implement VRR/DirectFlip/MPO diagnostics.
11. Run the display-mode and stress test matrix.

## Acceptance Gate

The implementation is not complete until evidence proves:

- The canvas draws anywhere on the monitor, not just a HUD region.
- Hotkey toggle works while the game has focus.
- The canvas is absent from taskbar and Alt-Tab.
- Locked mode is click-through and does not activate the canvas.
- Locked mode preserves target focus, mouse buttons, mouse wheel, and game input.
- Runtime never waits on canvas rendering or IPC reads.
- Performance targets are met with telemetry.
- Frame pacing path either uses a swapchain created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` or explicitly reports DWM-timer fallback.
- Multi-monitor and DPI behavior are correct.
- Borderless/windowed/FSO paths are validated.
- True FSE/DirectFlip/MPO degraded behavior is detected where possible or clearly reported as not verifiably visible.
- HDR/VRR behavior is tested and documented.
- Crash and device-loss recovery work.

## References

- Microsoft DirectComposition documentation: <https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-portal>
- Microsoft composition swapchain documentation: <https://learn.microsoft.com/en-us/windows/win32/comp_swapchain/comp-swapchain-portal>
- `CreateSwapChainForComposition`: <https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition>
- DXGI flip model guidance: <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model>
- `RegisterHotKey`: <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey>
- Layered windows: <https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows>
