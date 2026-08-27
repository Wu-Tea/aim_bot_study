# Sustained AimLab benchmark

This benchmark measures the production `NativeGamepadController` in a deterministic
60-second closed loop. It is a scoring and regression tool, not a replacement
controller policy.

All optimization and interpretation is governed by
[`AIMLAB_OPTIMIZATION_CONTRACT_V1_20260827.md`](AIMLAB_OPTIMIZATION_CONTRACT_V1_20260827.md).
Execution `PASS` and additive score are not acceptance gates by themselves.

Each target owns a fixed 1575 ms opportunity slot followed by a 50 ms gap. ADS
has a 250–330 ms acquisition scoring deadline, a 575 ms product execution
horizon (target wait + snap + extension), and a successful entry can score at most
1000 ms of tracking. A timeout or BodyLock-entry failure idles until that same
slot ends instead of spawning an extra target; only slots that fit completely
are generated, so a 60-second run has 36 opportunities independent of outcome.
The plant runs at 1000 Hz, observations arrive at deterministic 80–100 Hz intervals,
and the virtual game's slowdown transitions from 1.0 outside the target to 0.5 at
the 24 px circle edge and 0.4 at its center.

Those constants describe the legacy synthetic fixture. They are useful for a
stable micro-benchmark, but they are not evidence that a live session had that
cadence, camera response, target geometry, or manual input.

## Log-derived runtime profile

Runtime-profile mode replaces the most important hand-written covariates with
sanitized samples from an audited native telemetry session. The extractor
rechecks each selected shard's SHA-256, byte size, parsed row count, schema,
record inventory, session identity, runtime/config/engine identity, and zero
malformed-row claim before it emits a profile.

For the retained 2026-08-25 audit, extract only the three complete shards that
were frozen by the intake:

```powershell
python tools/extract_sustained_aimlab_runtime_profile.py `
  --manifest artifacts/telemetry-audits/20260825-latest-candidate-live/behavior-manifest.json `
  --intake artifacts/telemetry-audits/20260825-latest-candidate-live/behavior-intake.json `
  --file-id telemetry-0 --file-id telemetry-1 --file-id telemetry-2 `
  --max-manual-segments 32 `
  --output artifacts/benchmarks/sustained_aimlab/runtime-profiles/20260825T101739Z-profile-physical-habits-v2.json
```

The resulting profile contains:

- paired, same-target observation delivery intervals and capture ages;
- the first eligible stable target error and body size from target/ADS epochs;
- connected, delivered, physical right-stick segments projected into
  target-relative radial and tangential coordinates, so the production input
  filter runs exactly once and a logged helpful/opposing direction remains
  meaningful when applied to another sampled target;
- a sanitized input-habit summary: active fraction, helpful/opposing/tangential
  fractions, onset and release timing, stick deltas, radial reversals, operation
  classes, and raw-to-filtered attenuation;
- a direct-observed BodyLock distribution of the response scale used by the
  logged controller, recovered from its horizontal position term and source
  80 ms X-axis horizon;
  this is explicitly an inferred controller belief, not game-camera truth;
- the PASS audit identity and source telemetry/runtime/config/engine hashes,
  without raw paths, wall-clock timestamps, or target IDs.

Run the current production controller against those covariates with every
unidentified simulator input made explicit:

```powershell
python tools/run_sustained_aimlab_runtime_profile.py `
  --profile artifacts/benchmarks/sustained_aimlab/runtime-profiles/20260825T101739Z-profile-physical-habits-v2.json `
  --config config.toml `
  --output artifacts/benchmarks/sustained_aimlab/runtime-profiles/current-controller.json `
  --duration-ms 60000 `
  --target-motion-preset seeded-legacy `
  --pov-motion-preset off `
  --controller-tick-hz 1000 `
  --sensitivity-multiplier 1.0 `
  --slowdown-edge 0.50 `
  --slowdown-center 0.40 `
  --config-relationship counterfactual
```

When the response flags are omitted, the runner uses the profile's inferred
controller-response P50 and records `plant_source=inferred`. An explicit
`--camera-response-px-per-stick-second` override remains available, but it must
also provide `--plant-source measured|inferred|assumption`; the runner never
silently changes provenance.

