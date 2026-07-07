# Vision Red-Team Stability Before Optimization

Status: proposed
Date: 2026-07-07
Confirmed by: Not yet confirmed; proposed after user asked whether this should be recorded as a decision.
Related sessions: 2026-07-07 native vision performance and GPU stability discussion
Related files:
- `native/vision_native/src/vision_engine.cpp`
- `native/runtime_app/runtime_loop.cpp`
- `tools/benchmark_vision_dataset.py`
- `runs/native_perf/native_aim_perf_20260707_194259_828.jsonl`
- `runs/native_perf/native_aim_perf_20260707_200047_003.jsonl`
Supersedes: None
Superseded by: None

## Context

The native vision path is suspected to be less mature than the controller path. Recent live logs and overlay evidence show that vision does not hold stable GPU compute over time: inference can be gated by aim state, DXGI frame updates, controller polling cadence, and GPU scheduling. The user specifically wants to find missing cases and system vulnerabilities before doing optimization work.

Existing dataset benchmarks are useful for detection quality, but they do not prove that the live native vision service can obtain stable GPU compute, recover from idle-to-active transitions, or preserve latency under game/render contention.

## Decision

Treat the next native vision work as adversarial stability validation before optimization.

First build or extend benchmark tooling that can expose vision failure modes without requiring the user to repeatedly collect new game footage or manually review live sessions. Use existing native aim performance logs and synthetic/replay inputs to measure stability, activation behavior, GPU wait, frame-update gaps, live/offline path differences, and degradation under GPU contention.

Do not start by choosing an optimization strategy such as always-on full-rate inference, keep-warm kernels, higher capture frequency, runtime threading changes, or retraining. Those are hypotheses to test after the failure profile is observable.

## Reasons

- Controller behavior has already been iterated heavily; vision has not had the same adversarial validation depth.
- Current live evidence suggests `capture_fps` is only a polling ceiling, not a guarantee of actual inference cadence.
- Idle or non-aim states can leave the model cold, so first active frames may include latency spikes.
- DXGI no-update behavior can suppress inference even when the runtime loop is ticking.
- GPU percentage alone is not a reliable health metric; short burst kernels can show low or unstable utilization while still causing unacceptable latency or gaps.
- Dataset mAP/F1 does not measure whether the live pipeline can provide fresh, timely, stable targets.

## Rejected Alternatives

- Optimize first by raising frequency or running full-rate inference all the time.
  - Rejected for now because it may hide the failure mode instead of identifying it.
- Decide on keep-warm or repeat-last-frame behavior immediately.
  - Rejected for now because these should be benchmarked as candidate fixes, not assumed as correct.
- Use only live game testing for validation.
  - Rejected because it is slow, hard to reproduce, and depends on user collection/review.
- Use only offline valid-set detection metrics.
  - Rejected because they do not cover runtime stability, activation latency, capture gaps, or GPU contention.

## Evidence

- Existing native aim perf logs show warmup/cold spikes and a gap between configured capture FPS and observed unique inference cadence.
- Current code inspection indicates vision polling is synchronous with the runtime loop and can skip work when not aiming or when capture metadata reports no updated frame.
- User observed that GPU utilization does not remain stable and asked to focus on proving where the vision module fails rather than immediately optimizing it.
- User wants a benchmark that includes active/inactive switching and GPU resource stability, with game video replay as an acceptable later input source.

## Consequences

- Next low-participation work should prioritize log analysis and synthetic stability benchmark tooling.
- The first useful output is a failure scorecard, not a runtime optimization.
- Candidate fixes should be compared using the same stability scorecard before changing live behavior.
- Dataset/training benchmarks remain useful for recognition quality, but they are not sufficient acceptance criteria for live vision performance.

## Review Triggers

- A synthetic or replay stability benchmark produces a stable failure profile and candidate fix comparison.
- Runtime architecture changes introduce an independent vision worker, keep-warm mode, repeat-last-frame policy, or GPU scheduling changes.
- New live evidence contradicts the current suspected failure modes.
- Detection model retraining becomes the primary bottleneck again after runtime stability is measured.
