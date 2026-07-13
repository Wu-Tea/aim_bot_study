# Native Tracker / Authority / Bodylock Refactor Design

- Date: 2026-07-13
- Status: approved direction, pending written-spec review
- Branch at design time: `dev`
- Primary runtime: `native/vision_native/build/Release/cod_native_runtime.exe`

## 1. Objective

Refactor the native target-tracking and controller-assist pipeline so that:

- tracker memory preserves target identity and motion continuity across vision frames;
- target selection remains owned by the intent-aware selector;
- assist authority remains explicit and cannot be invented by tracker projection;
- bodylock survives legitimate short evidence gaps without repeatedly resetting;
- dynamic shaping smooths only the permitted AI-assist detail;
- manual input and recoil remain immediate and independent;
- all existing controller, vision, selector, recoil, and performance metrics stay close to the accepted pre-regression baselines.

The refactor may replace the current orchestration and split large classes, but its first implementation keeps the existing capture-time CV Kalman tracker core. Replacing the filter algorithm is deferred until an architecture-only ablation proves that the remaining errors originate inside the estimator.

## 2. User Outcome

During the refactor the user must retain a runnable copy of the current application. After the refactor, bodylock must feel continuously controlled rather than alternating between short pulls, stalls, and large direction changes.

Success is determined by benchmark and runtime evidence, not by whether a particular class, filter, or state-machine design was used.

## 3. Non-Goals

The first refactor does not:

- tune ADS strength, timing, or completion parameters;
- replace the detector or model;
- change vision crop, capture cadence, or TensorRT behavior;
- make tracker-only or predicted-only targets eligible for fire authority;
- change recoil strength, timing, profile playback, or feed-forward ordering;
- smooth final output after recoil;
- expose a large new set of user-facing configuration keys;
- replace CV Kalman with alpha-beta, One Euro, IMM, or another filter before architecture ablation.

ADS and vision benchmarks remain mandatory non-regression gates even though they are not the optimization target.

## 4. Evidence and Current Failure

### 4.1 Reproduced mode chatter

`bodylock_mode_chatter_defect_100hz` deterministically reports:

- mode transitions: `29`;
- short bodylock runs: `6`;
- authority-loss overrides: `36`;
- assist delta p95: `0.900`;
- final jerk p95: `0.900`;
- maximum excess final delta: `1.164`.

The direct cause of the largest jump is that `OutputValidationPolicy` can use a tracker projection to replace stable manual input `(0.80, 0.66)` with a bounded reverse correction near `(-0.24, -0.24)` after aim authority has already been lost.

### 4.2 Current bodylock regression

The current-HEAD benchmark artifact is:

`runs/native_perf/native_gamepad_benchmark_bodylock_pre_fix_current_head_20260713.json`

Compared with the earlier `native_gamepad_benchmark_bodylock_current_20260713.json` artifact, current bodylock frames are substantially lower and dropouts are substantially higher:

| Scenario | Earlier bodylock frames | Current bodylock frames | Earlier dropout | Current dropout |
| --- | ---: | ---: | ---: | ---: |
| moving | 2573 | 2010 | 0 | 211 |
| slide visible | 2469 | 1248 | 104 | 625 |
| slide occlusion | 2570 | 1468 | 3 | 445 |
| crouch | 2367 | 1090 | 184 | 740 |
| jump | 2362 | 1292 | 84 | 791 |
| arc jump | 2506 | 1132 | 63 | 911 |

This is directional evidence that current entry arbitration and repeated controller resets amplify bodylock interruption. The saved artifacts were not produced from an identical clean checkout and therefore are comparison targets, not a substitute for the clean pre-refactor baseline required below.

### 4.3 Dynamic does not currently provide general continuity

The native dynamics stage currently:

- straightens aligned manual curves;
- guards small assist sign flips while firing;
- resets its assist history on ordinary non-firing ticks;
- runs before later controller policies that may still alter output.

It therefore cannot provide the intended 1 ms-scale bodylock entry, exit, or mode-boundary continuity.

### 4.4 Tracker responsibilities are coupled

The current pipeline allows tracker-related code to participate in all of these concerns:

- candidate observation;
- internal association and preferred-track selection;
- a second target selection after the intent-aware selector;
- projection and body-box continuation;
- target credibility hold/reacquire state;
- assist-authority reconstruction;
- observation timestamp rewriting;
- controller output validation;
- final-stick ego-motion compensation.

The CV Kalman estimator is not isolated from policy. Increasing its inertia can therefore make an incorrect target or permission decision more persistent.