Use `--config-relationship matched` only when the controller config is the
logged source config, and also provide the exact context used by native runtime
provenance hashing, for example
`--config-provenance-context 'profile=;auto_fire=0;capture_fps=200'`. The runner
rehashes the supplied config bytes plus that context and rejects `matched` when
the result does not equal the profile's source config hash. A current controller
evaluated with historical covariates should normally be labelled
`counterfactual`.

The runner recomputes the profile's canonical payload SHA-256, refuses to
overwrite an existing report, builds the native target unless `--skip-build` is
given, and verifies the written report. Each report binds the source audit and
profile identities, current benchmark executable and raw config-file hashes,
config relationship, sample counts, response curve, anatomical aim-height
ratio, plant attribution, and scenario script hashes.

Profile v2 retains up to 32 manual episodes with proportional stratification
across idle/helpful/opposing/mixed behavior. Each seed traverses that library
with a seed-dependent permutation before any trace is reused. The trace retains
its ADS/BodyLock scope and delay relative to that mode's first eligible sample;
one mode never borrows another mode's habits. The retained 2026-08-25 source has
eligible BodyLock habits but no retained ADS habit trace, so v2 deliberately
keeps the ADS manual stimulus neutral instead of inventing an ADS user model. A
v1 artifact can still be decoded for historical reproducibility, but it contains
already filtered stick values and must not be used as the current human-input
model.

### Configurable runtime axes

The extracted profile is immutable evidence. Counterfactual settings are
applied when a benchmark is encoded, and every transform is recorded in the
report:

- `--sensitivity-multiplier` scales the supplied base
  camera response (profile P50 by default, explicit override when supplied) in
  the virtual camera plant. It does
  not scale or rewrite logged manual-stick values. The report records the base,
  multiplier, and effective px/(stick*s) response separately.
- `--vision-hz` scales the sequence of logged delivery intervals to a requested
  mean rate using deterministic cumulative rounding. It retains the extracted
  short/long cadence shape instead of replacing it with one ideal fixed
  interval. Omitting it replays the extracted intervals unchanged.
- `--vision-age-scale` independently scales every logged capture-to-controller
  age. `1.0` retains the extracted latency distribution.
- `--controller-tick-hz` changes only controller update cadence. The plant and
  scorer remain at 1 ms, the previous controller output is held between updates,
  and the latest Vision publication is latched until the next controller tick.
  This setting is required; the report separately retains the log-derived
  controller-sample p50/p95 interval and its p50-based rate estimate, so the
  requested fixed clock cannot be mistaken for observed runtime behavior.
  The initial implementation accepts exact integer-ms rates: positive divisors
  of 1000 such as 1000, 500, 250, 200, 125, or 100 Hz.
- `--target-slot-ms` is a fixed opportunity window (default/minimum 1575 ms).
  Runtime-profile runs reject zero/outcome-dependent replacement. Reports bind
  the slot, 1000 ms scoring window, 50 ms gap, and bounded target count; the
  verifier rejects any BodyLock-isolate entry failure before considering score.

The AI solver, target/lifecycle plan, dynamics, manual arbitration, AutoFire,
recoil, composition, and output always update together at the requested
controller cadence. Independent AI-proposal cadence is intentionally not a
benchmark option because the 2026-08-27 experiment violated same-tick authority.

The retained `20260825T101739Z-profile-output-delivered.json` evidence reports a
controller-sample interval p50 of 5 ms and p95 of 14 ms (a p50-based 200 Hz
estimate). This is descriptive source evidence, not proof that the loop was a
fixed 200 Hz clock.

For example, this is an explicitly counterfactual 120 Hz Vision / 250 Hz
controller / 1.25x sensitivity run:

```powershell
python tools/run_sustained_aimlab_runtime_profile.py `
  --profile artifacts/benchmarks/sustained_aimlab/runtime-profiles/20260825T101739Z-profile-physical-habits-v2.json `
  --config config.toml `
  --output artifacts/benchmarks/sustained_aimlab/runtime-profiles/vision120-controller250-sens125.json `
  --duration-ms 60000 `
  --target-motion-preset stationary `
  --pov-motion-preset off `
  --vision-hz 120 `
  --vision-age-scale 1.5 `
  --controller-tick-hz 250 `
  --camera-response-px-per-stick-second 500 `
  --sensitivity-multiplier 1.25 `
  --slowdown-edge 0.50 `
  --slowdown-center 0.40 `
  --plant-source assumption `
  --config-relationship counterfactual
