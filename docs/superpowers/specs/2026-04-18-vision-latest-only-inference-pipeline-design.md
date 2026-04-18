# Vision Latest-Only Inference Pipeline Design

**Goal:** Reduce end-to-end vision latency in the gamepad runtime by replacing the current single-threaded capture-plus-infer loop with a latest-only two-thread pipeline, while keeping target selection and controller behavior unchanged.

**Scope:**
- Optimize the `vision` capture and object-recognition path for lower latency.
- Preserve the existing `frame + detections -> _resolve_tracking_frame()` contract.
- Preserve existing `TargetSelector`, `AimEnhancementPipeline`, `CrosshairPersonHitDetector`, and controller behavior.
- Prefer newest data over full frame coverage when the system is overloaded.
- Keep model lifetime warm across ADS sessions to avoid cold-start penalties.

**Non-goals:**
- No changes to `vision/targeting.py` target-selection behavior.
- No changes to `vision/enhancement.py`.
- No changes to any controller or gamepad logic.
- No detector retraining, model replacement, or inference-quality tuning.
- No attempt to process every captured frame in order.

## Problem

The current `vision` runtime is latency-limited by a strictly serial main loop:

1. wait for a new frame
2. run inference
3. run post-processing and downstream handoff

This structure couples screenshot timing, model execution, and target-resolution work into one blocking path. Even if throughput is acceptable, the data that reaches the controller is older than necessary because the pipeline does not overlap capture and inference work.

For the current user goal, lower end-to-end latency matters more than absolute throughput. The user also explicitly accepts dropping stale frames under load, which means the runtime should be optimized for "freshest possible result" rather than "every frame processed."

## Approaches Considered

### 1. Keep the single-thread loop and only micro-optimize inference copies

Continue using the current `process_vision()` structure and reduce per-frame overhead inside `_fast_predict()`.

Pros:
- smallest code change
- easier to validate

Cons:
- does not remove the main serial latency cost
- capture, inference, and downstream processing still block one another
- limited benefit for the user's latency-first goal

### 2. Add a latest-only inference thread fed by the existing capture thread

Keep `ScreenCaptureThread` as the capture producer, add a dedicated `InferenceThread` that consumes the latest available frame and publishes the latest available detection result, and leave the rest of the vision flow unchanged.

Pros:
- attacks the real latency problem with minimal architecture change
- preserves downstream contracts and behavior
- matches the explicit requirement to drop stale frames
- avoids cold-start risk if inference resources stay warm

Cons:
- requires careful synchronization, pause/resume semantics, and shutdown handling
- adds concurrency-focused tests

### 3. Build a deeper multi-stage pipeline with separate pre-process, infer, and consume stages

Split capture, preprocessing, inference, and result consumption into multiple threaded stages.

Pros:
- highest theoretical optimization headroom

Cons:
- significantly more complexity
- harder to reason about freshness guarantees
- over-scoped for the current request, which explicitly limits changes to screenshot plus recognition flow

## Chosen Design

Choose **Approach 2**.

The optimized runtime will use a latest-only two-thread front-half pipeline:

1. `ScreenCaptureThread` continues to own screenshot capture and always stores only the newest frame.
2. A new `InferenceThread` consumes only the newest unseen captured frame, runs object recognition, and always stores only the newest inference result.
3. `process_vision()` stops performing inference directly. Instead, it waits for the latest inference result and passes `frame + detections` into the existing downstream path.
4. If the system is overloaded, older frames are skipped automatically. This is expected and desired because freshness is more important than completeness.
5. `InferenceThread` remains alive for the process lifetime and supports pause/resume rather than repeated teardown and recreation. This avoids repeated model cold starts across ADS sessions.

The main behavioral invariant is:

> `TargetSelector`, `AimEnhancementPipeline`, autofire gating, and controller handoff must continue to receive the same shape of inputs as before, only with fresher timing.

## Data Flow

### Current flow