## 5. Pre-Refactor Safety Gate

No refactor source change begins until all items in this section pass.

### 5.1 Preserve the dirty workspace without loss

The design-time workspace contains:

- deleted `config.native.example.toml`;
- modified `native/controller_native/body_lock_short_plan_policy_tests.cpp`;
- modified `native/controller_native/controller_behavior_tests.cpp`;
- untracked `color_readback_benchmark.json`;
- untracked `scheduler_benchmark.json`;
- untracked `telemetry_benchmark.json`.

These files must not be deleted or silently discarded. Preserve them on the dedicated safety branch `codex/pre-tracker-refactor-workspace-snapshot-20260713` with an explicit snapshot commit, verify that the branch contains every dirty path, then return to `dev` and verify `git status --short` is empty. A local stash alone is not the durable record.

The safety branch is not merged into `dev` automatically. Each preserved change is reviewed later and either incorporated intentionally or left only in the snapshot branch.

### 5.2 Freeze a rollback point

After the workspace is clean:

- record the exact `dev` commit hash;
- create the annotated tag `pre-tracker-authority-refactor-20260713`;
- run the clean baseline tests and benchmark suite;
- store baseline artifacts with the commit hash and effective-config summary.

No history rewrite, force push, destructive reset, or deletion is part of this process.

### 5.3 Produce a runnable pre-refactor release

Build the native runtime from the clean rollback commit and stage a self-contained local package under an ignored artifact location such as:

`artifacts/releases/native-runtime-pre-tracker-refactor-20260713/`

The package contains the relative layout required by the existing launcher:

- `config.toml` used for the release;
- `scripts/launch/gamepad_start.bat`;
- `scripts/launch/gamepad_native_cpp_start.bat`;
- `native/vision_native/build/Release/cod_native_runtime.exe`;
- required adjacent runtime DLLs, including TensorRT, SDL2, and ViGEm client files;
- the configured TensorRT engine and any required companion metadata that exists for that engine;
- recoil profiles, calibration, weapon identities, and current state needed by the native launcher;
- a concise `README.txt` with the exact startup entry;
- `manifest.json` containing source commit, build time, effective config, file sizes, and SHA-256 hashes.

Do not include telemetry logs, benchmark logs, source code, build intermediates, unrelated models, secrets, or credentials.

Keep both an extracted runnable directory and a `.zip` archive. The package must not rely on binaries rebuilt later in the development worktree.

### 5.4 Release acceptance

From inside the extracted release directory:

1. launcher print-only resolution exits `0` and resolves the packaged executable and config;
2. `cod_native_runtime.exe --config config.toml --once` exits `0`;
3. the configured engine loads without a legacy/Pascal compatibility requirement;
4. required recoil directories and state paths resolve inside the package;
5. manifest hashes verify;
6. the zip extracts to a different directory and repeats checks 1-5;
7. the original `dev` worktree remains clean.

If a machine-level CUDA, TensorRT, ViGEm driver, or display-capture dependency cannot be packaged, the README names it explicitly and the smoke test verifies it is available on the current machine.

## 6. Isolation and Integration Strategy

The refactor is developed on an isolated `codex/` branch and worktree created from the clean, tagged rollback commit. The `dev` worktree remains the runnable baseline until the refactor passes all acceptance gates.

The refactor branch may contain intermediate architectural commits, but each commit should build or clearly identify the intentionally incomplete boundary. `dev` is updated only after the complete benchmark scorecard passes.

The independent pre-refactor release remains untouched after creation and is the user-facing fallback throughout the refactor.

## 7. Target Architecture

```text
Vision candidates + evidence
  -> Track Memory (all eligible candidates; estimation only)
  -> Intent-aware Selector (the only selected-track owner)
  -> Assist Authority (the only assist/fire permission owner)
  -> Bodylock Lifecycle + Controller (planned AI assist)
  -> Assist Dynamics (permitted assist detail only)
  -> Recoil (final feed-forward)
  -> Final stick
  -> calibrated ego-motion observation back to Track Memory
```

### 7.1 Separate time scales

| Layer | Intended lifetime | Owns | Must not own |
| --- | --- | --- | --- |
| target identity | complete engagement | stable track identity | stick output |
| motion estimator | observation interval plus short coast | position, velocity, box, uncertainty | selection or authority |
| assist authority | evidence transition | strong, continuity, track-only, reject | geometry filtering |
| bodylock controller | controller ticks | planned correction | target identity |
| assist dynamics | nominally 1-20 ms | assist step, jerk, sign transition | manual, recoil, authority |
| recoil | weapon playback | final feed-forward correction | target/tracker feedback |