```

Motion is also an attributed algorithmic input. Target presets are
`stationary` and `seeded-legacy`. POV presets are `off`, `seeded-strafe`,
`seeded-vertical`, and `seeded-combined`; they map to the existing deterministic
strafe/reversal and vertical-event generators. These presets are scaffolding for
later algorithms, not facts reconstructed from the telemetry.

### Evidence boundary

Runtime-profile mode is deliberately not an exact gameplay replay:

| Missing or synthetic evidence | Resulting blind spot | Required next evidence |
| --- | --- | --- |
| target, camera, detector and FOV motion are not separable | apparent target velocity cannot be attributed to world motion or POV motion | capture-time camera pose or game telemetry |
| the controller-used response scale is inferred, but true camera response and slowdown are not independently identified | the simulated plant matches the controller belief, not proven game motion | measured stick-step calibration per sensitivity/FOV |
| target and POV paths remain algorithmic presets | correlated player/target maneuvers and reaction timing are not replayed | synchronized pose/target trajectories |
| manual, target and observation samples are sanitized independently | their exact episode-level causal correlation is lost | a privacy-safe episode bundle keyed by relative time |
| only right-stick control-error-relative habits are replayed | multi-target intent, handover choice, left-stick coupling and fire/recoil habits are incomplete | synchronized input-purpose and target-candidate traces |
| controller simulation advances on deterministic millisecond boundaries | OS scheduling, USB polling and ViGEm delivery jitter are absent | physical-read/output-delivery timestamp distributions or HIL capture |
| geometry corruption, identity churn and cue availability are mostly fixtures | wrong-target, corpse, occlusion and reacquisition rates are not naturally distributed | audited selector/candidate episode profiles |
| benchmark `PASS` only checks execution and finite metrics | it is not a gameplay-quality or release gate | matched native gameplay A/B plus visible outcome review |

All algorithmic motion is labelled `algorithmic_preset`; camera plant inputs are
labelled `measured`, `inferred`, or `assumption`. The benchmark does not model
separate AI/manual threads or live transport jitter.

Do not compare two reports as a policy A/B result unless their profile hash,
plant base/multiplier/source, Vision transform, controller tick, config
relationship, target/POV presets, seeds, duration, cohorts, response curve, and
script identities are compatible. Live replay or native gameplay evidence
remains required for product acceptance.

The 2026-08-27 fixed-250 live trial was rolled back in configuration and code.
Keep manual input, authority plan, AI solve, dynamics, recoil composition, and
ViGEm publication in lockstep for the accepted runtime:

```toml
[runtime.scheduler]
controller_tick_hz = 1000
```

The live fixed-250 log had 24.9% authoritative ticks with zero requested assist,
versus 0% in the earlier lockstep session, without a measured efficiency gain.
The deterministic incident fixture isolates the mechanism: five off-cadence
selector-generation changes produce five proposal gaps; stable generation and
lockstep controls produce zero. The independent scheduler, held-proposal cache,
runtime/benchmark switches and schema-v19 cadence telemetry were removed. The
old config keys are regression-tested as unknown and inert. The hashed audit and
incident artifacts remain historical evidence for any future redesign.

The official baseline runs seeds `1337`, `20260718`, and `424242` twice: once with
pure target motion and once with deterministic mixed human mistakes. Scores are
additive: faster acquisition, longer center-weighted tracking, and smooth output
increase the result. `over_events`, `undertrack_events`, false BodyLock interruption,
false stop, error percentiles, output delta, and jerk remain explicit diagnostics.

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify/run_sustained_aimlab_baseline.ps1
```

The script refuses to overwrite an existing baseline. Pass `-Output` for a new
comparison artifact. Every JSON records the git revision, dirty state, config path
and FNV-1a config fingerprint, simulator constants, script hashes, aggregate scores,
and per-target metrics.

## ADS-to-BodyLock and directional motion stress