The current runtime effectively does:

1. get latest frame from `ScreenCaptureThread`
2. run `_fast_predict(...)` or `model.predict(...)`
3. call `_resolve_tracking_frame(frame, detections, ...)`
4. hand off `best_target_delta` and `auto_fire_active` to the controller

### New flow

The new runtime will do:

1. `ScreenCaptureThread` captures frames and publishes `CapturedFrame`.
2. `InferenceThread` waits for a newer `CapturedFrame` than the one it last processed.
3. `InferenceThread` runs recognition on that frame and publishes `InferenceResult`.
4. `process_vision()` waits for a newer `InferenceResult` than the one it last consumed.
5. `process_vision()` calls `_resolve_tracking_frame(frame=result.frame, detections=result.detections, ...)`.
6. Controller and debug/perf logic consume the downstream outputs exactly as before.

This keeps all downstream behavior anchored on real `frame + detections` pairs while removing inference work from the main loop.

## Latest-Only Semantics

Latest-only behavior is a requirement, not an accident.

### Capture side

`ScreenCaptureThread` should continue to expose only the newest captured frame. It should not accumulate a backlog.

### Inference side

`InferenceThread` must not queue multiple frames for future work. It should:

- remember the last `frame_id` it processed
- ask `ScreenCaptureThread` for a frame newer than that id
- process that newest frame
- publish only the newest result

If several new frames appear while inference is busy, the older unprocessed frames are intentionally skipped.

### Main-thread consumption

`process_vision()` must consume only inference results newer than the last consumed `result_id` or `frame_id`. If the main thread falls behind, it should also skip stale intermediate results rather than replaying them.

## Thread Lifecycle and ADS Behavior

The design must avoid inference cold starts and stale-result leakage across ADS sessions.

### Startup

At process startup:

1. load the model
2. warm it up
3. initialize fast path if available
4. start `ScreenCaptureThread`
5. start `InferenceThread`

`InferenceThread` should receive already-initialized model resources rather than loading its own model.

### ADS inactive

When ADS is not held:

- `ScreenCaptureThread` remains active
- `InferenceThread` stays alive but paused
- the latest published inference result is cleared
- the main thread does not reuse any pre-pause result on the next ADS session

### ADS active

When ADS becomes active:

- `InferenceThread` resumes without recreating model resources
- the main thread resets its last-consumed inference marker
- inference resumes from the newest available captured frame

### Shutdown

On process exit:

1. stop `InferenceThread`
2. wake any waits
3. join `InferenceThread`
4. stop `ScreenCaptureThread`
5. join `ScreenCaptureThread`
6. keep the current controller reset/stop cleanup order

## Data Structures

The runtime needs two explicit transport objects.

### `CapturedFrame`

Fields:
- `frame_id: int`
- `captured_at: float`
- `frame: np.ndarray`

Purpose:
- make frame freshness explicit
- support "newer than last seen" semantics
- let downstream code reason about result age

### `InferenceResult`

Fields:
- `frame_id: int`
- `captured_at: float`
- `inferred_at: float`
- `frame: np.ndarray`
- `detections: list[ParsedDetections]`
- `infer_ms: float`

Purpose:
- preserve the downstream `frame + detections` contract
- make inference timing explicit
- support future latency observability via frame age

## Performance and Observability

The performance output should remain useful for latency-first tuning.

### Preserve

- `infer_ms`: real object-recognition runtime for the consumed result
- `post_ms`: main-thread downstream processing time after result consumption

### Redefine

The current `wait` field should be interpreted as waiting for a new inference result rather than waiting directly on capture.

### Add

Add `age_ms` to measure freshness:

```text
age_ms = consume_timestamp - captured_at
```

This is the most direct indicator that the pipeline is delivering newer data to the controller. For this design, `age_ms` is more important than raw loop FPS.

## File-Level Design

### `vision/capture.py`

