# 2026-07-26 Next Benchmark Scenarios

Status: user-requested scenarios recorded; no implementation started.

## Scope rule

Keep these scenarios separate from the first horizontal causal ego-motion
Shadow Oracle Gate. They exercise different failure sources and should become
independent benchmark cohorts with shared controller/runtime identity and fixed
seeds. Do not solve them by adding another output gate or by globally reducing
assist strength.

## V1: Large vertical acquisition with cooperative AI and manual input

### Reproduction

- Spawn a valid target 200-300 px above the reticle.
- Give AI a strong upward acquisition request.
- Give the simulated user a meaningful upward input in the same direction.
- Include input variants: user releases near the target, user reacts late, and
  user continues the original upward command after center crossing.
- Include ADS acquisition and the ADS-to-BodyLock handoff.
- Retain the normal slowdown ring and controller saturation/output shaping.

### Defect to expose

AI and user are initially both correct, but their combined vertical command can
accumulate too much closing velocity. If the fusion/ADS stopping prediction
does not account for the combined delivered response, the reticle crosses the
target and continues upward, producing a large skyward excursion and recovery
debt.

### Required metrics

- acquisition time and acquisition points;
- maximum post-cross vertical error;
- vertical overshoot area;
- obsolete upward push duration after crossing;
- peak delivered vertical stick and saturation duration;
- correction reversal count and recovery time;
- ADS-to-BodyLock handoff residual;
- missed acquisition / target-circle exit;
- p95 output delta and jerk;
- user-fight after the user begins a valid downward correction.

### Acceptance direction

Reduce post-cross vertical excursion and obsolete upward push without weakening
the first 200-300 px acquisition, increasing missed acquisitions, or swallowing
the user's later correction.

## V2: Slide/down-forward target with upward recoil and temporary vision loss

### Reproduction

- Model a target that enters a slide: its aim anchor accelerates downward while
  forward movement changes apparent size/geometry.
- Apply upward weapon recoil to the camera/reticle at the same time.
- Publish partial/unstable observations followed by a short vision dropout,
  then reacquire the same identity.
- Include variations with and without user downward correction, and with
  horizontal movement as a later stress extension.
- Recoil remains final feed-forward and must not be learned as target or ego
  motion.

### Defect to expose

The target moves down while recoil moves the reticle up, so relative vertical
error grows quickly. Geometry change or obstruction may temporarily remove the
detection. Tracker prediction can then coast in the wrong direction, lose the
identity, keep obsolete upward AI pressure, or reacquire with an anchor jump.

### Required metrics

- target identity retention and wrong-target switches;
- time from last reliable observation to loss and reacquisition;
- tracker-only hold duration and confidence/authority decay;
- vertical prediction error during dropout;
- maximum relative vertical error and error-area debt;
- obsolete upward AI/final output after slide onset;
- recovery time after reliable vision returns;
- false interruption and false stop;
- AutoFire authority during weak/cue-only/dropout intervals;
- recoil isolation: recoil must not update target-, ego-, or camera-response
  learning;
- anchor discontinuity rejection count.

### Acceptance direction

Retain the target through a bounded dropout and recover smoothly when vision
returns, while refusing to increase authority or learn dynamics from weak
identity, geometry jumps, or recoil-contaminated intervals.

## Recommended order

1. Finish the horizontal Shadow Oracle Gate and decide whether causal ego-motion
   estimation is viable.
2. Add V1 as the vertical acquisition/fusion hard-stress cohort.
3. Add V2 as the tracker/recoil/dropout attribution cohort.
4. Only after each isolated cohort has a trustworthy baseline, add a combined
   stress run. Do not begin with all effects mixed together.
