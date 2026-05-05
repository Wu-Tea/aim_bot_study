# DEC-2026-05-01-006: Prioritize native hotpath copy reduction over full controller C++ rewrite

Status: accepted
Date: 2026-05-01
Confirmed by: user
Related sessions:
- 2026-05-01T21:06:29+08:00
Related files:
- native/vision_native/src/vision_engine.cpp
- native/vision_native/src/target_selector.cpp
- native/vision_native/src/vision_native_module.cpp
- native/vision_native/src/dxgi_capture.cpp
- native/vision_native/src/tensorrt_engine.cpp
- vision/native_runner.py
- vision/perf.py
- controllers/gamepad_controller.py
- controllers/mouse_controller.py
Supersedes: none
Superseded by: none

## Context

After stabilizing the reverted native baseline and adding `cue_hold`, the next question was whether moving the controller system into C++ would reduce end-to-end latency enough to justify the work.

Three focused explorer reviews examined:

1. the native-to-Python controller boundary
2. the current targeting / yellow-cue hot path after `cue_hold`
3. the broader native capture / TensorRT / selector runtime

The reviews agreed on the same core shape:

- the expensive detector path is already in C++
- the Python side still adds some thread / marshaling / wrapper overhead
- but the more immediate cost center is still native hotpath copy and sync behavior, especially full-frame host downloads and duplicated CPU ROI scans for color / cue logic

## Decision

Do not prioritize a full controller C++ rewrite as the next optimization project.

Instead, treat the next accepted optimization order as:

1. measure and reduce hidden native hotpath copy cost, especially full-frame host download for color / cue logic
2. shrink host-side color/cue work to smaller ROI-only copies where possible
3. consume an application-provided yellow cue point directly if it already exists cheaply
4. merge duplicated color / yellow-cue ROI scans
5. only then reconsider either pickup-confirm relaxation or a narrow native controller host/output migration

If controller-side native work is explored later, prefer a partial host/output transport migration before porting the full controller state machine.

## Reasons

- The current native path already performs capture, TensorRT inference, selector, and aim enhancement in C++.
- The remaining Python boundary mostly handles result marshaling, thread handoff, and OS input emission; that is real overhead, but not the clearest next bottleneck.
- The current runtime still downloads the full BGRA ROI back to host whenever detections or cue-hold color logic need CPU-side pixels, which is a larger and more obvious optimization target.
- `target_selector.cpp` still duplicates ROI scanning work across color classification and yellow-cue extraction.
- A full controller rewrite carries higher behavior-regression risk because the controller state machines are hand-feel-sensitive and already covered by a large Python-side behavior surface.

## Rejected Alternatives

- Start a full controller-in-C++ rewrite now: rejected because ROI is lower than the already-visible native hotpath copy/sync opportunities.
- Reopen the whole earlier hotpath/body-state experiment branch first: rejected because the current objective is to improve the reverted stable baseline rather than reintroduce larger behavioral scope.
- Treat yellow cue as independent acquisition authority to reduce latency: rejected because the accepted baseline keeps cue continuation-only and detector-led.

## Evidence

- `VisionEngine` currently downloads a full host-visible BGRA ROI before selector-side CPU color/cue work in `native/vision_native/src/vision_engine.cpp`.
- `VisionTargetSelector` separately scans overlapping ROI regions for color classification and yellow cue extraction in `native/vision_native/src/target_selector.cpp`.
- `vision/native_runner.py` and `vision_native_module.cpp` show that the Python boundary receives one small result per frame after the heavier native work has already completed.
- Controller host loops in `controllers/gamepad_controller.py` and `controllers/mouse_controller.py` do show Python thread wake and wrapper costs, but the review judged those as medium-ROI follow-up work rather than the first optimization cut.
- The earlier accepted decision to defer a full controller rewrite still matches the current architecture review:
  - `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`

## Consequences

- Near-term profiling and implementation work should focus on hidden copy/sync cost and cue/color ROI reduction before controller-language migration.
- Any future cue integration work should prefer consuming a precomputed application cue point over recomputing it expensively in multiple layers.
- A later native controller experiment is still allowed, but it should start with a narrow scheduler/output slice and not with a full AI-aim state-machine port.
- The project should keep treating full controller C++ rewrite as a revisit trigger, not as current default direction.

## Review Triggers

- Revisit if profiling shows Python controller wake/jitter or marshaling has become the dominant remaining latency source after hotpath copy cleanup.
- Revisit if external cue input is unavailable or too unreliable, forcing more expensive selector-side cue scanning to remain.
- Revisit if smaller ROI host copies and duplicated ROI-scan cleanup fail to produce meaningful gains.
- Revisit if a narrow native controller host/output prototype demonstrates a clear end-to-end gain without hand-feel regression.