Responsibilities:
- keep screenshot capture isolated
- publish `CapturedFrame` metadata with `frame_id` and `captured_at`
- preserve latest-only semantics

Changes:
- replace the raw `_latest_frame` payload with a `CapturedFrame`
- keep the existing "newer than `last_seen_id`" access pattern
- keep capture ownership and cleanup responsibilities unchanged

### `vision/inference.py`

New file.

Responsibilities:
- define `InferenceResult`
- implement `InferenceThread`
- consume `CapturedFrame` from `ScreenCaptureThread`
- run recognition using already-created model resources and fast path objects
- publish only the newest result
- expose pause/resume/stop methods and "newer than last seen" retrieval

Must not:
- contain target-selection logic
- contain controller logic
- change detection output schema

### `vision/runner.py`

Responsibilities after redesign:
- own startup and shutdown wiring
- create model resources once
- create and control `ScreenCaptureThread` and `InferenceThread`
- consume latest inference results
- keep downstream `_resolve_tracking_frame()` behavior unchanged

Changes:
- remove direct per-frame inference from the main loop
- replace it with latest-result consumption
- add ADS-boundary pause/resume logic for `InferenceThread`
- reset result-consumption markers when ADS sessions change

### `vision/perf.py`

Responsibilities:
- continue logging runtime timings
- report result freshness metrics needed for latency validation

Changes:
- source `infer_ms` from `InferenceResult`
- source `wait` from result-wait time
- extend log output with `age_ms`

No change should alter how `TRACK` is defined. It should remain based on whether the resolved frame has a selected target.

## Error Handling

- If capture produces no newer frame within timeout, inference should continue waiting rather than emitting a synthetic stale result.
- If inference produces an empty `detections` list, downstream behavior remains unchanged and still flows through `_resolve_tracking_frame(...)`.
- If paused, `InferenceThread` must not publish new results.
- If resumed after pause, previously published results from the old ADS session must not be reused.
- Shutdown must wake blocked waits cleanly so the process can exit without hanging.

## Testing

Add or update tests to prove the concurrency semantics without changing downstream behavior.

### `tests/test_vision_inference.py`

Cover:

1. `InferenceThread` publishes a new result when a newer captured frame appears.
2. `InferenceThread` does not backlog older frames when multiple frames arrive quickly.
3. pause prevents publication of new results.
4. resume continues using the already-initialized thread/model path.
5. stop wakes waits and exits cleanly.
6. `get_latest_result(last_seen_id=...)` only returns newer results.

### `tests/test_vision_runner.py`

Cover:

1. the main loop consumes inference results instead of running inference inline
2. ADS exit clears stale result state
3. ADS re-entry does not reuse old-session results
4. `_resolve_tracking_frame()` still receives the same `frame + detections` shapes

### `tests/test_vision_runner_config.py`

Only update if any config-facing default or new runtime flag becomes necessary. The preferred design is to avoid introducing new user-facing configuration for this optimization pass.

## Validation Criteria

The optimization is successful only if all of the following are true:

1. **Behavioral stability**
   - no change to target-selection behavior
   - no change to controller handoff behavior
   - existing vision tests continue to pass

2. **Lower freshness age**
   - measured `age_ms` decreases in the real `gamepad_start.bat` runtime path

3. **No ADS-session leakage**
   - rapid ADS on/off transitions do not replay stale results

4. **Stable shutdown**
   - both threads stop and join cleanly on exit

5. **No inference cold-start regression across ADS sessions**
   - inference resources are kept warm and reused across pause/resume boundaries

## Rollout

Phase 1:
- add `CapturedFrame`
- add `InferenceThread`
- switch `runner.py` to latest-result consumption
- extend perf logging with `age_ms` while preserving existing timing fields

Phase 2:
- extend perf output with `age_ms`
- compare latency behavior in the real `gamepad_start.bat` path

Deferred:
- deeper inference micro-optimizations inside `_fast_predict()`
- larger multi-stage pipelines
- targeting/controller changes
