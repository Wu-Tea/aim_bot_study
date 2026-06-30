# Fullscreen Canvas Visual Fusion Scheme

Date: 2026-06-24T23:21:48+08:00
Scope: mature Windows/C++ scheme for whole-screen visual fusion that behaves like the entire screen is a drawable canvas and can be toggled by hotkey.

Update: the formal project-level design is now `docs/project/FULLSCREEN_CANVAS_FUSION.md`. Treat that file as the authoritative scheme. This research note remains the short checkpoint that led to it.

## Product Interpretation

The desired behavior is not a small HUD window. The user-facing model is:

- the entire screen / virtual desktop is a transparent canvas;
- markers can be drawn anywhere in screen coordinates;
- the canvas is not focusable, not taskbar-visible, and normally click-through;
- one hotkey toggles the whole visual fusion layer immediately;
- the native vision runtime must not wait on rendering.

Windows still needs a compositor participant. A mature user-mode implementation cannot reliably draw directly into the final scanout buffer without either:

- a DWM/DirectComposition surface anchored by a top-level HWND;
- drawing inside the target application's swapchain;
- a driver/display-layer implementation.

Therefore, the recommended implementation is "windowless in UX, compositor-backed in implementation".

## Recommended Architecture

Use an external fullscreen canvas process:

```text
native runtime
  |
  | shared memory / named pipe / lock-free latest snapshot
  v
fusion canvas process
  |
  | DirectComposition visual tree
  | D3D11 composition swapchain
  | Direct2D / DirectWrite drawing
  v
DWM final composition over full virtual desktop
```

The canvas process owns one transparent fullscreen popup HWND per monitor, or one HWND covering the full virtual desktop. Per-monitor HWNDs are preferred for DPI, refresh-rate, HDR, and display-mode diagnostics.

## Concrete Windows Stack

### Window Shell