Target identity may persist across the engagement. Unobserved kinematic extrapolation may persist only for a short uncertainty-bounded coast window. Long identity memory must not imply long control authority.

## 8. Component Contracts

### 8.1 Track Memory

Track Memory consumes all eligible non-friendly candidate observations, even when no candidate currently has assist authority. It continues to use capture time, ready time, body box, aim point, class/tier evidence, and final-stick ego-motion samples.

It returns a list of `TrackEstimate` records containing at least:

- stable internal `track_id`;
- last backing detection/frame identity;
- predicted body box and aim point;
- velocity;
- position uncertainty;
- association ambiguity/quality;
- real last-observed capture time and observation age;
- lifecycle: tentative, confirmed, coasting, lost;
- observed versus projected source.

Track Memory does not return aim authority or fire authority. It does not rewrite an observed timestamp to controller query time. It does not select a target for the controller.

The initial Track Memory implementation adapts the existing `fps::TargetTracker` and CV Kalman core. The adapter must expose selected track IDs and estimator uncertainty rather than collapsing them into a controller-ready target.

### 8.2 Intent-Aware Selector

The selector is the only component that chooses the production target. It consumes:

- current vision evidence and candidates;
- Track Memory candidate estimates;
- user aim intent;
- previous selected track with bounded hysteresis;
- corpse/friendly/validity evidence.

It returns an explicit `SelectedTrackRef` containing the selected `track_id`, backing evidence identity, switch reason, and selection confidence. It may select no target.

Tracker internal preferred-track scoring may support association stability, but it cannot override `SelectedTrackRef` when producing the controller snapshot.

### 8.3 Assist Authority

Authority consumes the selected track and current evidence, then returns exactly one state:

- `observed_strong`: normal permitted bodylock assist; observed-only fire may be evaluated separately;
- `continuity`: same selected track, short evidence gap, bounded uncertainty, reduced assist, no fire;
- `track_only`: retain identity/estimate, no controller assist and no fire;
- `reject`: target is invalid, switched, stale, contradicted by user intent, or otherwise unsafe.

Only this component can grant assist or fire permission. A projection by itself can never upgrade permission.

Predicted-only, cue-only, weak-only, stale, and tracker-only states always have zero fire authority.

### 8.4 Bodylock Lifecycle

Bodylock uses explicit lifecycle states:

- `inactive`;
- `warm`: selected target is known but bodylock output is not yet permitted;
- `tracking`: normal observed/authorized bodylock;
- `coast`: continuity authority with bounded, decaying assist;
- `yield`: immediate user handoff and state release.

`warm` may prepare estimator/controller history but cannot write bodylock output. `coast` retains motion and smoothing state without increasing confidence or fire readiness. It ends on uncertainty timeout, selected-track change, invalid evidence, or strong opposing user intent.

A one-tick authority fluctuation must not destructively reset target-motion state. A real reject or user escape must not be delayed merely to keep the curve smooth.

### 8.5 Output Validation

Validation may constrain an already authorized AI-assist contribution. It may not reverse, attenuate, or replace manual input when assist authority is absent.

The invariant is:

```text
authority == none or track_only
  => permitted AI assist == 0
  => controller output before recoil preserves manual input exactly
```

Tracker information can remain available for logging and future selection while this invariant holds.

### 8.6 Assist Dynamics

Assist Dynamics consumes:

- raw manual stick;
- authorized planned bodylock assist;
- bodylock lifecycle and authority state;
- target error and uncertainty only as shaping context;
- actual controller `dt`.

It returns a shaped AI-assist delta. It does not smooth manual input, final output after recoil, or recoil itself.

The first algorithm is a bounded, asymmetric assist envelope rather than a universal EMA:

- fast attack for large, correctly directed errors;
- tighter step/jerk limits near target and across lifecycle boundaries;
- decelerate to zero before establishing opposite assist;
- immediate or fastest permitted yield under strong opposing manual input;
- no residual assist after reject;
- no accumulation of unapplied requested output.

If the dynamics A/B improves curve metrics only by degrading tracking or handoff metrics beyond the gates, it is rejected or narrowed.

### 8.7 Ego-Motion Feedback

Track Memory continues to consume the actual final stick because that is the command sent to the game. The component split remains separately logged.

