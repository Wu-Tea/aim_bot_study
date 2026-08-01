# Current State

**Last reviewed:** 2026-08-01
**Reviewed baseline:** current `dev` source plus the accepted contextual
manual/AI arbitration recorded in
[DEC-2026-08-01-002](../../.agent-context/decisions/DEC-2026-08-01-002-contextual-manual-ai-dual-proposal-arbitration.md)
**Scope:** production runtime facts present in the reviewed repository, plus explicitly marked active directions

## Production path

The default gamepad path is the full native C++ runtime:

```text
scripts/launch/gamepad_start.bat
  -> scripts/launch/gamepad_native_cpp_start.bat
  -> native/vision_native/build/Release/cod_native_runtime.exe
```

Python remains available for fallback, training/export, recoil tooling and
debug utilities. The current production contract is `640x512` capture,
isotropic resize to `480x384` and
`models/best_480x384.engine`. The background VBS launchers run the same native
runtime without a console window and enforce owned-process start/stop behavior.

The installed validation runtime is
`native/vision_native/build/Release/cod_native_runtime.exe`, SHA-256
`DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590`.
It includes the four-situation lifecycle fixes and contextual dual-proposal
manual/AI arbitration.

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
- The ADS ownership ceiling is currently `220 ms` from the physical LT epoch.
  A target that appears late in the epoch receives only the remaining
  acquisition time before BodyLock handoff.
- BodyLock follows tracker-owned target motion and may preserve bounded target
  inertia. It must not acquire a permanent brake path.
- The selector keeps a near committed target through short occlusion, rejects
  green friendly cues, and uses yellow enemy cues only as auxiliary evidence.
- Fresh firing position remains authoritative; firing innovation limits apply
  to velocity admission, not to the current observed position.
- Identity hold and coasting actuation have separate lifetimes. A short
  no-observation gap receives a 12.5 ms grace, then AI authority retires by
  65 ms while target association may remain alive longer.
- Manual and shaped AI enter `VectorIntentFuser` as absolute proposals. Strong
  same-direction ADS and near-BodyLock input use contextual AI-priority
  arbitration with 20% normalized manual headroom. Opposing/tangential input,
  full manual escape and far BodyLock retain their explicit ownership
  boundaries.
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

The latest long bot session is
`runs/native_perf/sessions/20260801T131000Z_6544_1/`. It covers 25.51 minutes,
uses config hash `db80e13a...df79df8` and engine hash `45fc5627...deB21`, and
is analyzed in
[ADS Long-Session Diagnosis](ADS_LONG_SESSION_DIAGNOSIS_20260801.md).
Its manifest does not contain the executable hash and retains a stale
`git_commit` value, so installation-chain evidence is still required.

Every comparable runtime artifact should identify:

- build revision;
- effective config or config hash;
- engine identity and `640x512 -> 480x384` capture/tensor contract;
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

### Late-target ADS handoff

The current residual is occasional slight ADS overshoot or undertracking after
extended moving play. Read-only telemetry does not support monotonic learning
drift as the primary cause. The stronger explanation is a target arriving or
crossing center near the physical-epoch `220 ms` ceiling.

No production policy change is accepted yet. The next safe stage is:

- log physical ADS epoch time, current target-segment time and handoff reason;
- log position versus motion/feed-forward contribution and production response
  scale/confidence;
- reproduce late target arrival, near-ceiling center crossing and a stationary
  control in deterministic fixtures;
- evaluate only a bounded continuation or motion-aware handoff rule that does
  not rearm strong ADS within a held LT epoch.

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
- Validate late-target and center-cross handoffs before attributing the current
  residual to response learning.
- Add executable SHA-256 and production response estimator scale/confidence to
  future session identity/telemetry.
- Keep small/far-target authority work on existing size/reliability evidence
  before considering another production Vision pass.
- Treat live hand feel as a required smoke test after synthetic gates pass.

## Non-regression boundaries

- Do not restore duplicated hold, lifecycle, brake, authority or output gates
  just to recover strength.
- Do not return to `left_x * constant`, independent X/Y arbitration or a weapon
  database.
- Do not globally shrink manual input to improve benchmark scores; contextual
  normalization belongs inside the single fuser owner.
- Do not reset ADS acquisition for a late target while LT remains held.
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
