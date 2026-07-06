# 2026-06-25 Fusion Canvas Archive

This archive preserves the detailed June 25 fusion/canvas notes that were compacted out of `session-log.md` on 2026-07-06.

## Performance-First Fusion Canvas Decision

- User asked to record the decision and begin execution toward a usable version.
- Decision recorded: `decisions/DEC-2026-06-25-001-performance-first-fusion-canvas.md`.
- Direction: do not build full audio+visual fusion first and optimize later; build a usable vision-target canvas/publisher skeleton with performance and kill-switch boundaries from the start.
- First usable target: show native vision target or all vision detections on a full-screen canvas while preserving native runtime hot-path isolation and keeping audio visual-only for later phases.

## Fusion Canvas Usability Correction

- User rejected the first overlay-marker result as unusable: too many markers and positions were not tied to the actual game view.
- Root cause found in code and confirmed against 1920x1080 capture video: the canvas stretched normalized vision/capture coordinates across the whole screen, magnifying offsets and making markers visually unrelated to the target.
- Corrected default mode to target-dot only:
  - `FUSION_SHOW_ALL_DETECTIONS` defaults to `0` in the launcher and runtime config.
  - `FusionChannelPublisher` respects `show_all_` and publishes zero detections unless explicitly enabled.
  - `fusion_canvas` no longer draws body box or bottom status text in default target mode.
  - Selected target marker renders as a small circle using screen center plus normalized `dx/dy` restored to capture pixels.
- Verification during that work:
  - `fusion_canvas` Release build passed.
  - `cod_native_runtime` Release build passed after stopping an existing locked runtime process.
  - `native\vision_native\build\Release\cod_native_controller_tests.exe` passed.
  - `git diff --check` only reported existing LF/CRLF warnings.

## Idle Behavior And Capture Direction

- User accepted that target mapping was roughly correct, then requested:
  - Consider recognizing only the target process/game picture rather than generic desktop screenshot capture.
  - When vision data is unavailable, canvas should either show a center crosshair or disappear.
- Capture architecture review:
  - Native vision uses DXGI Desktop Duplication in `native\vision_native\src\dxgi_capture.cpp`.
  - It already copies only a center ROI texture, not a full GDI screenshot.
  - The next process-specific step should be window/output-aware ROI alignment: find target process HWND/window rect, map it to the selected DXGI output, and center/crop within that game rect.
  - Avoid swapchain injection/Present hooking as the first step due to higher risk for game stability, anti-cheat compatibility, and performance isolation.
- Implemented canvas idle mode:
  - `fusion_canvas` treats data as stale after 250ms without a new frame and clears previous target instead of holding a stale dot.
  - Default idle behavior is `hide`.
  - Optional center crosshair mode is available with `FUSION_IDLE_MODE=crosshair` or `--idle-mode crosshair`.
  - `scripts\launch\gamepad_fusion_canvas_start.bat` defaults `FUSION_IDLE_MODE=hide` and passes `--idle-mode`.
- Verification:
  - `fusion_canvas` Release build passed.
  - Print-only launcher check showed `--idle-mode hide`.
  - `git diff --check` only reported existing LF/CRLF warnings.

## Prevent Canvas Feedback Into Vision Capture

- User clarified that the target-process capture idea is mainly about avoiding canvas interference with vision.
- Risk model:
  - DXGI Desktop Duplication can observe the composed desktop rather than a raw game-only frame.
  - If the overlay is visible to that capture path, the marker/crosshair may feed back into the next vision frame.
- Implemented first-line isolation:
  - `fusion_canvas` calls `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` on its top-level overlay window.
  - Successful or failed display-affinity setup is logged in the canvas log.
  - This keeps the lower-risk current DirectComposition overlay path while trying to exclude the overlay from Windows capture APIs.
- Verification:
  - `fusion_canvas` Release build passed.
  - `git diff --check` only reported existing LF/CRLF warnings.
- Follow-up if interference remains:
  - Check `runs\fusion_canvas\fusion_canvas.log` for `display_affinity=exclude_from_capture`.
  - If unsupported or ineffective, implement process-window-aware DXGI ROI alignment next.
