# DEC-2026-05-05-001: Add external yellow-cue input and sidecar fallback to the native runtime

Status: accepted
Date: 2026-05-05
Confirmed by: user
Related sessions:
- 2026-05-05T16:27:45+08:00
Related files:
- native/vision_native/include/vision_native/types.h
- native/vision_native/include/vision_native/vision_engine.h
- native/vision_native/include/vision_native/target_selector.h
- native/vision_native/src/vision_engine.cpp
- native/vision_native/src/target_selector.cpp
- native/vision_native/src/vision_native_module.cpp
- vision/native_runner.py
- vision/yellow_cue.py
- controllers/base_controller.py
- tests/test_native_vision_runner.py
- tests/test_native_vision_targeting_bridge.py
- tests/test_yellow_cue.py
- tests/test_performance_tracker.py
Supersedes: none
Superseded by: none

## Context

After accepting the priority shift toward native hotpath copy reduction, the runtime still had two practical gaps:

1. yellow cue remained useful for short continuation, but native selector often had to rediscover it by scanning image pixels itself
2. color/cue CPU work still paid too much host-copy cost because the engine downloaded more image data than necessary

The user had also clarified that a usable yellow cue signal already exists conceptually upstream: the application can identify the yellow marker above the enemy. That made it worthwhile to treat cue as an input signal rather than only as a selector-internal image side effect.

## Decision

Extend the native runtime so yellow cue can arrive through an explicit external channel, while still keeping a built-in fallback:

1. `VisionEngine` and `VisionTargetSelector` accept frame-level external cue coordinates plus confidence
2. `cue_hold` may use that external cue directly on empty-detection frames
3. `vision/native_runner.py` resolves cue sources in this precedence order:
   - explicit `cue_provider`
   - controller hook (`get_external_cue`, `get_targeting_cue`, `get_yellow_cue`)
   - built-in `ScreenCaptureCueProvider` sidecar
4. native hotpath color/cue work should prefer selector-requested ROI-only host copies instead of unconditional full-frame BGRA download
5. selector-side normal color classification and yellow-cue extraction should be merged into one CPU scan where possible

This keeps detector-led targeting intact: external cue supports ranking and continuation, not idle-state cue-only acquisition.

## Reasons

- It realizes the already-accepted direction to consume a cheap application-provided cue signal when available.
- It preserves usability even before the real upstream application cue source is wired, because the sidecar can provide a default fallback.
- It lowers host-copy overhead and makes that overhead measurable through `color_copy_ms`.
- It supports the user’s most credible yellow-cue use case: short continuation through muzzle flash or one-frame obstruction.

## Rejected Alternatives

- Wait for a full controller-in-C++ rewrite before threading cue through the runtime: rejected because the accepted priority is to exhaust current native hotpath wins first.
- Keep cue as selector-only pixel scanning with no explicit external channel: rejected because it duplicates work and blocks cheap upstream reuse.
- Let cue become an independent acquisition authority: rejected because the accepted baseline keeps cue continuation-first and detector-led.
- Require a real upstream cue provider before landing any runtime support: rejected because the built-in sidecar offers a usable default path now.

## Evidence

- Implementation added:
  - frame-level external cue fields in `DetectionBatch` / `VisionResult`
  - `VisionEngine.set_external_cue(...)`
  - pybind bridge methods that accept cue coordinates
  - `vision/yellow_cue.py` with `detect_yellow_cue(...)` and `ScreenCaptureCueProvider`
  - native runner auto-resolution of cue source with fallback logging (`explicit`, `controller`, `sidecar`, `off`)
  - `color_copy_ms` perf surfacing
- Regression fix completed during verification:
  - vertically truncated follow-up detections can now still match the same active target, preventing a one-frame stale `auto_fire` hold when the observed box leaves the fire zone
- Verification:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - `py -3 -m unittest tests.test_native_vision_runner tests.test_native_vision_targeting_bridge tests.test_performance_tracker tests.test_vision_runner tests.test_yellow_cue -v`

## Consequences

- Native runtime now has a real cue-input contract instead of only latent internal cue scanning.
- Live profiling can distinguish copy cost from inference cost more cleanly through `color_copy_ms`.
- The default runtime behavior remains robust even without an app-integrated cue source because the sidecar fallback exists.
- Future live tuning should compare sidecar cue quality and cost against a true upstream cue signal before locking the default source permanently.

## Review Triggers

- Revisit if sidecar capture adds noticeable overhead or instability in live play.
- Revisit if a true upstream application cue source becomes available and is clearly cheaper or more authoritative.
- Revisit if ROI-only color copies fail to reduce end-to-end latency meaningfully.
- Revisit if cue-source precedence needs to change for a specific deployment.
