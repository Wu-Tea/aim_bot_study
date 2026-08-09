# Subagent Brief: Adaptive CUDA Submit Phase Core

Created: 2026-08-08T00:00:00+08:00
Expires: after the pure controller and focused tests are reviewed, or the adaptive-phase contract changes
Owner session: current Codex task

## Mission

Implement and verify a pure, deterministic C++ adaptive CUDA-submit-phase controller plus focused unit tests. The component must learn a useful phase online from completed Vision-frame timing observations while always retaining safe natural-submit fallback. Do not integrate it into `VisionEngine`, runtime config, perf-summary schema, installed binaries, or live config in this mission.

## Role

worker with light-skeptic responsibility for unsafe/noisy adaptation assumptions

## Required Context

- Commit `1d23037` added the current fixed `cuda_submit_phase_us` probe and per-frame measurements.
- The latest phase-0 run (`runs/perf_summary/runtime_perf_summary_1786108193277.jsonl`) showed, in denser high-load windows, roughly `135.7 Hz` active Vision, `30.4%` active accumulated-frame `>1`, `gpu_total ~= 2.52 ms`, `output_wait ~= 4.65 ms`, and `sync_queue_residual ~= 2.14 ms`.
- The user rejected a permanently fixed phase: the useful GPU gap can move with game FPS, scene load, Reflex/VRR and scheduling. A fixed target is diagnostic only.
- A phase target is relative to each frame's `source_present_steady_ns`, not wall-clock time. `0` means natural/no forced wait.
- The optimization objective must count explicit wait. Reducing `sync_queue_residual` by merely waiting longer is not a win.
- The controller will eventually be owned and called by the single Vision thread using only completed prior-frame observations; no locks or background control loop are needed.

## Relevant Decisions

- `.agent-context/decisions/DEC-2026-08-05-001-adopt-fixed-shape-cuda-graph-vision.md`
- Existing fixed-phase measurement implementation at commit `1d23037`
- Current user direction: final behavior must adapt online; fixed phases remain only for diagnosis/rollback.

## Files To Read

- `.agent-context/handoff.md`
- `docs/project/CURRENT_STATE.md` (Vision/performance sections only)
- `native/vision_native/src/vision_engine.cpp` (phase waiter and inference timing only)
- `native/vision_native/include/vision_native/vision_engine.h`
- `native/vision_native/include/vision_native/types.h` (timing fields only)
- `native/vision_native/CMakeLists.txt` (focused-test registration patterns only)
- `native/runtime_app/perf_logger.h` and `native/runtime_app/perf_logger.cpp` only to understand existing metrics; do not edit them
- `tools/compare_runtime_perf_summary.py` only to preserve metric meanings; do not edit it

## Files Not To Read Unless Needed

- Controller, selector, ADS, BodyLock, fusion, recoil and marker code
- Large `runs/native_perf` sessions, videos and benchmark artifact trees
- Historical archived session logs beyond links required above
- Installed runtime backups or model/engine binaries

## Constraints

- Preserve all existing dirty and untracked work. Do not reset, clean, checkout over, delete, stage, commit, merge or push.
- Allowed production writes are limited to new pure-component files under `native/vision_native/include/vision_native/` and `native/vision_native/src/`.
- `native/vision_native/CMakeLists.txt` may receive only the smallest focused test-target registration needed to compile/run the new component tests.
- Do not edit `vision_engine.cpp/.h`, runtime config, runtime loop, perf logger, TOML files, launchers, `.agent-context`, docs, installed `build/Release/cod_native_runtime.exe`, or any live runtime state.
- No build or install into the user runtime path. Use an isolated build directory/target already present if safe, or configure another narrowly named isolated build directory without deleting anything.
- Hot-path state must be bounded and allocation-free after construction: fixed-size candidate/statistic storage, no per-frame heap growth, no locks, no sleeps.
- Required safety behavior:
  - invalid/missing source timestamps, insufficient samples, unstable cadence, or loss of confidence returns target phase `0`;
  - natural phase `0` remains a candidate and fallback;
  - phase is bounded to `0..5000 us` and must respect the estimated source period with a guard before the next frame;
  - adaptation occurs over blocks/epochs, not noisy per-frame chasing;
  - cadence changes and material idle-to-active/load regime changes reset or re-enter exploration;
  - candidate promotion requires hysteresis/material improvement; ties prefer `0`;
  - periodic bounded neighbor re-probing is allowed, but exploration must not become permanent high-delay behavior.
- The primary cost must include `cuda_submit_wait_ms + output_wait_ms` and a bounded tail/backlog penalty. `sync_queue_residual_ms` may be diagnostic or a secondary signal, never the sole objective.
- Treat exact candidate grid, epoch length, robust statistic and weights as provisional implementation choices. Explain them and keep them centralized/testable rather than scattering magic constants.
- No claim that synthetic tests prove live GPU benefit.

## Expected Output

- A small, reviewable controller API, implementation and focused deterministic tests.
- Tests must cover at least:
  1. invalid/unstable cadence stays at `0`;
  2. stable 200 Hz cadence produces only bounded valid candidates;
  3. a synthetic nonzero optimum can be learned and held;
  4. reduced queue residual that is outweighed by explicit waiting still selects `0`;
  5. FPS/cadence change resets and relearns;
  6. idle/active regime transition does not reuse stale confidence;
  7. ties/low confidence prefer natural submit;
  8. target changes and exploration are block-bounded rather than per-frame oscillation.
- Run the new focused test repeatedly (at least 20 deterministic passes) and `git diff --check` for touched files. Report exact commands/results.
- Final response must list changed files, the API/algorithm contract, default constants with rationale, test evidence, known limitations, and integration questions for the owner task.

## Non-goals

- No VisionEngine/runtime integration.
- No runtime config or JSONL schema changes.
- No live game launch, HWiNFO logging, executable install, backup, or A/B conclusion.
- No changes to detection outputs, CUDA Graph inference, stream priority, controller behavior, W3/W4/W5, or target tracking.
- No automatic claim that one learned phase is globally optimal across games or sessions.