The first architecture pass keeps the existing ego-motion calibration so architecture and model changes are not mixed. A later ablation may calibrate deadzone, nonlinear response, sensitivity/FOV, diagonal saturation, and actual reticle speed.

Recoil remains part of the observed final camera command but never receives target error, tracker state, or controller correction feedback.

## 9. Data and Type Migration

Introduce explicit types instead of extending the overloaded `NativeControllerVisionState` indefinitely:

- `TrackObservationBatch` for all eligible candidate observations;
- `TrackEstimate` for estimator-only outputs;
- `SelectedTrackRef` for selector ownership;
- `AssistAuthorityDecision` for permission and reason;
- `BodylockLifecycleState` for controller continuity;
- `AssistDynamicsInput/Output` for micro-shaping.

During migration, adapters may populate the old state for unchanged consumers, but new components must use the explicit types. The compatibility adapter is removed when all native controller/runtime call sites have migrated.

`TargetSnapshotProvider` is reduced or split so it no longer contains target selection, candidate verification, tracker projection, authority reconstruction, and controller snapshot adaptation in one class.

## 10. Observability

The runtime and benchmark traces must distinguish:

- selected production track ID;
- tracker estimate ID and backing detection/frame;
- last true observation time and projection age;
- uncertainty and ambiguity;
- authority state and reason;
- bodylock lifecycle and transition reason;
- planned assist, dynamics-applied assist, manual, before-recoil, recoil, and final output;
- target switch, user yield, coast entry, coast expiry, and reject events.

Diagnostic geometry IDs remain diagnostic and must not silently become production target identity.

## 11. Migration Sequence

### Phase 0: Safety and baseline

- preserve dirty work on a safety branch;
- restore clean `dev`;
- tag rollback commit;
- build, package, and smoke-test the pre-refactor release;
- record clean benchmark artifacts.

### Phase 1: Estimator-only contract

- add explicit estimator types;
- adapt the current CV Kalman tracker to return per-track estimates and IDs;
- ingest eligible candidates independently of assist permission;
- run the new path in shadow with zero live output effect.

### Phase 2: Single selected-track owner

- bind production selection to the intent-aware selector;
- query the estimator by explicit selected track ID;
- remove tracker self-selection from the controller-facing path;
- benchmark crossing targets, intent switching, corpse/cue loss, and reacquisition.

### Phase 3: Explicit authority

- implement observed/continuity/track-only/reject decisions;
- stop projection from refreshing observation time or rebuilding aim authority;
- remove duplicate candidate/output hold state as its replacement becomes active;
- fix no-authority validation so manual remains exact.

### Phase 4: Bodylock lifecycle

- introduce warm/tracking/coast/yield;
- preserve state across legitimate short gaps;
- reset on real target switch, reject, timeout, or user escape;
- run mode-chatter and moving/occlusion regressions.

### Phase 5: Detail dynamics

- make dynamics maintain bodylock assist continuity on ordinary ticks;
- place shaping after authorized bodylock policy output and before recoil;
- add adaptive step, jerk, release, and sign-transition behavior;
- reject tuning that causes tracking lag beyond acceptance.

### Phase 6: Algorithm ablation

Only after phases 1-5 pass:

- calibrate ego-motion response;
- expose and test the tracker parameters that currently do not affect `fps_reference`;
- compare process/measurement noise, coast decay, and association thresholds;
- compare a different motion estimator only if CV Kalman remains the measured bottleneck.

## 12. Benchmark and Test Strategy

### 12.1 Architecture ablation

Record each stage separately. The clean current commit is the correctness baseline. `runs/native_perf/native_gamepad_benchmark_bodylock_current_20260713.json` is the positive bodylock continuity target for the scenarios it contains. A refactor must preserve current correctness behavior while restoring the earlier continuity target; it may not select whichever artifact is easier for each individual metric.

The stages are:

- A0: clean current pipeline;
- A1: existing Kalman with estimator-only output and explicit selected track;
- A2: A1 plus explicit authority and real observation age;
- A3: A2 plus bodylock lifecycle;
- A4: A3 plus assist dynamics;
- A5: A4 plus tracker/ego-motion algorithm tuning.

Do not combine A1-A5 into one opaque result. If a stage regresses required metrics, diagnose or revert it before continuing.

### 12.2 Required scenarios

