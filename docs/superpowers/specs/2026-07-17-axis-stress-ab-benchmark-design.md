# Axis Stress A/B Benchmark Design

## Purpose

The existing partial-occlusion benchmark is intentionally conservative. Its Wrong-X and Wrong-Y cases use a `0.40` manual-input cap for `100 ms`, while target motion is mostly smooth. That is useful as a hand-feel regression baseline, but it does not expose enough separation between the pre-arbitration controller and the current controller.

This work adds deterministic stress coverage without changing the existing baseline cases or their score history. It measures whether per-axis arbitration remains useful when target motion, tracker observations, and human input are all unfavorable.

## Version boundaries

The same benchmark-only patch will be run against three controller revisions in isolated worktrees:

- `990946c`: before the retained per-axis correction work.
- `0710d11`: per-axis retention present, before inter-observation correction rise.
- `a92ad73`: current candidate with cautious confirmed-axis rise while coasting.

The benchmark source, runtime config, seed, generated input sequences, and scoring formulas must be identical for all three revisions. Historical JSON files are not substitutes for these fresh runs.

## Scenario suites

### Existing baseline

The current `partial_occlusion_combat` and `partial_occlusion_human_errors` suites remain unchanged. They continue to guard normal tracking, ADS settle behavior, output smoothness, and ordinary Wrong-X/Wrong-Y handling.

### Practical stress

This suite represents difficult but plausible combat input:

- Target truth includes two or three deterministic accelerations or reversals on both axes.
- A band-limited truth-motion component is added so the target does not merely move faster in a straight line.
- Vision observations receive deterministic `6-12 px` jitter plus occasional `12-20 px` transient offsets. Observation noise does not alter target truth.
- Wrong-axis right-stick input reaches `0.70`, lasts `180 ms`, and has short attack/release ramps.
- Cases include Wrong-X, Wrong-Y, and dual-axis error. The dual-axis case must also exercise one-axis-wrong/one-axis-helpful behavior before both axes become wrong.
- Partial occlusion and normal 100 Hz observation cadence remain present.

### Destructive stress

This suite finds the failure boundary rather than defining normal hand feel:

- Target truth contains repeated reversals and coupled X/Y motion.
- Vision observations receive deterministic `20-35 px` jitter and sparse `40-55 px` transient jumps.
- Wrong-axis right-stick input reaches `0.95`, lasts `320 ms`, and includes Wrong-X, Wrong-Y, and dual-axis error cases.
- At least one case overlaps strong manual error with partial occlusion.
- Output remains clamped through the production controller path; the harness must not bypass normal controller safeguards.

## Deterministic data model

Target truth and measured vision positions are separate values:

1. The truth trajectory updates at the 1 kHz controller tick.
2. Vision samples are produced at 100 Hz from truth plus the suite's deterministic observation noise.
3. The reticle is advanced only by `final_stick` through the existing simulated response model.
4. Manual input is generated from delayed truth error, then the selected error envelope is applied per axis.

All pseudo-random choices derive from the command-line seed. Repeating a run with the same version, config, and seed must produce byte-identical JSON.

## Benchmark structure

Stress behavior will be represented in `ScenarioCase` data rather than scattered mode checks. A case will carry its stress tier, target-motion parameters, observation-noise parameters, and per-axis manual-error envelope. Existing cases retain default zero values and therefore preserve their current behavior.

The executable will emit four named reports:

- `partial_occlusion_combat`
- `partial_occlusion_human_errors`
- `partial_occlusion_practical_stress`
- `partial_occlusion_destructive_stress`

The existing score calculation remains unchanged for the two historical reports. Stress reports use the same component formulas, but their results are reported independently and never folded into the historical overall score.

## Metrics

Each suite and case records the existing metrics plus stress-specific visibility:

- mean, P95, peak, and final radial error;
- X/Y maximum overshoot and recovery time;
- P95 output delta and output spike count;
- Wrong-X and Wrong-Y intervention frames;
- error-window mean, P95, peak, and recovery time measured only while the injected manual error is active and during its recovery tail;
- maximum observation offset, so the generated stress can be audited from the artifact;
- per-axis manual-error peak and active-frame count.

Dual-axis cases must remain individually visible in JSON rather than being represented only by the suite aggregate.

## A/B report

An A/B summary will compare:

- `0710d11 -> a92ad73`, isolating the current inter-observation correction change;
- `990946c -> a92ad73`, showing the cumulative per-axis arbitration result.

For every case, the report includes absolute values and percentage changes. It must call out improvements and regressions separately; a single overall score cannot hide a worse P95, overshoot, or smoothness result.

## Acceptance rules

The benchmark implementation is accepted when:

- existing baseline output remains unchanged before controller changes;
- all three revisions can run the same stress patch and config;
- fixed-seed duplicate runs are byte-identical;
- tests prove practical and destructive tiers actually generate stronger target/vision disturbance and stronger manual error than baseline;
- tests prove observation jitter affects submitted vision measurements but not target truth;
- JSON contains all four reports and the new audit metrics.

The current controller candidate is considered suitable for user testing only if:

- baseline ADS settle and ordinary-combat metrics remain within the existing regression envelope;
- practical-stress tracking or recovery improves materially without increasing output spikes or P95 output delta;
- helpful axes are not weakened merely because the other axis is wrong.

Destructive stress has no pass-score requirement. It is used to disclose where the controller saturates, oscillates, or stops recovering.

## Scope

This phase changes the benchmark and produces evidence. It does not tune production controller constants, add new runtime gates, or merge the candidate into `dev`. Any controller change suggested by the stress results requires a separate failing test and focused A/B run.