- `WS_POPUP`
- `WS_EX_TOPMOST`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOOLWINDOW`
- `WS_EX_TRANSPARENT` when locked / click-through
- optional `WS_EX_NOREDIRECTIONBITMAP` for DirectComposition-only content after prototype validation

Window behavior:

- no caption, no taskbar entry, no Alt-Tab entry;
- covers monitor bounds exactly;
- never takes focus;
- follows monitor add/remove, DPI, resolution, and game window movement;
- lock/unlock mode can toggle `WS_EX_TRANSPARENT`.

### Rendering

- Create D3D11 device with BGRA support.
- Create composition swapchain via `CreateSwapChainForComposition`.
- Use premultiplied alpha surface format.
- Create DirectComposition device/target for the fullscreen HWND.
- Attach swapchain visual to the root visual.
- Draw markers with Direct2D and text with DirectWrite.
- Present only when a new snapshot or animation tick requires it.

Release rendering should use a small retained scene model and batched primitives. Dear ImGui is acceptable for diagnostics/settings, not the core shipping renderer.

### Coordinate System

- Runtime publishes screen-space primitives in physical pixels:
  - direction arcs
  - target boxes
  - confidence rings
  - crosshair-relative cues
  - debug text only when enabled
- Overlay process converts physical pixels to each monitor's render target.
- Every primitive has:
  - timestamp
  - TTL
  - confidence
  - source type
  - optional fade/animation parameters

### Hotkey

Use `RegisterHotKey` in the canvas process message loop.

Default:

- `Ctrl+Shift+F10`: enable/disable visual fusion;
- `Ctrl+Shift+F11`: lock/unlock click-through/settings mode.

Toggle behavior:

- `off`: stop rendering dynamic content, clear transparent frame, keep hotkey listener alive;
- `on`: restore rendering from latest snapshot;
- do not require restarting the native runtime;
- do not block the game or vision loop.

## Why This Is The Mature Default

This gives the requested whole-screen canvas behavior while staying in normal Windows compositor rules:

- no target-process injection;
- no swapchain patching;
- no kernel/display driver;
- can draw anywhere on screen;
- can be hidden instantly;
- performance is bounded to sparse GPU drawing plus DWM composition;
- lower compatibility and safety risk than hooks.

## Paths Not Recommended As The Default

### Direct GDI / Desktop DC Drawing

Rejected. Drawing to the desktop DC is not persistent under DWM, is easily overwritten by composition, is high CPU for animation, and is not a reliable fullscreen fusion layer.

### DXGI Desktop Duplication / Windows Graphics Capture

These are capture APIs, not direct drawing APIs. They can feed vision or diagnostics, but they do not provide a low-latency way to draw into the final desktop.

### Swapchain Hook / In-Game Render Injection

This is the closest to "true fusion" because primitives are drawn into the target application's backbuffer before `Present`. It can work in exclusive fullscreen and can have excellent timing, but it requires injection/hooking and creates major stability, compatibility, and anti-cheat risk. Keep it as an internal/offline research branch only unless the target environment explicitly authorizes it.

### Driver / Hardware Overlay Plane

Not recommended. Windows DWM and the display driver choose multi-plane overlay usage; arbitrary user-mode apps do not get a simple public API to reserve a global hardware overlay plane for custom game markers. A driver path is too costly and risky for this project.

### Capture-And-Replay Compositor

Capturing the screen, drawing onto the captured image, and presenting a new fullscreen mirror would add latency, break interaction/focus semantics, fight the original game window, and consume much more GPU bandwidth. Not suitable for live aiming/vision work.

## Performance Rules

- Render at monitor refresh or lower, not at the controller tick rate.
- Use latest-state snapshots; drop stale overlay frames instead of queueing.
- Avoid CPU readback and CPU-side bitmap upload every frame.
- Keep geometry sparse and batched.
- Use Direct2D retained resources for brushes, text formats, and path geometries.
- Keep all IPC non-blocking.
- Add telemetry:
  - snapshot age
  - render time
  - present time
  - dropped overlay frames
  - active monitor/display mode
  - DWM/fullscreen compatibility state when detectable

## Integration With This Repo

Suggested module split:

- `native/overlay_canvas/`
  - fullscreen canvas process
  - Win32 hotkey/message loop
  - DirectComposition/D3D11/Direct2D renderer
  - diagnostics panel
- `native/shared_fusion/`
  - snapshot structs
  - IPC layout/versioning
  - primitive schema
- `native/runtime_app/`
  - publish latest visual/audio fusion snapshot
  - never block on overlay state

First implementation slice:

1. Mock snapshot producer.
2. Fullscreen transparent canvas over the primary monitor.
3. Hotkey toggle.
4. Draw a static direction arc and a few screen-space boxes.
5. Add transparent click-through lock mode.
6. Measure render overhead and verify display-mode compatibility.

Second slice:

1. Shared-memory or named-pipe snapshot feed from native runtime.
2. Multi-monitor/DPI support.
3. TTL/fade and confidence-based styling.
4. Diagnostics overlay toggle.

Third slice:

1. Audio direction cue primitives.
2. Vision target markers.
3. Replay/benchmark mode for visual-fusion timing.

## Acceptance Criteria

- Toggle hotkey works while the game has focus.
- Canvas can draw anywhere on the target monitor, not just in a small HUD region.
- Canvas is invisible in taskbar/Alt-Tab and does not steal focus.
- Locked mode is click-through.
- Rendering is non-blocking relative to native runtime.
- Overlay render p95 remains below 1 ms for normal marker counts on target hardware, measured separately from DWM composition.
- Snapshot age is logged.
- Works in borderless fullscreen/windowed mode.
- Legacy exclusive fullscreen behavior is explicitly reported as unsupported or degraded unless proven otherwise.

## Sources Researched

- Microsoft DirectComposition documentation.
- Microsoft composition swapchain documentation.
- Microsoft layered window and extended window style documentation.
- Microsoft `RegisterHotKey` documentation.
- Microsoft DXGI flip model / DirectFlip / multi-plane overlay documentation.
- Microsoft Desktop Duplication and Windows Graphics Capture documentation.
- Open-source references: Dear ImGui Win32/DX11 backend, Microsoft DirectComposition samples, OBS capture sources, PresentMon architecture.
