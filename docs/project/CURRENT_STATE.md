# Current State

**Last reviewed:** 2026-07-27
**Reviewed baseline:** `dev` at `9791a55` before this documentation-only branch
**Scope:** production runtime facts present in the reviewed repository, plus explicitly marked active directions

## Production path

The default gamepad path is the full native C++ runtime:

```text
scripts/launch/gamepad_start.bat
  -> scripts/launch/gamepad_native_cpp_start.bat
  -> native/vision_native/build/Release/cod_native_runtime.exe
```

Python remains available for fallback, training/export, recoil tooling and
debug utilities. The production capture/model contract is `480x416` with the
matching `models/best.engine`. The background VBS launchers run the same native
runtime without a console window and enforce owned-process start/stop behavior.

## Current control ownership

```text
Vision observation + physical intent
  -> intent-aware selector
  -> TargetCoordinator (identity, lifecycle and ADS-epoch owner)
  -> immutable TargetPlan
  -> ADS acquisition OR BodyLock trajectory follow
  -> AimDynamicsShaper
  -> VectorIntentFuser
  -> ADS-only brake
  -> recoil final feed-forward
  -> ViGEm delivery
```

Current boundaries:

- ADS snap is consumed once per physical ADS epoch. A new target while LT
  remains held must not restart strong snap.
- BodyLock follows tracker-owned target motion and may preserve bounded target
  inertia. It must not acquire a permanent brake path.
- The selector keeps a near committed target through short occlusion, rejects
  green friendly cues, and uses yellow enemy cues only as auxiliary evidence.
- Fresh, reliable single-target Vision may constrain a clearly wrong radial
  manual component for a short envelope. Tangential manual input remains
  user-owned; ADS and tracker-only states do not inherit that stronger rule.
- AutoFire uses a 100 ms pulse-start period with at least 30 ms pressed,
  releases on a fresh miss, and preserves physical RB/RT passthrough.
- Recoil remains the final feed-forward stage and does not consume
  selector/tracker state.

The accepted fresh-Vision constraint evidence is archived at
[Fresh-Vision Manual Counter-Correction](../archive/control-history/FRESH_VISION_MANUAL_COUNTER_CORRECTION_ACCEPTANCE_20260722.md).

## Telemetry and runtime evidence

Structured local evidence is enabled by:

```toml
[runtime.telemetry]
enabled = true
```

It writes asynchronous JSONL and a session manifest under
`runs/native_perf/<session>/`. `--perf-log` enables console-oriented timing and
Vision diagnostics; it is not the structured evidence switch.

Every comparable runtime artifact should identify:

- build revision;
- effective config or config hash;
- engine identity and `480x416` capture contract;
- telemetry schema;
- seeds/duration/scenario semantics for synthetic runs.

See [Native Log Sessions](NATIVE_LOG_SESSIONS.md) and
[Native runtime telemetry](../benchmarks/native-runtime-telemetry.md).

## Verified benchmark foundation

- Sustained AimLab provides additive acquisition/tracking scoring and separate
  ADS/BodyLock, pure/mixed and target-size cohorts.
- The Vision blind-window benchmark separates capture, result publication,
  controller and delayed-response clocks. K1 demonstrates that stale
  observation windows can create queued-motion and recovery debt, but K1 alone
  does not authorize a production policy change.
- Historical control acceptance records remain useful evidence only when their
  runtime/config/schema identity matches the candidate being compared.

Start at [Benchmark index](../benchmarks/README.md).

## Active directions

### Dynamic ROI

Dynamic ROI is the next high-priority Vision/control integration direction,
not current production behavior.

The first safe stage is bounded translation:

- no target: ROI returns to the crosshair center;
- each new physical ADS epoch: reset ROI offset;
- manual look intent may move the ROI within a bounded sniffing range;
- a reliable tracked target may support limited follow movement;
- detections must be converted from ROI-local coordinates back to crosshair
  coordinates before selector/controller use;
- no extra production inference pass.

Dynamic scaling is a later stage. It should not be combined with translation
until translation, reset and coordinate conversion have independent benchmark
coverage. Full-screen video plus synchronized runtime telemetry is needed to
label targets that the current fixed crop never observes.

### Observation degradation

Short contiguous occlusion is a useful undertracking/recovery fixture. Further
twitch investigation should isolate partial-obstruction center innovation and
intermittent publication cadence before adding another controller gate.

## Open validation

- Record synchronized video and telemetry for fixed-ROI misses, partial scope
  obstruction and weak-aim-assist twitch.
- Establish a current-revision, fingerprinted full intent-fusion acceptance
  artifact before quoting old percentage improvements as current.
- Keep small/far-target authority work on existing size/reliability evidence
  before considering another production Vision pass.
- Treat live hand feel as a required smoke test after synthetic gates pass.

## Non-regression boundaries

- Do not restore duplicated hold, lifecycle, brake, authority or output gates
  just to recover strength.
- Do not return to `left_x * constant`, independent X/Y arbitration or a weapon
  database.
- Do not let cue-only, weak-only or predicted-only targets gain fire authority.
- Do not compare artifacts across incompatible revision, config, schema or
  scenario identity.
- Do not add persistent learning, live exploration or extra Vision inference
  without separate evidence and approval.
- Benchmarks are instruments for improving runtime behavior, not product
  features to optimize for their own sake.

Historical rationale is indexed in [Archive](../archive/README.md). Reusable
methodology begins at
[Evidence-Driven Real-Time Control Optimization](EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md).