The cohort selects the lifecycle being measured:

- `--cohort ads` is the end-to-end path. Each new target creates a fresh LT
  epoch, ADS acquires the moving target, and the production controller
  transitions naturally into BodyLock for the remaining tracking window.
- `--cohort bodylock` is an isolated tracking path. The target is warmed to at
  most 8 px from center and remains stationary until BodyLock is confirmed.
  Acquisition is recorded only on a real same-target BodyLock entry; an entry
  timeout receives zero acquisition points, counts as a miss, and fails the
  benchmark eligibility gate.

The default `--scenario baseline` retains the original seven motion profiles.
The opt-in `--scenario compound_directional` starts in a deterministic random
360-degree direction and makes two substantial 70-150 degree turns. Each
randomized dwell interval is 80-180 ms and speed remains constant across the
turns.

Run the same directional script through both cohorts to separate acquisition
and handoff debt from pure BodyLock prediction debt:

```powershell
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort ads --scenario compound_directional `
  --output runs/native_perf/ads-bodylock-compound-directional.json

& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort bodylock --scenario compound_directional `
  --output runs/native_perf/bodylock-compound-directional.json
```

Every artifact records the selected scenario under `simulator.scenario`. Do not
compare artifacts with different scenario identities as if they were paired
policy A/B runs.

For tracker parameter sweeps, `--tracker-velocity-alpha 0..1` overrides the
production motion-velocity smoothing coefficient in the benchmark adapter only.
The selected value is recorded under `tracker.velocity_alpha_override`; omitted
means the production default was exercised.

## Counterfactual conflict analysis

Counterfactual analysis is an offline diagnostic. It replays selected fixed
scenario anchors and dynamically detected manual/AI conflicts from the beginning
of the deterministic script, substitutes a bounded mix policy, and measures the
resulting local regret and downstream correction burden. It does not add an oracle,
planner, gate, or per-tick branch to the live runtime binary.

The primary comparison is the causal oracle, which selects among the finite
candidate mixes using only observations delivered by the branch time. The
hindsight oracle sees realized future branch cost and reports theoretical
headroom; it is not a production acceptance requirement.

Quick regression:

```powershell
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --profile both --cohort both `
  --counterfactual quick `
  --output runs/native_perf/counterfactual-quick.json
```

Full baseline or parameter analysis:

```powershell
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort both --counterfactual full `
  --output runs/native_perf/counterfactual-full.json
```

Compare two artifacts with identical seeds, simulator settings, mode, schema,
candidate set, and replay budget:

```powershell
powershell -ExecutionPolicy Bypass `
  -File scripts/verify/compare_sustained_aimlab.ps1 `
  -Baseline runs/native_perf/counterfactual-before.json `
  -Candidate runs/native_perf/counterfactual-after.json
```

The main new diagnostics are:

- `regret_40/80/160_px_ms`: excess integrated error against the best eligible
  branch over each local horizon;
- `future_burden_px_ms`: downstream integrated error above the hindsight lower
  bound;
- `causal_error_area_gap_px_ms`: production error area minus causal-oracle error
  area; a negative value means production beat this deliberately simple oracle;
- `manual_helped_but_suppressed_ms` and `ai_helped_but_suppressed_ms`: attribution
  of useful isolated components lost by the production mix;
- detected, analyzed, and skipped counts: explicit replay coverage under the
  deterministic per-kind budget.

Do not optimize a single aggregate number. Reduce causal regret and future burden
while requiring non-regression in acquisition speed, tracking, braking,
interruption, overshoot, smoothness, and live feel.

## Intent fusion experiment

Use `--intent-fusion legacy|vector` to select the controller mixing path. Every
artifact records the fusion schema, mode, candidate-set version, per-candidate
tick counts, fallback/escape ticks, and mean applied manual/AI weights.

The comparison script rejects a missing or mismatched fusion identity by
default. Comparing legacy and vector policies is an explicit experiment:

```powershell
& scripts/verify/compare_sustained_aimlab.ps1 `
  -Baseline runs/native_perf/fusion-legacy.json `
  -Candidate runs/native_perf/fusion-vector.json `
  -AllowIntentFusionDifference
```