- `bodylock_continuity_defect_100hz`;
- `bodylock_mode_chatter_defect_100hz`;
- moving bodylock;
- visible slide;
- slide with occlusion;
- crouch cycle;
- jump;
- arc jump;
- manual escape and target crossing;
- multi-target crossing and user-intent switch;
- wrong target and err-target recovery;
- cue loss, suspected corpse, weak-only, and projected-only states;
- random-FOV tracker scenarios;
- recoil contract and runtime pipeline contract;
- vision/selector benchmarks as non-regression.

The mode-chatter benchmark must exercise the production controller path when dynamics is evaluated. A lower-level reproduction may remain for pinpointing validation and authority defects, but it cannot alone prove end-to-end curve continuity.

### 12.3 Core acceptance

For the mode-chatter reproduction:

- authority-loss manual override events: `0`;
- maximum excess final delta: `<= 0.15`;
- assist delta p95: `<= 0.10`;
- final jerk p95: `<= 0.15`;
- strong manual input is never reversed;
- same-track short evidence gaps do not produce repeated 1-3 ms bodylock runs.

For sustained manual correction:

- longest opposing assist: `<= 8 ms`;
- manual gain plateau: `<= 8 ms`;
- manual direction preservation: `>= 0.90`;
- longest continuously reversed final output: `<= 20 ms`;
- manual takeover latency: `<= 60 ms`;
- first-frame strong user escape keeps the manual direction.

For bodylock tracking:

- positive moving/slide/occlusion/crouch/jump/arc scenarios retain at least 95% of the bodylock frames in the pre-regression continuity target;
- dropout does not increase by more than one percentage point versus the selected baseline;
- sustain does not decrease by more than one percentage point;
- target-error p95, final error, and max overshoot do not regress by more than 2-5%, depending on scenario noise;
- moving, slide, occlusion, crouch, jump, and arc do not trade continuity for false smoothness;
- p95 bodylock output delta and turn smoothness improve materially over the current-HEAD artifact.

For identity and authority:

- wrong-target duration and ID switches do not increase;
- user-fight, invalid-strong, stale-high, and authority-without-fire counters do not increase;
- cue-only, weak-only, predicted-only, stale, and tracker-only fire violations remain exactly zero;
- a tracker estimate cannot become strong authority without current qualifying evidence.

For performance and pipeline safety:

- controller/runtime focused tests exit `0`;
- `native_pipeline_contract.bat` exits `0`;
- tracker/controller p99 processing cost does not regress by more than 5% or `0.010 ms`, whichever allowance is larger;
- recoil output and ordering remain unchanged;
- vision results remain unchanged for deterministic fixtures;
- `git diff --check` has no new errors.

## 13. Algorithm Decision Gate

The CV Kalman core remains when A1-A4 pass and the remaining estimator error is within the accepted tracking gates.

Algorithm optimization is justified only when:

- selected-track identity and authority are already correct;
- bodylock lifecycle no longer chatters;
- dynamic is not masking estimator error;
- recorded prediction error, innovation, or ego residual proves the estimator is the remaining bottleneck.

A replacement estimator is accepted only if it beats the existing CV Kalman on the full scenario set without increasing wrong-lock, user-fight, authority, or performance failures.

## 14. Configuration

Do not expose architecture internals as a large set of compact-config keys. The normal example config remains concise.

Advanced experimental tuning may be available internally or in a non-default advanced section during A/B, but the accepted implementation should expose only parameters that benchmarks demonstrate are meaningful and stable. Parameters ignored by the active backend must be removed, wired correctly, or reported as inactive.

## 15. Rollback

Rollback remains possible at every level:

- the pre-refactor tag identifies the exact source baseline;
- the independent release package remains runnable;
- the safety branch preserves all pre-existing dirty work;
- the refactor occurs in an isolated branch/worktree;
- architecture phases are separate commits;
- `dev` is not updated until the final scorecard passes;
- a failed dynamics or tracker-algorithm phase can be dropped without removing the earlier authority/bodylock fixes.

No database, cloud service, production deployment, or irreversible migration is involved.

## 16. Completion Criteria

The refactor is complete only when:

1. the pre-refactor release package remains independently runnable;
2. the refactor worktree is clean;
3. all focused tests and pipeline contracts pass;
4. mode-chatter and continuous-input acceptance gates pass;
5. bodylock tracking metrics remain close to the selected pre-regression baseline;
6. selector, authority, vision, ADS, recoil, and performance non-regression gates pass;
7. telemetry demonstrates that tracker projection, selection, authority, bodylock lifecycle, dynamics, and recoil have separate ownership;
8. the resulting commits can be rolled back independently;
9. only then is the accepted result synchronized to `dev`.
