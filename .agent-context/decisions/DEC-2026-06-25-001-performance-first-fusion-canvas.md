# DEC-2026-06-25-001: Performance-First Fusion Canvas Execution

Status: accepted
Date: 2026-06-25
Confirmed by: user
Related sessions:
- 2026-06-25T01:23:41+08:00
- 2026-09-01 native-only branch Fusion startup recovery
Related files:
- docs/project/AUDIO_VISUAL_FUSION_IMPLEMENTATION_PLAN.md
- docs/project/AUDIO_VISUAL_FUSION_TODO.md
- docs/project/FUSION_ENEMY_VISIBILITY_OVERLAY_V2_PLAN_20260830.md
- native/overlay_canvas/fusion_canvas.cpp
- native/overlay_canvas/fusion_overlay_contract.h
- native/overlay_canvas/fusion_overlay_contract.cpp
- native/overlay_canvas/fusion_overlay_contract_tests.cpp
- scripts/launch/gamepad_fusion_background_start.ps1
- .agent-context/research-2026-06-24-audio-direction-github-scan.md
- .agent-context/research-2026-06-24-fullscreen-canvas-fusion.md
- .agent-context/research-2026-06-24-visual-audio-fusion.md
Supersedes: none
Superseded by: none

## Context

The project is exploring native C++ visual fusion and audio-direction assistance. A multi-POV review found the three-process direction viable but not approved for full integration until performance risk is measured. The user then asked to record the decision and start execution, expecting a usable version at the end, with a possible first user-visible target of showing vision targets on a screen canvas.

## Decision

Execute a usable performance-first skeleton before building the full audio/visual fusion stack.

The first usable version should prioritize:

- A canvas/probe that can display native vision targets or all vision detections as visual markers.
- A no-op or minimal `VisionFusionChannel` publisher path that stays disabled by default and proves it does not contaminate the native runtime hot path.
- Performance measurement hooks and kill-switch behavior early enough that optimization is not deferred until after the architecture is locked.

The plan must not drift into "build complete functionality first, optimize later." Full audio capture, DSP direction, ONNX audio recognition, and gameplay-cue bridges remain later phases gated by performance and authority boundaries.

## Reasons

- The user's current hard constraint is no acceptable regression to game or native vision performance.
- Full-screen transparent canvas can affect DirectFlip, Independent Flip, MPO, DWM composition, and GPU queue behavior in ways that may not be recoverable by later draw-call optimization.
- The native C++ runtime owns the current live vision/controller/recoil hot path; any publisher must be proven to avoid waits, locks, allocations, file I/O, synchronous logging, and IPC retries in that path.
- A usable vision-target canvas is the smallest real product slice that exercises the visual fusion direction without bringing audio CPU/DSP risk into the first implementation.

## Rejected Alternatives

- Build the full audio + canvas + runtime integration first and optimize later: rejected because display-path and hot-path contamination risks may be architectural, not local optimization problems.
- Start with audio capture/DSP first: rejected for the first usable version because it does not immediately prove the visual fusion canvas and native vision target display path the user wants to see.
- Add audio-to-gameplay cue authority in v1: rejected because audio remains visual-only by default and cannot grant fire, recoil, target lock, or aim authority.
- Use injection, render hooks, drivers, game memory access, or anti-cheat evasion to draw the canvas: rejected by existing safety boundary and public-API direction.

## Evidence

- Multi-POV review on 2026-06-25 identified missing hard performance gates in the earlier plan.
- `docs/project/AUDIO_VISUAL_FUSION_IMPLEMENTATION_PLAN.md` now defines G0.5 performance non-regression gates before full integration.
- `docs/project/AUDIO_VISUAL_FUSION_TODO.md` now blocks runtime/canvas/audio integration until G0.5 passes.
- Existing handoff states the default live gamepad runtime is native C++ with CUDA preprocess, TensorRT inference, native target selector, controller, recoil, and ViGEm output.
- User explicitly asked to record this decision and begin execution, with the end state expected to be usable and able to show vision targets on the canvas.

## Consequences

- Execution should start with a bounded usable visual slice, not the entire fusion stack.
- The first implementation should keep feature flags default-off and avoid changing gameplay authority.
- Canvas/vision publisher code must include kill-switch and measurement hooks from the start.
- Audio-side work can proceed after the visual skeleton is usable and the CPU/DSP performance probes are planned or implemented.

## Review Triggers

- Any measurable regression in game frame time, native vision timing, controller/recoil/output jitter, present mode, or CPU/GPU queue behavior.
- Any request to enable fusion by default.
- Any proposal to route audio into gameplay authority.
- Any evidence that the canvas changes DirectFlip, Independent Flip, MPO, or DWM composition behavior in target display modes.
- Any implementation that requires injection, hooking, admin privileges, drivers, game-process memory access, or anti-cheat evasion.

## Implementation checkpoint — 2026-09-01

- A clean-worktree incident disproved repository absolute-path resolution as the
  Fusion startup failure: preflight, IPC, input passthrough, affinity and renderer
  initialization all passed before the Canvas exited.
- Root cause was capture-isolation lifecycle ownership. A
  `WM_DISPLAYCHANGE` or `WM_DWMCOMPOSITIONCHANGED` notification was treated as
  terminal proof loss instead of a trigger to hide and revalidate.
- The production Canvas now enters `RevalidationPending`, stays hidden, refreshes
  virtual-screen geometry, reapplies and reads back
  `WDA_EXCLUDEFROMCAPTURE`, reruns the production `DxgiRoiCapture` exclusion
  probe and resumes only after the entire proof passes. A failed, ambiguous or
  repeatedly invalidated check remains terminal fail-closed.
- The recovery probe owns a separate window procedure so its teardown cannot
  terminate the production Canvas. The launcher also uses its current
  PowerShell host and verifies Canvas liveness before publishing state.
- RED/GREEN evidence: the lifecycle regression failed before the repair and
  passed afterward. Injected display-change and DWM-composition-change messages
  each completed a real DXGI revalidation while Canvas/native stayed alive.
- Verification passed on both branches: native-only full Release build and
  `11/11` contracts; dev Fusion build, `26/26` CTest and DXGI preflight.
  Commits are `62e3604` (`codex/runtime-clean`) and `bd462f3` (`dev`).
- **Open:** the old combined log cannot establish which Windows notification
  caused the original two exits. Real MW4 display-mode and physical monitor
  topology transitions remain live acceptance checks.
